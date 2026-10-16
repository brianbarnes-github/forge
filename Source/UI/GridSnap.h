#pragma once

#include <algorithm>
#include <cmath>

// Snapping a tick to the editor's grid (Edit > Grid size) and stepping along it.
// Grid lines are the multiples of gridTicks from tick 0; gridTicks <= 0 is
// "grid Off", which leaves every tick where it is. Header-only, no JUCE.
namespace lotro
{

// The nearest grid line, never below 0.
inline double snapTickToGrid (double tick, int gridTicks) noexcept
{
    if (gridTicks <= 0)
        return tick;
    return std::max (0.0, std::round (tick / gridTicks) * gridTicks);
}

// The next grid line after `tick` (direction > 0) or the previous one before it
// (direction < 0), stopping at 0. From between two lines it goes to the nearer
// line on that side, not a whole grid on.
inline double stepTickByGrid (double tick, int gridTicks, int direction) noexcept
{
    if (gridTicks <= 0 || direction == 0)
        return tick;
    const double line = direction > 0 ? std::floor (tick / gridTicks) + 1.0
                                      : std::ceil (tick / gridTicks) - 1.0;
    return std::max (0.0, line * gridTicks);
}

} // namespace lotro
