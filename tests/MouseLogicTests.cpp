#include "modules/mouse/MouseLogic.h"

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
    using namespace ftc::modules::mouse;
    try
    {
        Config config;
        config.moveDistance = 100U;
        config.moveAxisDistance = 40U;
        config.wheelSteps = 1U;
        config.leftClicks = 1U;
        config.doubleClicks = 1U;
        config.rightClicks = 1U;
        config.middleClicks = 1U;
        config.drags = 1U;
        config.dragDistance = 50U;
        config.clickTolerance = 5U;

        TestState state(config, 500U);
        Check(!state.MoveComplete(), "movement completed without input");
        state.Move(100, 100);
        Check(state.MoveComplete() && state.Current().moveX == 100U && state.Current().moveY == 100U,
            "movement accumulation wrong");
        Check(!state.WheelComplete(), "wheel completed without input");
        state.Wheel(60);
        state.Wheel(60);
        state.Wheel(-120);
        Check(state.WheelComplete() && state.Current().wheelUp == 120U && state.Current().wheelDown == 120U,
            "wheel accumulation wrong");

        state.Button(MouseButton::Left, true, 1000U);
        state.Button(MouseButton::Left, false, 1000U);
        Check(state.LeftComplete() && !state.DoubleComplete(), "single click classification wrong");
        state.Button(MouseButton::Left, true, 1100U);
        state.Button(MouseButton::Left, false, 1100U);
        Check(state.DoubleComplete() && state.Current().leftClicks == 1U && state.Current().doubleClicks == 1U,
            "double click classification wrong");

        state.Button(MouseButton::Right, true, 1200U);
        state.Button(MouseButton::Right, false, 1200U);
        state.Button(MouseButton::Middle, true, 1200U);
        state.Button(MouseButton::Middle, false, 1200U);
        Check(state.RightComplete() && state.MiddleComplete(), "right/middle click not counted");

        state.Button(MouseButton::Left, true, 2000U);
        state.Move(60, 0);
        state.Button(MouseButton::Left, false, 2000U);
        Check(state.DragComplete() && state.Current().drags == 1U, "drag not classified");
        Check(state.IsComplete() && state.CompletedTasks() == 7U, "complete state wrong");

        TestState ambiguous(config, 500U);
        ambiguous.Button(MouseButton::Left, true, 1000U);
        ambiguous.Move(10, 0);
        ambiguous.Button(MouseButton::Left, false, 1000U);
        Check(ambiguous.Current().leftClicks == 0U && ambiguous.Current().drags == 0U,
            "medium movement accepted as click or drag");

        TestState slow(config, 500U);
        slow.Button(MouseButton::Left, true, 1000U);
        slow.Button(MouseButton::Left, false, 1000U);
        slow.Button(MouseButton::Left, true, 2000U);
        slow.Button(MouseButton::Left, false, 2000U);
        Check(slow.Current().leftClicks == 2U && slow.Current().doubleClicks == 0U,
            "slow clicks counted as double click");

        TestState reset(config, 500U);
        reset.Button(MouseButton::Left, true, 1000U);
        reset.Move(60, 0);
        reset.ResetPressed();
        reset.Button(MouseButton::Left, false, 1000U);
        Check(reset.Current().leftClicks == 0U && reset.Current().drags == 0U,
            "focus reset kept an old press");

        Config zero;
        zero.moveDistance = 0U;
        zero.moveAxisDistance = 0U;
        zero.wheelSteps = 0U;
        zero.leftClicks = 0U;
        zero.doubleClicks = 0U;
        zero.rightClicks = 0U;
        zero.middleClicks = 0U;
        zero.drags = 0U;
        TestState disabled(zero, 500U);
        Check(disabled.IsComplete(), "all-disabled requirements not complete");

        std::cout << "MouseTestMouseLogicTests passed\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "MouseTestMouseLogicTests failed: " << error.what() << '\n';
        return 1;
    }
}
