#pragma once

#include <string>
#include <vector>

namespace ftc::modules { class ModuleConfig; }

namespace ftc::modules::touch_pad
{
enum class Language { Chinese, English, Vietnamese };

struct Exclusion final
{
    bool enabled{};
    unsigned corner{};
    unsigned width{};
    unsigned height{};
};

struct Config final
{
    Language language{Language::Chinese};
    unsigned rows{5U};
    unsigned columns{10U};
    unsigned leftClicks{1U};
    unsigned rightClicks{1U};
    Exclusion exclusion;
};

struct ConfigLoadResult final
{
    bool success{};
    Config config;
    std::wstring error;
    std::vector<std::wstring> warnings;
};

[[nodiscard]] ConfigLoadResult LoadConfig(const ModuleConfig& source);
} // namespace ftc::modules::touch_pad
