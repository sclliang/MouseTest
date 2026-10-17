#include "modules/mouse/MouseWindow.h"

#include "modules/mouse/MouseRawInput.h"

#include <fluentui/design.hpp>

#include <d2d1.h>
#include <dwrite.h>
#include <wrl/client.h>

#include <algorithm>
#include <cmath>
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

[[nodiscard]] float Ratio(const unsigned value, const unsigned target) noexcept
{
    if (target == 0U) return 1.0F;
    return (std::min)(1.0F, static_cast<float>(value) / static_cast<float>(target));
}

struct Payload final
{
    std::shared_ptr<WindowState> state;
    std::wstring* error{};
};

struct Context final
{
    Context(std::shared_ptr<WindowState> value, std::wstring* target)
        : state(std::move(value)), creationError(target),
          test(state->config, GetDoubleClickTime()) {}

    std::shared_ptr<WindowState> state;
    std::wstring* creationError{};
    TestState test;
    std::vector<POINT> trail;
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
    ComPtr<IDWriteTextFormat> rowTitleFormat;
    ComPtr<IDWriteTextFormat> rowDetailFormat;
    ComPtr<IDWriteTextFormat> liveFormat;
    ComPtr<IDWriteTextFormat> cardLiveFormat;
    ComPtr<IDWriteTextFormat> numberFormat;
    ComPtr<IDWriteTextFormat> sectionFormat;
};

void DrawText(Context& context, const std::wstring_view value, IDWriteTextFormat* const format,
    const D2D1_RECT_F rect, const unsigned rgb)
{
    if (value.empty()) return;
    context.brush->SetColor(Color(rgb));
    context.target->DrawTextW(value.data(), static_cast<UINT32>(value.size()), format, rect,
        context.brush.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
}

void DrawProgressBar(Context& context, const D2D1_RECT_F bar, const float ratio)
{
    context.brush->SetColor(Color(0xE6EBF1));
    context.target->FillRoundedRectangle(D2D1::RoundedRect(bar, 2.0F, 2.0F), context.brush.Get());
    const float width = (std::max)(0.0F, (std::min)(1.0F, ratio));
    if (width <= 0.0F) return;
    D2D1_RECT_F filled = bar;
    filled.right = bar.left + (bar.right - bar.left) * width;
    context.brush->SetColor(Color(width >= 0.999F ? 0x107C10 : UiRgb(kFluentDesign.accent)));
    context.target->FillRoundedRectangle(D2D1::RoundedRect(filled, 2.0F, 2.0F), context.brush.Get());
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
        result = make(30.0F, DWRITE_FONT_WEIGHT_SEMI_BOLD, context.titleFormat);
        if (SUCCEEDED(result)) result = make(14.0F, DWRITE_FONT_WEIGHT_NORMAL, context.subtitleFormat);
        if (SUCCEEDED(result)) result = make(16.0F, DWRITE_FONT_WEIGHT_SEMI_BOLD, context.rowTitleFormat);
        if (SUCCEEDED(result)) result = make(11.5F, DWRITE_FONT_WEIGHT_NORMAL, context.rowDetailFormat);
        if (SUCCEEDED(result)) result = make(14.0F, DWRITE_FONT_WEIGHT_SEMI_BOLD, context.liveFormat);
        if (SUCCEEDED(result)) result = make(24.0F, DWRITE_FONT_WEIGHT_SEMI_BOLD, context.cardLiveFormat);
        if (SUCCEEDED(result)) result = make(13.0F, DWRITE_FONT_WEIGHT_SEMI_BOLD, context.numberFormat);
        if (SUCCEEDED(result)) result = make(15.0F, DWRITE_FONT_WEIGHT_SEMI_BOLD, context.sectionFormat);
        if (FAILED(result)) return result;
        static_cast<void>(context.liveFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_TRAILING));
        static_cast<void>(context.numberFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER));
        static_cast<void>(context.numberFormat->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER));
        static_cast<void>(context.sectionFormat->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER));
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

[[nodiscard]] std::wstring CountDetail(const Language language, const unsigned count)
{
    if (count == 0U) return Text(language, L"已禁用", L"Disabled", L"Đã tắt");
    if (language == Language::English)
        return std::to_wstring(count) + (count == 1U ? L" time" : L" times");
    return std::to_wstring(count) + Text(language, L" 次", L"", L" lần");
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

[[nodiscard]] std::wstring TaskDetail(const Config& config, const unsigned index)
{
    const Language language = config.language;
    switch (index)
    {
    case 0U:
        if (config.moveDistance == 0U && config.moveAxisDistance == 0U)
            return Text(language, L"已禁用", L"Disabled", L"Đã tắt");
        return Text(language, L"水平/垂直各 ", L"Each axis ", L"Mỗi trục ") +
            std::to_wstring(config.moveAxisDistance) +
            Text(language, L"，总计 ", L", total ", L", tổng ") +
            std::to_wstring(config.moveDistance);
    case 1U:
        if (config.wheelSteps == 0U) return Text(language, L"已禁用", L"Disabled", L"Đã tắt");
        if (language == Language::English)
            return L"Up/down each " + std::to_wstring(config.wheelSteps) +
                (config.wheelSteps == 1U ? L" notch" : L" notches");
        return Text(language, L"向上/向下各 ", L"", L"Lên/xuống mỗi hướng ") +
            std::to_wstring(config.wheelSteps) + Text(language, L" 格", L"", L" nấc");
    case 2U: return CountDetail(language, config.leftClicks);
    case 3U:
        if (config.doubleClicks == 0U) return Text(language, L"已禁用", L"Disabled", L"Đã tắt");
        if (language == Language::English)
            return config.doubleClicks == 1U ? L"Double-click quickly once" :
                L"Double-click quickly " + std::to_wstring(config.doubleClicks) + L" times";
        return Text(language, L"快速双击 ", L"", L"Nhấp đúp nhanh ") +
            std::to_wstring(config.doubleClicks) + Text(language, L" 次", L"", L" lần");
    case 4U: return CountDetail(language, config.rightClicks);
    case 5U: return CountDetail(language, config.middleClicks);
    default:
        if (config.drags == 0U) return Text(language, L"已禁用", L"Disabled", L"Đã tắt");
        if (language == Language::English)
            return L"Hold left and drag " + std::to_wstring(config.dragDistance) + L" counts";
        return Text(language, L"按住左键拖动 ", L"", L"Giữ trái và kéo ") +
            std::to_wstring(config.dragDistance) + Text(language, L" 计数", L"", L" đơn vị");
    }
}

[[nodiscard]] std::wstring TaskLive(const Config& config, const TestState& test, const unsigned index)
{
    const Language language = config.language;
    const Progress& progress = test.Current();
    switch (index)
    {
    case 0U:
        if (config.moveDistance == 0U) return L"—";
        return Text(language, L"总 ", L"total ", L"tổng ") + std::to_wstring(progress.moveTotal) +
            L"/" + std::to_wstring(config.moveDistance);
    case 1U:
        if (config.wheelSteps == 0U) return L"—";
        return L"↑ " + std::to_wstring(progress.wheelUp / 120U) + L"/" + std::to_wstring(config.wheelSteps) +
            L" · ↓ " + std::to_wstring(progress.wheelDown / 120U) + L"/" + std::to_wstring(config.wheelSteps);
    case 2U:
        return config.leftClicks == 0U ? L"—" : std::to_wstring(progress.leftClicks) + L"/" + std::to_wstring(config.leftClicks);
    case 3U:
        return config.doubleClicks == 0U ? L"—" : std::to_wstring(progress.doubleClicks) + L"/" + std::to_wstring(config.doubleClicks);
    case 4U:
        return config.rightClicks == 0U ? L"—" : std::to_wstring(progress.rightClicks) + L"/" + std::to_wstring(config.rightClicks);
    case 5U:
        return config.middleClicks == 0U ? L"—" : std::to_wstring(progress.middleClicks) + L"/" + std::to_wstring(config.middleClicks);
    default:
        return config.drags == 0U ? L"—" : std::to_wstring(progress.drags) + L"/" + std::to_wstring(config.drags);
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

[[nodiscard]] std::wstring TipOne(const Config& config)
{
    const std::wstring distance = std::to_wstring(config.dragDistance);
    switch (config.language)
    {
    case Language::Chinese:
        return L"单击：按下后原地松开；拖动：按住左键移动超过 " + distance + L" 计数后松开";
    case Language::English:
        return L"Click: press and release in place; Drag: hold left and move more than " + distance + L" counts";
    case Language::Vietnamese:
        return L"Nhấp: nhấn và nhả tại chỗ; Kéo: giữ trái và di chuyển hơn " + distance + L" đơn vị";
    }
    return {};
}

[[nodiscard]] std::wstring TipTwo(const Language language)
{
    switch (language)
    {
    case Language::Chinese: return L"双击间隔按系统设置；各项可任意顺序完成";
    case Language::English: return L"Double-click uses the system interval; complete tasks in any order";
    case Language::Vietnamese: return L"Nhấp đúp theo cài đặt hệ thống; có thể làm theo thứ tự bất kỳ";
    }
    return {};
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

void UpdateTrail(const HWND window, Context& context)
{
    POINT point{};
    if (!GetCursorPos(&point) || !ScreenToClient(window, &point)) return;
    if (context.trail.size() >= 96U) context.trail.erase(context.trail.begin());
    context.trail.push_back(point);
}

void DrawTrail(const Context& context, const D2D1_RECT_F trail, const D2D1_SIZE_F size, const UINT dpi)
{
    if (context.trail.size() < 2U) return;
    const auto map = [&](const POINT point) {
        const float x = (static_cast<float>(point.x) * 96.0F / static_cast<float>(dpi == 0U ? 96U : dpi)) /
            (std::max)(1.0F, size.width);
        const float y = (static_cast<float>(point.y) * 96.0F / static_cast<float>(dpi == 0U ? 96U : dpi)) /
            (std::max)(1.0F, size.height);
        return D2D1::Point2F(
            trail.left + (std::clamp)(x, 0.0F, 1.0F) * (trail.right - trail.left),
            trail.top + (std::clamp)(y, 0.0F, 1.0F) * (trail.bottom - trail.top));
    };
    context.brush->SetColor(Color(0x4A86C8));
    for (std::size_t index = 1U; index < context.trail.size(); ++index)
        context.target->DrawLine(map(context.trail[index - 1U]), map(context.trail[index]),
            context.brush.Get(), 1.5F);
    context.brush->SetColor(Color(UiRgb(kFluentDesign.accent)));
    context.target->FillEllipse(
        D2D1::Ellipse(map(context.trail.back()), 4.0F, 4.0F), context.brush.Get());
}

void DrawButton(Context& context, const D2D1_POINT_2F center, const wchar_t* const label,
    const bool down, const bool done)
{
    const unsigned fill = down ? UiRgb(kFluentDesign.accent) : done ? 0x107C10 : 0xE8EDF3;
    context.brush->SetColor(Color(fill));
    context.target->FillEllipse(D2D1::Ellipse(center, 20.0F, 20.0F), context.brush.Get());
    context.brush->SetColor(Color(down || done ? 0xFFFFFF : UiRgb(kFluentDesign.text)));
    const D2D1_RECT_F rect = D2D1::RectF(center.x - 20.0F, center.y - 10.0F, center.x + 20.0F, center.y + 10.0F);
    context.target->DrawTextW(label, static_cast<UINT32>(wcslen(label)), context.numberFormat.Get(), rect,
        context.brush.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
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

    const float margin = (std::clamp)(size.width * 0.035F, 24.0F, 64.0F);
    const float titleTop = (std::max)(18.0F, size.height * 0.03F);
    const float contentTop = titleTop + 82.0F;
    const float contentBottom = size.height - margin;
    const bool wide = size.width >= 980.0F;
    const float listRight = wide ? size.width * 0.58F : size.width - margin;

    DrawText(context, passed ? Text(language, L"鼠标测试通过", L"Mouse test PASS", L"Kiểm tra chuột đạt") :
        Text(language, L"鼠标测试", L"Mouse Test", L"Kiểm tra chuột"),
        context.titleFormat.Get(), D2D1::RectF(margin, titleTop, listRight, titleTop + 42.0F),
        passed ? 0x107C10 : UiRgb(kFluentDesign.text));
    DrawText(context, passed ? Text(language, L"全部 7 项已完成，窗口即将关闭",
        L"All 7 tasks completed; the window is closing", L"Đã xong 7 mục; cửa sổ sắp đóng") :
        Text(language, L"请使用普通鼠标完成全部操作 · Esc：失败退出",
            L"Use a standard mouse to complete every task · Esc: fail and exit",
            L"Dùng chuột thường hoàn thành mọi thao tác · Esc: lỗi và thoát"),
        context.subtitleFormat.Get(), D2D1::RectF(margin, titleTop + 44.0F, size.width - margin, titleTop + 70.0F),
        UiRgb(kFluentDesign.secondary));
    const std::wstring progressText = ProgressText(language, context.test.CompletedTasks(), passed);
    DrawText(context, progressText, context.liveFormat.Get(),
        D2D1::RectF(size.width * 0.55F, titleTop + 8.0F, size.width - margin, titleTop + 32.0F),
        passed ? 0x107C10 : UiRgb(kFluentDesign.accent));

    const bool twoColumns = wide && size.height >= 680.0F;
    const unsigned columns = twoColumns ? 2U : 1U;
    const unsigned rows = twoColumns ? 4U : 7U;
    const float gapX = 14.0F;
    const float gapY = (std::clamp)(size.height * 0.008F, 4.0F, 10.0F);
    const float availableWidth = listRight - margin;
    const float cardWidth = twoColumns ? (availableWidth - gapX) * 0.5F : availableWidth;
    float cardHeight = twoColumns
        ? (contentBottom - contentTop - gapY * 3.0F) / 4.0F
        : (contentBottom - contentTop - gapY * 6.0F) / 7.0F;
    cardHeight = (std::min)(cardHeight, twoColumns ? 160.0F : 68.0F);
    cardHeight = (std::max)(20.0F, cardHeight);
    const float totalHeight = static_cast<float>(rows) * cardHeight + static_cast<float>(rows - 1U) * gapY;
    constexpr float footerHeight = 74.0F;
    constexpr float footerGap = 12.0F;
    const float availableHeight = contentBottom - contentTop;
    const bool showFooter = twoColumns && totalHeight + footerGap + footerHeight <= availableHeight;
    const float contentHeight = totalHeight + (showFooter ? footerGap + footerHeight : 0.0F);
    const float listTop = contentTop + (std::max)(0.0F, (availableHeight - contentHeight) * 0.5F);
    const bool compact = cardHeight < (twoColumns ? 112.0F : 40.0F);

    for (unsigned index = 0U; index < 7U; ++index)
    {
        const unsigned row = index / columns;
        const unsigned column = index % columns;
        const float left = margin + static_cast<float>(column) * (cardWidth + gapX);
        const float top = listTop + static_cast<float>(row) * (cardHeight + gapY);
        const D2D1_RECT_F card = D2D1::RectF(left, top, left + cardWidth, top + cardHeight);
        const bool done = TaskComplete(context.test, index);
        context.brush->SetColor(Color(done ? 0xEAF7EE : 0xFFFFFF));
        context.target->FillRoundedRectangle(D2D1::RoundedRect(card, 10.0F, 10.0F), context.brush.Get());
        context.brush->SetColor(Color(done ? 0x107C10 : UiRgb(kFluentControls.bottomBorder)));
        context.target->DrawRoundedRectangle(D2D1::RoundedRect(card, 10.0F, 10.0F), context.brush.Get(), 1.0F);

        const float radius = twoColumns ? 14.0F : (std::min)(12.0F, cardHeight * 0.28F);
        const float centerY = twoColumns ? top + 22.0F : top + cardHeight * 0.5F;
        const float badgeX = twoColumns ? left + 24.0F : margin + 20.0F;
        context.brush->SetColor(Color(done ? 0x107C10 : 0xE8F0FA));
        context.target->FillEllipse(D2D1::Ellipse(D2D1::Point2F(badgeX, centerY), radius, radius),
            context.brush.Get());
        DrawText(context, std::to_wstring(index + 1U), context.numberFormat.Get(),
            D2D1::RectF(badgeX - radius, centerY - radius, badgeX + radius, centerY + radius),
            done ? 0xFFFFFF : UiRgb(kFluentDesign.accent));

        if (twoColumns)
        {
            const float textLeft = left + 48.0F;
            DrawText(context, TaskTitle(language, index), context.rowTitleFormat.Get(),
                D2D1::RectF(textLeft, top + 10.0F, left + cardWidth - 16.0F, top + 34.0F),
                done ? 0x107C10 : UiRgb(kFluentDesign.text));
            if (!compact)
            {
                DrawText(context, TaskDetail(config, index), context.rowDetailFormat.Get(),
                    D2D1::RectF(textLeft, top + 36.0F, left + cardWidth - 16.0F, top + cardHeight - 62.0F),
                    UiRgb(kFluentDesign.secondary));
            }
            DrawText(context, TaskLive(config, context.test, index), context.cardLiveFormat.Get(),
                D2D1::RectF(left + 20.0F, top + cardHeight - 54.0F, left + cardWidth - 20.0F,
                    top + cardHeight - 24.0F),
                done ? 0x107C10 : UiRgb(kFluentDesign.accent));
            DrawProgressBar(context,
                D2D1::RectF(left + 20.0F, top + cardHeight - 16.0F, left + cardWidth - 20.0F,
                    top + cardHeight - 12.0F),
                TaskProgress(config, context.test, index));
        }
        else
        {
            const float textLeft = margin + 40.0F;
            const float titleY = top + (compact ? 2.0F : 7.0F);
            DrawText(context, TaskTitle(language, index), context.rowTitleFormat.Get(),
                D2D1::RectF(textLeft, titleY, listRight - 190.0F, titleY + 22.0F),
                done ? 0x107C10 : UiRgb(kFluentDesign.text));
            if (!compact)
            {
                DrawText(context, TaskDetail(config, index), context.rowDetailFormat.Get(),
                    D2D1::RectF(textLeft, top + 26.0F, listRight - 190.0F, top + cardHeight - 10.0F),
                    UiRgb(kFluentDesign.secondary));
            }
            DrawText(context, TaskLive(config, context.test, index), context.liveFormat.Get(),
                D2D1::RectF(listRight - 185.0F, top + 8.0F, listRight - 14.0F, top + 28.0F),
                done ? 0x107C10 : UiRgb(kFluentDesign.accent));
            if (!compact)
            {
                DrawProgressBar(context,
                    D2D1::RectF(textLeft, top + cardHeight - 8.0F, listRight - 14.0F,
                        top + cardHeight - 4.0F),
                    TaskProgress(config, context.test, index));
            }
        }
    }

    if (showFooter)
    {
        const float footerTop = listTop + totalHeight + footerGap;
        const D2D1_RECT_F footer = D2D1::RectF(margin, footerTop, listRight, footerTop + footerHeight);
        context.brush->SetColor(Color(0xF7F9FC));
        context.target->FillRoundedRectangle(D2D1::RoundedRect(footer, 10.0F, 10.0F), context.brush.Get());
        context.brush->SetColor(Color(0xE6EBF1));
        context.target->DrawRoundedRectangle(D2D1::RoundedRect(footer, 10.0F, 10.0F), context.brush.Get(), 1.0F);
        DrawText(context, Text(language, L"提示", L"Tips", L"Gợi ý"), context.rowTitleFormat.Get(),
            D2D1::RectF(footer.left + 16.0F, footer.top + 5.0F, footer.right - 16.0F, footer.top + 27.0F),
            UiRgb(kFluentDesign.text));
        DrawText(context, TipOne(config), context.rowDetailFormat.Get(),
            D2D1::RectF(footer.left + 16.0F, footer.top + 28.0F, footer.right - 16.0F, footer.top + 46.0F),
            UiRgb(kFluentDesign.secondary));
        DrawText(context, TipTwo(language), context.rowDetailFormat.Get(),
            D2D1::RectF(footer.left + 16.0F, footer.top + 47.0F, footer.right - 16.0F, footer.top + 66.0F),
            UiRgb(kFluentDesign.secondary));
    }

    if (wide)
    {
        const D2D1_RECT_F panel = D2D1::RectF(size.width * 0.605F, contentTop, size.width - margin, contentBottom);
        context.brush->SetColor(Color(0xFFFFFF));
        context.target->FillRoundedRectangle(D2D1::RoundedRect(panel, 12.0F, 12.0F), context.brush.Get());
        context.brush->SetColor(Color(UiRgb(kFluentControls.bottomBorder)));
        context.target->DrawRoundedRectangle(D2D1::RoundedRect(panel, 12.0F, 12.0F), context.brush.Get(), 1.0F);
        DrawText(context, Text(language, L"活动", L"Activity", L"Hoạt động"), context.sectionFormat.Get(),
            D2D1::RectF(panel.left + 20.0F, panel.top + 12.0F, panel.right - 20.0F, panel.top + 38.0F),
            UiRgb(kFluentDesign.text));

        const float buttonsY = panel.bottom - 92.0F;
        const D2D1_RECT_F trail = D2D1::RectF(panel.left + 20.0F, panel.top + 46.0F,
            panel.right - 20.0F, buttonsY - 24.0F);
        context.brush->SetColor(Color(0xF7F9FC));
        context.target->FillRoundedRectangle(D2D1::RoundedRect(trail, 8.0F, 8.0F), context.brush.Get());
        context.brush->SetColor(Color(0xE6EBF1));
        context.target->DrawRoundedRectangle(D2D1::RoundedRect(trail, 8.0F, 8.0F), context.brush.Get(), 1.0F);
        DrawTrail(context, trail, size, context.dpi);
        if (context.trail.empty())
        {
            DrawText(context, Text(language, L"移动鼠标以显示轨迹", L"Move the mouse to show the trail",
                L"Di chuyển chuột để hiện vết"), context.rowDetailFormat.Get(),
                D2D1::RectF(trail.left + 12.0F, trail.top + (trail.bottom - trail.top) * 0.5F - 10.0F,
                    trail.right - 12.0F, trail.bottom - 10.0F), UiRgb(kFluentDesign.secondary));
        }

        const D2D1_POINT_2F left = D2D1::Point2F(panel.left + 44.0F, buttonsY);
        const D2D1_POINT_2F right = D2D1::Point2F(panel.left + 114.0F, buttonsY);
        const D2D1_POINT_2F middle = D2D1::Point2F(panel.left + 184.0F, buttonsY);
        DrawButton(context, left, L"L", context.test.LeftDown(), context.test.LeftComplete());
        DrawButton(context, right, L"R", context.test.RightDown(), context.test.RightComplete());
        DrawButton(context, middle, L"M", context.test.MiddleDown(), context.test.MiddleComplete());

        const Progress& value = context.test.Current();
        const std::wstring buttons = L"L " + std::to_wstring(value.leftClicks) + L"/" + std::to_wstring(config.leftClicks) +
            L"  R " + std::to_wstring(value.rightClicks) + L"/" + std::to_wstring(config.rightClicks) +
            L"  M " + std::to_wstring(value.middleClicks) + L"/" + std::to_wstring(config.middleClicks);
        DrawText(context, buttons, context.subtitleFormat.Get(),
            D2D1::RectF(panel.left + 20.0F, panel.bottom - 64.0F, panel.right - 20.0F, panel.bottom - 40.0F),
            UiRgb(kFluentDesign.text));
        const std::wstring wheel = L"↑ " + std::to_wstring(value.wheelUp / 120U) + L"/" + std::to_wstring(config.wheelSteps) +
            L"  ↓ " + std::to_wstring(value.wheelDown / 120U) + L"/" + std::to_wstring(config.wheelSteps);
        DrawText(context, wheel, context.subtitleFormat.Get(),
            D2D1::RectF(panel.left + 20.0F, panel.bottom - 40.0F, panel.right - 20.0F, panel.bottom - 16.0F),
            UiRgb(kFluentDesign.secondary));
    }

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
    const HWND window = CreateWindowExW(WS_EX_APPWINDOW, kClassName, L"MouseTest - Mouse", WS_POPUP,
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
        SetWindowPos(window, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
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
        context->test.Button(button, down, GetTickCount64());
        CheckComplete(window, *context);
        return true;
    }
    catch (...) { return false; }
}
} // namespace ftc::modules::mouse
