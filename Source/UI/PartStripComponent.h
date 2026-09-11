#pragma once

#include "PartSlotComponent.h"
#include "SongDocument.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

// Phase 4, component B5 — the horizontal strip of PartSlotComponents (middle
// region of SongsmithMainComponent), rebuilt from PARTS whenever anything in
// that subtree changes (part add/remove, and — since chips live under
// parts — ASSIGNMENT add/remove or any PART/ASSIGNMENT property change).
//
// Overflow handling (Phase 4 whole-branch review finding I3): slots have a
// minimum width (minSlotWidth) below which the instrument badge/chips no
// longer fit. When n * minSlotWidth fits the available width, slots share it
// equally (as before); once it doesn't, every slot is pinned to
// minSlotWidth and the row is placed inside a horizontal-only juce::Viewport
// so the strip scrolls instead of squeezing.
namespace lotro
{

class PartStripComponent : public juce::Component,
                            private juce::ValueTree::Listener,
                            private juce::AsyncUpdater
{
public:
    explicit PartStripComponent (SongDocument& document);
    ~PartStripComponent() override;

    void paint (juce::Graphics& g) override;
    void resized() override;

    // Fired whenever a slot is selected (by click, or automatically after
    // "+ Add"), with that part's partId.
    std::function<void (juce::int64)> onPartSelected;

private:
    void rebuild();
    void selectPart (juce::int64 partId);
    void addPartClicked();

    // juce::ValueTree::Listener. PARTS is a smaller subtree than SOURCE_MIDI
    // in practice, but the same bubbling hazard applies (e.g. many
    // assignments changing at once), so coalesce the same way
    // TrackListComponent does: listeners only request a rebuild via
    // AsyncUpdater, and handleAsyncUpdate() does the single real rebuild.
    void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override { triggerAsyncUpdate(); }
    void valueTreeChildAdded (juce::ValueTree&, juce::ValueTree&) override { triggerAsyncUpdate(); }
    void valueTreeChildRemoved (juce::ValueTree&, juce::ValueTree&, int) override { triggerAsyncUpdate(); }
    void valueTreeChildOrderChanged (juce::ValueTree&, int, int) override { triggerAsyncUpdate(); }
    void valueTreeParentChanged (juce::ValueTree&) override {}
    void handleAsyncUpdate() override { rebuild(); }

    // The scrollable row of slots (M3: painted in SongsmithColours::border so
    // a 1px gap left between adjacent slots reads as a gutter, matching the
    // mockup).
    class Row : public juce::Component
    {
    public:
        void paint (juce::Graphics& g) override;
        juce::OwnedArray<PartSlotComponent> slots;
    };

    SongDocument&    doc;
    // See TrackListComponent.h's identical member for why this must be a
    // persistent handle rather than a fresh doc.getPartsNode() temporary at
    // addListener()/removeListener() time: ValueTree::addListener() stores
    // the listener on THIS HANDLE OBJECT, not the shared tree data, so a
    // temporary's registration is dropped the instant the temporary expires.
    juce::ValueTree  partsNode;
    juce::Label      header;
    juce::TextButton addButton { "+ Add" };
    juce::Viewport   viewport;
    Row              row;
    juce::int64      selectedPartId = -1;

    static constexpr int headerHeight = 20;
    static constexpr int addButtonWidth = 60;
    static constexpr int minSlotWidth = 140;
};

} // namespace lotro
