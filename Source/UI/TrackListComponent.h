#pragma once

#include "Playback/PlaybackController.h"
#include "Playback/PlayheadOverlay.h"
#include "Playback/TimelineRuler.h"
#include "SongDocument.h"
#include "TimelineViewState.h"
#include "TrackRowComponent.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>
#include <set>

// Phase 4, component B2 — the vertical, scrollable list of MIDI-track rows
// (top-left panel of SongsmithMainComponent). Rebuilds itself from
// SOURCE_MIDI whenever the ValueTree changes; holds the transient
// (not-persisted) selected trackId.
namespace lotro
{

class TrackListComponent : public juce::Component,
                            private juce::ValueTree::Listener,
                            private juce::AsyncUpdater,
                            private juce::ScrollBar::Listener,
                            private PlaybackController::Listener
{
public:
    explicit TrackListComponent (SongDocument& document);
    ~TrackListComponent() override;

    void resized() override;
    void paint (juce::Graphics& g) override;
    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override;

    // Rescales the shared TimelineViewState so the latest-ending note across
    // all tracks fits the note-preview width, and resets scroll to the
    // start. Called explicitly by whoever just imported a MIDI file (see
    // MainWindow::openMidiFromPath) rather than from rebuild()/the
    // ValueTree-listener path, so routine edits to an already-loaded
    // document (dragging a note, renaming a track) don't keep snapping a
    // user's deliberate zoom/scroll back to the fitted default.
    void fitTimelineToDocument();

    // Connects the M / S buttons to `controller` (not owned; must outlive this
    // list or be replaced by nullptr first). Rows are rebuilt to pick up its state.
    // Also creates (non-null) / destroys (null) the seek ruler row above the
    // viewport and the playhead overlay over the note-preview strip.
    void setPlayback (PlaybackController* controller);

    TimelineRuler* rulerForTesting() noexcept { return ruler.get(); }
    void followPlayheadForTesting (bool playing) { followPlayhead (playing); }

    juce::int64 getSelectedTrackId() const noexcept { return selectedTrackId; }
    const std::set<juce::int64>& getSelectedTrackIds() const noexcept { return selectedTrackIds; }
    void selectAllTracks();

    // Forgets the selected row. Fires no callbacks.
    void clearSelection();

    // Fired when a row is double-clicked, with that row's trackId.
    std::function<void (juce::int64)> onTrackDoubleClicked;

    // Fired when a row's ghost toggle is clicked: (trackId, newVisibility).
    std::function<void (juce::int64, bool)> onGhostToggled;

    // Asked, per row, whether that track is currently ghosted, so rebuild()
    // can restore each row's eye icon the same way it restores selection.
    // The ghosted set itself lives with the owner (SongsmithMainComponent),
    // which is what actually drives the editor's overlays — a recreated row
    // has no way to know its own previous state. Unset means "nothing is
    // ghosted".
    std::function<bool (juce::int64)> isTrackGhosted;

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
    void muteSoloChanged() override;
    void playbackMarkerChanged() override { if (ruler != nullptr) ruler->repaint(); }
    void playbackPositionChanged() override { followPlayhead (playback != nullptr && playback->isPlaying()); }

    // While playing, page-flips the shared view so the playhead stays visible.
    // Does nothing when stopped, so a ruler click never scrolls the view.
    void zoomAboutMarker (double factor);
    void followPlayhead (bool playing);

    // The overlay skips repaints when the playhead x is unchanged, so every
    // change to the tick->x mapping (zoom, scroll, resize, fit) must call this.
    void refreshOverlay();
    void selectTrack (juce::int64 trackId, const juce::ModifierKeys& mods, bool fromStrip);
    void applySelectionToRows();

    // Content width for `content`, accounting for the viewport's vertical
    // scrollbar (M2: rebuild() used to set the un-subtracted viewport width,
    // clipping rows under the scrollbar until the next resize; both call
    // sites now share this one expression).
    int contentWidth() const;

    // X of the note-timeline strip's left edge, in this component's local
    // space. Rows span the content's full width and lay their
    // TrackNotePreview out to the right of the fixed-width info column
    // (TrackRowComponent::resized()), and it is that preview-local frame
    // which TimelineViewState's xForTick/tickForX are expressed in — so a
    // local x has to be shifted by this before it can be used as a zoom
    // anchor.
    int notePreviewOriginX() const;

    // End tick of the latest-ending note across all tracks (0 when empty).
    int documentEndTick() const;

    // Width of the note-preview strip in pixels.
    int previewWidth() const;

    // Clamps the shared scroll offset to [0, end of song - visible span] and
    // pushes the current song length / visible span / offset into
    // horizontalBar. Called after anything that changes zoom, scroll, width
    // or the song's length.
    void syncHorizontalBar();

    // Largest valid scroll offset: end of song minus the visible span (>= 0).
    double maxScrollOffset() const;

    void scrollBarMoved (juce::ScrollBar*, double newRangeStart) override;

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
    // Scrolls the shared TimelineViewState; spans only the note-preview
    // column along the bottom edge. Auto-hides when the whole song fits.
    juce::ScrollBar horizontalBar { false };

    // Must outlive `content`: every TrackRowComponent in content.rows embeds
    // a TrackNotePreview holding a const reference to this (see
    // TrackRowComponent.h's constructor doc), and members are destroyed in
    // reverse declaration order — declaring this after `content` would
    // destroy it first, leaving those references dangling during teardown.
    TimelineViewState timelineView;
    ListContent     content;
    juce::int64     selectedTrackId = -1;
    std::set<juce::int64> selectedTrackIds;
    PlaybackController* playback = nullptr;

    // Declared after `playback`/`content`: destroyed first, before the
    // controller (which the overlay's listener registration refers to).
    std::unique_ptr<TimelineRuler>    ruler;
    std::unique_ptr<PlayheadOverlay>  overlay;
    std::unique_ptr<MarkerOverlay>    markerOverlay;
    int                               overlayRepaintCount = 0;   // test observability

    // True from fitTimelineToDocument() until the user zooms by hand: while
    // set, resized() refits so the whole song stays visible across window
    // resizes. After a manual zoom, resizing keeps the zoom and the
    // horizontal scroll bar takes over.
    bool timelineFitted = false;
};

} // namespace lotro
