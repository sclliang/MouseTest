#pragma once

#include <windows.h>

#include <string>
#include <vector>

namespace ftc::modules::mouse
{
struct Device final
{
    std::wstring name;
    unsigned buttons{};
};

struct Frame final
{
    LONG dx{};
    LONG dy{};
    int wheel{};
    bool leftDown{};
    bool leftUp{};
    bool rightDown{};
    bool rightUp{};
    bool middleDown{};
    bool middleUp{};
    bool absolute{};

    [[nodiscard]] bool HasInput() const noexcept
    {
        return dx != 0 || dy != 0 || wheel != 0 || leftDown || leftUp || rightDown ||
            rightUp || middleDown || middleUp;
    }
};

[[nodiscard]] bool FindGenericMice(std::vector<Device>& devices, std::wstring& error);
[[nodiscard]] bool RegisterGenericMouse(HWND window, std::wstring& error);
void UnregisterGenericMouse() noexcept;
[[nodiscard]] bool ParseMouseFrame(LPARAM rawInput, Frame& frame, std::wstring& error);
} // namespace ftc::modules::mouse
