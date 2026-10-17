#include "modules/mouse/MouseRawInput.h"

#include <utility>
#include <vector>

namespace ftc::modules::mouse
{
namespace
{
constexpr USHORT kGenericDesktopPage = 0x01U;
constexpr USHORT kMouseUsage = 0x02U;

[[nodiscard]] std::wstring LastErrorText(const wchar_t* const api)
{
    return std::wstring(api) + L" 失败，Win32=" + std::to_wstring(GetLastError());
}

[[nodiscard]] std::wstring DeviceName(const HANDLE device)
{
    UINT characters{};
    if (GetRawInputDeviceInfoW(device, RIDI_DEVICENAME, nullptr, &characters) == UINT_MAX || characters == 0U)
        return {};
    std::wstring name(characters, L'\0');
    if (GetRawInputDeviceInfoW(device, RIDI_DEVICENAME, name.data(), &characters) == UINT_MAX) return {};
    while (!name.empty() && name.back() == L'\0') name.pop_back();
    return name;
}
} // namespace

bool FindGenericMice(std::vector<Device>& devices, std::wstring& error)
{
    devices.clear();
    UINT count{};
    if (GetRawInputDeviceList(nullptr, &count, sizeof(RAWINPUTDEVICELIST)) != 0U)
    {
        error = LastErrorText(L"GetRawInputDeviceList(count)");
        return false;
    }
    std::vector<RAWINPUTDEVICELIST> list(count);
    if (count > 0U && GetRawInputDeviceList(list.data(), &count, sizeof(RAWINPUTDEVICELIST)) == UINT_MAX)
    {
        error = LastErrorText(L"GetRawInputDeviceList(data)");
        return false;
    }
    for (const RAWINPUTDEVICELIST& entry : list)
    {
        if (entry.dwType != RIM_TYPEMOUSE) continue;
        RID_DEVICE_INFO information{};
        information.cbSize = sizeof(information);
        UINT bytes = sizeof(information);
        unsigned buttons{};
        if (GetRawInputDeviceInfoW(entry.hDevice, RIDI_DEVICEINFO, &information, &bytes) != UINT_MAX &&
            information.dwType == RIM_TYPEMOUSE)
        {
            buttons = information.mouse.dwNumberOfButtons;
        }
        std::wstring name = DeviceName(entry.hDevice);
        if (name.empty()) name = L"<设备名称不可用>";
        devices.push_back(Device{std::move(name), buttons});
    }
    if (devices.empty())
    {
        error = L"未发现普通鼠标（Raw Input RIM_TYPEMOUSE）";
        return false;
    }
    return true;
}

bool RegisterGenericMouse(const HWND window, std::wstring& error)
{
    RAWINPUTDEVICE device{kGenericDesktopPage, kMouseUsage, 0U, window};
    if (!RegisterRawInputDevices(&device, 1U, sizeof(device)))
    {
        error = LastErrorText(L"RegisterRawInputDevices(mouse)");
        return false;
    }
    return true;
}

void UnregisterGenericMouse() noexcept
{
    RAWINPUTDEVICE device{kGenericDesktopPage, kMouseUsage, RIDEV_REMOVE, nullptr};
    static_cast<void>(RegisterRawInputDevices(&device, 1U, sizeof(device)));
}

bool ParseMouseFrame(const LPARAM rawInput, Frame& frame, std::wstring& error)
{
    UINT bytes{};
    if (GetRawInputData(reinterpret_cast<HRAWINPUT>(rawInput), RID_INPUT, nullptr, &bytes, sizeof(RAWINPUTHEADER)) != 0U ||
        bytes < sizeof(RAWINPUT))
    {
        error = LastErrorText(L"GetRawInputData(size)");
        return false;
    }
    std::vector<BYTE> storage(bytes);
    if (GetRawInputData(reinterpret_cast<HRAWINPUT>(rawInput), RID_INPUT, storage.data(), &bytes,
            sizeof(RAWINPUTHEADER)) != bytes)
    {
        error = LastErrorText(L"GetRawInputData(data)");
        return false;
    }
    const RAWINPUT* const raw = reinterpret_cast<const RAWINPUT*>(storage.data());
    if (raw->header.dwType != RIM_TYPEMOUSE)
    {
        error = L"WM_INPUT 不是鼠标输入";
        return false;
    }

    frame = {};
    const RAWMOUSE& mouse = raw->data.mouse;
    if ((mouse.usFlags & MOUSE_MOVE_ABSOLUTE) != 0U)
    {
        frame.absolute = true;
    }
    else
    {
        frame.dx = mouse.lLastX;
        frame.dy = mouse.lLastY;
    }
    if ((mouse.usButtonFlags & RI_MOUSE_LEFT_BUTTON_DOWN) != 0U) frame.leftDown = true;
    if ((mouse.usButtonFlags & RI_MOUSE_LEFT_BUTTON_UP) != 0U) frame.leftUp = true;
    if ((mouse.usButtonFlags & RI_MOUSE_RIGHT_BUTTON_DOWN) != 0U) frame.rightDown = true;
    if ((mouse.usButtonFlags & RI_MOUSE_RIGHT_BUTTON_UP) != 0U) frame.rightUp = true;
    if ((mouse.usButtonFlags & RI_MOUSE_MIDDLE_BUTTON_DOWN) != 0U) frame.middleDown = true;
    if ((mouse.usButtonFlags & RI_MOUSE_MIDDLE_BUTTON_UP) != 0U) frame.middleUp = true;
    if ((mouse.usButtonFlags & RI_MOUSE_WHEEL) != 0U)
        frame.wheel = static_cast<short>(mouse.usButtonData);
    return true;
}
} // namespace ftc::modules::mouse
