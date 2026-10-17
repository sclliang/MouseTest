#pragma once

#include "modules/touch_pad/TouchPadConfig.h"

#include <filesystem>
#include <string>
#include <vector>

namespace mouse_app
{
// The standalone tool reads only these options. A missing config.ini uses the
// built-in 5x10 grid and one left/right click, matching the documented sample.
struct SessionOptions final
{
    ftc::modules::touch_pad::Config config;
    std::vector<std::wstring> warnings;
    std::wstring startupError;
    std::filesystem::path configPath;
    bool reportErrors{true};
    bool skipHardwareCheck{false};
};

[[nodiscard]] std::filesystem::path ExecutableDirectory();
[[nodiscard]] int RunApplication(const std::filesystem::path& programDirectory, const SessionOptions& options);
} // namespace mouse_app
