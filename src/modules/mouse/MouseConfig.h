#pragma once

#include "modules/Language.h"

#include <string>
#include <vector>

namespace ftc::modules { class ModuleConfig; }

namespace ftc::modules::mouse
{
struct Config final
{
    Language language{Language::Chinese};
    unsigned moveDistance{1200U};
    unsigned moveAxisDistance{300U};
    unsigned wheelSteps{3U};
    unsigned leftClicks{1U};
    unsigned doubleClicks{1U};
    unsigned rightClicks{1U};
    unsigned middleClicks{1U};
    unsigned drags{1U};
    unsigned dragDistance{300U};
    unsigned clickTolerance{8U};
};

struct ConfigLoadResult final
{
    bool success{};
    Config config;
    std::wstring error;
    std::vector<std::wstring> warnings;
};

// Mouse settings are strict: an unknown key is ignored, but a known key with
// an out-of-range or non-numeric value fails startup instead of falling back.
[[nodiscard]] ConfigLoadResult LoadConfig(const ModuleConfig& source);
} // namespace ftc::modules::mouse
