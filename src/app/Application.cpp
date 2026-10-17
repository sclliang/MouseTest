#include "Application.h"

#include "modules/mouse/MouseWindow.h"
#include "modules/touch_pad/TouchPadWindow.h"

#include <windows.h>

#include <filesystem>
#include <functional>
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
using ftc::modules::Language;
using MouseConfig = ftc::modules::mouse::Config;
using MouseResult = ftc::modules::mouse::WindowResult;
using MouseState = ftc::modules::mouse::WindowState;
using TouchPadConfig = ftc::modules::touch_pad::Config;
using TouchPadResult = ftc::modules::touch_pad::WindowResult;
using TouchPadState = ftc::modules::touch_pad::WindowState;

enum class Outcome
{
    Pass,
    Fail,
    Cancel,
    Fault,
};

using LogFunction = std::function<void(const wchar_t*, const std::wstring&)>;

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

[[nodiscard]] const wchar_t* ResultName(const Outcome result) noexcept
{
    switch (result)
    {
    case Outcome::Pass: return L"PASS";
    case Outcome::Cancel: return L"CANCEL";
    case Outcome::Fail:
    case Outcome::Fault: return L"FAIL";
    }
    return L"FAIL";
}

[[nodiscard]] const wchar_t* Text(const Language language, const wchar_t* const zh,
    const wchar_t* const en, const wchar_t* const vi) noexcept
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

[[nodiscard]] std::wstring TouchPadFinger(const TouchPadConfig& config)
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

[[nodiscard]] Outcome MapTouchPad(const TouchPadResult result) noexcept
{
    switch (result)
    {
    case TouchPadResult::Passed: return Outcome::Pass;
    case TouchPadResult::Cancelled: return Outcome::Cancel;
    case TouchPadResult::Failed: return Outcome::Fail;
    case TouchPadResult::Faulted: return Outcome::Fault;
    }
    return Outcome::Fault;
}

[[nodiscard]] Outcome MapMouse(const MouseResult result) noexcept
{
    switch (result)
    {
    case MouseResult::Passed: return Outcome::Pass;
    case MouseResult::Failed: return Outcome::Fail;
    case MouseResult::Faulted: return Outcome::Fault;
    }
    return Outcome::Fault;
}

[[nodiscard]] Outcome Combine(const Outcome left, const Outcome right) noexcept
{
    if (left == Outcome::Fail || left == Outcome::Fault || right == Outcome::Fail || right == Outcome::Fault)
        return Outcome::Fail;
    if (left == Outcome::Cancel || right == Outcome::Cancel) return Outcome::Cancel;
    return Outcome::Pass;
}

[[nodiscard]] Outcome RunTouchPad(const TouchPadConfig& config, const LogFunction& log)
{
    struct Run final
    {
        Outcome outcome{Outcome::Fault};
        bool completed{};
    } run;

    auto state = std::make_shared<TouchPadState>();
    state->config = config;
    state->skipHardwareCheck = false;
    state->onProgress = {};
    state->onDeviceReady = [&log](const std::wstring& deviceName) {
        log(L"INFO", L"已选择 Windows Precision Touchpad：" +
            (deviceName.empty() ? std::wstring(L"<设备名称不可用>") : deviceName));
    };
    state->onTrace = [&log](const std::wstring& message) { log(L"INFO", message); };
    state->onError = [&log](const std::wstring& message) { log(L"ERROR", message); };
    unsigned diagnostics{};
    state->onDiagnostic = [&log, &diagnostics](const std::wstring& message) {
        if (diagnostics < 20U) log(L"WARN", L"触摸板原始输入报告已忽略：" + message);
        else if (diagnostics == 20U) log(L"WARN", L"触摸板原始输入报告继续失败，后续同类 WARNING 已省略");
        ++diagnostics;
    };
    state->onFinished = [&run](const TouchPadResult result) {
        run.outcome = MapTouchPad(result);
        run.completed = true;
        PostQuitMessage(0);
    };

    std::wstring error;
    const HWND window = ftc::modules::touch_pad::CreateTouchPadWindow(nullptr, state, true, error);
    if (!window)
    {
        log(L"ERROR", error.empty() ? L"无法创建触摸板测试窗口" : error);
        return Outcome::Fault;
    }

    bool loopFailed = false;
    MSG message{};
    for (;;)
    {
        const BOOL status = GetMessageW(&message, nullptr, 0U, 0U);
        if (status == 0) break;
        if (status == -1)
        {
            loopFailed = true;
            log(L"ERROR", L"GetMessageW 失败，Win32=" + std::to_wstring(GetLastError()));
            break;
        }
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    if (!run.completed && IsWindow(window) != FALSE) DestroyWindow(window);
    if (!run.completed)
    {
        if (!loopFailed) log(L"WARN", L"触摸板测试窗口在完成前关闭");
        return loopFailed ? Outcome::Fault : Outcome::Fail;
    }
    return run.outcome;
}

[[nodiscard]] Outcome RunMouse(const MouseConfig& config, const LogFunction& log)
{
    struct Run final
    {
        Outcome outcome{Outcome::Fault};
        bool completed{};
    } run;

    auto state = std::make_shared<MouseState>();
    state->config = config;
    state->skipHardwareCheck = false;
    state->passDisplayMs = 1000U;
    unsigned diagnostics{};
    state->onDeviceReady = [&log](const std::wstring& devices) {
        log(L"INFO", L"已发现普通鼠标设备（任意一个均可）：" + devices);
    };
    state->onTrace = [&log](const std::wstring& message) { log(L"INFO", message); };
    state->onError = [&log](const std::wstring& message) { log(L"ERROR", message); };
    state->onDiagnostic = [&log, &diagnostics](const std::wstring& message) {
        if (diagnostics < 20U) log(L"WARN", L"鼠标原始输入报告已忽略：" + message);
        else if (diagnostics == 20U) log(L"WARN", L"鼠标原始输入报告继续失败，后续同类 WARNING 已省略");
        ++diagnostics;
    };
    state->onFinished = [&run](const MouseResult result) {
        run.outcome = MapMouse(result);
        run.completed = true;
        PostQuitMessage(0);
    };

    std::wstring error;
    const HWND window = ftc::modules::mouse::CreateMouseWindow(nullptr, state, true, error);
    if (!window)
    {
        log(L"ERROR", error.empty() ? L"无法创建鼠标测试窗口" : error);
        return Outcome::Fault;
    }

    bool loopFailed = false;
    MSG message{};
    for (;;)
    {
        const BOOL status = GetMessageW(&message, nullptr, 0U, 0U);
        if (status == 0) break;
        if (status == -1)
        {
            loopFailed = true;
            log(L"ERROR", L"GetMessageW 失败，Win32=" + std::to_wstring(GetLastError()));
            break;
        }
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    if (!run.completed && IsWindow(window) != FALSE) DestroyWindow(window);
    if (!run.completed)
    {
        if (!loopFailed) log(L"WARN", L"鼠标测试窗口在完成前关闭");
        return loopFailed ? Outcome::Fault : Outcome::Fail;
    }
    return run.outcome;
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

    const TestSelection& tests = options.tests;
    std::wstring log;
    log.reserve(8192U);
    log += L"\r\n========== MouseTest " MOUSETEST_VERSION L" ==========\r\n";
    log += L"Start: " + Timestamp() + L"\r\n";
    log += L"Computer: " + std::wstring(machine, machineSize) + L"\r\n";
    log += L"Process: " + std::to_wstring(GetCurrentProcessId()) + L"\r\n";
    log += L"ProgramDirectory: " + programDirectory.wstring() + L"\r\n";
    log += L"ConfigFile: " + (options.configPath.empty() ? std::wstring(L"<built-in defaults>") : options.configPath.wstring()) + L"\r\n";
    log += std::wstring(L"Selected: TouchPad=") + (tests.touchPadEnabled ? L"1" : L"0") +
        L", Mouse=" + (tests.mouseEnabled ? L"1" : L"0") + L"\r\n";
    log += L"Language: " + std::wstring(CultureName(tests.language)) + L"\r\n";
    log += L"TouchPad: RowCount=" + std::to_wstring(tests.touchPad.rows) +
        L", ColumnCount=" + std::to_wstring(tests.touchPad.columns) +
        L", LeftClickCount=" + std::to_wstring(tests.touchPad.leftClicks) +
        L", RightClickCount=" + std::to_wstring(tests.touchPad.rightClicks) +
        L", Finger=" + TouchPadFinger(tests.touchPad) + L"\r\n";
    log += L"Mouse: MoveDistance=" + std::to_wstring(tests.mouse.moveDistance) +
        L", MoveAxisDistance=" + std::to_wstring(tests.mouse.moveAxisDistance) +
        L", WheelSteps=" + std::to_wstring(tests.mouse.wheelSteps) +
        L", LeftClicks=" + std::to_wstring(tests.mouse.leftClicks) +
        L", DoubleClicks=" + std::to_wstring(tests.mouse.doubleClicks) +
        L", RightClicks=" + std::to_wstring(tests.mouse.rightClicks) +
        L", MiddleClicks=" + std::to_wstring(tests.mouse.middleClicks) +
        L", Drags=" + std::to_wstring(tests.mouse.drags) +
        L", DragDistance=" + std::to_wstring(tests.mouse.dragDistance) +
        L", ClickTolerance=" + std::to_wstring(tests.mouse.clickTolerance) + L"\r\n";
    for (const std::wstring& warning : options.warnings)
        log += Timestamp() + L" [WARN] " + warning + L"\r\n";

    const auto addLog = [&log](const wchar_t* const level, const std::wstring& message) {
        log += Timestamp() + L" [" + level + L"] " + message + L"\r\n";
    };

    const auto complete = [&](const Outcome result, const std::wstring& message) -> int {
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
        return result == Outcome::Pass ? 0 : result == Outcome::Cancel ? 2 : 1;
    };

    if (!options.startupError.empty())
    {
        addLog(L"ERROR", options.startupError);
        const int exitCode = complete(Outcome::Fail, L"鼠标测试无法启动：配置或启动参数无效");
        if (options.reportErrors && exitCode != 3)
        {
            const std::wstring text = L"鼠标测试无法启动 / Cannot start MouseTest:\r\n" + options.startupError;
            MessageBoxW(nullptr, text.c_str(), L"MouseTest", MB_OK | MB_ICONERROR);
        }
        return exitCode;
    }

    Outcome overall = Outcome::Pass;
    if (tests.touchPadEnabled)
    {
        addLog(L"INFO", L"===== TouchPad =====");
        const Outcome outcome = RunTouchPad(tests.touchPad, addLog);
        addLog(L"INFO", std::wstring(L"TouchPad result: ") + ResultName(outcome));
        overall = Combine(overall, outcome);
    }
    if (tests.mouseEnabled)
    {
        addLog(L"INFO", L"===== Mouse =====");
        const Outcome outcome = RunMouse(tests.mouse, addLog);
        addLog(L"INFO", std::wstring(L"Mouse result: ") + ResultName(outcome));
        overall = Combine(overall, outcome);
    }

    std::wstring finalMessage;
    if (overall == Outcome::Pass)
        finalMessage = Text(tests.language, L"全部已启用测试通过", L"All enabled tests PASS", L"Tất cả bài kiểm tra đã bật đạt");
    else if (overall == Outcome::Cancel)
        finalMessage = Text(tests.language, L"测试已取消", L"Tests cancelled", L"Đã hủy kiểm tra");
    else
        finalMessage = Text(tests.language, L"测试失败", L"Test FAIL", L"Kiểm tra thất bại");
    return complete(overall, finalMessage);
}
} // namespace mouse_app
