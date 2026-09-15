#pragma once

#include "DiagnosticListView.h"
#include "PartStripComponent.h"
#include "PianoRollComponent.h"
#include "PreviewAssignedPanel.h"
#include "PreviewNoteSource.h"
#include "SongDocument.h"
#include "SourceTrackNoteSource.h"
#include "SplitterComponent.h"
#include "TrackListComponent.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>
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
    explicit SongsmithMainComponent (SongDocument& document);
    ~SongsmithMainComponent() override;

    void paint (juce::Graphics& g) override;
    void resized() override;

    DiagnosticListView& getDiagnostics() noexcept { return diagnostics; }

private:
    friend struct SongsmithMainComponentTestAccess;

    // Groups the widgets shown above the splitter's drag bar so the splitter
    // can treat them as a single component for layout purposes (mirrors
    // MainWindow::Body's editor/diagnostics split — see SplitterComponent.h).
    class UpperRegion : public juce::Component
    {
    public:
        UpperRegion (juce::Label& headerIn, juce::ComboBox& gridComboIn, juce::TextButton& quantizeBtnIn,
                     TrackListComponent& trackListIn, PianoRollComponent& rollIn);
        void resized() override;

    private:
        juce::Label& header;
        juce::ComboBox& gridCombo;
        juce::TextButton& quantizeBtn;
        TrackListComponent& trackList;
        PianoRollComponent& roll;
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
        LowerRegion (PartStripComponent& partStripIn, PreviewRegion& previewRegionIn,
                     DiagnosticListView& diagnosticsIn);
        void resized() override;

    private:
        PartStripComponent& partStrip;
        PreviewRegion& previewRegion;
        DiagnosticListView& diagnostics;
        SplitterComponent innerSplitter { SplitterComponent::Orientation::topBottom };
    };

    void trackSelected (juce::int64 trackId);
    void updateGridTicks();

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

    juce::Label              sourceHeader;
    TrackListComponent       trackList;
    juce::ComboBox            gridSizeCombo;
    juce::TextButton          quantizeButton { "Quantize" };
    PianoRollComponent       sourceRoll;
    std::unique_ptr<SourceTrackNoteSource> currentNoteSource;

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

    UpperRegion   upperRegion;
    PreviewRegion previewRegion;
    LowerRegion   lowerRegion;
    SplitterComponent splitter;

    static constexpr int sourceHeaderHeight = 20;
    static constexpr int trackListWidth = 220;
    static constexpr int partStripHeight = 110;
    static constexpr int previewPanelWidth = 160;
};

} // namespace lotro
