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
    // Test-only access to selectTrack()/rebuild() so TrackListComponent_tests.cpp
    // can drive selection and stale-selection cleanup deterministically
    // without needing to pump JUCE's message loop for the real (async,
    // debounced) ValueTree-listener path. Declared here rather than widening
    // the public API for behavior that's otherwise only ever triggered
    // internally (a row click) or by JUCE's async update mechanism.
    friend struct TrackListComponentTestAccess;

    class ListContent : public juce::Component
    {
    public:
        void resized() override;
        juce::OwnedArray<TrackRowComponent> rows;
    };

    void rebuild();
    void selectTrack (juce::int64 trackId);

    // Content width for `content`, accounting for the viewport's vertical
    // scrollbar (M2: rebuild() used to set the un-subtracted viewport width,
    // clipping rows under the scrollbar until the next resize; both call
    // sites now share this one expression).
    int contentWidth() const;

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
    // ValueTree::addListener() stores the listener on THIS HANDLE OBJECT
    // (ValueTree::listeners is a per-handle member, not on the shared tree
    // data), and only keeps the SharedObject aware of the registration via a
    // raw back-pointer to this handle. Calling doc.getSourceMidiNode()
    // fresh at addListener()-time and letting that temporary expire at the
    // end of the statement silently drops the registration immediately —
    // this member exists so the listener stays registered for the
    // component's whole lifetime. Must be used for both addListener() and
    // removeListener(); plain reads (getNumTracks(), getTrack(i), etc.) are
    // unaffected and can keep using doc.getSourceMidiNode() freshly.
    juce::ValueTree sourceMidiNode;
    juce::Viewport  viewport;
    ListContent     content;
    juce::int64     selectedTrackId = -1;
};

} // namespace lotro
