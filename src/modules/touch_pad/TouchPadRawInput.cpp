#include "modules/touch_pad/TouchPadRawInput.h"

#include <hidsdi.h>

#include <array>
#include <map>
#include <vector>

namespace ftc::modules::touch_pad
{
namespace
{
constexpr USHORT kDigitizerPage = 0x0DU;
constexpr USHORT kTouchPadUsage = 0x05U;

[[nodiscard]] std::wstring LastErrorText(const wchar_t* api)
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

[[nodiscard]] bool ContainsUsage(const HIDP_VALUE_CAPS& capability, const USHORT usage) noexcept
{
    return capability.IsRange
        ? usage >= capability.Range.UsageMin && usage <= capability.Range.UsageMax
        : usage == capability.NotRange.Usage;
}

struct Partial final
{
    LONG id{};
    LONG x{};
    LONG y{};
    bool hasId{};
    bool hasX{};
    bool hasY{};
};
} // namespace

bool FindPrecisionTouchPad(std::wstring& deviceName, std::wstring& error)
{
    UINT count{};
    if (GetRawInputDeviceList(nullptr, &count, sizeof(RAWINPUTDEVICELIST)) != 0)
    {
        error = LastErrorText(L"GetRawInputDeviceList(count)");
        return false;
    }
    std::vector<RAWINPUTDEVICELIST> devices(count);
    if (count > 0U && GetRawInputDeviceList(devices.data(), &count, sizeof(RAWINPUTDEVICELIST)) == UINT_MAX)
    {
        error = LastErrorText(L"GetRawInputDeviceList(data)");
        return false;
    }
    for (const RAWINPUTDEVICELIST& device : devices)
    {
        if (device.dwType != RIM_TYPEHID) continue;
        RID_DEVICE_INFO information{};
        information.cbSize = sizeof(information);
        UINT bytes = sizeof(information);
        if (GetRawInputDeviceInfoW(device.hDevice, RIDI_DEVICEINFO, &information, &bytes) == UINT_MAX) continue;
        if (information.hid.usUsagePage == kDigitizerPage && information.hid.usUsage == kTouchPadUsage)
        {
            deviceName = DeviceName(device.hDevice);
            return true;
        }
    }
    error = L"未发现 UsagePage=0x0D、Usage=0x05 的 Windows Precision Touchpad 原始输入设备";
    return false;
}

bool RegisterPrecisionTouchPad(const HWND window, std::wstring& error)
{
    RAWINPUTDEVICE device{kDigitizerPage, kTouchPadUsage, 0U, window};
    if (!RegisterRawInputDevices(&device, 1U, sizeof(device)))
    {
        error = LastErrorText(L"RegisterRawInputDevices");
        return false;
    }
    return true;
}

void UnregisterPrecisionTouchPad() noexcept
{
    RAWINPUTDEVICE device{kDigitizerPage, kTouchPadUsage, RIDEV_REMOVE, nullptr};
    static_cast<void>(RegisterRawInputDevices(&device, 1U, sizeof(device)));
}

bool ParseRawFrame(const LPARAM rawInput, RawFrame& frame, std::wstring& error)
{
    UINT bytes{};
    if (GetRawInputData(reinterpret_cast<HRAWINPUT>(rawInput), RID_INPUT, nullptr, &bytes, sizeof(RAWINPUTHEADER)) != 0U || bytes < sizeof(RAWINPUT))
    {
        error = LastErrorText(L"GetRawInputData(size)");
        return false;
    }
    std::vector<BYTE> storage(bytes);
    if (GetRawInputData(reinterpret_cast<HRAWINPUT>(rawInput), RID_INPUT, storage.data(), &bytes, sizeof(RAWINPUTHEADER)) != bytes)
    {
        error = LastErrorText(L"GetRawInputData(data)");
        return false;
    }
    const RAWINPUT* raw = reinterpret_cast<const RAWINPUT*>(storage.data());
    if (raw->header.dwType != RIM_TYPEHID || raw->data.hid.dwSizeHid == 0U || raw->data.hid.dwCount == 0U)
    {
        error = L"WM_INPUT 不是有效的 HID 报告";
        return false;
    }
    UINT preparsedBytes{};
    if (GetRawInputDeviceInfoW(raw->header.hDevice, RIDI_PREPARSEDDATA, nullptr, &preparsedBytes) != 0U || preparsedBytes == 0U)
    {
        error = LastErrorText(L"GetRawInputDeviceInfo(preparsed size)");
        return false;
    }
    std::vector<BYTE> preparsed(preparsedBytes);
    if (GetRawInputDeviceInfoW(raw->header.hDevice, RIDI_PREPARSEDDATA, preparsed.data(), &preparsedBytes) == UINT_MAX)
    {
        error = LastErrorText(L"GetRawInputDeviceInfo(preparsed data)");
        return false;
    }
    PHIDP_PREPARSED_DATA data = reinterpret_cast<PHIDP_PREPARSED_DATA>(preparsed.data());
    HIDP_CAPS caps{};
    if (HidP_GetCaps(data, &caps) != HIDP_STATUS_SUCCESS || caps.NumberInputValueCaps == 0U)
    {
        error = L"HidP_GetCaps 未返回触摸板输入值能力";
        return false;
    }
    USHORT valueCount = caps.NumberInputValueCaps;
    std::vector<HIDP_VALUE_CAPS> valueCaps(valueCount);
    if (HidP_GetValueCaps(HidP_Input, valueCaps.data(), &valueCount, data) != HIDP_STATUS_SUCCESS)
    {
        error = L"HidP_GetValueCaps 失败";
        return false;
    }

    frame = {};
    const BYTE* reports = raw->data.hid.bRawData;
    for (DWORD reportIndex = 0U; reportIndex < raw->data.hid.dwCount; ++reportIndex)
    {
        PCHAR report = reinterpret_cast<PCHAR>(const_cast<BYTE*>(reports + reportIndex * raw->data.hid.dwSizeHid));
        ULONG contactCount{};
        std::map<USHORT, Partial> partials;
        for (USHORT index = 0U; index < valueCount; ++index)
        {
            const HIDP_VALUE_CAPS& capability = valueCaps[index];
            for (const USHORT usage : std::array<USHORT, 4U>{0x54U, 0x51U, 0x30U, 0x31U})
            {
                const USHORT page = usage == 0x30U || usage == 0x31U ? 0x01U : 0x0DU;
                if (capability.UsagePage != page || !ContainsUsage(capability, usage)) continue;
                ULONG value{};
                if (HidP_GetUsageValue(HidP_Input, page, capability.LinkCollection, usage, &value,
                        data, report, raw->data.hid.dwSizeHid) != HIDP_STATUS_SUCCESS) continue;
                if (usage == 0x54U && capability.LinkCollection == 0U)
                {
                    contactCount = value;
                    continue;
                }
                Partial& partial = partials[capability.LinkCollection];
                if (usage == 0x51U) { partial.id = static_cast<LONG>(value); partial.hasId = true; }
                else if (usage == 0x30U)
                {
                    partial.x = static_cast<LONG>(value); partial.hasX = true;
                    frame.bounds.minX = capability.LogicalMin; frame.bounds.maxX = capability.LogicalMax;
                }
                else
                {
                    partial.y = static_cast<LONG>(value); partial.hasY = true;
                    frame.bounds.minY = capability.LogicalMin; frame.bounds.maxY = capability.LogicalMax;
                }
            }
        }
        if (contactCount == 0U) continue;
        frame.declaredContactCount = contactCount;
        for (const auto& [link, partial] : partials)
        {
            static_cast<void>(link);
            if (partial.hasId && partial.hasX && partial.hasY)
                frame.contacts.push_back(Contact{partial.id, partial.x, partial.y});
            if (frame.contacts.size() >= contactCount) break;
        }
    }
    return frame.bounds.IsValid();
}
} // namespace ftc::modules::touch_pad
