#include "ToolConfig.h"

#include "modules/ModuleConfig.h"

namespace mouse_app
{
using ftc::modules::Language;

namespace
{
[[nodiscard]] bool ParseLanguage(const ftc::modules::ModuleConfig& source,
    Language& language, std::wstring& error)
{
    const ftc::modules::IniSection* const section = source.GetSection(L"language");
    if (section == nullptr) return true;
    const ftc::modules::IniEntry* const entry = section->FindLastEntry(L"Culture");
    if (entry == nullptr) return true;
    if (ftc::modules::IniNameEquals(entry->value, L"zh-CN", ftc::modules::IniNameComparison::AsciiCaseInsensitive))
        language = Language::Chinese;
    else if (ftc::modules::IniNameEquals(entry->value, L"en-US", ftc::modules::IniNameComparison::AsciiCaseInsensitive))
        language = Language::English;
    else if (ftc::modules::IniNameEquals(entry->value, L"vi-VN", ftc::modules::IniNameComparison::AsciiCaseInsensitive))
        language = Language::Vietnamese;
    else
    {
        error = L"[language] Culture 只允许 zh-CN、en-US 或 vi-VN（第 " +
            std::to_wstring(entry->sourceLine) + L" 行）";
        return false;
    }
    return true;
}

[[nodiscard]] bool ReadEnabled(const ftc::modules::IniSection* const section,
    const wchar_t* const name, bool& value, std::wstring& error)
{
    if (section == nullptr) return true;
    const ftc::modules::IniEntry* const entry = section->FindLastEntry(L"Enabled");
    if (entry == nullptr) return true;
    if (entry->value == L"1") value = true;
    else if (entry->value == L"0") value = false;
    else
    {
        error = std::wstring(L"[") + name + L"] Enabled 只允许 0 或 1（第 " +
            std::to_wstring(entry->sourceLine) + L" 行）";
        return false;
    }
    return true;
}
} // namespace

ToolConfigLoadResult LoadToolConfig(const ftc::modules::ModuleConfig& source)
{
    ToolConfigLoadResult result;
    result.success = true;
    result.selection.language = Language::Chinese;
    if (!ParseLanguage(source, result.selection.language, result.error))
    {
        result.success = false;
        return result;
    }

    bool touchPadEnabled = true;
    bool mouseEnabled = false;
    result.selection.touchPad.language = result.selection.language;
    result.selection.mouse.language = result.selection.language;
    if (!ReadEnabled(source.GetSection(L"TouchPad"), L"TouchPad", touchPadEnabled, result.error) ||
        !ReadEnabled(source.GetSection(L"Mouse"), L"Mouse", mouseEnabled, result.error))
    {
        result.success = false;
        return result;
    }
    if (!touchPadEnabled && !mouseEnabled)
    {
        result.success = false;
        result.error = L"[TouchPad] 和 [Mouse] 至少有一个 Enabled=1";
        return result;
    }

    result.selection.touchPadEnabled = touchPadEnabled;
    result.selection.mouseEnabled = mouseEnabled;
    if (touchPadEnabled)
    {
        auto loaded = ftc::modules::touch_pad::LoadConfig(source);
        if (!loaded.success)
        {
            result.success = false;
            result.error = loaded.error;
            return result;
        }
        loaded.config.language = result.selection.language;
        result.selection.touchPad = loaded.config;
        result.warnings.insert(result.warnings.end(), loaded.warnings.begin(), loaded.warnings.end());
    }
    if (mouseEnabled)
    {
        auto loaded = ftc::modules::mouse::LoadConfig(source);
        if (!loaded.success)
        {
            result.success = false;
            result.error = loaded.error;
            return result;
        }
        loaded.config.language = result.selection.language;
        result.selection.mouse = loaded.config;
        result.warnings.insert(result.warnings.end(), loaded.warnings.begin(), loaded.warnings.end());
    }
    return result;
}
} // namespace mouse_app
