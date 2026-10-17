#pragma once

#include "ToolConfig.h"

#include <filesystem>
#include <string>
#include <vector>

namespace mouse_app
{
struct SessionOptions final
{
    TestSelection tests;
    std::vector<std::wstring> warnings;
    std::wstring startupError;
    std::filesystem::path configPath;
    bool reportErrors{true};
};

[[nodiscard]] std::filesystem::path ExecutableDirectory();
[[nodiscard]] int RunApplication(const std::filesystem::path& programDirectory, const SessionOptions& options);
} // namespace mouse_app
