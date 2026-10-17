#pragma once

#include "modules/mouse/MouseConfig.h"

#include <cstdint>

namespace ftc::modules::mouse
{
enum class MouseButton
{
    Left,
    Right,
    Middle,
};

struct Progress final
{
    unsigned moveX{};
    unsigned moveY{};
    unsigned moveTotal{};
    unsigned wheelUp{};
    unsigned wheelDown{};
    unsigned leftClicks{};
    unsigned doubleClicks{};
    unsigned rightClicks{};
    unsigned middleClicks{};
    unsigned drags{};
};

class TestState final
{
public:
    explicit TestState(const Config& config, std::uint32_t doubleClickMs) noexcept;

    void Move(std::int32_t dx, std::int32_t dy) noexcept;
    void Wheel(int delta) noexcept;
    void Button(MouseButton button, bool down, std::uint64_t now) noexcept;
    void ResetPressed() noexcept;

    [[nodiscard]] const Config& Settings() const noexcept { return config_; }
    [[nodiscard]] const Progress& Current() const noexcept { return progress_; }
    [[nodiscard]] bool LeftDown() const noexcept { return leftDown_; }
    [[nodiscard]] bool RightDown() const noexcept { return rightDown_; }
    [[nodiscard]] bool MiddleDown() const noexcept { return middleDown_; }

    [[nodiscard]] bool MoveComplete() const noexcept;
    [[nodiscard]] bool WheelComplete() const noexcept;
    [[nodiscard]] bool LeftComplete() const noexcept;
    [[nodiscard]] bool DoubleComplete() const noexcept;
    [[nodiscard]] bool RightComplete() const noexcept;
    [[nodiscard]] bool MiddleComplete() const noexcept;
    [[nodiscard]] bool DragComplete() const noexcept;
    [[nodiscard]] unsigned CompletedTasks() const noexcept;
    [[nodiscard]] bool IsComplete() const noexcept;

private:
    static constexpr unsigned kTaskCount = 7U;
    static constexpr unsigned kMaxProgress = 1'000'000U;

    static unsigned AddSaturated(unsigned value, std::uint64_t amount) noexcept;
    static void Increment(unsigned& value) noexcept;
    void ResetClickChain() noexcept;

    Config config_;
    std::uint32_t doubleClickMs_{};
    Progress progress_;
    bool leftDown_{};
    bool rightDown_{};
    bool middleDown_{};
    unsigned leftPressDistance_{};
    std::uint64_t lastLeftClickTime_{};
    unsigned lastLeftClickDistance_{};
};
} // namespace ftc::modules::mouse
