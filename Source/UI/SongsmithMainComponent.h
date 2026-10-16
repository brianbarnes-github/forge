#pragma once

#include "DiagnosticListView.h"
#include "GridSize.h"
#include "MergeScope.h"
#include "PartStripComponent.h"
#include "PianoRollComponent.h"
#include "Dock/DockHost.h"
#include "Dock/DockStack.h"
#include "Playback/PlaybackController.h"
#include "PreviewAssignedPanel.h"
#include "PreviewNoteSource.h"
#include "SongDocument.h"
#include "SplitterComponent.h"
#include "TrackEditorPanel.h"
#include "TempoMeterEditorWindow.h"
#include "TrackListComponent.h"

#include "Playback/TransportStrip.h"
#include "TrackChipBar.h"
#include "Mixer/ActivePlaybackTarget.h"
#include "Mixer/MixerPanelComponent.h"
#include "Mixer/SourceMidiTarget.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>
#include <set>
#include <vector>

// Phase 4, component B6 — the top-level Songsmith preview view: a vertical
// stack of (1) the MIDI-source header, (2) the track list + piano roll, (3)
// the part strip, (4) the LOTRO preview region (assigned-track panel + a
// second, read-only piano roll with range/ghost/dropped overlays — Phase 6),
// and (5) a diagnostics list for import diagnostics.
//
// This hosts a bare DiagnosticListView, NOT the full DiagnosticsPane — the
// ABC-preview half of DiagnosticsPane has nothing to show in Songsmith mode
// (there is no Run/export path here until Phase 6), and driving it with an
// empty string produced a permanently-visible "0 bytes · 0 bars · 0 parts"
// status line that reads as a failed import (see I2 in the Phase 4
// whole-branch review). Phase 6 is expected to bring the ABC preview back as
// a toggleable panel once Songsmith has something to run.
//
// Phase 5: the boundary between the MIDI-source region (header + track list
// + piano roll) and the lower regions (part strip + diagnostics) is user-resizable.
// It is now a dock::DockStack (the MIDI editor panel can dock in it too); see
// DockHost and docs/ARCHITECTURE.md §9.19.
//
// Phase 6: the lower region gains a splitter, between the preview
// region (now a dock panel in a bottom DockStack) and the diagnostics list — see LowerRegion below.
namespace lotro
{

class SongsmithMainComponent : public juce::Component,
                                public juce::DragAndDropContainer,
                                private juce::ValueTree::Listener,
                                private juce::AsyncUpdater
{
public:
    explicit SongsmithMainComponent (SongDocument& document, PlaybackController* playbackIn = nullptr);
    ~SongsmithMainComponent() override;

    void paint (juce::Graphics& g) override;
    void resized() override;

    DiagnosticListView& getDiagnostics() noexcept { return diagnostics; }

    // View -> Diagnostics list menu toggle (MainWindow). Hidden by default;
    // hiding it hands its share of lowerRegion's height back to the preview
    // region instead of leaving a dead gap (see LowerRegion::resized()).
    void setDiagnosticsVisible (bool shouldShow)
    {
        lowerRegion.setDiagnosticsVisible (shouldShow);
        dockHost.setLowerHasOtherContent (shouldShow);
    }
    bool isDiagnosticsVisible() const noexcept { return lowerRegion.isDiagnosticsVisible(); }

    // Called by MainWindow right after a MIDI import completes, so the
    // per-track note previews default to showing the whole song instead of
    // TimelineViewState's very-zoomed-in initial pixelsPerTick.
    void fitTrackTimelineToDocument() { trackList.fitTimelineToDocument(); }

    // The song's key (keyIndex, 0..23) shown on the Conductor head as "key: G major";
    // nullopt shows none. Session state: MainWindow sets it after a MIDI import and
    // from Song ▸ Set Key; documentReplaced() clears it.
    void setSongKey (std::optional<int> keyIndex);
    std::optional<int> getSongKey() const noexcept { return songKey; }

    // Called by MainWindow after the document's contents were replaced
    // (New / Open). Resets everything that lives outside the tree:
    // closes the track editor (its note source holds the OLD MIDI_TRACK
    // handle), clears ghost / track / part selection and the preview (ids
    // persist per Song, so a stale id could re-select a different Song's
    // part), clears the detected-key text and refits the timeline. Must not
    // write to the tree.
    void documentReplaced();

    // Track-canvas keys, forwarded by MainWindow::keyPressed. Each returns / does
    // nothing when there is nothing to act on, so the caller can leave the key
    // unconsumed.
    bool splitSections()   { return trackList.splitAtPointer(); }
    bool deleteSections()  { return trackList.deleteSelectedSections(); }
    void selectAll()       { trackList.selectAll(); }
    // Escape (MainWindow::keyPressed, after its text-input guard). Precedence: a section or
    // merge drag is cancelled; otherwise, while a note-editor drag is in progress nothing
    // happens; otherwise the loop range is cleared. Returns false when nothing happened.
    bool escapePressed();

    // The grid, in ticks, loop-range edges snap to: the loaded editor's grid, else the
    // default grid (Preferences ▸ Editing) at the document's ticksPerQuarter.
    int currentLoopSnapTicks() const;
    // Menu paths: no pointer is consulted (the pointer is over the menu).
    bool splitAtMarker()          { return trackList.splitSections (std::nullopt, -1); }
    bool canSplitAtMarker() const { return trackList.canSplitAtMarker(); }
    bool hasSelectedSections() const { return trackList.hasSelectedSections(); }
    void selectAllTracks()        { trackList.selectAllHeads(); }
    void selectAllSections()      { trackList.selectAllCanvases(); }
    void clearSelectionForTesting()   { trackList.clearSelection(); }
    void selectAllHeadsForTesting()   { trackList.selectAllHeads(); }
    void selectAllCanvasesForTesting() { trackList.selectAllCanvases(); }
    std::set<juce::int64> getSelectedTrackIdsForTesting() const { return trackList.getSelectedTrackIds(); }

private:
    friend struct SongsmithMainComponentTestAccess;

    // Groups the widgets shown above the top dock divider so the stack
    // can treat them as a single component for layout purposes (mirrors
    // MainWindow::Body's editor/diagnostics split — see SplitterComponent.h).
    class UpperRegion : public juce::Component
    {
    public:
        UpperRegion (juce::Label& headerIn, TrackListComponent& trackListIn);
        void resized() override;

    private:
        juce::Label& header;
        TrackListComponent& trackList;
    };

    // The LOTRO-preview region: a fixed-width left info panel plus the
    // preview piano roll filling the rest — same layout idiom as
    // UpperRegion's trackList + roll.
    class PreviewRegion : public juce::Component
    {
    public:
        PreviewRegion (PreviewAssignedPanel& panelIn, PianoRollComponent& rollIn);
        void resized() override;

    private:
        PreviewAssignedPanel& panel;
        PianoRollComponent& roll;
    };

    class LowerRegion : public juce::Component
    {
    public:
        // `transportIn` (null when there is no playback) is the bar across the
        // top of the region; it rides along when the dock divider above is dragged.
        LowerRegion (juce::Component* transportIn, dock::DockStack& bottomStackIn, DiagnosticListView& diagnosticsIn,
                     dock::DockStack& topStackIn);
        void resized() override;

        void setDiagnosticsVisible (bool shouldShow);
        bool isDiagnosticsVisible() const noexcept { return diagnostics.isVisible(); }

    private:
        // The bar between the transport strip and what is docked beneath it. The strip itself
        // has a fixed height, so dragging this bar drags the top stack's last divider instead.
        class Divider : public juce::Component
        {
        public:
            explicit Divider (dock::DockStack& stackIn) : stack (stackIn)
            {
                setMouseCursor (juce::MouseCursor::UpDownResizeCursor);
            }
            void paint (juce::Graphics& g) override { g.fillAll (juce::Colours::grey); }
            void mouseDown (const juce::MouseEvent&) override { stack.beginDrag(); }
            void mouseDrag (const juce::MouseEvent& e) override
            {
                if (stack.dividerCount() > 0)
                    stack.dragBar (stack.dividerCount() - 1, e.getDistanceFromDragStartY());
            }
            void mouseUp (const juce::MouseEvent& e) override { stack.endDrag (e.mouseWasDraggedSinceMouseDown()); }

        private:
            dock::DockStack& stack;
        };

        juce::Component* transport;
        Divider transportDivider;
        dock::DockStack& bottomStack;
        DiagnosticListView& diagnostics;
        SplitterComponent innerSplitter { SplitterComponent::Orientation::topBottom };
    };

public:
    void trackDoubleClicked (juce::int64 trackId);

    // The dock layout (which panels are shown, docked or floating, where). Applying one does
    // not fire onDockLayoutChanged; any user-driven change does, so the owner can persist it.
    dock::LayoutState getDockLayout() const { return dockHost.state(); }
    // The user's strip command buttons, on both transport strips (main and MIDI editor). Not owned.
    void setStripButtonsHost (StripButtonsHost* host);
    void setDockLayout (const dock::LayoutState& layout) { dockHost.apply (layout); }
    // The layout saved by the last session, at launch: as setDockLayout, except a MIDI editor that
    // was left showing opens hidden, because it has no track yet and would only show an empty roll.
    void restoreDockLayout (dock::LayoutState layout);
    bool isMidiEditorVisible() const { return dockHost.isVisible ("midiEditor"); }
    void setMidiEditorVisible (bool show)
    {
        dockHost.setCentreFloatsAround (getTopLevelComponent());
        dockHost.setVisible ("midiEditor", show);
    }
    bool isSourceVisible() const { return dockHost.isUpperVisible(); }
    void setSourceVisible (bool show) { dockHost.setUpperVisible (show); }
    bool isTracksVisible() const { return dockHost.isVisible ("tracks"); }
    void setTracksVisible (bool show) { dockHost.setVisible ("tracks", show); }
    bool isLotroPreviewVisible() const { return dockHost.isVisible ("lotroPreview"); }
    void setLotroPreviewVisible (bool show) { dockHost.setVisible ("lotroPreview", show); }
    bool isPartsVisible() const { return dockHost.isVisible ("parts"); }
    void setPartsVisible (bool show) { dockHost.setVisible ("parts", show); }
    // No mixer without a playback controller: always hidden then, and showing it does nothing.
    bool isMixerVisible() const { return mixerPanel != nullptr && dockHost.isVisible ("mixer"); }
    void setMixerVisible (bool show)
    {
        if (mixerPanel != nullptr)
            dockHost.setVisible ("mixer", show);
    }
    // Window shortcuts (Ctrl+S, Delete) a floating panel did not handle; the owner runs them as if
    // the main window had the focus. Return true if consumed. Esc is handled here.
    std::function<bool (const juce::KeyPress&)> onFloatingShortcut;
    std::function<void (const dock::LayoutState&)> onDockLayoutChanged;

    // Opens the Tempo/Meter editor, or re-fronts it when already open.
    void openTempoMeterEditor();

    // Marker single-click: when the editor is open, selects that event's row and fronts it
    // (returns true); when closed does nothing and returns false. A no-op selection if the
    // event is gone. openTempoMeterEditorOnEvent is the double-click: open/front, then select.
    bool selectTempoMeterEvent (TempoMeterKind kind, int tick);   // only when the editor is open; false = not handled
    void openTempoMeterEditorOnEvent (TempoMeterKind kind, int tick);
    bool isTempoMeterEditorOpen() const noexcept { return tempoMeterWindow != nullptr; }
    void trackGhostToggled (juce::int64 trackId, bool visible);

    bool isTrackEditorOpen() const noexcept { return dockHost.isVisible ("midiEditor"); }
    // True when the editor is showing a track (visible and loaded); Edit > Quantize / Grid need this.
    bool isTrackEditorLoaded() const noexcept { return isTrackEditorOpen() && trackEditor->getTrackId() != -1; }
    void setActiveEditorGridSize (GridSize size);
    void setFollowPlayhead (bool follow);
    // Pushed from the setting to the MIDI editor; its Options > Snap to Grid toggle reports
    // the new value through onSnapToGridChanged (to be saved).
    void setSnapToGrid (bool on);
    std::function<void (bool on)> onSnapToGridChanged;
    void setMergeScope (MergeScope scope);
    // The track list's head-column width (see TrackListComponent::setHeadWidth). A divider
    // drag reports its final width through onTrackHeadWidthChanged, so the owner can save it.
    void setTrackHeadWidth (int width) { trackList.setHeadWidth (width); }
    std::function<void (int)> onTrackHeadWidthChanged;
    // The MIDI editor's File > Export to New MIDI File, for the track it has loaded;
    // the owner runs the save dialog and writes the file.
    std::function<void (juce::int64 trackId)> onExportTrackMidi;
    void setDefaultGridSize (GridSize size);
    void quantizeActiveEditor() { if (trackEditor->getTrackId() != -1) trackEditor->quantizeSelection(); }

private:
    void refreshGhostTracksOnEditor();

    // Unregisters from the previously-watched PART/ASSIGNMENT-referenced-
    // MIDI_TRACK nodes, resolves and registers on the newly-selected part's
    // current set, and recomputes the preview immediately. partId == -1
    // clears the preview (no part selected).
    void selectPartForPreview (juce::int64 partId);

    // juce::ValueTree::Listener, scoped (via selectPartForPreview's
    // add/removeListener calls) to the selected PART node and each of its
    // currently-assigned MIDI_TRACK nodes. Coalesced through AsyncUpdater,
    // same rationale as TrackListComponent/PartStripComponent. Playback
    // volume and pan cannot change the preview, so a slider drag recomputes nothing.
    void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier& property) override
    {
        if (property != SongIDs::playbackVolume && property != SongIDs::playbackPan)
            triggerAsyncUpdate();
    }
    void valueTreeChildAdded (juce::ValueTree&, juce::ValueTree&) override { triggerAsyncUpdate(); }
    void valueTreeChildRemoved (juce::ValueTree&, juce::ValueTree&, int) override { triggerAsyncUpdate(); }
    void valueTreeChildOrderChanged (juce::ValueTree&, int, int) override { triggerAsyncUpdate(); }

    // ValueTree::removeChild notifies listeners on the removed node's
    // PARENT, never on the removed child's own listener list — so removing
    // the watched PART node itself never reaches valueTreeChildRemoved
    // above. It does fire valueTreeParentChanged on the removed node, which
    // is how this catches it.
    void valueTreeParentChanged (juce::ValueTree& tree) override
    {
        if (tree == watchedPartNode)
            triggerAsyncUpdate();
    }
    void handleAsyncUpdate() override;

    SongDocument& doc;
    PlaybackController* playback = nullptr;   // not owned; may be null (tests)

    juce::Label              sourceHeader;
    TrackListComponent       trackList;

    // Hosted by the dock host's "midiEditor" panel (constructed once, empty until a track is opened).
    std::unique_ptr<TrackEditorPanel> trackEditor;
    // Must stay declared AFTER trackList: the window's graph reads trackList's TimelineViewState,
    // so the window has to be destroyed first.
    std::unique_ptr<TempoMeterEditorWindow> tempoMeterWindow;
    std::set<juce::int64>    ghostedTrackIds;
    std::optional<int> songKey;   // see setSongKey
    bool                     followPlayheadSetting = true;
    GridSize                 defaultGridSize = GridSize::Off;

    PartStripComponent        partStrip;
    TrackChipBar              trackChipBar;

    // The mixer, created only when there is a playback controller. Declared in construction order
    // (target, the active-target holder, then the panel that reads both) so the panel goes first,
    // and all before dockHost below, which re-parents the panel and must be destroyed before it.
    std::unique_ptr<SourceMidiTarget>     mixerTarget;
    std::unique_ptr<ActivePlaybackTarget> activeTarget;
    std::unique_ptr<MixerPanelComponent>  mixerPanel;

    PianoRollComponent          previewRoll { PianoRollComponent::Role::Preview };
    PreviewAssignedPanel        previewAssignedPanel;
    std::unique_ptr<PreviewNoteSource> currentPreviewNoteSource;

    // Currently-selected preview part, and the ValueTree handles currently
    // watched for it — see juce-valuetree-conventions: addListener/
    // removeListener must always be called on THESE persistent members,
    // never on a fresh doc.findPartById()/findTrackById() temporary.
    juce::int64                 selectedPreviewPartId = -1;
    juce::ValueTree             watchedPartNode;
    std::vector<juce::ValueTree> watchedTrackNodes;

    DiagnosticListView         diagnostics;

    std::unique_ptr<TransportStrip> transportStrip;   // before lowerRegion, which lays it out
    dock::DockStack topStack;      // [upper, panels below the track list, lower]
    dock::DockStack bottomStack;   // the bottom-zone panels, inside lowerRegion
    UpperRegion   upperRegion;
    PreviewRegion previewRegion;
    LowerRegion   lowerRegion;
    // After everything it hosts: its panels re-parent the content, so it must be destroyed first.
    dock::DockHost dockHost;

    static constexpr int sourceHeaderHeight = 20;
    static constexpr int previewPanelWidth = 160;
};

} // namespace lotro
