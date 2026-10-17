#include "modules/mouse/MouseLogic.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace ftc::modules::mouse
{
TestState::TestState(const Config& config, const std::uint32_t doubleClickMs) noexcept
    : config_(config), doubleClickMs_(doubleClickMs)
{
}

unsigned TestState::AddSaturated(const unsigned value, const std::uint64_t amount) noexcept
{
    const std::uint64_t total = static_cast<std::uint64_t>(value) + amount;
    return static_cast<unsigned>((std::min)(total, static_cast<std::uint64_t>(kMaxProgress)));
}

void TestState::Increment(unsigned& value) noexcept
{
    if (value < kMaxProgress) ++value;
}

void TestState::ResetClickChain() noexcept
{
    lastLeftClickTime_ = 0U;
    lastLeftClickDistance_ = 0U;
}

void TestState::Move(const std::int32_t dx, const std::int32_t dy) noexcept
{
    if (dx == 0 && dy == 0) return;
    const auto absolute = [](const std::int32_t value) {
        const std::int64_t wide = value;
        return static_cast<std::uint64_t>(wide < 0 ? -wide : wide);
    };
    progress_.moveX = AddSaturated(progress_.moveX, absolute(dx));
    progress_.moveY = AddSaturated(progress_.moveY, absolute(dy));
    const double distance = std::hypot(static_cast<double>(dx), static_cast<double>(dy));
    progress_.moveTotal = AddSaturated(progress_.moveTotal,
        static_cast<std::uint64_t>((std::min)(distance, 1'000'000.0)));
    if (leftDown_)
    {
        leftPressDistance_ = AddSaturated(leftPressDistance_,
            static_cast<std::uint64_t>((std::min)(distance, 1'000'000.0)));
    }
}

void TestState::Wheel(const int delta) noexcept
{
    if (delta > 0)
        progress_.wheelUp = AddSaturated(progress_.wheelUp, static_cast<unsigned>(delta));
    else if (delta < 0)
        progress_.wheelDown = AddSaturated(progress_.wheelDown,
            static_cast<unsigned>(-static_cast<std::int64_t>(delta)));
}

void TestState::Button(const MouseButton button, const bool down, const std::uint64_t now) noexcept
{
    if (button == MouseButton::Right)
    {
        if (down) rightDown_ = true;
        else if (rightDown_) { rightDown_ = false; Increment(progress_.rightClicks); }
        return;
    }
    if (button == MouseButton::Middle)
    {
        if (down) middleDown_ = true;
        else if (middleDown_) { middleDown_ = false; Increment(progress_.middleClicks); }
        return;
    }

    if (down)
    {
        if (!leftDown_)
        {
            leftDown_ = true;
            leftPressDistance_ = 0U;
        }
        return;
    }
    if (!leftDown_) return;
    leftDown_ = false;
    const unsigned distance = leftPressDistance_;
    leftPressDistance_ = 0U;

    if (config_.drags > 0U && distance >= config_.dragDistance)
    {
        Increment(progress_.drags);
        ResetClickChain();
        return;
    }
    if (distance > config_.clickTolerance)
    {
        ResetClickChain();
        return;
    }
    if (lastLeftClickTime_ != 0U && now >= lastLeftClickTime_ &&
        now - lastLeftClickTime_ <= doubleClickMs_ &&
        lastLeftClickDistance_ <= config_.clickTolerance)
    {
        Increment(progress_.doubleClicks);
        ResetClickChain();
        return;
    }
    Increment(progress_.leftClicks);
    lastLeftClickTime_ = now;
    lastLeftClickDistance_ = distance;
}

void TestState::ResetPressed() noexcept
{
    leftDown_ = false;
    rightDown_ = false;
    middleDown_ = false;
    leftPressDistance_ = 0U;
    ResetClickChain();
}

bool TestState::MoveComplete() const noexcept
{
    return progress_.moveX >= config_.moveAxisDistance &&
        progress_.moveY >= config_.moveAxisDistance &&
        progress_.moveTotal >= config_.moveDistance;
}

bool TestState::WheelComplete() const noexcept
{
    const unsigned target = config_.wheelSteps * 120U;
    return progress_.wheelUp >= target && progress_.wheelDown >= target;
}

bool TestState::LeftComplete() const noexcept { return progress_.leftClicks >= config_.leftClicks; }
bool TestState::DoubleComplete() const noexcept { return progress_.doubleClicks >= config_.doubleClicks; }
bool TestState::RightComplete() const noexcept { return progress_.rightClicks >= config_.rightClicks; }
bool TestState::MiddleComplete() const noexcept { return progress_.middleClicks >= config_.middleClicks; }
bool TestState::DragComplete() const noexcept { return progress_.drags >= config_.drags; }

unsigned TestState::CompletedTasks() const noexcept
{
    unsigned count{};
    if (MoveComplete()) ++count;
    if (WheelComplete()) ++count;
    if (LeftComplete()) ++count;
    if (DoubleComplete()) ++count;
    if (RightComplete()) ++count;
    if (MiddleComplete()) ++count;
    if (DragComplete()) ++count;
    return count;
}

bool TestState::IsComplete() const noexcept
{
    return CompletedTasks() == kTaskCount;
}
} // namespace ftc::modules::mouse
