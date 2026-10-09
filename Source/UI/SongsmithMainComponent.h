#pragma once

#include "DiagnosticListView.h"
#include "GridSize.h"
#include "PartStripComponent.h"
#include "PianoRollComponent.h"
#include "Playback/PlaybackController.h"
#include "PreviewAssignedPanel.h"
#include "PreviewNoteSource.h"
#include "SongDocument.h"
#include "SplitterComponent.h"
#include "TrackEditorWindow.h"
#include "TrackListComponent.h"

#include "Playback/TransportStrip.h"

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
// + piano roll) and the lower regions (part strip + diagnostics) is a
// user-resizable SplitterComponent, replacing the earlier fixed
// 40%-of-height split.
//
// Phase 6: the lower region gains a second splitter, between the new
// preview region and the diagnostics list — see LowerRegion below.
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
    void setDiagnosticsVisible (bool shouldShow) { lowerRegion.setDiagnosticsVisible (shouldShow); }
    bool isDiagnosticsVisible() const noexcept { return lowerRegion.isDiagnosticsVisible(); }

    // Called by MainWindow right after a MIDI import completes, so the
    // per-track note previews default to showing the whole song instead of
    // TimelineViewState's very-zoomed-in initial pixelsPerTick.
    void fitTrackTimelineToDocument() { trackList.fitTimelineToDocument(); }

    // Called by MainWindow after the document's contents were replaced
    // (New / Open). Resets everything that lives outside the tree:
    // closes the track editor (its note source holds the OLD MIDI_TRACK
    // handle), clears ghost / track / part selection and the preview (ids
    // persist per Song, so a stale id could re-select a different Song's
    // part), and refits the timeline. Must not write to the tree.
    void documentReplaced();

    // Track-canvas keys, forwarded by MainWindow::keyPressed. Each returns / does
    // nothing when there is nothing to act on, so the caller can leave the key
    // unconsumed.
    bool splitSections()   { return trackList.splitAtPointer(); }
    bool deleteSections()  { return trackList.deleteSelectedSections(); }
    void selectAll()       { trackList.selectAll(); }
    bool cancelSectionDrag() { return trackList.cancelSectionDrag(); }
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

    // Groups the widgets shown above the splitter's drag bar so the splitter
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
        PreviewRegion (juce::Label& headerIn, PreviewAssignedPanel& panelIn, PianoRollComponent& rollIn);
        void resized() override;

    private:
        juce::Label& header;
        PreviewAssignedPanel& panel;
        PianoRollComponent& roll;
    };

    class LowerRegion : public juce::Component
    {
    public:
        // `transportIn` (null when there is no playback) is the bar across the
        // top of the region; it rides along when the splitter above is dragged.
        LowerRegion (juce::Component* transportIn, PartStripComponent& partStripIn,
                     PreviewRegion& previewRegionIn, DiagnosticListView& diagnosticsIn);
        void resized() override;

        void setDiagnosticsVisible (bool shouldShow);
        bool isDiagnosticsVisible() const noexcept { return diagnostics.isVisible(); }

    private:
        juce::Component* transport;
        PartStripComponent& partStrip;
        PreviewRegion& previewRegion;
        DiagnosticListView& diagnostics;
        SplitterComponent innerSplitter { SplitterComponent::Orientation::topBottom };
    };

public:
    void trackDoubleClicked (juce::int64 trackId);
    void trackGhostToggled (juce::int64 trackId, bool visible);

    bool isTrackEditorOpen() const noexcept { return trackEditorWindow != nullptr; }
    void setActiveEditorGridSize (GridSize size);
    void quantizeActiveEditor() { if (trackEditorWindow != nullptr) trackEditorWindow->quantizeSelection(); }

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
    // same rationale as TrackListComponent/PartStripComponent.
    void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override { triggerAsyncUpdate(); }
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

    std::unique_ptr<TrackEditorWindow> trackEditorWindow;
    std::set<juce::int64>    ghostedTrackIds;

    PartStripComponent        partStrip;

    juce::Label                previewHeader;
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
    UpperRegion   upperRegion;
    PreviewRegion previewRegion;
    LowerRegion   lowerRegion;
    SplitterComponent splitter;

    static constexpr int sourceHeaderHeight = 20;
    static constexpr int partStripHeight = 110;
    static constexpr int previewPanelWidth = 160;
};

} // namespace lotro
