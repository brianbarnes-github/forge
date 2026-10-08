#pragma once

#include "Playback/PlaybackController.h"
#include "Playback/PlayheadOverlay.h"
#include "Playback/TimelineRuler.h"
#include "SectionViewState.h"
#include "SongDocument.h"
#include "TimelineViewState.h"
#include "TrackRowComponent.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>
#include <optional>
#include <set>
#include <vector>

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
    // Entry point for wheel events that reach the list itself rather than the
    // viewport: i.e. the ruler. Treated as the note-canvas region.
    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override;

    // Where the wheel pointer is. Decides the plain wheel's meaning, independent
    // of whether a vertical scrollbar exists (see handleWheel).
    enum class WheelRegion { Heads, Canvas };

    // Current height of every track row (session-only; survives rebuild()).
    int getRowHeight() const noexcept { return rowHeight; }

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

    // Two independent selections. HEAD: selectedTrackIds / selectedTrackId, changed
    // by clicks on a track head (and selectAllHeads). CANVAS: sectionView.selected,
    // changed only by clicks on a note strip (and selectAllCanvases). Every canvas
    // change is mirrored one way onto the heads (the heads then show exactly the
    // tracks owning a selected section); a head click never touches the canvas.
    //
    // Ctrl/Cmd+A: acts on the region under the real pointer. Over the note strips
    // it is selectAllCanvases(); anywhere else selectAllHeads().
    void selectAll();
    // Every non-conductor track's head; the canvas selection is left alone.
    void selectAllHeads();
    // Every section of every non-conductor track, mirrored onto the heads.
    void selectAllCanvases();

    // Key S. Splits at `pointerTick` (the tick under the pointer, empty when the
    // pointer is not over a track's note strip), else at the playback marker, else
    // does nothing. Acts on the tracks owning a selected canvas section, else on
    // `pointerTrackId` (-1 for none); the head selection is not consulted. One undo step; returns whether any section was split.
    bool splitSections (std::optional<int> pointerTick, juce::int64 pointerTrackId);

    // Whether splitSections (nullopt, -1) -- the menu's Split, which has no pointer --
    // would split something: a marker is set and falls strictly inside a section of a
    // track that owns a selected canvas section.
    bool canSplitAtMarker() const;
    // Whether any canvas section is selected (Delete's enabled state).
    bool hasSelectedSections() const noexcept { return ! sectionView.selected.empty(); }

    // Key S from the real pointer: resolves the strip and tick under it, then
    // splitSections. Returns whether a section was split.
    bool splitAtPointer();

    // Key Delete: removes the selected sections (and their notes) in one undo
    // step, then forgets the selection. Returns false (and does nothing) when none
    // is selected.
    bool deleteSelectedSections();

    // Forgets the head and canvas selections. Fires no callbacks.
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
        int rowHeight = TrackRowComponent::defaultRowHeight;
    };

    // The viewport swallows wheel events over a row whenever its vertical
    // scrollbar is showing, so the list's own mouseWheelMove would only see them
    // when there is none. This subclass takes them first and routes by pointer
    // region through handleWheel, making the wheel's meaning scrollbar-independent.
    class WheelViewport : public juce::Viewport
    {
    public:
        explicit WheelViewport (TrackListComponent& o) : owner (o) {}
        void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override;
    private:
        TrackListComponent& owner;
    };

    // The one wheel decision. Ctrl/Cmd: resize every row (4 px per notch, wheel up
    // grows), keeping the row under `pointerY` (list coordinates) in place. Shift:
    // horizontal timeline scroll. Plain: Canvas zooms about the marker; Heads
    // scrolls the rows vertically (50 px per notch, wheel up = towards the first
    // row) and does nothing when there is nothing to scroll.
    void handleWheel (WheelRegion region, const juce::ModifierKeys& mods,
                      const juce::MouseWheelDetails& wheel, int pointerY);
    void resizeRowsBy (double pixels, int pointerY);
    void scrollRowsBy (int pixels);
    void applyRowHeight();

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
    // A click on a track head: changes only the head selection.
    void selectTrack (juce::int64 trackId, const juce::ModifierKeys& mods);
    void applySelectionToRows();

    // Canvas -> heads, one way: selectedTrackIds becomes the non-conductor tracks
    // owning a selected section, and the clicked track (if given) the anchor.
    void mirrorCanvasToHeads (std::optional<juce::int64> clickedTrackId);

    bool isConductorTrack (juce::int64 trackId) const;   // also true for an unknown id

    // True when the real pointer is over a row's note strip (not its head).
    bool pointerIsOverNoteStrips() const;
    bool anySplittable (const std::vector<juce::int64>& trackIds, int tick) const;

    // Section gestures from a row's note strip. A press edits the canvas selection
    // (plain: just that section, unless it is already part of a multi-selection,
    // which is kept so a drag moves them all; Ctrl toggles; Shift selects the
    // same-start sections across a range of tracks) and, for a plain press on a
    // section, starts a gesture; a drag only updates sectionView.drag;
    // the release commits once through moveSections/resizeSectionsBy. The document
    // is never touched before the release: any change rebuilds the rows, which
    // would destroy the strip holding the mouse.
    void sectionPressed (juce::int64 trackId, const SectionHit& hit, int tick, const juce::ModifierKeys& mods);
    void sectionDragged (int tick);
    void sectionReleased (int tick);

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
    WheelViewport   viewport { *this };
    // Scrolls the shared TimelineViewState; spans only the note-preview
    // column along the bottom edge. Auto-hides when the whole song fits.
    juce::ScrollBar horizontalBar { false };

    // Must outlive `content`: every TrackRowComponent in content.rows embeds
    // a TrackNotePreview holding a const reference to this (see
    // TrackRowComponent.h's constructor doc), and members are destroyed in
    // reverse declaration order — declaring this after `content` would
    // destroy it first, leaving those references dangling during teardown.
    TimelineViewState timelineView;
    // Must outlive `content` too: every row's preview holds a pointer to it.
    SectionViewState sectionView;
    ListContent     content;
    juce::int64     selectedTrackId = -1;
    std::set<juce::int64> selectedTrackIds;
    PlaybackController* playback = nullptr;

    struct SectionGesture
    {
        SectionZone zone = SectionZone::None;
        std::vector<SectionRef> refs;
        int pressTick = 0;
        int minStart = 0;   // earliest start among refs and their notes, for clamping the move
        // Set when the press landed on a section of a multi-selection: a release
        // without a drag collapses the selection to it.
        std::optional<SectionRef> collapseTo;
    };
    // The last plain/Ctrl-clicked section: the start of a Shift range.
    std::optional<SectionRef> canvasAnchor;
    std::optional<SectionGesture> gesture;

    static SectionDragPreview previewFor (const SectionGesture& g, int tick);

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

    // Row height, kept unrounded so touchpad deltas under one pixel accumulate
    // (rowHeight is its rounded value, what layout uses).
    double rowHeightExact = TrackRowComponent::defaultRowHeight;
    int    rowHeight = TrackRowComponent::defaultRowHeight;
};

} // namespace lotro
