#include "PianoRollGeometry.h"

#include <algorithm>
#include <cmath>

namespace lotro
{

int PianoRollGeometry::xForTick (int tick) const noexcept
{
    const double ticksFromOrigin = (double) tick - contentOriginTick;
    const double quarters = ticksFromOrigin / (double) ticksPerQuarter;
    const double pixels = quarters * pixelsPerQuarterNote;
    return keyboardGutterWidth + (int) std::lround (pixels);
}

int PianoRollGeometry::tickForX (int x) const noexcept
{
    const double pixels = (double) (x - keyboardGutterWidth);
    const double quarters = pixels / pixelsPerQuarterNote;
    const double ticksFromOrigin = quarters * (double) ticksPerQuarter;
    return (int) std::lround (contentOriginTick + ticksFromOrigin);
}

int PianoRollGeometry::yForPitch (int pitch) const noexcept
{
    return (topPitch - pitch) * rowHeight;
}

int PianoRollGeometry::pitchForY (int y) const noexcept
{
    return topPitch - (int) std::floor ((double) y / (double) rowHeight);
}

bool PianoRollGeometry::isBlackKey (int pitch) noexcept
{
    const int pitchClass = ((pitch % 12) + 12) % 12;
    return pitchClass == 1 || pitchClass == 3 || pitchClass == 6 || pitchClass == 8 || pitchClass == 10;
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

    // Horizontal: choose a zoom factor so the span from the fixed tick-0
    // origin (below) to the end of the tick range fits the available width
    // (viewport minus the keyboard gutter) without scrolling — this is the
    // "see the whole track" default view. Measuring from the range's own
    // start (its length) would under-zoom whenever the range doesn't start
    // near 0, leaving the content off the right edge of the viewport.
    const int tickSpan = tickRange.isEmpty() || tickRange.getEnd() <= 0 ? 1 : tickRange.getEnd();
    const int availableWidth = std::max (1, viewportWidth - geometry.keyboardGutterWidth);
    const double quarterSpan = std::max (0.0001, (double) tickSpan / (double) ticksPerQuarter);
    geometry.pixelsPerQuarterNote = std::max (1.0, (double) availableWidth / quarterSpan);
    // Fixed at tick 0, not the content's own first tick — a document-level
    // origin is required for Phase 6's shared time axis between the source
    // and preview rolls; "document's earliest note" was rejected because
    // that value shifts on every note add/delete.
    geometry.contentOriginTick = 0.0;

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
