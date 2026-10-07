#pragma once

#include "UI/Playback/TimelineRulerMarks.h"

#include <vector>

namespace lotro
{

// Which grid a line belongs to. Bar lines come from the meter; the rest are
// note values measured against the whole note (independent of the meter).
// Ordered coarse to fine, so a smaller value is a stronger line.
enum class GridLevel
{
    Bar,
    Whole,
    Half,
    Quarter,
    Eighth,
    Sixteenth,
    ThirtySecond,
    SixtyFourth
};

struct GridLine
{
    int tick = 0;
    GridLevel level = GridLevel::Bar;
};

// A division is drawn only once its lines are at least this many pixels apart.
constexpr double minGridLinePixels = 8.0;

// The grid lines in [firstTick, lastTick] at `pixelsPerTick`, sorted by tick,
// one per tick, each at the coarsest level it belongs to (a bar line beats the
// whole note on the same tick). Bar lines are always returned; finer levels
// appear as the view zooms in. An invalid meter is treated as 4/4. When the
// 1/64 note is not a whole number of ticks (odd PPQ) positions are rounded.
std::vector<GridLine> computeGridLines (int firstTick, int lastTick, double pixelsPerTick,
                                        int ticksPerQuarter, RulerMeter meter);

} // namespace lotro
