#include "modules/touch_pad/TouchPadWindow.h"

#include <fluentui/design.hpp>

#include "modules/touch_pad/TouchPadLogic.h"
#include "modules/touch_pad/TouchPadRawInput.h"

#include <d2d1.h>
#include <dwrite.h>
#include <wrl/client.h>

#include <algorithm>
#include <cmath>
#include <memory>
#include <new>
#include <string_view>

namespace ftc::modules::touch_pad
{
namespace
{
// Module-private presentation only. The timer never delays the test worker,
// changes focus, or handles operator input; Windows removes it on destruction.
constexpr UINT_PTR kWindowEntranceTimer = 0x46544345U;
constexpr wchar_t kWindowEntranceStart[] = L"Ftc.TouchPadWindow.EntranceStart";

void FinishWindowEntrance(const HWND window) noexcept
{
    KillTimer(window, kWindowEntranceTimer);
    RemovePropW(window, kWindowEntranceStart);
    SetLayeredWindowAttributes(window, 0, 255, LWA_ALPHA);
    SetWindowLongPtrW(window, GWL_EXSTYLE,
        GetWindowLongPtrW(window, GWL_EXSTYLE) & ~static_cast<LONG_PTR>(WS_EX_LAYERED));
    RedrawWindow(window, nullptr, nullptr,
        RDW_INVALIDATE | RDW_FRAME | RDW_ALLCHILDREN | RDW_UPDATENOW);
}

void CALLBACK WindowEntranceTick(HWND window, UINT, UINT_PTR, DWORD) noexcept
{
    const auto start = reinterpret_cast<ULONGLONG>(GetPropW(window, kWindowEntranceStart));
    if (start == 0) return;
    // Start the clock only once the UI loop can deliver frames. Other
    // simultaneously opening tests may still be preparing their resources.
    const ULONGLONG now = GetTickCount64();
    if (start == 1 && SetPropW(window, kWindowEntranceStart, reinterpret_cast<HANDLE>(now)) == FALSE)
    {
        FinishWindowEntrance(window);
        return;
    }
    const ULONGLONG elapsed = start == 1 ? 0 : now - start;
    if (elapsed >= 160 || IsWindowVisible(window) == FALSE || IsIconic(window) != FALSE)
    {
        FinishWindowEntrance(window);
        return;
    }
    const double remaining = 1.0 - static_cast<double>(elapsed) / 160.0;
    const BYTE alpha = static_cast<BYTE>(255.0 - 254.0 * remaining * remaining * remaining);
    if (SetLayeredWindowAttributes(window, 0, alpha, LWA_ALPHA) == FALSE)
    {
        FinishWindowEntrance(window);
    }
}

void ShowPreparedWindow(const HWND window, const int showCommand) noexcept
{
    const LONG_PTR style = GetWindowLongPtrW(window, GWL_EXSTYLE);
    bool prepared = false;
    if ((style & WS_EX_LAYERED) == 0)
    {
        SetLastError(ERROR_SUCCESS);
        const LONG_PTR previous = SetWindowLongPtrW(window, GWL_EXSTYLE, style | WS_EX_LAYERED);
        if (previous != 0 || GetLastError() == ERROR_SUCCESS)
        {
            prepared = SetLayeredWindowAttributes(window, 0, 0, LWA_ALPHA) != FALSE;
            if (!prepared) SetWindowLongPtrW(window, GWL_EXSTYLE, style);
        }
    }
    ShowWindow(window, showCommand);
    // Paint the complete first frame (including native controls and D2D
    // resources) while transparent, before exposing the surface to the user.
    RedrawWindow(window, nullptr, nullptr,
        RDW_INVALIDATE | RDW_FRAME | RDW_ALLCHILDREN | RDW_UPDATENOW);
    if (!prepared) return;

    BOOL animations = FALSE;
    HIGHCONTRASTW contrast{sizeof(contrast)};
    SystemParametersInfoW(SPI_GETHIGHCONTRAST, sizeof(contrast), &contrast, 0);
    if (SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &animations, 0) && animations &&
        (contrast.dwFlags & HCF_HIGHCONTRASTON) == 0 &&
        SetPropW(window, kWindowEntranceStart, reinterpret_cast<HANDLE>(static_cast<INT_PTR>(1))) &&
        SetTimer(window, kWindowEntranceTimer, 15, WindowEntranceTick) != 0)
    {
        return;
    }
    FinishWindowEntrance(window);
}

// Visual tokens only; this module retains its own window and test state.
const auto kFluentTheme = fluent::Palette::get(fluent::ThemeMode::Light,
    fluent::AccentPalette::fromColor(fluent::Color::rgb(0x0067C0)));
const auto kFluentDesign = fluent::DesignPalette::get(kFluentTheme);
const auto kFluentControls = fluent::ControlPalette::get(kFluentTheme);

[[nodiscard]] unsigned int UiRgb(const fluent::Color color) noexcept
{
    return (static_cast<unsigned int>(color.r * 255.0F + 0.5F) << 16U) |
        (static_cast<unsigned int>(color.g * 255.0F + 0.5F) << 8U) |
        static_cast<unsigned int>(color.b * 255.0F + 0.5F);
}

using Microsoft::WRL::ComPtr;
constexpr wchar_t kClassName[] = L"MouseTest.TouchPad.Window";
constexpr UINT kCancelMessage = WM_APP + 0x4C1U;
constexpr UINT kInitializeMessage = WM_APP + 0x4C2U;

struct Payload final { std::shared_ptr<WindowState> state; std::wstring* error{}; };

struct Context final
{
    explicit Context(std::shared_ptr<WindowState> value, std::wstring* target)
        : state(std::move(value)), creationError(target), coverage(state->config) {}
    std::shared_ptr<WindowState> state;
    std::wstring* creationError{};
    Coverage coverage;
    LogicalBounds bounds;
    std::wstring deviceName;
    std::wstring status;
    WindowResult result{WindowResult::Failed};
    UINT dpi{96U};
    bool registered{};
    bool initialized{};
    bool created{};
    bool finishing{};
    bool rawInputReported{};
    bool contactReported{};
    double lastX{-1.0};
    double lastY{-1.0};
    ComPtr<ID2D1Factory> factory;
    ComPtr<IDWriteFactory> writeFactory;
    ComPtr<ID2D1HwndRenderTarget> target;
    ComPtr<ID2D1SolidColorBrush> brush;
    ComPtr<IDWriteTextFormat> titleFormat;
    ComPtr<IDWriteTextFormat> bodyFormat;
    ComPtr<IDWriteTextFormat> cellFormat;
};

[[nodiscard]] const wchar_t* Text(Language language, const wchar_t* zh, const wchar_t* en, const wchar_t* vi) noexcept
{
    switch (language) { case Language::Chinese: return zh; case Language::English: return en; case Language::Vietnamese: return vi; }
    return en;
}

[[nodiscard]] D2D1_COLOR_F Color(const unsigned rgb) noexcept
{
    return D2D1::ColorF(
        static_cast<float>((rgb >> 16U) & 0xFFU) / 255.0F,
        static_cast<float>((rgb >> 8U) & 0xFFU) / 255.0F,
        static_cast<float>(rgb & 0xFFU) / 255.0F);
}

[[nodiscard]] float Dip(const LONG value, const UINT dpi) noexcept
{
    return static_cast<float>(value) * 96.0F / static_cast<float>(dpi == 0U ? 96U : dpi);
}

[[nodiscard]] D2D1_SIZE_F ClientSize(HWND window, const UINT dpi) noexcept
{
    RECT rect{}; GetClientRect(window, &rect);
    return D2D1::SizeF(Dip(rect.right, dpi), Dip(rect.bottom, dpi));
}

[[nodiscard]] D2D1_RECT_F GridRect(const D2D1_SIZE_F size) noexcept
{
    return D2D1::RectF(size.width * 0.25F, size.height * 0.25F, size.width * 0.75F, size.height * 0.75F);
}

void DrawText(Context& context, std::wstring_view value, IDWriteTextFormat* format, D2D1_RECT_F rect, unsigned color)
{
    context.brush->SetColor(Color(color));
    context.target->DrawTextW(value.data(), static_cast<UINT32>(value.size()), format, rect,
        context.brush.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
}

[[nodiscard]] HRESULT EnsureResources(HWND window, Context& context)
{
    if (!context.factory)
    {
        HRESULT result = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, IID_PPV_ARGS(&context.factory));
        if (FAILED(result)) return result;
        result = DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
            reinterpret_cast<IUnknown**>(context.writeFactory.GetAddressOf()));
        if (FAILED(result)) return result;
        const auto make = [&context](float size, DWRITE_FONT_WEIGHT weight, ComPtr<IDWriteTextFormat>& output) {
            return context.writeFactory->CreateTextFormat(L"Microsoft YaHei UI", nullptr, weight,
                DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, size, L"", &output);
        };
        result = make(26.0F, DWRITE_FONT_WEIGHT_SEMI_BOLD, context.titleFormat);
        if (SUCCEEDED(result)) result = make(15.0F, DWRITE_FONT_WEIGHT_NORMAL, context.bodyFormat);
        if (SUCCEEDED(result)) result = make(12.0F, DWRITE_FONT_WEIGHT_SEMI_BOLD, context.cellFormat);
        if (FAILED(result)) return result;
        static_cast<void>(context.titleFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER));
        static_cast<void>(context.bodyFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER));
        static_cast<void>(context.cellFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER));
        static_cast<void>(context.cellFormat->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER));
    }
    if (!context.target)
    {
        RECT rect{}; GetClientRect(window, &rect);
        HRESULT result = context.factory->CreateHwndRenderTarget(D2D1::RenderTargetProperties(),
            D2D1::HwndRenderTargetProperties(window, D2D1::SizeU(
                static_cast<UINT32>((std::max)(1L, rect.right)), static_cast<UINT32>((std::max)(1L, rect.bottom)))),
            &context.target);
        if (SUCCEEDED(result))
        {
            context.target->SetDpi(static_cast<float>(context.dpi), static_cast<float>(context.dpi));
            result = context.target->CreateSolidColorBrush(Color(UiRgb(kFluentDesign.text)), &context.brush);
        }
        if (FAILED(result)) context.target.Reset();
        return result;
    }
    return S_OK;
}

void Paint(HWND window, Context& context)
{
    if (FAILED(EnsureResources(window, context))) throw std::bad_alloc();
    const D2D1_SIZE_F size = ClientSize(window, context.dpi);
    const D2D1_RECT_F grid = GridRect(size);
    const Config& config = context.state->config;
    context.target->BeginDraw();
    context.target->Clear(Color(UiRgb(kFluentDesign.window)));
    const float titleTop = (std::min)(size.height * 0.06F, (std::max)(4.0F, grid.top - 96.0F));
    DrawText(context, Text(config.language, L"触摸板测试", L"TouchPad Test", L"Kiểm tra bàn di chuột"),
        context.titleFormat.Get(), D2D1::RectF(24.0F, titleTop, size.width - 24.0F, titleTop + 36.0F), UiRgb(kFluentDesign.text));
    const std::wstring progress = std::wstring(Text(config.language,
        L"请用触摸板划过全部网格，并完成左右按键测试  ·  ",
        L"Cover all cells and complete both click checks  ·  ",
        L"Di chuyển qua mọi ô và hoàn tất kiểm tra hai nút  ·  ")) +
        std::to_wstring(context.coverage.CompletedCells()) + L"/" + std::to_wstring(context.coverage.TotalCells());
    const std::wstring instructions = (context.status.empty() ? progress : context.status) +
        Text(config.language, L"\nEsc：失败退出", L"\nEsc: fail and exit", L"\nEsc: báo lỗi và thoát");
    const float instructionsTop = (std::max)(titleTop + 38.0F,
        (std::min)(size.height * 0.135F, grid.top - 48.0F));
    DrawText(context, instructions, context.bodyFormat.Get(),
        D2D1::RectF(24.0F, instructionsTop, size.width - 24.0F, grid.top - 4.0F), UiRgb(kFluentDesign.secondary));

    const float cellWidth = (grid.right - grid.left) / static_cast<float>(config.columns);
    const float cellHeight = (grid.bottom - grid.top) / static_cast<float>(config.rows);
    const auto& visited = context.coverage.Visited();
    for (unsigned row = 0U; row < config.rows; ++row)
    {
        for (unsigned column = 0U; column < config.columns; ++column)
        {
            const std::size_t index = static_cast<std::size_t>(row) * config.columns + column;
            const D2D1_RECT_F cell = D2D1::RectF(grid.left + column * cellWidth + 2.0F,
                grid.top + row * cellHeight + 2.0F, grid.left + (column + 1U) * cellWidth - 2.0F,
                grid.top + (row + 1U) * cellHeight - 2.0F);
            context.brush->SetColor(Color(visited[index] ? 0xDFF4E3 : 0xFFFFFF));
            context.target->FillRoundedRectangle(D2D1::RoundedRect(cell, 6.0F, 6.0F), context.brush.Get());
            context.brush->SetColor(Color(visited[index] ? 0x107C10 : UiRgb(kFluentControls.bottomBorder)));
            context.target->DrawRoundedRectangle(D2D1::RoundedRect(cell, 6.0F, 6.0F), context.brush.Get(), 1.0F);
        }
    }
    if (context.lastX >= 0.0 && context.lastY >= 0.0)
    {
        const float x = grid.left + static_cast<float>(context.lastX) * (grid.right - grid.left);
        const float y = grid.top + static_cast<float>(context.lastY) * (grid.bottom - grid.top);
        context.brush->SetColor(Color(UiRgb(kFluentDesign.accent)));
        context.target->FillEllipse(D2D1::Ellipse(D2D1::Point2F(x, y), 8.0F, 8.0F), context.brush.Get());
    }
    const std::wstring left = std::wstring(Text(config.language, L"左键剩余 ", L"Left remaining ", L"Nút trái còn ")) + std::to_wstring(context.coverage.LeftRemaining());
    const std::wstring right = std::wstring(Text(config.language, L"右键剩余 ", L"Right remaining ", L"Nút phải còn ")) + std::to_wstring(context.coverage.RightRemaining());
    DrawText(context, left, context.bodyFormat.Get(), D2D1::RectF(grid.left, grid.bottom + 32.0F, size.width / 2.0F, grid.bottom + 72.0F),
        context.coverage.LeftRemaining() == 0U ? 0x107C10 : UiRgb(kFluentDesign.text));
    DrawText(context, right, context.bodyFormat.Get(), D2D1::RectF(size.width / 2.0F, grid.bottom + 32.0F, grid.right, grid.bottom + 72.0F),
        context.coverage.RightRemaining() == 0U ? 0x107C10 : UiRgb(kFluentDesign.text));
    const HRESULT result = context.target->EndDraw();
    if (result == D2DERR_RECREATE_TARGET) { context.brush.Reset(); context.target.Reset(); }
    else if (FAILED(result)) throw std::bad_alloc();
}

void NotifyProgress(Context& context)
{
    if (!context.state->onProgress) return;
    try { context.state->onProgress(context.coverage.CompletedCells(), context.coverage.TotalCells(),
        context.coverage.LeftRemaining(), context.coverage.RightRemaining()); } catch (...) {}
}

void Finish(HWND window, Context& context, const WindowResult result) noexcept
{
    if (context.finishing) return;
    context.finishing = true;
    context.result = result;
    DestroyWindow(window);
}

void CheckComplete(HWND window, Context& context)
{
    NotifyProgress(context);
    InvalidateRect(window, nullptr, FALSE);
    if (context.coverage.IsComplete()) Finish(window, context, WindowResult::Passed);
}

void ApplyPoint(HWND window, Context& context, const double x, const double y)
{
    context.lastX = x; context.lastY = y;
    static_cast<void>(context.coverage.Visit(x, y));
    CheckComplete(window, context);
}

LRESULT CALLBACK WindowProcedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
{
    auto* context = reinterpret_cast<Context*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE)
    {
        const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lParam);
        const auto* payload = static_cast<const Payload*>(create->lpCreateParams);
        context = new (std::nothrow) Context(payload->state, payload->error);
        if (!context) { if (payload->error) *payload->error = L"无法分配触摸板测试窗口状态"; return FALSE; }
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(context));
        context->state->hwnd.store(window, std::memory_order_release);
    }
    if (!context) return DefWindowProcW(window, message, wParam, lParam);
    try
    {
        switch (message)
        {
        case WM_CREATE:
            context->dpi = GetDpiForWindow(window);
            context->initialized = context->state->skipHardwareCheck;
            context->created = true;
            NotifyProgress(*context);
            return 0;
        case kInitializeMessage:
            if (context->initialized || context->finishing) return 0;
            context->initialized = true;
            {
                std::wstring error;
                if (!FindPrecisionTouchPad(context->deviceName, error) || !RegisterPrecisionTouchPad(window, error))
                {
                    if (context->state->onError) try { context->state->onError(error); } catch (...) {}
                    Finish(window, *context, WindowResult::Failed);
                    return 0;
                }
                context->registered = true;
                if (context->state->onDeviceReady)
                    try { context->state->onDeviceReady(context->deviceName); } catch (...) {}
                if (context->state->onTrace)
                    try { context->state->onTrace(L"触摸板 Raw Input 已在测试窗口显示并取得焦点后注册"); } catch (...) {}
            }
            return 0;
        case WM_INPUT:
        {
            if (!context->rawInputReported)
            {
                context->rawInputReported = true;
                if (context->state->onTrace)
                    try { context->state->onTrace(L"触摸板测试窗口已收到首个 WM_INPUT"); } catch (...) {}
            }
            RawFrame frame;
            std::wstring error;
            if (!ParseRawFrame(lParam, frame, error))
            {
                if (context->state->onDiagnostic) try { context->state->onDiagnostic(error); } catch (...) {}
                return DefWindowProcW(window, message, wParam, lParam);
            }
            context->bounds = frame.bounds;
            if (!context->contactReported && !frame.contacts.empty())
            {
                context->contactReported = true;
                if (context->state->onTrace)
                {
                    const std::wstring detail = L"已解析首个触摸板触点：ContactCount=" +
                        std::to_wstring(frame.declaredContactCount) + L"，本帧触点=" +
                        std::to_wstring(frame.contacts.size()) + L"，首点=(" +
                        std::to_wstring(frame.contacts.front().x) + L"," +
                        std::to_wstring(frame.contacts.front().y) + L")，X范围=" +
                        std::to_wstring(context->bounds.minX) + L".." + std::to_wstring(context->bounds.maxX) +
                        L"，Y范围=" + std::to_wstring(context->bounds.minY) + L".." + std::to_wstring(context->bounds.maxY);
                    try { context->state->onTrace(detail); } catch (...) {}
                }
            }
            bool coverageChanged{};
            for (const Contact& contact : frame.contacts)
            {
                const double x = std::clamp(static_cast<double>(contact.x - context->bounds.minX) /
                    static_cast<double>(context->bounds.maxX - context->bounds.minX), 0.0, 0.999999);
                const double y = std::clamp(static_cast<double>(contact.y - context->bounds.minY) /
                    static_cast<double>(context->bounds.maxY - context->bounds.minY), 0.0, 0.999999);
                context->lastX = x; context->lastY = y;
                coverageChanged = context->coverage.Visit(x, y) || coverageChanged;
            }
            const LRESULT defaultResult = DefWindowProcW(window, message, wParam, lParam);
            NotifyProgress(*context);
            InvalidateRect(window, nullptr, FALSE);
            if (coverageChanged)
            {
                // A Precision Touchpad can produce a dense WM_INPUT stream. Force
                // each newly covered cell to paint now instead of waiting until
                // the stream becomes idle.
                UpdateWindow(window);
            }
            if (context->coverage.IsComplete()) Finish(window, *context, WindowResult::Passed);
            return defaultResult;
        }
        case WM_LBUTTONDOWN:
            static_cast<void>(context->coverage.LeftClick());
            CheckComplete(window, *context);
            return 0;
        case WM_RBUTTONDOWN: static_cast<void>(context->coverage.RightClick()); CheckComplete(window, *context); return 0;
        case WM_KEYDOWN: if (wParam == VK_ESCAPE) Finish(window, *context, WindowResult::Failed); return 0;
        case WM_CLOSE: Finish(window, *context, WindowResult::Failed); return 0;
        case kCancelMessage: Finish(window, *context, WindowResult::Cancelled); return 0;
        case WM_SETCURSOR: SetCursor(nullptr); return TRUE;
        case WM_DPICHANGED:
        {
            context->dpi = HIWORD(wParam);
            if (context->target)
                context->target->SetDpi(static_cast<float>(context->dpi), static_cast<float>(context->dpi));
            const RECT* rect = reinterpret_cast<const RECT*>(lParam);
            SetWindowPos(window, nullptr, rect->left, rect->top, rect->right - rect->left, rect->bottom - rect->top,
                SWP_NOACTIVATE | SWP_NOZORDER);
            InvalidateRect(window, nullptr, FALSE);
            return 0;
        }
        case WM_SIZE:
            if (context->target) context->target->Resize(D2D1::SizeU(LOWORD(lParam), HIWORD(lParam)));
            InvalidateRect(window, nullptr, FALSE);
            return 0;
        case WM_PAINT:
        {
            PAINTSTRUCT paint{}; BeginPaint(window, &paint); Paint(window, *context); EndPaint(window, &paint); return 0;
        }
        case WM_NCDESTROY:
        {
            if (context->registered) UnregisterPrecisionTouchPad();
            context->state->hwnd.store(nullptr, std::memory_order_release);
            const bool submit = context->created;
            const WindowResult result = context->result;
            auto state = context->state;
            SetWindowLongPtrW(window, GWLP_USERDATA, 0);
            delete context;
            if (submit && !state->submitted.exchange(true, std::memory_order_acq_rel) && state->onFinished)
                try { state->onFinished(result); } catch (...) {}
            return DefWindowProcW(window, message, wParam, lParam);
        }
        default: break;
        }
    }
    catch (...)
    {
        Finish(window, *context, WindowResult::Faulted);
        return 0;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

[[nodiscard]] bool RegisterClassOnce(std::wstring& error)
{
    static const bool registered = [] {
        WNDCLASSEXW value{sizeof(value)};
        value.lpfnWndProc = WindowProcedure;
        value.hInstance = GetModuleHandleW(nullptr);
        value.lpszClassName = kClassName;
        value.hCursor = nullptr;
        value.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
        return RegisterClassExW(&value) != 0U || GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
    }();
    if (!registered) error = L"RegisterClassExW(触摸板窗口) 失败，Win32=" + std::to_wstring(GetLastError());
    return registered;
}
} // namespace

HWND CreateTouchPadWindow(HWND owner, const std::shared_ptr<WindowState>& state, const bool showWindow, std::wstring& error)
{
    if (!state || !RegisterClassOnce(error)) return nullptr;
    HMONITOR monitor = MonitorFromWindow(owner, MONITOR_DEFAULTTOPRIMARY);
    MONITORINFO information{sizeof(information)};
    if (!GetMonitorInfoW(monitor, &information)) { error = L"GetMonitorInfoW 失败"; return nullptr; }
    Payload payload{state, &error};
    const RECT rect = information.rcMonitor;
    HWND window = CreateWindowExW(WS_EX_APPWINDOW, kClassName, L"MouseTest - TouchPad", WS_POPUP,
        rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top, owner, nullptr,
        GetModuleHandleW(nullptr), &payload);
    if (!window)
    {
        if (error.empty()) error = L"CreateWindowExW(触摸板窗口) 失败，Win32=" + std::to_wstring(GetLastError());
        return nullptr;
    }
    if (showWindow)
    {
        ShowPreparedWindow(window, SW_SHOW);
        UpdateWindow(window);
        SetWindowPos(window, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
        SetForegroundWindow(window);
        SetActiveWindow(window);
        SetFocus(window);
        SetWindowPos(window, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
    }
    if (!state->skipHardwareCheck && !PostMessageW(window, kInitializeMessage, 0U, 0))
    {
        error = L"PostMessageW(初始化触摸板) 失败，Win32=" + std::to_wstring(GetLastError());
        state->submitted.store(true, std::memory_order_release);
        DestroyWindow(window);
        return nullptr;
    }
    return window;
}

void CancelWindow(const std::shared_ptr<WindowState>& state) noexcept
{
    if (!state || state->submitted.load(std::memory_order_acquire)) return;
    HWND window = state->hwnd.load(std::memory_order_acquire);
    if (!window) return;
    if (GetWindowThreadProcessId(window, nullptr) == GetCurrentThreadId()) SendMessageW(window, kCancelMessage, 0U, 0);
    else PostMessageW(window, kCancelMessage, 0U, 0);
}

bool SubmitContactForTesting(HWND window, const double x, const double y) noexcept
{
    if (!window || GetWindowThreadProcessId(window, nullptr) != GetCurrentThreadId()) return false;
    auto* context = reinterpret_cast<Context*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (!context || context->finishing) return false;
    try { ApplyPoint(window, *context, x, y); return true; } catch (...) { return false; }
}

bool SubmitClickForTesting(HWND window, const bool left) noexcept
{
    if (!window || GetWindowThreadProcessId(window, nullptr) != GetCurrentThreadId()) return false;
    auto* context = reinterpret_cast<Context*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (!context || context->finishing) return false;
    if (left) static_cast<void>(context->coverage.LeftClick()); else static_cast<void>(context->coverage.RightClick());
    CheckComplete(window, *context);
    return true;
}
} // namespace ftc::modules::touch_pad
