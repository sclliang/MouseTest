#include "app/ToolConfig.h"

#include "modules/ModuleConfig.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

namespace
{
void Check(const bool value, const char* const message)
{
    if (!value) throw std::runtime_error(message);
}

mouse_app::ToolConfigLoadResult Load(const std::filesystem::path& path, const char* const text)
{
    {
        std::ofstream file(path, std::ios::binary | std::ios::trunc);
        file << text;
    }
    const auto source = ftc::modules::LoadModuleConfig(path);
    Check(source.success, "shared config failed");
    return mouse_app::LoadToolConfig(*source.config);
}
} // namespace

int main()
{
    using namespace mouse_app;
    using ftc::modules::Language;
    try
    {
        const auto path = std::filesystem::temp_directory_path() / L"mouse_test_tool_config.ini";

        auto result = Load(path, "[language]\nCulture=en-us\n[TouchPad]\nRowCount=2\nColumnCount=2\n");
        Check(result.success, "default selection rejected");
        Check(result.selection.touchPadEnabled && !result.selection.mouseEnabled,
            "default Enabled values changed");
        Check(result.selection.language == Language::English, "language parse wrong");

        result = Load(path,
            "[language]\nCulture=vi-VN\n"
            "[TouchPad]\nEnabled=0\nRowCount=bad\n"
            "[Mouse]\nEnabled=1\nMoveDistance=50\nMoveAxisDistance=20\nWheelSteps=1\n");
        Check(result.success, "mouse-only config rejected");
        Check(!result.selection.touchPadEnabled && result.selection.mouseEnabled,
            "Enabled selection wrong");
        Check(result.selection.mouse.moveDistance == 50U &&
            result.selection.mouse.moveAxisDistance == 20U,
            "mouse config not loaded");
        Check(result.selection.language == Language::Vietnamese, "mouse-only language wrong");
        Check(result.selection.touchPad.rows == 5U && result.selection.touchPad.columns == 10U,
            "disabled touchpad config was parsed");

        result = Load(path, "[TouchPad]\nEnabled=0\n[Mouse]\nEnabled=0\n");
        Check(!result.success, "both tests disabled accepted");

        result = Load(path, "[TouchPad]\nEnabled=2\n");
        Check(!result.success, "invalid Enabled accepted");

        result = Load(path, "[language]\nCulture=fr-FR\n[TouchPad]\nEnabled=1\n");
        Check(!result.success, "invalid language accepted");

        result = Load(path, "[TouchPad]\nEnabled=1\n[Mouse]\nEnabled=0\nMoveDistance=bad\n");
        Check(result.success, "disabled mouse invalid value blocked touchpad");

        std::filesystem::remove(path);
        std::cout << "MouseTestToolConfigTests passed\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "MouseTestToolConfigTests failed: " << error.what() << '\n';
        return 1;
    }
}
