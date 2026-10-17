#pragma once

#include "modules/mouse/MouseConfig.h"
#include "modules/mouse/MouseLogic.h"

#include <windows.h>

#include <atomic>
#include <functional>
#include <memory>
#include <string>

namespace ftc::modules::mouse
{
enum class WindowResult
{
    Passed,
    Failed,
    Faulted,
};

struct WindowState final
{
    Config config;
    bool skipHardwareCheck{};
    unsigned passDisplayMs{1000U};
    std::atomic<HWND> hwnd{nullptr};
    std::atomic_bool submitted{};
    std::function<void(const std::wstring&)> onDeviceReady;
    std::function<void(const std::wstring&)> onTrace;
    std::function<void(const std::wstring&)> onError;
    std::function<void(const std::wstring&)> onDiagnostic;
    std::function<void(WindowResult)> onFinished;
};

[[nodiscard]] HWND CreateMouseWindow(
    HWND owner,
    const std::shared_ptr<WindowState>& state,
    bool showWindow,
    std::wstring& error);

[[nodiscard]] bool SubmitMoveForTesting(HWND window, LONG dx, LONG dy) noexcept;
[[nodiscard]] bool SubmitWheelForTesting(HWND window, int delta) noexcept;
[[nodiscard]] bool SubmitButtonForTesting(HWND window, MouseButton button, bool down) noexcept;
} // namespace ftc::modules::mouse
