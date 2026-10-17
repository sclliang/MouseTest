#include "modules/touch_pad/TouchPadConfig.h"

#include "modules/ModuleConfig.h"

#include <limits>

namespace ftc::modules::touch_pad
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

[[nodiscard]] bool ParsePositive(const IniEntry* entry, unsigned& value) noexcept
{
    return entry && ParseUnsigned(entry->value, value) && value > 0U;
}

[[nodiscard]] bool ParseFinger(
    const std::wstring& text,
    const unsigned rows,
    const unsigned columns,
    Exclusion& value) noexcept
{
    if (text.empty()) return true;
    const std::size_t first = text.find_first_of(L"xX");
    const std::size_t second = first == std::wstring::npos
        ? std::wstring::npos : text.find_first_of(L"xX", first + 1U);
    if (first == std::wstring::npos || second == std::wstring::npos ||
        text.find_first_of(L"xX", second + 1U) != std::wstring::npos) return false;
    unsigned corner{}, width{}, height{};
    if (!ParseUnsigned(text.substr(0U, first), corner) ||
        !ParseUnsigned(text.substr(first + 1U, second - first - 1U), width) ||
        !ParseUnsigned(text.substr(second + 1U), height) ||
        corner > 3U || width == 0U || height == 0U || width > columns || height > rows)
    {
        return false;
    }
    value = Exclusion{true, corner, width, height};
    return true;
}
} // namespace

ConfigLoadResult LoadConfig(const ModuleConfig& source)
{
    ConfigLoadResult result;
    result.success = true;
    if (const IniSection* language = source.GetSection(L"language"))
    {
        if (const IniEntry* culture = language->FindLastEntry(L"Culture"))
        {
            if (IniNameEquals(culture->value, L"zh-CN", IniNameComparison::AsciiCaseInsensitive))
                result.config.language = Language::Chinese;
            else if (IniNameEquals(culture->value, L"en-US", IniNameComparison::AsciiCaseInsensitive))
                result.config.language = Language::English;
            else if (IniNameEquals(culture->value, L"vi-VN", IniNameComparison::AsciiCaseInsensitive))
                result.config.language = Language::Vietnamese;
            else
            {
                result.success = false;
                result.error = L"[language] Culture 只允许 zh-CN、en-US 或 vi-VN（第 " +
                    std::to_wstring(culture->sourceLine) + L" 行）";
                return result;
            }
        }
    }

    const IniSection* section = source.GetSection(L"TouchPad");
    unsigned rows{}, columns{};
    if (section && ParsePositive(section->FindLastEntry(L"RowCount"), rows) &&
        ParsePositive(section->FindLastEntry(L"ColumnCount"), columns) &&
        rows <= 10'000U / columns)
    {
        result.config.rows = rows;
        result.config.columns = columns;
    }
    else
    {
        result.warnings.emplace_back(L"[TouchPad] RowCount/ColumnCount 无效，按旧程序一起回退为 5/10");
    }

    unsigned left{}, right{};
    const IniEntry* leftEntry = section ? section->FindLastEntry(L"LeftClickCount") : nullptr;
    const IniEntry* rightEntry = section ? section->FindLastEntry(L"RightClickCount") : nullptr;
    if (leftEntry && rightEntry && ParseUnsigned(leftEntry->value, left) && ParseUnsigned(rightEntry->value, right))
    {
        result.config.leftClicks = left;
        result.config.rightClicks = right;
    }
    else
    {
        result.config.leftClicks = 0U;
        result.config.rightClicks = 0U;
        result.warnings.emplace_back(L"[TouchPad] LeftClickCount/RightClickCount 无效，按旧程序一起关闭按键计数");
    }

    if (const IniEntry* finger = section ? section->FindLastEntry(L"Finger") : nullptr)
    {
        if (!ParseFinger(finger->value, result.config.rows, result.config.columns, result.config.exclusion))
        {
            result.success = false;
            result.error = L"[TouchPad] Finger 必须为空或使用 角编号x宽x高，且区域不得超出网格（第 " +
                std::to_wstring(finger->sourceLine) + L" 行）";
        }
    }
    return result;
}
} // namespace ftc::modules::touch_pad
