#include "PianoRollGeometry.h"

#include <algorithm>
#include <cmath>

namespace lotro
{

int PianoRollGeometry::xForTick (int tick) const noexcept
{
    const double ticksFromStart = (double) tick - visibleTickRange.getStart();
    const double quarters = ticksFromStart / (double) ticksPerQuarter;
    const double pixels = quarters * pixelsPerQuarterNote;
    return keyboardGutterWidth + (int) std::lround (pixels);
}

int PianoRollGeometry::tickForX (int x) const noexcept
{
    const double pixels = (double) (x - keyboardGutterWidth);
    const double quarters = pixels / pixelsPerQuarterNote;
    const double ticksFromStart = quarters * (double) ticksPerQuarter;
    return (int) std::lround (visibleTickRange.getStart() + ticksFromStart);
}

int PianoRollGeometry::yForPitch (int pitch) const noexcept
{
    return (topPitch - pitch) * rowHeight;
}

int PianoRollGeometry::pitchForY (int y) const noexcept
{
    return topPitch - (int) std::floor ((double) y / (double) rowHeight);
}

PianoRollNoteBounds PianoRollGeometry::noteBounds (const PianoRollNote& note) const noexcept
{
    const int x1 = xForTick (note.startTick);
    const int x2 = xForTick (note.startTick + note.durationTicks);

    PianoRollNoteBounds bounds;
    bounds.x = x1;
    bounds.y = yForPitch (note.pitch);
    bounds.width = std::max (1, x2 - x1);
    bounds.height = rowHeight;
    return bounds;
}

PianoRollGeometry PianoRollGeometry::fitToContent (juce::Range<int> tickRange,
                                                    juce::Range<int> pitchRange,
                                                    int ticksPerQuarter,
                                                    int viewportWidth,
                                                    int viewportHeight)
{
    juce::ignoreUnused (viewportHeight);

    PianoRollGeometry geometry;
    geometry.ticksPerQuarter = ticksPerQuarter;

    // Horizontal: choose a zoom factor so the whole tick span fits the
    // available width (viewport minus the keyboard gutter) without
    // scrolling — this is the "see the whole track" default view.
    const int tickSpan = tickRange.isEmpty() ? 1 : tickRange.getLength();
    const int availableWidth = std::max (1, viewportWidth - geometry.keyboardGutterWidth);
    const double quarterSpan = std::max (0.0001, (double) tickSpan / (double) ticksPerQuarter);
    geometry.pixelsPerQuarterNote = std::max (1.0, (double) availableWidth / quarterSpan);
    geometry.visibleTickRange = { (double) tickRange.getStart(),
                                  (double) tickRange.getStart() + (double) tickSpan };

    // Vertical: row height stays the fixed default (only horizontal zoom is
    // interactively adjustable — see PianoRollComponent's ctrl+wheel zoom in
    // Task B). Position the highest pitch in range at the top of the
    // viewport; a pitch span taller than the viewport is expected to scroll,
    // same as every other piano-roll editor's vertical axis.
    const bool emptyPitch = pitchRange.isEmpty();
    geometry.topPitch = emptyPitch ? pitchRange.getStart() : pitchRange.getEnd() - 1;

    return geometry;
}

} // namespace lotro
