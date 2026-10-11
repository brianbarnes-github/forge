#pragma once

#include "UI/GridLines.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace lotro
{

// White at a per-level opacity, so one set of lines reads on any row shade:
// bar lines are the strongest and each finer division fades.
inline juce::Colour gridLineColour (GridLevel level)
{
    constexpr float alpha[] = { 0.20f, 0.14f, 0.12f, 0.10f, 0.08f, 0.06f, 0.045f, 0.035f };
    return juce::Colours::white.withAlpha (alpha[(int) level]);
}

// Draws the grid for the ticks under `clip` (x range) from y = top to bottom.
// `xForTick` maps a tick to a component x; `tickForX` is its inverse.
template <typename XForTick, typename TickForX>
void paintGridLines (juce::Graphics& g, juce::Rectangle<int> clip, double pixelsPerTick, int ticksPerQuarter,
                     const std::vector<MeterChange>& meters, XForTick xForTick, TickForX tickForX)
{
    if (clip.isEmpty())
        return;

    // One tick of slack either side so a line sitting on the clip edge is kept.
    const auto lines = computeGridLines (tickForX (clip.getX()) - 1, tickForX (clip.getRight()) + 1,
                                         pixelsPerTick, ticksPerQuarter, meters);
    for (const auto& line : lines)
    {
        const int x = xForTick (line.tick);
        if (x < clip.getX() || x > clip.getRight())
            continue;
        g.setColour (gridLineColour (line.level));
        g.drawVerticalLine (x, (float) clip.getY(), (float) clip.getBottom());
    }
}

// One meter for the whole timeline.
template <typename XForTick, typename TickForX>
void paintGridLines (juce::Graphics& g, juce::Rectangle<int> clip, double pixelsPerTick, int ticksPerQuarter,
                     RulerMeter meter, XForTick xForTick, TickForX tickForX)
{
    paintGridLines (g, clip, pixelsPerTick, ticksPerQuarter,
                    std::vector<MeterChange> { { 0, meter.numerator, meter.denominator } }, xForTick, tickForX);
}

} // namespace lotro
