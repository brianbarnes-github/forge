#pragma once

#include "UI/GridLength.h"
#include "UI/MeterSegments.h"
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

// As above, with bar lines following every meter change (segments from meterSegments).
std::vector<GridLine> computeGridLines (int firstTick, int lastTick, double pixelsPerTick,
                                        int ticksPerQuarter, const std::vector<MeterChange>& meters);

// The MIDI editor's own grid (Options > Grid Length / Grid Division): lines every
// spacingTicks, drawn at `level`. 0 ticks is no editor grid.
struct EditorGridLines
{
    int spacingTicks = 0;
    GridLevel level = GridLevel::Quarter;
};

// The level a Grid Length's lines are drawn at (Off has none; it reads as Bar).
constexpr GridLevel gridLevelForLength (GridLength length) noexcept
{
    switch (length)
    {
        case GridLength::Whole:        return GridLevel::Whole;
        case GridLength::Half:         return GridLevel::Half;
        case GridLength::Quarter:      return GridLevel::Quarter;
        case GridLength::Eighth:       return GridLevel::Eighth;
        case GridLength::Sixteenth:    return GridLevel::Sixteenth;
        case GridLength::ThirtySecond: return GridLevel::ThirtySecond;
        case GridLength::SixtyFourth:  return GridLevel::SixtyFourth;
        case GridLength::Off:
        default:                       return GridLevel::Bar;
    }
}

// As above with an editor grid: bar lines, the note values that land on the
// editor grid's lines (a whole multiple of its spacing), and the editor grid's
// own lines at its level; note values it does not land on are left out. Each
// still needs minGridLinePixels. With 0 spacing it is the plain grid.
std::vector<GridLine> computeGridLines (int firstTick, int lastTick, double pixelsPerTick,
                                        int ticksPerQuarter, const std::vector<MeterChange>& meters,
                                        EditorGridLines editorGrid);

} // namespace lotro
