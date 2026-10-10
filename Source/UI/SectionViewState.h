#pragma once

#include "UI/SectionEdit.h"

#include <juce_graphics/juce_graphics.h>

#include <optional>
#include <set>
#include <vector>

// Transient section selection and drag preview for the main canvas. Owned by
// TrackListComponent and shared by every TrackNotePreview, like TimelineViewState.
// Nothing here is in the document: a drag is committed once, on release.
namespace lotro
{

struct SectionDragPreview
{
    enum class Kind { Move, ResizeLeft, ResizeRight } kind = Kind::Move;
    int deltaTicks = 0;   // how far the pointer moved: every selected section (or edge) moves by it
};

// An Alt-drag that would merge the carried sections into another track. Preview only:
// the notes move on release (see NoteMerge).
struct MergeDragPreview
{
    juce::int64 targetTrackId = -1;     // the row under the pointer; -1 when none
    bool valid = false;                 // that row can take the notes
    bool copy = false;                  // Ctrl/Cmd is down
    juce::Point<int> pointerScreen;     // for the Move/Copy label
    std::vector<SectionRange> ghosts;   // the carried sections, at their own ticks
};

struct SectionViewState
{
    std::set<SectionRef> selected;
    std::optional<SectionDragPreview> drag;
    std::optional<MergeDragPreview> merge;
};

} // namespace lotro
