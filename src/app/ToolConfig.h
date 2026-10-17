#pragma once

#include "modules/Language.h"
#include "modules/mouse/MouseConfig.h"
#include "modules/touch_pad/TouchPadConfig.h"

#include <string>
#include <vector>

namespace ftc::modules { class ModuleConfig; }

namespace mouse_app
{
struct TestSelection final
{
    bool touchPadEnabled{true};
    bool mouseEnabled{false};
    ftc::modules::Language language{ftc::modules::Language::Chinese};
    ftc::modules::touch_pad::Config touchPad;
    ftc::modules::mouse::Config mouse;
};

struct ToolConfigLoadResult final
{
    bool success{};
    TestSelection selection;
    std::wstring error;
    std::vector<std::wstring> warnings;
};

// [TouchPad] Enabled defaults to 1, [Mouse] Enabled defaults to 0. A disabled
// test's own section is not parsed, so its invalid values cannot block the
// selected test. At least one test must remain enabled.
[[nodiscard]] ToolConfigLoadResult LoadToolConfig(const ftc::modules::ModuleConfig& source);
} // namespace mouse_app
