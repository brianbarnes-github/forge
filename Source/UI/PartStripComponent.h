#pragma once

#include "PartSlotComponent.h"
#include "SongDocument.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

// Phase 4, component B5 — the horizontal strip of PartSlotComponents (middle
// region of SongsmithMainComponent), rebuilt from PARTS whenever anything in
// that subtree changes (part add/remove, and — since chips live under
// parts — ASSIGNMENT add/remove or any PART/ASSIGNMENT property change).
namespace lotro
{

class PartStripComponent : public juce::Component,
                            private juce::ValueTree::Listener,
                            private juce::AsyncUpdater
{
public:
    explicit PartStripComponent (SongDocument& document);
    ~PartStripComponent() override;

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

    SongDocument&    doc;
    juce::Label      header;
    juce::TextButton addButton { "+ Add" };
    juce::OwnedArray<PartSlotComponent> slots;
    juce::int64      selectedPartId = -1;

    static constexpr int headerHeight = 20;
    static constexpr int addButtonWidth = 60;
};

} // namespace lotro
