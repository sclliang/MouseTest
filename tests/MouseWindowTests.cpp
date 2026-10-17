#include "modules/mouse/MouseWindow.h"

#include <windows.h>

#include <atomic>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>

namespace
{
using ftc::modules::mouse::Config;
using ftc::modules::mouse::CreateMouseWindow;
using ftc::modules::mouse::MouseButton;
using ftc::modules::mouse::SubmitButtonForTesting;
using ftc::modules::mouse::SubmitMoveForTesting;
using ftc::modules::mouse::SubmitWheelForTesting;
using ftc::modules::mouse::WindowResult;
using ftc::modules::mouse::WindowState;

void Check(const bool value, const char* const message)
{
    if (!value) throw std::runtime_error(message);
}

struct ResultSink final
{
    std::atomic<unsigned> count{};
    WindowResult result{WindowResult::Failed};
};

[[nodiscard]] Config TestConfig()
{
    Config config;
    config.moveDistance = 100U;
    config.moveAxisDistance = 40U;
    config.wheelSteps = 1U;
    config.leftClicks = 1U;
    config.doubleClicks = 1U;
    config.rightClicks = 1U;
    config.middleClicks = 1U;
    config.drags = 1U;
    config.dragDistance = 50U;
    config.clickTolerance = 5U;
    return config;
}

[[nodiscard]] std::shared_ptr<WindowState> MakeState(ResultSink& sink)
{
    auto state = std::make_shared<WindowState>();
    state->config = TestConfig();
    state->skipHardwareCheck = true;
    state->passDisplayMs = 0U;
    state->onFinished = [&sink](const WindowResult value) {
        sink.result = value;
        sink.count.fetch_add(1U, std::memory_order_acq_rel);
    };
    return state;
}

[[nodiscard]] HWND CreateHidden(const std::shared_ptr<WindowState>& state)
{
    std::wstring error;
    const HWND window = CreateMouseWindow(nullptr, state, false, error);
    Check(window != nullptr, error.empty() ? "mouse test window unavailable" : "mouse test window creation failed");
    return window;
}
} // namespace

int main()
{
    try
    {
        SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

        ResultSink passSink;
        auto passState = MakeState(passSink);
        HWND passWindow = CreateHidden(passState);
        Check(SubmitMoveForTesting(passWindow, 100, 100), "move injection failed");
        Check(SubmitWheelForTesting(passWindow, 120), "wheel up injection failed");
        Check(SubmitWheelForTesting(passWindow, -120), "wheel down injection failed");
        Check(SubmitButtonForTesting(passWindow, MouseButton::Left, true), "left down injection failed");
        Check(SubmitButtonForTesting(passWindow, MouseButton::Left, false), "left up injection failed");
        Check(SubmitButtonForTesting(passWindow, MouseButton::Left, true), "double down injection failed");
        Check(SubmitButtonForTesting(passWindow, MouseButton::Left, false), "double up injection failed");
        Check(SubmitButtonForTesting(passWindow, MouseButton::Right, true), "right down injection failed");
        Check(SubmitButtonForTesting(passWindow, MouseButton::Right, false), "right up injection failed");
        Check(SubmitButtonForTesting(passWindow, MouseButton::Middle, true), "middle down injection failed");
        Check(SubmitButtonForTesting(passWindow, MouseButton::Middle, false), "middle up injection failed");
        Check(passSink.count.load(std::memory_order_acquire) == 0U, "window completed before the drag");
        Check(SubmitButtonForTesting(passWindow, MouseButton::Left, true), "drag down injection failed");
        Check(SubmitMoveForTesting(passWindow, 60, 0), "drag move injection failed");
        Check(SubmitButtonForTesting(passWindow, MouseButton::Left, false), "drag up injection failed");
        Check(passSink.count.load(std::memory_order_acquire) == 1U &&
            passSink.result == WindowResult::Passed && IsWindow(passWindow) == FALSE,
            "complete mouse flow did not pass");

        ResultSink escapeSink;
        auto escapeState = MakeState(escapeSink);
        HWND escapeWindow = CreateHidden(escapeState);
        static_cast<void>(SendMessageW(escapeWindow, WM_KEYDOWN, VK_ESCAPE, 0));
        Check(escapeSink.count.load(std::memory_order_acquire) == 1U &&
            escapeSink.result == WindowResult::Failed && IsWindow(escapeWindow) == FALSE,
            "Esc did not close the mouse test with one FAIL");

        ResultSink closeSink;
        auto closeState = MakeState(closeSink);
        HWND closeWindow = CreateHidden(closeState);
        static_cast<void>(SendMessageW(closeWindow, WM_CLOSE, 0, 0));
        Check(closeSink.count.load(std::memory_order_acquire) == 1U &&
            closeSink.result == WindowResult::Failed && IsWindow(closeWindow) == FALSE,
            "WM_CLOSE did not report FAIL");

        std::cout << "MouseTestMouseWindowTests passed\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "MouseTestMouseWindowTests failed: " << error.what() << '\n';
        return 1;
    }
}
