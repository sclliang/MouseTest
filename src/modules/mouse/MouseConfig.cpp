#include "modules/mouse/MouseConfig.h"

#include "modules/ModuleConfig.h"

#include <limits>
#include <string>

namespace ftc::modules::mouse
{
namespace
{
[[nodiscard]] bool ParseUnsigned(const std::wstring& text, unsigned& value) noexcept
{
    if (text.empty()) return false;
    unsigned parsed{};
    for (const wchar_t character : text)
    {
        if (character < L'0' || character > L'9') return false;
        const unsigned digit = static_cast<unsigned>(character - L'0');
        if (parsed > (std::numeric_limits<unsigned>::max() - digit) / 10U) return false;
        parsed = parsed * 10U + digit;
    }
    value = parsed;
    return true;
}

[[nodiscard]] bool ReadUnsigned(
    const IniSection* const section,
    const wchar_t* const key,
    const unsigned minimum,
    const unsigned maximum,
    unsigned& value,
    std::wstring& error)
{
    if (section == nullptr) return true;
    const IniEntry* const entry = section->FindLastEntry(key);
    if (entry == nullptr) return true;
    unsigned parsed{};
    if (!ParseUnsigned(entry->value, parsed) || parsed < minimum || parsed > maximum)
    {
        error = L"[Mouse] " + std::wstring(key) + L" 必须是 " + std::to_wstring(minimum) +
            L"–" + std::to_wstring(maximum) + L" 的整数（第 " + std::to_wstring(entry->sourceLine) + L" 行）";
        return false;
    }
    value = parsed;
    return true;
}
} // namespace

ConfigLoadResult LoadConfig(const ModuleConfig& source)
{
    ConfigLoadResult result;
    result.success = true;
    const IniSection* const section = source.GetSection(L"Mouse");

    const auto read = [&](const wchar_t* const key, const unsigned minimum, const unsigned maximum,
        unsigned& target) {
        unsigned value = target;
        if (!ReadUnsigned(section, key, minimum, maximum, value, result.error))
        {
            result.success = false;
            return false;
        }
        target = value;
        return true;
    };

    if (!read(L"MoveDistance", 0U, 1'000'000U, result.config.moveDistance) ||
        !read(L"MoveAxisDistance", 0U, 1'000'000U, result.config.moveAxisDistance) ||
        !read(L"WheelSteps", 0U, 1'000U, result.config.wheelSteps) ||
        !read(L"LeftClicks", 0U, 1'000U, result.config.leftClicks) ||
        !read(L"DoubleClicks", 0U, 1'000U, result.config.doubleClicks) ||
        !read(L"RightClicks", 0U, 1'000U, result.config.rightClicks) ||
        !read(L"MiddleClicks", 0U, 1'000U, result.config.middleClicks) ||
        !read(L"Drags", 0U, 1'000U, result.config.drags) ||
        !read(L"DragDistance", 0U, 1'000'000U, result.config.dragDistance) ||
        !read(L"ClickTolerance", 0U, 10'000U, result.config.clickTolerance))
    {
        return result;
    }

    if (result.config.moveAxisDistance > result.config.moveDistance)
    {
        result.success = false;
        result.error = L"[Mouse] MoveDistance 不能小于 MoveAxisDistance，否则单轴要求无法完成";
        return result;
    }
    if (result.config.drags > 0U && result.config.dragDistance <= result.config.clickTolerance)
    {
        result.success = false;
        result.error = L"[Mouse] DragDistance 必须大于 ClickTolerance，避免普通单击被计成拖动";
        return result;
    }
    return result;
}
} // namespace ftc::modules::mouse
