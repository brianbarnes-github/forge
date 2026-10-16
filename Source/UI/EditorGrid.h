#pragma once

#include "GridLength.h"
#include "MeterSegments.h"

#include <string>
#include <vector>

// The MIDI editor's grid as a value: straight lines at a spacing, or, for the
// Swing division, REAPER-style swung lines that restart at every bar line.
// Every editor snap, step and grid-line draw goes through the functions here.
// For a non-Swing grid they reproduce GridSnap.h exactly. No JUCE.
namespace lotro
{

// Swing strength as the slider shows it: whole percent, 0 = straight.
constexpr int minSwingPercent = -100, maxSwingPercent = 100;

constexpr int clampSwingPercent (int percent) noexcept
{
    return percent < minSwingPercent ? minSwingPercent
         : percent > maxSwingPercent ? maxSwingPercent
                                     : percent;
}

struct EditorGrid
{
    int spacingTicks = 0;              // 0 = no grid (every function returns the tick unchanged / no lines)
    bool swung = false;                // GridDivision::Swing: pairs restart at each bar line
    double swing = 0.0;                // -1..1, read only when swung
    std::vector<MeterSegment> bars;    // swung only; never empty when swung

    static EditorGrid straight (int spacingTicks);
};

// Swing keeps the Straight spacing; `meters` supplies the bars it restarts on.
EditorGrid makeEditorGrid (GridLength, GridDivision, int swingPercent, int ticksPerQuarter,
                           const std::vector<MeterChange>& meters);

// The last line <= tick (0 for tick < 0).
double editorGridLineAtOrBefore (const EditorGrid&, double tick);

// The first line > tick.
double editorGridLineAfter (const EditorGrid&, double tick);

// The nearest line, ties to the later one, never below 0.
double snapToEditorGrid (const EditorGrid&, double tick);

// The next (direction > 0) or previous (direction < 0) line, stopping at 0.
double stepEditorGrid (const EditorGrid&, double tick, int direction);

// Every line in [firstTick, lastTick], sorted.
std::vector<int> editorGridLinesIn (const EditorGrid&, int firstTick, int lastTick);

// The smallest distance between neighbouring lines: s * (1 - |w| / 2) swung,
// s otherwise, 0 when the grid is off.
double editorGridMinGap (const EditorGrid&);

// "+67%", "0%", "-25%".
std::string swingPercentText (int percent);

} // namespace lotro
