#pragma once

#include "SongDocument.h"
#include "TrackRowComponent.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

// Phase 4, component B2 — the vertical, scrollable list of MIDI-track rows
// (top-left panel of SongsmithMainComponent). Rebuilds itself from
// SOURCE_MIDI whenever the ValueTree changes; holds the transient
// (not-persisted) selected trackId.
namespace lotro
{

class TrackListComponent : public juce::Component,
                            private juce::ValueTree::Listener,
                            private juce::AsyncUpdater
{
public:
    explicit TrackListComponent (SongDocument& document);
    ~TrackListComponent() override;

    void resized() override;
    void paint (juce::Graphics& g) override;

    // Fired whenever a row is clicked, with that row's trackId.
    std::function<void (juce::int64)> onTrackSelected;

private:
    class ListContent : public juce::Component
    {
    public:
        void resized() override;
        juce::OwnedArray<TrackRowComponent> rows;
    };

    void rebuild();
    void selectTrack (juce::int64 trackId);

    // juce::ValueTree::Listener — any of these firing on the SOURCE_MIDI
    // subtree (track add/remove/reorder or a property change on a track)
    // means the row list is stale. A real MIDI import appends notes one at a
    // time (SongModelBridge::importMidiFile), and JUCE bubbles every one of
    // those child-added notifications up through SOURCE_MIDI, so rebuilding
    // synchronously here would tear down and reconstruct every row (each
    // O(notes in that track)) once per note — thousands of rebuilds for a
    // real file. Coalesce via AsyncUpdater: listeners just request a
    // rebuild, and handleAsyncUpdate() performs at most one per message-loop
    // iteration no matter how many notifications land in between.
    void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override { triggerAsyncUpdate(); }
    void valueTreeChildAdded (juce::ValueTree&, juce::ValueTree&) override { triggerAsyncUpdate(); }
    void valueTreeChildRemoved (juce::ValueTree&, juce::ValueTree&, int) override { triggerAsyncUpdate(); }
    void valueTreeChildOrderChanged (juce::ValueTree&, int, int) override { triggerAsyncUpdate(); }
    void valueTreeParentChanged (juce::ValueTree&) override {}
    void handleAsyncUpdate() override { rebuild(); }

    SongDocument&   doc;
    juce::Viewport  viewport;
    ListContent     content;
    juce::int64     selectedTrackId = -1;
};

} // namespace lotro
