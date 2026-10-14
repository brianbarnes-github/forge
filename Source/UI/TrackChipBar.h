#pragma once

#include "SongDocument.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <vector>

// The body of the Tracks dock panel: a thin horizontally scrolling row of
// chips, one per assignable track (a non-conductor track with notes). A chip
// can be dragged onto a part card, carrying the track id exactly like a track
// row does (PartSlotComponent accepts it); a click reports the track so the
// owner can select it. Rebuilt, coalesced, from the SOURCE_MIDI subtree, and
// only when a chip's number, name or colour actually changed -- note edits fire
// the same listener constantly and must not tear the chips down.
namespace lotro
{

class TrackChipBar : public juce::Component,
                     private juce::ValueTree::Listener,
                     private juce::AsyncUpdater
{
public:
    static constexpr int barHeight = 36;
    static constexpr int chipWidth = 150;

    explicit TrackChipBar (SongDocument& document);
    ~TrackChipBar() override;

    void paint (juce::Graphics& g) override;
    void resized() override;

    // A chip was pressed (not dragged): the track id and the press modifiers.
    std::function<void (juce::int64, const juce::ModifierKeys&)> onTrackClicked;

    class Chip : public juce::Component
    {
    public:
        Chip (juce::int64 trackIdIn, juce::String labelIn, juce::Colour colourIn);

        juce::int64 getTrackId() const noexcept { return trackId; }
        // What a drag carries: the same value a track row drags.
        juce::var dragDescription() const { return juce::var (trackId); }

        void paint (juce::Graphics& g) override;
        void mouseDown (const juce::MouseEvent& e) override;
        void mouseDrag (const juce::MouseEvent& e) override;

        std::function<void (juce::int64, const juce::ModifierKeys&)> onClicked;

    private:
        juce::int64 trackId;
        juce::String label;
        juce::Colour colour;
    };

    struct ChipInfo
    {
        juce::int64  trackId = 0;
        juce::String label;
        juce::Colour colour;
        bool operator== (const ChipInfo& o) const { return trackId == o.trackId && label == o.label && colour == o.colour; }
    };

    // test access
    void refreshForTesting() { cancelPendingUpdate(); refresh(); }
    int chipCountForTesting() const noexcept { return row.chips.size(); }
    ChipInfo chipInfoForTesting (int i) const { return infos[(size_t) i]; }
    Chip* chipComponentForTesting (int i) const { return row.chips[i]; }
    int contentWidthForTesting() const noexcept { return row.getWidth(); }

private:
    class Row : public juce::Component
    {
    public:
        juce::OwnedArray<Chip> chips;
    };

    std::vector<ChipInfo> currentInfos() const;
    void refresh();

    void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override { triggerAsyncUpdate(); }
    void valueTreeChildAdded (juce::ValueTree&, juce::ValueTree&) override { triggerAsyncUpdate(); }
    void valueTreeChildRemoved (juce::ValueTree&, juce::ValueTree&, int) override { triggerAsyncUpdate(); }
    void valueTreeChildOrderChanged (juce::ValueTree&, int, int) override { triggerAsyncUpdate(); }
    void valueTreeParentChanged (juce::ValueTree&) override {}
    void handleAsyncUpdate() override { refresh(); }

    SongDocument& doc;
    juce::ValueTree sourceMidiNode;   // a persistent handle: the listener lives on this object
    juce::Viewport viewport;
    Row row;
    std::vector<ChipInfo> infos;
};

} // namespace lotro
