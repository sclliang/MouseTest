#include "modules/mouse/MouseWindow.h"

#include "modules/mouse/MouseRawInput.h"

#include <fluentui/design.hpp>

#include <d2d1.h>
#include <dwrite.h>
#include <wrl/client.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cwchar>
#include <memory>
#include <new>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <vector>

namespace ftc::modules::mouse
{
namespace
{
using Microsoft::WRL::ComPtr;

constexpr UINT kInitializeMessage = WM_APP + 0x4D1U;
constexpr UINT_PTR kPassTimer = 0x4654434DU;
constexpr wchar_t kClassName[] = L"MouseTest.Mouse.Window";

const auto kFluentTheme = fluent::Palette::get(fluent::ThemeMode::Light,
    fluent::AccentPalette::fromColor(fluent::Color::rgb(0x0067C0)));
const auto kFluentDesign = fluent::DesignPalette::get(kFluentTheme);
const auto kFluentControls = fluent::ControlPalette::get(kFluentTheme);

constexpr unsigned kMoveInk = 0x2B6CB0U;
constexpr unsigned kDragInk = 0xE07B00U;
constexpr unsigned kClickInk = 0x107C10U;

[[nodiscard]] unsigned int UiRgb(const fluent::Color color) noexcept
{
    return (static_cast<unsigned int>(color.r * 255.0F + 0.5F) << 16U) |
        (static_cast<unsigned int>(color.g * 255.0F + 0.5F) << 8U) |
        static_cast<unsigned int>(color.b * 255.0F + 0.5F);
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

[[nodiscard]] D2D1_COLOR_F Color(const unsigned rgb) noexcept
{
    return D2D1::ColorF(
        static_cast<float>((rgb >> 16U) & 0xFFU) / 255.0F,
        static_cast<float>((rgb >> 8U) & 0xFFU) / 255.0F,
        static_cast<float>(rgb & 0xFFU) / 255.0F);
}

[[nodiscard]] D2D1_COLOR_F ColorA(const unsigned rgb, const float alpha) noexcept
{
    D2D1_COLOR_F color = Color(rgb);
    color.a = alpha;
    return color;
}

[[nodiscard]] float Dip(const LONG value, const UINT dpi) noexcept
{
    return static_cast<float>(value) * 96.0F / static_cast<float>(dpi == 0U ? 96U : dpi);
}

[[nodiscard]] D2D1_SIZE_F ClientSize(const HWND window, const UINT dpi) noexcept
{
    RECT rect{};
    GetClientRect(window, &rect);
    return D2D1::SizeF(Dip(rect.right, dpi), Dip(rect.bottom, dpi));
}

struct Payload final
{
    std::shared_ptr<WindowState> state;
    std::wstring* error{};
};

enum class TrailKind
{
    Move,
    Drag,
    Click,
};

struct TrailPoint final
{
    POINT point{};
    TrailKind kind{TrailKind::Move};
    bool newStroke{};
};

[[nodiscard]] D2D1_POINT_2F InkPoint(const POINT point, const UINT dpi) noexcept
{
    return D2D1::Point2F(Dip(point.x, dpi), Dip(point.y, dpi));
}

struct Context final
{
    Context(std::shared_ptr<WindowState> value, std::wstring* target)
        : state(std::move(value)), creationError(target),
          test(state->config, GetDoubleClickTime()) {}

    std::shared_ptr<WindowState> state;
    std::wstring* creationError{};
    TestState test;
    std::vector<TrailPoint> trail;
    WindowResult result{WindowResult::Failed};
    UINT dpi{96U};
    bool registered{};
    bool initialized{};
    bool created{};
    bool finishing{};
    bool passed{};
    bool rawInputReported{};
    bool absoluteReported{};
    ComPtr<ID2D1Factory> factory;
    ComPtr<IDWriteFactory> writeFactory;
    ComPtr<ID2D1HwndRenderTarget> target;
    ComPtr<ID2D1SolidColorBrush> brush;
    ComPtr<IDWriteTextFormat> titleFormat;
    ComPtr<IDWriteTextFormat> subtitleFormat;
    ComPtr<IDWriteTextFormat> dotLabelFormat;
    ComPtr<IDWriteTextFormat> progressFormat;
    ComPtr<IDWriteTextFormat> hintFormat;
};

void DrawText(Context& context, const std::wstring_view value, IDWriteTextFormat* const format,
    const D2D1_RECT_F rect, const unsigned rgb)
{
    if (value.empty()) return;
    context.brush->SetColor(Color(rgb));
    context.target->DrawTextW(value.data(), static_cast<UINT32>(value.size()), format, rect,
        context.brush.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
}

[[nodiscard]] HRESULT EnsureResources(const HWND window, Context& context)
{
    if (!context.factory)
    {
        HRESULT result = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, IID_PPV_ARGS(&context.factory));
        if (FAILED(result)) return result;
        result = DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
            reinterpret_cast<IUnknown**>(context.writeFactory.GetAddressOf()));
        if (FAILED(result)) return result;
        const auto make = [&context](const float size, const DWRITE_FONT_WEIGHT weight,
            ComPtr<IDWriteTextFormat>& output) {
            return context.writeFactory->CreateTextFormat(L"Microsoft YaHei UI", nullptr, weight,
                DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, size, L"", &output);
        };
        result = make(28.0F, DWRITE_FONT_WEIGHT_SEMI_BOLD, context.titleFormat);
        if (SUCCEEDED(result)) result = make(13.0F, DWRITE_FONT_WEIGHT_NORMAL, context.subtitleFormat);
        if (SUCCEEDED(result)) result = make(11.0F, DWRITE_FONT_WEIGHT_NORMAL, context.dotLabelFormat);        if (SUCCEEDED(result)) result = make(13.0F, DWRITE_FONT_WEIGHT_SEMI_BOLD, context.progressFormat);
        if (SUCCEEDED(result)) result = make(16.0F, DWRITE_FONT_WEIGHT_NORMAL, context.hintFormat);
        if (FAILED(result)) return result;
        static_cast<void>(context.dotLabelFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER));
        static_cast<void>(context.progressFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_TRAILING));
        static_cast<void>(context.hintFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER));
    }
    if (!context.target)
    {
        RECT rect{};
        GetClientRect(window, &rect);
        HRESULT result = context.factory->CreateHwndRenderTarget(D2D1::RenderTargetProperties(),
            D2D1::HwndRenderTargetProperties(window, D2D1::SizeU(
                static_cast<UINT32>((std::max)(1L, rect.right)),
                static_cast<UINT32>((std::max)(1L, rect.bottom)))), &context.target);
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

[[nodiscard]] std::wstring TaskTitle(const Language language, const unsigned index)
{
    switch (index)
    {
    case 0U: return Text(language, L"滑动鼠标", L"Move mouse", L"Di chuyển chuột");
    case 1U: return Text(language, L"滚轮滚动", L"Scroll wheel", L"Cuộn bánh xe");
    case 2U: return Text(language, L"左键单击", L"Left click", L"Nhấp trái");
    case 3U: return Text(language, L"左键双击", L"Double click", L"Nhấp đúp");
    case 4U: return Text(language, L"右键单击", L"Right click", L"Nhấp phải");
    case 5U: return Text(language, L"滚轮点击", L"Middle click", L"Nhấp nút giữa");
    default: return Text(language, L"按住左键拖动", L"Click and drag", L"Giữ trái và kéo");
    }
}

[[nodiscard]] bool TaskComplete(const TestState& test, const unsigned index) noexcept
{
    switch (index)
    {
    case 0U: return test.MoveComplete();
    case 1U: return test.WheelComplete();
    case 2U: return test.LeftComplete();
    case 3U: return test.DoubleComplete();
    case 4U: return test.RightComplete();
    case 5U: return test.MiddleComplete();
    default: return test.DragComplete();
    }
}

[[nodiscard]] float Ratio(const unsigned value, const unsigned target) noexcept
{
    if (target == 0U) return 1.0F;
    return (std::min)(1.0F, static_cast<float>(value) / static_cast<float>(target));
}

[[nodiscard]] float TaskProgress(const Config& config, const TestState& test, const unsigned index) noexcept
{
    const Progress& progress = test.Current();
    switch (index)
    {
    case 0U:
        return (std::min)({ Ratio(progress.moveX, config.moveAxisDistance),
            Ratio(progress.moveY, config.moveAxisDistance), Ratio(progress.moveTotal, config.moveDistance) });
    case 1U:
        return (std::min)(Ratio(progress.wheelUp, config.wheelSteps * 120U),
            Ratio(progress.wheelDown, config.wheelSteps * 120U));
    case 2U: return Ratio(progress.leftClicks, config.leftClicks);
    case 3U: return Ratio(progress.doubleClicks, config.doubleClicks);
    case 4U: return Ratio(progress.rightClicks, config.rightClicks);
    case 5U: return Ratio(progress.middleClicks, config.middleClicks);
    default: return Ratio(progress.drags, config.drags);
    }
}

[[nodiscard]] std::wstring ProgressText(const Language language, const unsigned completed, const bool passed)
{
    const unsigned value = passed ? 7U : completed;
    switch (language)
    {
    case Language::Chinese: return L"已完成 " + std::to_wstring(value) + L"/7";
    case Language::English: return std::to_wstring(value) + L"/7 done";
    case Language::Vietnamese: return L"Đã xong " + std::to_wstring(value) + L"/7";
    }
    return {};
}

[[nodiscard]] bool CursorClientPoint(const HWND window, POINT& point) noexcept
{
    if (!GetCursorPos(&point) || !ScreenToClient(window, &point)) return false;
    RECT client{};
    if (!GetClientRect(window, &client)) return false;
    return PtInRect(&client, point) != FALSE;
}

void AddTrailPoint(Context& context, const POINT point, const TrailKind kind)
{
    bool newStroke = context.trail.empty() || context.trail.back().kind != kind;
    if (!newStroke && kind != TrailKind::Click)
    {
        const LONG dx = point.x - context.trail.back().point.x;
        const LONG dy = point.y - context.trail.back().point.y;
        const long long distance = static_cast<long long>(std::llabs(dx)) + std::llabs(dy);
        const double limit = 300.0 * static_cast<double>(context.dpi) / 96.0;
        if (static_cast<double>(distance) > limit) newStroke = true;
    }
    context.trail.push_back(TrailPoint{point, kind, newStroke});
    if (context.trail.size() > 4096U)
    {
        context.trail.erase(context.trail.begin());
        context.trail.front().newStroke = true;
    }
}

void UpdateTrail(const HWND window, Context& context) noexcept
{
    POINT point{};
    if (!CursorClientPoint(window, point)) return;
    try
    {
        AddTrailPoint(context, point, context.test.LeftDown() ? TrailKind::Drag : TrailKind::Move);
    }
    catch (...) {}
}

void AddClickPoint(const HWND window, Context& context) noexcept
{
    POINT point{};
    if (!CursorClientPoint(window, point)) return;
    try { AddTrailPoint(context, point, TrailKind::Click); } catch (...) {}
}

void DrawTrail(const HWND window, Context& context)
{
    if (context.trail.empty()) return;
    for (std::size_t index = 1U; index < context.trail.size(); ++index)
    {
        const TrailPoint& previous = context.trail[index - 1U];
        const TrailPoint& current = context.trail[index];
        if (current.newStroke || current.kind == TrailKind::Click || previous.kind == TrailKind::Click) continue;
        if (previous.kind != current.kind) continue;
        context.brush->SetColor(Color(current.kind == TrailKind::Drag ? kDragInk : kMoveInk));
        context.target->DrawLine(InkPoint(previous.point, context.dpi), InkPoint(current.point, context.dpi),
            context.brush.Get(), current.kind == TrailKind::Drag ? 4.0F : 2.5F);
    }
    for (const TrailPoint& point : context.trail)
    {
        if (point.newStroke && point.kind != TrailKind::Click)
        {
            context.brush->SetColor(Color(point.kind == TrailKind::Drag ? kDragInk : kMoveInk));
            context.target->FillEllipse(D2D1::Ellipse(InkPoint(point.point, context.dpi), 2.5F, 2.5F),
                context.brush.Get());
        }
        else if (point.kind == TrailKind::Click)
        {
            const D2D1_POINT_2F center = InkPoint(point.point, context.dpi);
            context.brush->SetColor(Color(kClickInk));
            context.target->FillEllipse(D2D1::Ellipse(center, 7.0F, 7.0F), context.brush.Get());
            context.brush->SetColor(Color(0xFFFFFF));
            context.target->DrawEllipse(D2D1::Ellipse(center, 7.0F, 7.0F), context.brush.Get(), 1.5F);
        }
    }
    POINT cursor{};
    if (CursorClientPoint(window, cursor))
    {
        context.brush->SetColor(Color(context.test.LeftDown() ? kDragInk : kMoveInk));
        context.target->FillEllipse(D2D1::Ellipse(InkPoint(cursor, context.dpi), 3.5F, 3.5F),
            context.brush.Get());
    }
}

void Paint(const HWND window, Context& context)
{
    if (FAILED(EnsureResources(window, context))) throw std::runtime_error("mouse test D2D resources failed");
    const D2D1_SIZE_F size = ClientSize(window, context.dpi);
    const Config& config = context.state->config;
    const Language language = config.language;
    const bool passed = context.passed;
    context.target->BeginDraw();
    context.target->Clear(Color(UiRgb(kFluentDesign.window)));
    DrawTrail(window, context);

    const float headerHeight = (std::clamp)(size.height * 0.14F, 108.0F, 136.0F);
    const float margin = (std::clamp)(size.width * 0.03F, 20.0F, 48.0F);
    const float titleTop = (std::max)(6.0F, headerHeight * 0.08F);
    const float subtitleTop = titleTop + 34.0F;
    const float dotCenterY = headerHeight - 28.0F;
    const float labelTop = headerHeight - 16.0F;

    if (context.trail.empty() && !passed)
    {
        DrawText(context, Text(language, L"在整个屏幕上移动鼠标",
            L"Move the mouse anywhere on the screen", L"Di chuyển chuột trên toàn màn hình"),
            context.hintFormat.Get(),
            D2D1::RectF(margin, (headerHeight + size.height) * 0.5F - 16.0F,
                size.width - margin, (headerHeight + size.height) * 0.5F + 20.0F),
            UiRgb(kFluentDesign.secondary));
    }

    context.brush->SetColor(ColorA(UiRgb(kFluentDesign.window), 0.93F));
    context.target->FillRectangle(D2D1::RectF(0.0F, 0.0F, size.width, headerHeight), context.brush.Get());
    context.brush->SetColor(Color(0xE1E5EA));
    context.target->DrawLine(D2D1::Point2F(0.0F, headerHeight), D2D1::Point2F(size.width, headerHeight),
        context.brush.Get(), 1.0F);

    DrawText(context, passed ? Text(language, L"鼠标测试通过", L"Mouse test PASS", L"Kiểm tra chuột đạt") :
        Text(language, L"鼠标测试", L"Mouse Test", L"Kiểm tra chuột"), context.titleFormat.Get(),
        D2D1::RectF(margin, titleTop, size.width * 0.55F, titleTop + 36.0F),
        passed ? kClickInk : UiRgb(kFluentDesign.text));
    DrawText(context, ProgressText(language, context.test.CompletedTasks(), passed),
        context.progressFormat.Get(),
        D2D1::RectF(size.width * 0.55F, titleTop + 2.0F, size.width - margin, titleTop + 26.0F),
        passed ? kClickInk : UiRgb(kFluentDesign.accent));
    DrawText(context, passed ? Text(language, L"全部 7 项已完成，窗口即将关闭",
        L"All 7 tasks completed; the window is closing", L"Đã xong 7 mục; cửa sổ sắp đóng") :
        Text(language, L"请使用普通鼠标完成全部操作 · Esc：失败退出",
            L"Use a standard mouse to complete every task · Esc: fail and exit",
            L"Dùng chuột thường hoàn thành mọi thao tác · Esc: lỗi và thoát"),
        context.subtitleFormat.Get(),
        D2D1::RectF(margin, subtitleTop, size.width - margin, subtitleTop + 20.0F),
        UiRgb(kFluentDesign.secondary));

    const float slotWidth = (size.width - margin * 2.0F) / 7.0F;
    for (unsigned index = 0U; index < 7U; ++index)
    {
        const float centerX = margin + slotWidth * (static_cast<float>(index) + 0.5F);
        const bool done = TaskComplete(context.test, index);
        const float progress = TaskProgress(config, context.test, index);
        context.brush->SetColor(Color(done ? kClickInk : progress > 0.0F ? UiRgb(kFluentDesign.accent) : 0xDCE3EB));
        context.target->FillEllipse(D2D1::Ellipse(D2D1::Point2F(centerX, dotCenterY), 9.0F, 9.0F),
            context.brush.Get());
        if (!done)
        {
            context.brush->SetColor(Color(0xAEB8C4));
            context.target->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(centerX, dotCenterY), 9.0F, 9.0F),
                context.brush.Get(), 1.2F);
        }
        else
        {
            context.brush->SetColor(Color(0xFFFFFF));
            context.target->DrawLine(D2D1::Point2F(centerX - 4.5F, dotCenterY),
                D2D1::Point2F(centerX - 1.5F, dotCenterY + 3.0F), context.brush.Get(), 1.8F);
            context.target->DrawLine(D2D1::Point2F(centerX - 1.5F, dotCenterY + 3.0F),
                D2D1::Point2F(centerX + 5.0F, dotCenterY - 4.0F), context.brush.Get(), 1.8F);
        }
        DrawText(context, TaskTitle(language, index), context.dotLabelFormat.Get(),
            D2D1::RectF(centerX - slotWidth * 0.5F + 2.0F, labelTop,
                centerX + slotWidth * 0.5F - 2.0F, labelTop + 16.0F),
            done ? kClickInk : UiRgb(kFluentDesign.secondary));
    }

    const float legendTop = size.height - 28.0F;
    const float legendRight = (std::min)(size.width - margin, margin + 500.0F);
    context.brush->SetColor(ColorA(0xFFFFFF, 0.86F));
    context.target->FillRoundedRectangle(D2D1::RoundedRect(
        D2D1::RectF(margin - 8.0F, legendTop - 6.0F, legendRight, legendTop + 24.0F), 8.0F, 8.0F),
        context.brush.Get());
    float x = margin + 4.0F;
    context.brush->SetColor(Color(kMoveInk));
    context.target->DrawLine(D2D1::Point2F(x, legendTop + 9.0F), D2D1::Point2F(x + 16.0F, legendTop + 9.0F),
        context.brush.Get(), 2.5F);
    DrawText(context, Text(language, L"移动", L"Move", L"Di chuyển"), context.subtitleFormat.Get(),
        D2D1::RectF(x + 22.0F, legendTop, x + 90.0F, legendTop + 18.0F), UiRgb(kFluentDesign.text));
    x += 92.0F;
    context.brush->SetColor(Color(kDragInk));
    context.target->DrawLine(D2D1::Point2F(x, legendTop + 9.0F), D2D1::Point2F(x + 16.0F, legendTop + 9.0F),
        context.brush.Get(), 4.0F);
    DrawText(context, Text(language, L"拖动", L"Drag", L"Kéo"), context.subtitleFormat.Get(),
        D2D1::RectF(x + 22.0F, legendTop, x + 100.0F, legendTop + 18.0F), UiRgb(kFluentDesign.text));
    x += 102.0F;
    context.brush->SetColor(Color(kClickInk));
    context.target->FillEllipse(D2D1::Ellipse(D2D1::Point2F(x + 6.0F, legendTop + 9.0F), 5.0F, 5.0F),
        context.brush.Get());
    DrawText(context, Text(language, L"左键单击", L"Left click", L"Nhấp trái"), context.subtitleFormat.Get(),
        D2D1::RectF(x + 16.0F, legendTop, x + 120.0F, legendTop + 18.0F), UiRgb(kFluentDesign.text));

    const HRESULT result = context.target->EndDraw();
    if (result == D2DERR_RECREATE_TARGET)
    {
        context.brush.Reset();
        context.target.Reset();
    }
    else if (FAILED(result))
    {
        throw std::runtime_error("mouse test EndDraw failed");
    }
}

void Finish(const HWND window, Context& context, const WindowResult result) noexcept
{
    if (context.finishing) return;
    context.finishing = true;
    context.result = result;
    KillTimer(window, kPassTimer);
    DestroyWindow(window);
}

void CheckComplete(const HWND window, Context& context)
{
    if (context.passed || context.finishing) return;
    InvalidateRect(window, nullptr, FALSE);
    if (!context.test.IsComplete()) return;
    context.passed = true;
    if (context.state->onTrace)
        try { context.state->onTrace(L"普通鼠标全部操作已完成"); } catch (...) {}
    if (context.state->passDisplayMs == 0U ||
        SetTimer(window, kPassTimer, context.state->passDisplayMs, nullptr) == 0U)
    {
        Finish(window, context, WindowResult::Passed);
    }
}

LRESULT CALLBACK WindowProcedure(const HWND window, const UINT message, const WPARAM wParam, const LPARAM lParam)
{
    auto* context = reinterpret_cast<Context*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE)
    {
        const auto* const create = reinterpret_cast<const CREATESTRUCTW*>(lParam);
        const auto* const payload = static_cast<const Payload*>(create->lpCreateParams);
        context = new (std::nothrow) Context(payload->state, payload->error);
        if (context == nullptr)
        {
            if (payload->error != nullptr) *payload->error = L"无法分配鼠标测试窗口状态";
            return FALSE;
        }
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(context));
        context->state->hwnd.store(window, std::memory_order_release);
    }
    if (context == nullptr) return DefWindowProcW(window, message, wParam, lParam);
    try
    {
        switch (message)
        {
        case WM_CREATE:
            context->dpi = GetDpiForWindow(window);
            context->initialized = context->state->skipHardwareCheck;
            context->created = true;
            InvalidateRect(window, nullptr, FALSE);
            return 0;
        case kInitializeMessage:
            if (context->initialized || context->finishing) return 0;
            context->initialized = true;
            {
                std::vector<Device> devices;
                std::wstring error;
                if (!FindGenericMice(devices, error) || !RegisterGenericMouse(window, error))
                {
                    if (context->state->onError) try { context->state->onError(error); } catch (...) {}
                    Finish(window, *context, WindowResult::Failed);
                    return 0;
                }
                context->registered = true;
                std::wstring summary;
                for (const Device& device : devices)
                {
                    if (!summary.empty()) summary += L"; ";
                    summary += device.name + L" (buttons=" + std::to_wstring(device.buttons) + L")";
                }
                if (context->state->onDeviceReady)
                    try { context->state->onDeviceReady(summary); } catch (...) {}
                if (context->state->onTrace)
                    try { context->state->onTrace(L"普通鼠标 Raw Input 已在测试窗口显示并取得焦点后注册"); } catch (...) {}
            }
            return 0;
        case WM_INPUT:
        {
            if (context->passed || context->finishing) return DefWindowProcW(window, message, wParam, lParam);
            if (!context->rawInputReported)
            {
                context->rawInputReported = true;
                if (context->state->onTrace)
                    try { context->state->onTrace(L"鼠标测试窗口已收到首个 WM_INPUT"); } catch (...) {}
            }
            Frame frame;
            std::wstring error;
            if (!ParseMouseFrame(lParam, frame, error))
            {
                if (context->state->onDiagnostic) try { context->state->onDiagnostic(error); } catch (...) {}
                return DefWindowProcW(window, message, wParam, lParam);
            }
            if (frame.absolute && !context->absoluteReported)
            {
                context->absoluteReported = true;
                if (context->state->onDiagnostic)
                    try { context->state->onDiagnostic(L"收到绝对坐标鼠标输入，移动量未计入"); } catch (...) {}
            }
            if (!frame.HasInput()) return DefWindowProcW(window, message, wParam, lParam);

            const ULONGLONG now = GetTickCount64();
            const unsigned clicksBefore = context->test.Current().leftClicks +
                context->test.Current().doubleClicks;
            if (frame.leftDown) context->test.Button(MouseButton::Left, true, now);
            if (frame.rightDown) context->test.Button(MouseButton::Right, true, now);
            if (frame.middleDown) context->test.Button(MouseButton::Middle, true, now);
            if (frame.dx != 0 || frame.dy != 0)
            {
                context->test.Move(frame.dx, frame.dy);
                UpdateTrail(window, *context);
            }
            if (frame.wheel != 0) context->test.Wheel(frame.wheel);
            if (frame.leftUp) context->test.Button(MouseButton::Left, false, now);
            if (frame.rightUp) context->test.Button(MouseButton::Right, false, now);
            if (frame.middleUp) context->test.Button(MouseButton::Middle, false, now);
            const unsigned clicksAfter = context->test.Current().leftClicks +
                context->test.Current().doubleClicks;
            if (clicksAfter > clicksBefore) AddClickPoint(window, *context);

            const LRESULT defaultResult = DefWindowProcW(window, message, wParam, lParam);
            CheckComplete(window, *context);
            return defaultResult;
        }
        case WM_ACTIVATE:
            if (LOWORD(wParam) == WA_INACTIVE)
            {
                context->test.ResetPressed();
                InvalidateRect(window, nullptr, FALSE);
            }
            return 0;
        case WM_KEYDOWN:
            if (wParam == VK_ESCAPE && !context->passed) Finish(window, *context, WindowResult::Failed);
            return 0;
        case WM_CLOSE:
            if (!context->passed) Finish(window, *context, WindowResult::Failed);
            return 0;
        case WM_TIMER:
            if (wParam == kPassTimer) Finish(window, *context, WindowResult::Passed);
            return 0;
        case WM_DPICHANGED:
        {
            context->dpi = HIWORD(wParam);
            if (context->target)
                context->target->SetDpi(static_cast<float>(context->dpi), static_cast<float>(context->dpi));
            const RECT* const rect = reinterpret_cast<const RECT*>(lParam);
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
            PAINTSTRUCT paint{};
            BeginPaint(window, &paint);
            Paint(window, *context);
            EndPaint(window, &paint);
            return 0;
        }
        case WM_NCDESTROY:
        {
            if (context->registered) UnregisterGenericMouse();
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
        value.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        value.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
        return RegisterClassExW(&value) != 0U || GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
    }();
    if (!registered) error = L"RegisterClassExW(鼠标窗口) 失败，Win32=" + std::to_wstring(GetLastError());
    return registered;
}
} // namespace

HWND CreateMouseWindow(const HWND owner, const std::shared_ptr<WindowState>& state, const bool showWindow,
    std::wstring& error)
{
    if (!state || !RegisterClassOnce(error)) return nullptr;
    const HMONITOR monitor = MonitorFromWindow(owner, MONITOR_DEFAULTTOPRIMARY);
    MONITORINFO information{sizeof(information)};
    if (!GetMonitorInfoW(monitor, &information)) { error = L"GetMonitorInfoW 失败"; return nullptr; }
    Payload payload{state, &error};
    const RECT rect = information.rcMonitor;
    const HWND window = CreateWindowExW(WS_EX_APPWINDOW | WS_EX_TOPMOST, kClassName, L"MouseTest - Mouse", WS_POPUP,
        rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top, owner, nullptr,
        GetModuleHandleW(nullptr), &payload);
    if (!window)
    {
        if (error.empty()) error = L"CreateWindowExW(鼠标窗口) 失败，Win32=" + std::to_wstring(GetLastError());
        return nullptr;
    }
    if (showWindow)
    {
        ShowWindow(window, SW_SHOW);
        UpdateWindow(window);
        SetWindowPos(window, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
        SetForegroundWindow(window);
        SetActiveWindow(window);
        SetFocus(window);
    }
    if (!state->skipHardwareCheck && !PostMessageW(window, kInitializeMessage, 0U, 0))
    {
        error = L"PostMessageW(初始化鼠标) 失败，Win32=" + std::to_wstring(GetLastError());
        state->submitted.store(true, std::memory_order_release);
        DestroyWindow(window);
        return nullptr;
    }
    return window;
}

bool SubmitMoveForTesting(const HWND window, const LONG dx, const LONG dy) noexcept
{
    if (!window || GetWindowThreadProcessId(window, nullptr) != GetCurrentThreadId()) return false;
    auto* const context = reinterpret_cast<Context*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (context == nullptr || context->finishing || context->passed) return false;
    try
    {
        context->test.Move(dx, dy);
        UpdateTrail(window, *context);
        CheckComplete(window, *context);
        return true;
    }
    catch (...) { return false; }
}

bool SubmitWheelForTesting(const HWND window, const int delta) noexcept
{
    if (!window || GetWindowThreadProcessId(window, nullptr) != GetCurrentThreadId()) return false;
    auto* const context = reinterpret_cast<Context*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (context == nullptr || context->finishing || context->passed) return false;
    try
    {
        context->test.Wheel(delta);
        CheckComplete(window, *context);
        return true;
    }
    catch (...) { return false; }
}

bool SubmitButtonForTesting(const HWND window, const MouseButton button, const bool down) noexcept
{
    if (!window || GetWindowThreadProcessId(window, nullptr) != GetCurrentThreadId()) return false;
    auto* const context = reinterpret_cast<Context*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (context == nullptr || context->finishing || context->passed) return false;
    try
    {
        const unsigned before = context->test.Current().leftClicks + context->test.Current().doubleClicks;
        context->test.Button(button, down, GetTickCount64());
        const unsigned after = context->test.Current().leftClicks + context->test.Current().doubleClicks;
        if (after > before) AddClickPoint(window, *context);
        CheckComplete(window, *context);
        return true;
    }
    catch (...) { return false; }
}
} // namespace ftc::modules::mouse
