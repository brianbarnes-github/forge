#pragma once

#include "DiagnosticListView.h"
#include "PartStripComponent.h"
#include "SongDocument.h"
#include "TrackListComponent.h"

#include <juce_gui_basics/juce_gui_basics.h>

// Phase 4, component B6 — the top-level Songsmith preview view: a vertical
// stack of (1) the MIDI-source header, (2) the track list + a Phase-5
// piano-roll placeholder, (3) the part strip, and (4) a diagnostics list for
// import diagnostics.
//
// This hosts a bare DiagnosticListView, NOT the full DiagnosticsPane — the
// ABC-preview half of DiagnosticsPane has nothing to show in Songsmith mode
// (there is no Run/export path here until Phase 6), and driving it with an
// empty string produced a permanently-visible "0 bytes · 0 bars · 0 parts"
// status line that reads as a failed import (see I2 in the Phase 4
// whole-branch review). Phase 6 is expected to bring the ABC preview back as
// a toggleable panel once Songsmith has something to run.
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
    juce::Label             sourceHeader;
    TrackListComponent       trackList;
    juce::Label              sourcePlaceholder;

    PartStripComponent        partStrip;

    DiagnosticListView         diagnostics;

    static constexpr int sourceHeaderHeight = 20;
    static constexpr int trackListWidth = 220;
    static constexpr int partStripHeight = 110;
};

} // namespace lotro
