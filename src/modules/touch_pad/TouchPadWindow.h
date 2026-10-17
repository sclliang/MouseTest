#pragma once

#include "modules/touch_pad/TouchPadConfig.h"

#include <windows.h>

#include <atomic>
#include <functional>
#include <memory>
#include <string>

namespace ftc::modules::touch_pad
{
enum class WindowResult { Passed, Failed, Cancelled, Faulted };

struct WindowState final
{
    Config config;
    bool skipHardwareCheck{};
    std::atomic<HWND> hwnd{nullptr};
    std::atomic_bool submitted{};
    std::function<void(unsigned, unsigned, unsigned, unsigned)> onProgress;
    std::function<void(const std::wstring&)> onDeviceReady;
    std::function<void(const std::wstring&)> onTrace;
    std::function<void(const std::wstring&)> onError;
    std::function<void(const std::wstring&)> onDiagnostic;
    std::function<void(WindowResult)> onFinished;
};

[[nodiscard]] HWND CreateTouchPadWindow(
    HWND owner,
    const std::shared_ptr<WindowState>& state,
    bool showWindow,
    std::wstring& error);
void CancelWindow(const std::shared_ptr<WindowState>& state) noexcept;
[[nodiscard]] bool SubmitContactForTesting(HWND window, double x, double y) noexcept;
[[nodiscard]] bool SubmitClickForTesting(HWND window, bool left) noexcept;
} // namespace ftc::modules::touch_pad
