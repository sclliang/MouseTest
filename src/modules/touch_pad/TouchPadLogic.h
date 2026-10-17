#pragma once

#include "modules/touch_pad/TouchPadConfig.h"

#include <cstdint>
#include <vector>

namespace ftc::modules::touch_pad
{
struct Contact final
{
    std::int32_t id{};
    std::int32_t x{};
    std::int32_t y{};
    bool operator==(const Contact&) const noexcept = default;
};

class Coverage final
{
public:
    explicit Coverage(const Config& config);
    [[nodiscard]] bool Visit(double normalizedX, double normalizedY) noexcept;
    [[nodiscard]] bool LeftClick() noexcept;
    [[nodiscard]] bool RightClick() noexcept;
    [[nodiscard]] bool IsComplete() const noexcept;
    [[nodiscard]] unsigned CompletedCells() const noexcept;
    [[nodiscard]] unsigned TotalCells() const noexcept;
    [[nodiscard]] unsigned LeftRemaining() const noexcept;
    [[nodiscard]] unsigned RightRemaining() const noexcept;
    [[nodiscard]] const std::vector<bool>& Visited() const noexcept;
private:
    unsigned rows_{};
    unsigned columns_{};
    unsigned leftRemaining_{};
    unsigned rightRemaining_{};
    unsigned completed_{};
    std::vector<bool> visited_;
};
} // namespace ftc::modules::touch_pad
