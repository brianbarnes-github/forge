#pragma once

#include "DiagnosticListView.h"
#include "PartStripComponent.h"
#include "PianoRollComponent.h"
#include "SongDocument.h"
#include "SourceTrackNoteSource.h"
#include "SplitterComponent.h"
#include "TrackListComponent.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>

// Phase 4, component B6 — the top-level Songsmith preview view: a vertical
// stack of (1) the MIDI-source header, (2) the track list + piano roll, (3)
// the part strip, and (4) a diagnostics list for import diagnostics.
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
namespace lotro
{

class SongsmithMainComponent : public juce::Component,
                                public juce::DragAndDropContainer
{
public:
    explicit SongsmithMainComponent (SongDocument& document);

    void paint (juce::Graphics& g) override;
    void resized() override;

    DiagnosticListView& getDiagnostics() noexcept { return diagnostics; }

private:
    // Groups the widgets shown above the splitter's drag bar so the splitter
    // can treat them as a single component for layout purposes (mirrors
    // MainWindow::Body's editor/diagnostics split — see SplitterComponent.h).
    class UpperRegion : public juce::Component
    {
    public:
        UpperRegion (juce::Label& headerIn, TrackListComponent& trackListIn, PianoRollComponent& rollIn);
        void resized() override;

    private:
        juce::Label& header;
        TrackListComponent& trackList;
        PianoRollComponent& roll;
    };

    class LowerRegion : public juce::Component
    {
    public:
        LowerRegion (PartStripComponent& partStripIn, DiagnosticListView& diagnosticsIn);
        void resized() override;

    private:
        PartStripComponent& partStrip;
        DiagnosticListView& diagnostics;
    };

    void trackSelected (juce::int64 trackId);

    SongDocument& doc;

    juce::Label              sourceHeader;
    TrackListComponent       trackList;
    PianoRollComponent       sourceRoll;
    std::unique_ptr<SourceTrackNoteSource> currentNoteSource;

    PartStripComponent        partStrip;

    DiagnosticListView         diagnostics;

    UpperRegion upperRegion;
    LowerRegion lowerRegion;
    SplitterComponent splitter;

    static constexpr int sourceHeaderHeight = 20;
    static constexpr int trackListWidth = 220;
    static constexpr int partStripHeight = 110;
};

} // namespace lotro
