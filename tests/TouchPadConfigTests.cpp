#include "modules/ModuleConfig.h"
#include "modules/touch_pad/TouchPadConfig.h"

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

ftc::modules::touch_pad::ConfigLoadResult Load(const std::filesystem::path& path, const char* const text)
{
    {
        std::ofstream file(path, std::ios::binary | std::ios::trunc);
        file << text;
    }
    const auto source = ftc::modules::LoadModuleConfig(path);
    Check(source.success, "shared config failed");
    return ftc::modules::touch_pad::LoadConfig(*source.config);
}
} // namespace

int main()
{
    try
    {
        const auto path = std::filesystem::temp_directory_path() / L"mouse_test_config_test.ini";
        auto result = Load(path, "[language]\nCulture=vi-vn\n[TOUCHPAD]\nrowcount=4\ncolumncount=8\nleftclickcount=2\nrightclickcount=3\nfinger=1x2x1\nUnknown=ok\n");
        Check(result.success, "valid config rejected");
        Check(result.config.rows == 4U && result.config.columns == 8U, "grid parse wrong");
        Check(result.config.leftClicks == 2U && result.config.rightClicks == 3U, "click parse wrong");
        Check(result.config.exclusion.enabled && result.config.exclusion.corner == 1U, "finger parse wrong");

        for (const char* finger : {"1x2x1", "1X2X1", "1x2X1", "1X2x1"})
        {
            const std::string text = std::string("[TouchPad]\nRowCount=4\nColumnCount=8\nLeftClickCount=1\nRightClickCount=1\nFinger=") + finger + "\n";
            result = Load(path, text.c_str());
            Check(result.success && result.config.exclusion.enabled && result.config.exclusion.corner == 1U &&
                result.config.exclusion.width == 2U && result.config.exclusion.height == 1U,
                "Finger separator case changed the exclusion");
        }
        for (const char* finger : {"1X2X1X1", "1x2X1x1", "1X2", "1X0X1", "1X11X1"})
        {
            const std::string text = std::string("[TouchPad]\nRowCount=5\nColumnCount=10\nLeftClickCount=1\nRightClickCount=1\nFinger=") + finger + "\n";
            Check(!Load(path, text.c_str()).success, "invalid uppercase/mixed Finger accepted");
        }

        result = Load(path, "[TouchPad]\nRowCount=no\nColumnCount=9\nLeftClickCount=1\n");
        Check(result.success, "old fallback config rejected");
        Check(result.config.rows == 5U && result.config.columns == 10U, "paired grid fallback changed");
        Check(result.config.leftClicks == 0U && result.config.rightClicks == 0U, "paired click fallback changed");
        Check(result.warnings.size() == 2U, "fallback warnings missing");

        result = Load(path, "[language]\nCulture=zh-CN\n");
        Check(result.success && result.config.rows == 5U && result.config.columns == 10U,
            "missing section grid fallback changed");
        Check(result.config.leftClicks == 0U && result.config.rightClicks == 0U,
            "missing section click fallback changed");

        result = Load(path, "[TouchPad]\nRowCount=5\nColumnCount=10\nLeftClickCount=1\nRightClickCount=1\nFinger=4x1x1\n");
        Check(!result.success, "invalid Finger accepted");
        std::filesystem::remove(path);
        std::cout << "MouseTestConfigTests passed\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "MouseTestConfigTests failed: " << error.what() << '\n';
        return 1;
    }
}
