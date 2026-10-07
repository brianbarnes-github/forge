#pragma once

#include "UI/SectionEdit.h"

#include <optional>
#include <set>

// Transient section selection and drag preview for the main canvas. Owned by
// TrackListComponent and shared by every TrackNotePreview, like TimelineViewState.
// Nothing here is in the document: a drag is committed once, on release.
namespace lotro
{

struct SectionDragPreview
{
    enum class Kind { Move, ResizeLeft, ResizeRight } kind = Kind::Move;
    int deltaTicks = 0;   // how far the pointer moved: every selected section (or edge) moves by it
    int edgeTick = 0;     // ResizeLeft / ResizeRight: where the pressed section's edge is now
};

struct SectionViewState
{
    std::set<SectionRef> selected;
    std::optional<SectionDragPreview> drag;
};

} // namespace lotro
