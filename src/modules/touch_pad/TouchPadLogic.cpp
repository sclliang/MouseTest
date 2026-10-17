#include "modules/touch_pad/TouchPadLogic.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace ftc::modules::touch_pad
{
Coverage::Coverage(const Config& config)
    : rows_(config.rows), columns_(config.columns), leftRemaining_(config.leftClicks),
      rightRemaining_(config.rightClicks), visited_(static_cast<std::size_t>(config.rows) * config.columns, false)
{
    if (rows_ == 0U || columns_ == 0U || visited_.size() > 10'000U)
        throw std::invalid_argument("invalid touchpad grid");
    if (!config.exclusion.enabled) return;
    for (unsigned row = 0U; row < rows_; ++row)
    {
        for (unsigned column = 0U; column < columns_; ++column)
        {
            const bool horizontal = config.exclusion.corner == 0U || config.exclusion.corner == 2U
                ? column < config.exclusion.width : column >= columns_ - config.exclusion.width;
            const bool vertical = config.exclusion.corner == 0U || config.exclusion.corner == 1U
                ? row < config.exclusion.height : row >= rows_ - config.exclusion.height;
            if (horizontal && vertical)
            {
                visited_[static_cast<std::size_t>(row) * columns_ + column] = true;
                ++completed_;
            }
        }
    }
}

bool Coverage::Visit(const double x, const double y) noexcept
{
    if (!std::isfinite(x) || !std::isfinite(y) || x < 0.0 || y < 0.0 || x >= 1.0 || y >= 1.0) return false;
    const unsigned column = (std::min)(columns_ - 1U, static_cast<unsigned>(x * columns_));
    const unsigned row = (std::min)(rows_ - 1U, static_cast<unsigned>(y * rows_));
    const std::size_t index = static_cast<std::size_t>(row) * columns_ + column;
    if (visited_[index]) return false;
    visited_[index] = true;
    ++completed_;
    return true;
}
bool Coverage::LeftClick() noexcept { if (leftRemaining_ == 0U) return false; --leftRemaining_; return true; }
bool Coverage::RightClick() noexcept { if (rightRemaining_ == 0U) return false; --rightRemaining_; return true; }
bool Coverage::IsComplete() const noexcept { return completed_ == TotalCells() && leftRemaining_ == 0U && rightRemaining_ == 0U; }
unsigned Coverage::CompletedCells() const noexcept { return completed_; }
unsigned Coverage::TotalCells() const noexcept { return rows_ * columns_; }
unsigned Coverage::LeftRemaining() const noexcept { return leftRemaining_; }
unsigned Coverage::RightRemaining() const noexcept { return rightRemaining_; }
const std::vector<bool>& Coverage::Visited() const noexcept { return visited_; }
} // namespace ftc::modules::touch_pad
