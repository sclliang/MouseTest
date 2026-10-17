#include "modules/ModuleConfig.h"
#include "modules/mouse/MouseConfig.h"

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

ftc::modules::mouse::ConfigLoadResult Load(const std::filesystem::path& path, const char* const text)
{
    {
        std::ofstream file(path, std::ios::binary | std::ios::trunc);
        file << text;
    }
    const auto source = ftc::modules::LoadModuleConfig(path);
    Check(source.success, "shared config failed");
    return ftc::modules::mouse::LoadConfig(*source.config);
}
} // namespace

int main()
{
    try
    {
        const auto path = std::filesystem::temp_directory_path() / L"mouse_test_mouse_config.ini";
        auto result = Load(path, "[TouchPad]\nRowCount=2\nColumnCount=2\n");
        Check(result.success, "missing Mouse section rejected");
        Check(result.config.moveDistance == 1200U && result.config.moveAxisDistance == 300U,
            "default movement changed");
        Check(result.config.wheelSteps == 3U && result.config.dragDistance == 300U,
            "default thresholds changed");

        result = Load(path,
            "[MOUSE]\nmovedistance=50\nmoveaxisdistance=20\nwheelsteps=1\nleftclicks=2\n"
            "doubleclicks=3\nrightclicks=4\nmiddleclicks=5\ndrags=2\ndragdistance=60\n"
            "clicktolerance=4\nUnknown=ok\n");
        Check(result.success, "valid mouse config rejected");
        Check(result.config.moveDistance == 50U && result.config.moveAxisDistance == 20U,
            "movement parse wrong");
        Check(result.config.wheelSteps == 1U && result.config.leftClicks == 2U,
            "wheel/click parse wrong");
        Check(result.config.doubleClicks == 3U && result.config.rightClicks == 4U &&
            result.config.middleClicks == 5U, "click counts parse wrong");
        Check(result.config.drags == 2U && result.config.dragDistance == 60U &&
            result.config.clickTolerance == 4U, "drag parse wrong");

        Check(!Load(path, "[Mouse]\nMoveDistance=abc\n").success, "non-numeric value accepted");
        Check(!Load(path, "[Mouse]\nWheelSteps=-1\n").success, "negative value accepted");
        Check(!Load(path, "[Mouse]\nLeftClicks=1001\n").success, "out-of-range count accepted");
        Check(!Load(path, "[Mouse]\nMoveDistance=100\nMoveAxisDistance=200\n").success,
            "impossible axis target accepted");
        Check(!Load(path, "[Mouse]\nDrags=1\nDragDistance=5\nClickTolerance=10\n").success,
            "drag distance at/below click tolerance accepted");
        result = Load(path, "[Mouse]\nDrags=0\nDragDistance=0\nClickTolerance=10\n");
        Check(result.success && result.config.dragDistance == 0U,
            "disabled drag with zero distance rejected");

        std::filesystem::remove(path);
        std::cout << "MouseTestMouseConfigTests passed\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "MouseTestMouseConfigTests failed: " << error.what() << '\n';
        return 1;
    }
}
