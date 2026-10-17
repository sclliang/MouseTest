#include "Application.h"

#include "modules/touch_pad/TouchPadWindow.h"

#include <windows.h>

#include <filesystem>
#include <iterator>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace mouse_app
{
namespace
{
using ftc::modules::touch_pad::Config;
using ftc::modules::touch_pad::Language;
using ftc::modules::touch_pad::WindowResult;
using ftc::modules::touch_pad::WindowState;

struct Handle final
{
    HANDLE value{};
    ~Handle() { if (value && value != INVALID_HANDLE_VALUE) CloseHandle(value); }
};

[[nodiscard]] std::wstring Timestamp()
{
    SYSTEMTIME now{};
    GetLocalTime(&now);
    wchar_t value[48]{};
    static_cast<void>(swprintf_s(value, L"%04u-%02u-%02u %02u:%02u:%02u.%03u",
        now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute, now.wSecond, now.wMilliseconds));
    return value;
}

[[nodiscard]] const wchar_t* ResultName(const WindowResult result) noexcept
{
    switch (result)
    {
    case WindowResult::Passed: return L"PASS";
    case WindowResult::Cancelled: return L"CANCEL";
    case WindowResult::Failed:
    case WindowResult::Faulted: return L"FAIL";
    }
    return L"FAIL";
}

[[nodiscard]] const wchar_t* Text(const Language language, const wchar_t* zh, const wchar_t* en, const wchar_t* vi) noexcept
{
    switch (language)
    {
    case Language::Chinese: return zh;
    case Language::English: return en;
    case Language::Vietnamese: return vi;
    }
    return en;
}

[[nodiscard]] const wchar_t* CultureName(const Language language) noexcept
{
    switch (language)
    {
    case Language::Chinese: return L"zh-CN";
    case Language::English: return L"en-US";
    case Language::Vietnamese: return L"vi-VN";
    }
    return L"zh-CN";
}

[[nodiscard]] std::wstring FingerText(const Config& config)
{
    if (!config.exclusion.enabled) return L"<none>";
    return std::to_wstring(config.exclusion.corner) + L"x" +
        std::to_wstring(config.exclusion.width) + L"x" +
        std::to_wstring(config.exclusion.height);
}

[[nodiscard]] DWORD AppendLog(const std::filesystem::path& path, const std::wstring& text)
{
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    if (error) return static_cast<DWORD>(error.value());
    if (text.size() > static_cast<std::size_t>((std::numeric_limits<int>::max)())) return ERROR_FILE_TOO_LARGE;
    const auto size = static_cast<int>(text.size());
    const int length = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(), size, nullptr, 0, nullptr, nullptr);
    if (!length) return GetLastError();
    std::string bytes(static_cast<std::size_t>(length), '\0');
    if (!WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(), size, bytes.data(), length, nullptr, nullptr))
        return GetLastError();
    if (bytes.size() > (std::numeric_limits<DWORD>::max)()) return ERROR_FILE_TOO_LARGE;

    Handle file{CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
        OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr)};
    if (file.value == INVALID_HANDLE_VALUE) return GetLastError();
    OVERLAPPED overlap{};
    if (!LockFileEx(file.value, LOCKFILE_EXCLUSIVE_LOCK, 0, MAXDWORD, MAXDWORD, &overlap)) return GetLastError();
    DWORD status = ERROR_SUCCESS;
    LARGE_INTEGER end{};
    if (!SetFilePointerEx(file.value, end, nullptr, FILE_END)) status = GetLastError();
    DWORD written{};
    const auto byteCount = static_cast<DWORD>(bytes.size());
    if (!status && !WriteFile(file.value, bytes.data(), byteCount, &written, nullptr)) status = GetLastError();
    if (!status && written != byteCount) status = ERROR_WRITE_FAULT;
    if (!status && !FlushFileBuffers(file.value)) status = GetLastError();
    static_cast<void>(UnlockFileEx(file.value, 0, MAXDWORD, MAXDWORD, &overlap));
    return status;
}
} // namespace

std::filesystem::path ExecutableDirectory()
{
    std::vector<wchar_t> buffer(32768U);
    const DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (length == 0U) throw std::runtime_error("GetModuleFileNameW failed");
    if (length >= buffer.size()) throw std::runtime_error("executable path is too long");
    return std::filesystem::path(std::wstring(buffer.data(), length)).parent_path();
}

int RunApplication(const std::filesystem::path& programDirectory, const SessionOptions& options)
{
    const ULONGLONG started = GetTickCount64();
    const std::filesystem::path logPath = programDirectory / L"MouseTest.log";
    wchar_t machine[MAX_COMPUTERNAME_LENGTH + 1U]{};
    DWORD machineSize = static_cast<DWORD>(std::size(machine));
    if (!GetComputerNameW(machine, &machineSize))
    {
        machine[0] = L'\0';
        machineSize = 0U;
    }

    std::wstring log;
    log.reserve(4096U);
    log += L"\r\n========== MouseTest " MOUSETEST_VERSION L" ==========\r\n";
    log += L"Start: " + Timestamp() + L"\r\n";
    log += L"Computer: " + std::wstring(machine, machineSize) + L"\r\n";
    log += L"Process: " + std::to_wstring(GetCurrentProcessId()) + L"\r\n";
    log += L"ProgramDirectory: " + programDirectory.wstring() + L"\r\n";
    log += L"ConfigFile: " + (options.configPath.empty() ? std::wstring(L"<built-in defaults>") : options.configPath.wstring()) + L"\r\n";
    log += L"Config: RowCount=" + std::to_wstring(options.config.rows) +
        L", ColumnCount=" + std::to_wstring(options.config.columns) +
        L", LeftClickCount=" + std::to_wstring(options.config.leftClicks) +
        L", RightClickCount=" + std::to_wstring(options.config.rightClicks) +
        L", Finger=" + FingerText(options.config) +
        L", Culture=" + CultureName(options.config.language) + L"\r\n";
    for (const std::wstring& warning : options.warnings)
        log += Timestamp() + L" [WARN] " + warning + L"\r\n";

    const auto addLog = [&log](const wchar_t* level, const std::wstring& message) {
        log += Timestamp() + L" [" + level + L"] " + message + L"\r\n";
    };

    const auto complete = [&](const WindowResult result, const std::wstring& message) -> int {
        log += L"End: " + Timestamp() + L"\r\n";
        log += L"DurationMs: " + std::to_wstring(GetTickCount64() - started) + L"\r\n";
        log += L"Result: " + std::wstring(ResultName(result)) + L"\r\n" + message + L"\r\n";
        log += L"================================================\r\n";
        const DWORD logError = AppendLog(logPath, log);
        if (options.reportErrors && logError)
        {
            const std::wstring text = L"测试结果 / Result: " + std::wstring(ResultName(result)) +
                L"\r\n日志保存失败 / Could not save log:\r\n" + logPath.wstring() +
                L"\r\nWindows error: " + std::to_wstring(logError);
            MessageBoxW(nullptr, text.c_str(), L"MouseTest", MB_OK | MB_ICONERROR);
        }
        if (logError) return 3;
        return result == WindowResult::Passed ? 0 : result == WindowResult::Cancelled ? 2 : 1;
    };

    if (!options.startupError.empty())
    {
        addLog(L"ERROR", options.startupError);
        const int exitCode = complete(WindowResult::Failed, L"鼠标测试无法启动：配置或启动参数无效");
        if (options.reportErrors && exitCode != 3)
        {
            const std::wstring text = L"鼠标测试无法启动 / Cannot start MouseTest:\r\n" + options.startupError;
            MessageBoxW(nullptr, text.c_str(), L"MouseTest", MB_OK | MB_ICONERROR);
        }
        return exitCode;
    }

    WindowResult result = WindowResult::Failed;
    bool completed = false;
    unsigned ignoredDiagnostics = 0U;
    auto state = std::make_shared<WindowState>();
    state->config = options.config;
    state->skipHardwareCheck = options.skipHardwareCheck;
    state->onProgress = {};
    state->onDeviceReady = [&addLog](const std::wstring& deviceName) {
        addLog(L"INFO", L"已选择 Windows Precision Touchpad：" +
            (deviceName.empty() ? std::wstring(L"<设备名称不可用>") : deviceName));
    };
    state->onTrace = [&addLog](const std::wstring& message) { addLog(L"INFO", message); };
    state->onError = [&addLog](const std::wstring& message) { addLog(L"ERROR", message); };
    state->onDiagnostic = [&addLog, &ignoredDiagnostics](const std::wstring& message) {
        if (ignoredDiagnostics < 20U) addLog(L"WARN", L"触摸板原始输入报告已忽略：" + message);
        else if (ignoredDiagnostics == 20U) addLog(L"WARN", L"触摸板原始输入报告继续失败，后续同类 WARNING 已省略");
        ++ignoredDiagnostics;
    };
    state->onFinished = [&result, &completed](const WindowResult value) {
        result = value;
        completed = true;
        PostQuitMessage(0);
    };

    std::wstring createError;
    HWND window = ftc::modules::touch_pad::CreateTouchPadWindow(nullptr, state, true, createError);
    if (!window)
    {
        if (createError.empty()) createError = L"无法创建鼠标测试窗口";
        addLog(L"ERROR", createError);
        const int exitCode = complete(WindowResult::Failed, L"鼠标测试窗口创建失败");
        if (options.reportErrors && exitCode != 3)
        {
            const std::wstring text = L"鼠标测试窗口创建失败 / Could not create MouseTest window:\r\n" + createError;
            MessageBoxW(nullptr, text.c_str(), L"MouseTest", MB_OK | MB_ICONERROR);
        }
        return exitCode;
    }

    MSG message{};
    bool loopFailed = false;
    for (;;)
    {
        const BOOL status = GetMessageW(&message, nullptr, 0U, 0U);
        if (status == 0) break;
        if (status == -1)
        {
            loopFailed = true;
            addLog(L"ERROR", L"GetMessageW 失败，Win32=" + std::to_wstring(GetLastError()));
            break;
        }
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    if (!completed && IsWindow(window) != FALSE)
        DestroyWindow(window);
    if (!completed)
    {
        result = WindowResult::Failed;
        if (!loopFailed) addLog(L"WARN", L"测试窗口在完成前关闭，按失败处理");
    }

    std::wstring finalMessage;
    switch (result)
    {
    case WindowResult::Passed:
        finalMessage = Text(options.config.language, L"触摸板测试通过", L"TouchPad test PASS", L"Kiểm tra bàn di chuột đạt");
        break;
    case WindowResult::Cancelled:
        finalMessage = Text(options.config.language, L"触摸板测试已取消", L"TouchPad test cancelled", L"Đã hủy kiểm tra bàn di chuột");
        break;
    case WindowResult::Faulted:
        addLog(L"ERROR", L"触摸板测试窗口发生异常");
        finalMessage = Text(options.config.language, L"触摸板测试失败：窗口异常", L"TouchPad test FAIL: window fault", L"Kiểm tra bàn di chuột thất bại: lỗi cửa sổ");
        break;
    case WindowResult::Failed:
        finalMessage = Text(options.config.language, L"触摸板测试失败", L"TouchPad test FAIL", L"Kiểm tra bàn di chuột thất bại");
        break;
    }
    return complete(result, finalMessage);
}
} // namespace mouse_app
