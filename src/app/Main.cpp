#include "Application.h"

#include "modules/ModuleConfig.h"

#include <windows.h>

#include <filesystem>
#include <utility>

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int)
{
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    try
    {
        const std::filesystem::path directory = mouse_app::ExecutableDirectory();
        mouse_app::SessionOptions options;
        const std::filesystem::path configPath = directory / L"config.ini";
        if (std::filesystem::exists(configPath))
        {
            options.configPath = configPath;
            const auto source = ftc::modules::LoadModuleConfig(configPath);
            if (!source.success)
            {
                options.startupError = L"读取 config.ini 失败：" + source.error;
            }
            else
            {
                auto loaded = mouse_app::LoadToolConfig(*source.config);
                if (!loaded.success)
                {
                    options.startupError = loaded.error;
                }
                else
                {
                    options.tests = loaded.selection;
                    options.warnings = std::move(loaded.warnings);
                }
            }
        }
        return mouse_app::RunApplication(directory, options);
    }
    catch (...)
    {
        MessageBoxW(nullptr, L"无法启动鼠标测试 / Cannot start MouseTest.", L"MouseTest", MB_OK | MB_ICONERROR);
        return 3;
    }
}
