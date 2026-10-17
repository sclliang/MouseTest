#include "modules/touch_pad/TouchPadWindow.h"

#include <windows.h>

#include <atomic>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>

namespace
{
using ftc::modules::touch_pad::Config;
using ftc::modules::touch_pad::CreateTouchPadWindow;
using ftc::modules::touch_pad::CancelWindow;
using ftc::modules::touch_pad::SubmitClickForTesting;
using ftc::modules::touch_pad::SubmitContactForTesting;
using ftc::modules::touch_pad::WindowResult;
using ftc::modules::touch_pad::WindowState;

void Check(const bool value, const char* const message)
{
    if (!value) throw std::runtime_error(message);
}

struct ResultSink final
{
    std::atomic<unsigned> count{};
    WindowResult result{WindowResult::Failed};
};

[[nodiscard]] std::shared_ptr<WindowState> MakeState(const Config& config, ResultSink& sink)
{
    auto state = std::make_shared<WindowState>();
    state->config = config;
    state->skipHardwareCheck = true;
    state->onFinished = [&sink](const WindowResult value) {
        sink.result = value;
        sink.count.fetch_add(1U, std::memory_order_acq_rel);
    };
    return state;
}

[[nodiscard]] HWND CreateHidden(const std::shared_ptr<WindowState>& state)
{
    std::wstring error;
    HWND window = CreateTouchPadWindow(nullptr, state, false, error);
    Check(window != nullptr, error.empty() ? "test window unavailable" : "test window creation failed");
    return window;
}
} // namespace

int main()
{
    using namespace ftc::modules::touch_pad;
    try
    {
        SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        Config config;
        config.rows = 2U;
        config.columns = 2U;
        config.leftClicks = 1U;
        config.rightClicks = 1U;

        ResultSink passSink;
        auto passState = MakeState(config, passSink);
        HWND passWindow = CreateHidden(passState);
        RECT client{};
        Check(GetClientRect(passWindow, &client) != FALSE, "client bounds unavailable");
        static_cast<void>(SendMessageW(passWindow, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(client.right - 1, 1)));
        Check(IsWindow(passWindow) != FALSE && passSink.count.load(std::memory_order_acquire) == 0U,
            "left click outside the grid closed the window");
        for (const double y : {0.25, 0.75})
        {
            for (const double x : {0.25, 0.75})
                Check(SubmitContactForTesting(passWindow, x, y), "contact injection failed");
        }
        Check(passSink.count.load(std::memory_order_acquire) == 0U, "window completed before the right click");
        Check(SubmitClickForTesting(passWindow, false), "right click injection failed");
        Check(passSink.count.load(std::memory_order_acquire) == 1U &&
            passSink.result == WindowResult::Passed && IsWindow(passWindow) == FALSE,
            "complete flow did not pass");

        ResultSink escapeSink;
        auto escapeState = MakeState(config, escapeSink);
        HWND escapeWindow = CreateHidden(escapeState);
        static_cast<void>(SendMessageW(escapeWindow, WM_KEYDOWN, VK_ESCAPE, 0));
        Check(escapeSink.count.load(std::memory_order_acquire) == 1U &&
            escapeSink.result == WindowResult::Failed && IsWindow(escapeWindow) == FALSE,
            "Esc did not close with one FAIL");

        ResultSink cancelSink;
        auto cancelState = MakeState(config, cancelSink);
        HWND cancelWindow = CreateHidden(cancelState);
        CancelWindow(cancelState);
        Check(cancelSink.count.load(std::memory_order_acquire) == 1U &&
            cancelSink.result == WindowResult::Cancelled && IsWindow(cancelWindow) == FALSE,
            "standalone cancel did not report CANCEL");

        ResultSink closeSink;
        auto closeState = MakeState(config, closeSink);
        HWND closeWindow = CreateHidden(closeState);
        static_cast<void>(SendMessageW(closeWindow, WM_CLOSE, 0, 0));
        Check(closeSink.count.load(std::memory_order_acquire) == 1U &&
            closeSink.result == WindowResult::Failed && IsWindow(closeWindow) == FALSE,
            "WM_CLOSE did not report FAIL");

        std::cout << "MouseTestWindowTests passed\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "MouseTestWindowTests failed: " << error.what() << '\n';
        return 1;
    }
}
