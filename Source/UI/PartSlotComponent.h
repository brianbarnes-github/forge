#pragma once

#include "AssignmentChipComponent.h"
#include "SongDocument.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

// Phase 4, component B4 — one slot in PartStripComponent: header (x number,
// instrument badge, label) plus a wrapping row of assignment chips (or a
// "drop here" placeholder when empty). A juce::DragAndDropTarget accepting
// MIDI-track drags from TrackListComponent.
//
// PartStripComponent fully rebuilds its PartSlotComponents (and therefore
// their chip children) on every PARTS-subtree change, so this class builds
// its chips once, in its constructor, from `partNode`'s current ASSIGNMENT
// children — it does not listen for changes itself.
namespace lotro
{

class PartSlotComponent : public juce::Component,
                           public juce::DragAndDropTarget
{
public:
    PartSlotComponent (SongDocument& document, juce::ValueTree partNode);

    void setSelected (bool shouldBeSelected);
    bool isSelected() const noexcept { return selected; }
    juce::int64 getPartId() const;

    void paint (juce::Graphics& g) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent& e) override;

    // juce::DragAndDropTarget
    bool isInterestedInDragSource (const SourceDetails& details) override;
    void itemDragEnter (const SourceDetails& details) override;
    void itemDragExit (const SourceDetails& details) override;
    void itemDropped (const SourceDetails& details) override;

    // Fired on a plain (non-drag, non-context-menu) click, with this slot's
    // partId.
    std::function<void (juce::int64)> onPartSelected;

private:
    void showContextMenu();
    void promptRename();

    SongDocument&    doc;
    juce::ValueTree  part;
    bool             selected = false;
    bool             dragHighlight = false;

    juce::OwnedArray<AssignmentChipComponent> chips;
};

} // namespace lotro
