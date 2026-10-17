#include "modules/touch_pad/TouchPadLogic.h"

#include <iostream>
#include <stdexcept>

namespace
{
void Check(const bool value, const char* const message)
{
    if (!value) throw std::runtime_error(message);
}
} // namespace

int main()
{
    using namespace ftc::modules::touch_pad;
    try
    {
        Config config;
        config.rows = 2U;
        config.columns = 2U;
        config.leftClicks = 1U;
        config.rightClicks = 1U;
        config.exclusion = Exclusion{true, 0U, 1U, 1U};
        Coverage coverage(config);
        Check(coverage.CompletedCells() == 1U, "exclusion not pre-completed");
        Check(coverage.Visit(0.75, 0.25), "top right not visited");
        Check(coverage.Visit(0.25, 0.75), "bottom left not visited");
        Check(coverage.Visit(0.75, 0.75), "bottom right not visited");
        Check(!coverage.IsComplete(), "click checks skipped");
        Check(coverage.LeftClick() && coverage.RightClick() && coverage.IsComplete(), "coverage did not complete");
        Check(!coverage.Visit(1.0, 0.5), "out-of-range contact accepted");

        std::cout << "MouseTestLogicTests passed\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "MouseTestLogicTests failed: " << error.what() << '\n';
        return 1;
    }
}
