#pragma once

#include "modules/touch_pad/TouchPadLogic.h"

#include <windows.h>

#include <string>
#include <vector>

namespace ftc::modules::touch_pad
{
struct LogicalBounds final
{
    LONG minX{};
    LONG maxX{};
    LONG minY{};
    LONG maxY{};
    [[nodiscard]] bool IsValid() const noexcept { return maxX > minX && maxY > minY; }
};

struct RawFrame final
{
    unsigned declaredContactCount{};
    std::vector<Contact> contacts;
    LogicalBounds bounds;
};

[[nodiscard]] bool FindPrecisionTouchPad(std::wstring& deviceName, std::wstring& error);
[[nodiscard]] bool RegisterPrecisionTouchPad(HWND window, std::wstring& error);
void UnregisterPrecisionTouchPad() noexcept;
[[nodiscard]] bool ParseRawFrame(LPARAM rawInput, RawFrame& frame, std::wstring& error);
} // namespace ftc::modules::touch_pad
