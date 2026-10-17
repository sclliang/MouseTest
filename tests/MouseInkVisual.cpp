#include "modules/mouse/MouseWindow.h"

#include <windows.h>

#include <cmath>
#include <iostream>
#include <memory>
#include <string>

namespace
{
using ftc::modules::mouse::Config;
using ftc::modules::mouse::CreateMouseWindow;
using ftc::modules::mouse::MouseButton;
using ftc::modules::mouse::SubmitButtonForTesting;
using ftc::modules::mouse::SubmitMoveForTesting;
using ftc::modules::mouse::WindowState;

void Pump(const int milliseconds)
{
    const ULONGLONG end = GetTickCount64() + static_cast<ULONGLONG>(milliseconds);
    while (GetTickCount64() < end)
    {
        MSG message{};
        while (PeekMessageW(&message, nullptr, 0U, 0U, PM_REMOVE) != FALSE)
        {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        Sleep(8);
    }
}

struct InkWindow final
{
    HWND window{};
    POINT origin{};
    int width{};
    int height{};
};

void MoveInk(const InkWindow& target, const double x, const double y)
{
    SetCursorPos(target.origin.x + static_cast<int>(x), target.origin.y + static_cast<int>(y));
    static_cast<void>(SubmitMoveForTesting(target.window, 3, 2));
    Pump(8);
}
} // namespace

int main()
{
    try
    {
        SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        Config config;
        config.moveDistance = 1'000'000U;
        config.moveAxisDistance = 1'000U;
        config.wheelSteps = 100U;
        config.leftClicks = 100U;
        config.doubleClicks = 100U;
        config.rightClicks = 100U;
        config.middleClicks = 100U;
        config.drags = 100U;
        config.dragDistance = 100'000U;
        config.clickTolerance = 5U;

        auto state = std::make_shared<WindowState>();
        state->config = config;
        state->skipHardwareCheck = true;
        state->passDisplayMs = 0U;
        std::wstring error;
        InkWindow target;
        target.window = CreateMouseWindow(nullptr, state, true, error);
        if (!target.window)
        {
            std::cerr << "visual mouse window failed\n";
            return 1;
        }
        RECT client{};
        GetClientRect(target.window, &client);
        POINT origin{};
        ClientToScreen(target.window, &origin);
        target.origin = origin;
        target.width = client.right - client.left;
        target.height = client.bottom - client.top;
        Pump(500);

        for (int index = 0; index <= 36; ++index)
        {
            const double x = target.width * 0.18 + target.width * 0.28 * (index / 36.0);
            const double y = target.height * 0.36 + std::sin(index * 0.55) * target.height * 0.10;
            MoveInk(target, x, y);
        }

        const double dragStartX = target.width * 0.18;
        const double dragStartY = target.height * 0.70;
        MoveInk(target, dragStartX, dragStartY);
        static_cast<void>(SubmitButtonForTesting(target.window, MouseButton::Left, true));
        for (int index = 0; index <= 30; ++index)
        {
            const double x = dragStartX + target.width * 0.34 * (index / 30.0);
            const double y = dragStartY + std::sin(index * 0.65) * target.height * 0.06;
            MoveInk(target, x, y);
        }
        static_cast<void>(SubmitButtonForTesting(target.window, MouseButton::Left, false));

        MoveInk(target, target.width * 0.82, target.height * 0.30);
        static_cast<void>(SubmitButtonForTesting(target.window, MouseButton::Left, true));
        static_cast<void>(SubmitButtonForTesting(target.window, MouseButton::Left, false));

        Pump(10000);
        DestroyWindow(target.window);
        std::cout << "MouseInkVisual finished\n";
        return 0;
    }
    catch (...)
    {
        return 1;
    }
}
