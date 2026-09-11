#pragma once

#include "DiagnosticsPane.h"
#include "PartStripComponent.h"
#include "SongDocument.h"
#include "TrackListComponent.h"

#include <juce_gui_basics/juce_gui_basics.h>

// Phase 4, component B6 — the top-level Songsmith preview view: a vertical
// stack of (1) the MIDI-source header, (2) the track list + a Phase-5
// piano-roll placeholder, (3) the part strip, and (4) a DiagnosticsPane for
// import diagnostics. Owns its own DiagnosticsPane instance (rather than
// sharing MainWindow::Body's classic-mode one) so toggling modes never has
// to reparent a Component between two different owners.
namespace lotro
{

class SongsmithMainComponent : public juce::Component,
                                public juce::DragAndDropContainer
{
public:
    explicit SongsmithMainComponent (SongDocument& document);

    void paint (juce::Graphics& g) override;
    void resized() override;

    DiagnosticsPane& getDiagnostics() noexcept { return diagnostics; }

private:
    SongDocument& doc;

    juce::Label             sourceHeader;
    TrackListComponent       trackList;
    juce::Label              sourcePlaceholder;

    PartStripComponent        partStrip;

    DiagnosticsPane            diagnostics;

    static constexpr int sourceHeaderHeight = 20;
    static constexpr int trackListWidth = 220;
    static constexpr int partStripHeight = 110;
};

} // namespace lotro
