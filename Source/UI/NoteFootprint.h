#pragma once

// Where a note sits on the editor roll for each View > Piano Roll Notes shape:
// what is painted, what a click or the rubber band hits, and which edges resize.
// Painting and SourceRollEditor's hit tests share it so they always agree.

#include "NoteDisplay.h"
#include "PianoRollGeometry.h"

#include <juce_graphics/juce_graphics.h>

namespace lotro
{

enum class NoteEdges { both, rightOnly, none };

struct NoteFootprint
{
    juce::Rectangle<int> body;       // rectangle: the note; diamond shapes: the diamond's square
    juce::Rectangle<int> lengthLine; // diamondLength only: 2 px at the row's centre, start x to end x; else empty
    juce::Rectangle<int> hit;        // what clicks and the rubber band test
    NoteEdges edges = NoteEdges::both;
};

inline NoteFootprint noteFootprint (const PianoRollNoteBounds& note, NoteShape shape)
{
    const juce::Rectangle<int> rect (note.x, note.y, note.width, note.height);
    if (shape == NoteShape::rectangle)
        return { rect, {}, rect, NoteEdges::both };

    // One row tall and one row wide, centred on the note's start.
    const juce::Rectangle<int> square (note.x - note.height / 2, note.y, note.height, note.height);
    if (shape == NoteShape::diamond)
        return { square, {}, square, NoteEdges::none };

    const juce::Rectangle<int> line (note.x, note.y + note.height / 2 - 1, note.width, 2);
    return { square, line, square.getUnion (rect), NoteEdges::rightOnly };
}

} // namespace lotro
