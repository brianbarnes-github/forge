#pragma once

// The text View > Piano Roll Notes' Show Note Names / Show Velocity Numbers
// draw on a note: what it says, where it starts and its colour over the fill.
// Never shortened: a label longer than its note spills past it. Hidden only
// when zoomed out below noteLabelMinPixelsPerQuarter.

#include "NoteDisplay.h"
#include "NoteFootprint.h"
#include "PianoRollGeometry.h"

#include <juce_graphics/juce_graphics.h>

namespace lotro
{

// Gap between the label and a rectangle's left edge or a diamond's right point.
inline constexpr int noteLabelInset = 3;

// Zoomed out past this (pixels per quarter note) the labels hide: a sixteenth
// is under 8 px and the spills smear together. Picked by eye.
inline constexpr double noteLabelMinPixelsPerQuarter = 31.6;

inline bool noteLabelsShownAt (double pixelsPerQuarterNote) noexcept
{
    return pixelsPerQuarterNote >= noteLabelMinPixelsPerQuarter;
}

// "C4", "96", "C4 96" with both on, or empty with neither.
inline juce::String noteLabelText (NoteLabels labels, int pitch, int velocity)
{
    juce::StringArray parts;
    if (labels.names)
        parts.add (PianoRollGeometry::noteName (pitch));
    if (labels.velocities)
        parts.add (juce::String (velocity));
    return parts.joinIntoString (" ");
}

// Inside a rectangle from its left edge; beside a diamond (over a Diamond +
// Length's line).
inline int noteLabelX (const NoteFootprint& footprint)
{
    return footprint.edges == NoteEdges::both ? footprint.body.getX() + noteLabelInset
                                              : footprint.body.getRight() + noteLabelInset;
}

// Readable over `fill`: the part of a label inside a rectangle note.
inline juce::Colour noteLabelColour (juce::Colour fill)
{
    return fill.getPerceivedBrightness() > 0.55f ? juce::Colour (0xFF1A1C20u) : juce::Colour (0xFFF4F4F2u);
}

} // namespace lotro
