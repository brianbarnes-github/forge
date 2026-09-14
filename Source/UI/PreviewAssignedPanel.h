#pragma once

#include "PreviewNoteDiff.h"
#include "PreviewPipeline.h"
#include "SongDocument.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <vector>

// Phase 6 — the preview region's left-side info panel: assigned-track chips,
// instrument/range readout, "Octave shift" range-policy label, and output
// stats (total notes + dropped count). Subsumes the mockup's separate
// bottom "In-range / Out of range" status bar — those counts fold into this
// panel's Output stats section instead, a deliberate simplification (see
// the Phase 6 plan entry).
namespace lotro
{

class PreviewAssignedPanel : public juce::Component
{
public:
    PreviewAssignedPanel();

    // Reads the current state of `part` off `doc` plus `result`/`diff` for
    // this preview computation. Safe to call repeatedly (e.g. after every
    // preview recompute) — always overwrites prior state.
    void setPreview (SongDocument& doc, juce::int64 partId, const PreviewResult& result,
                      const std::vector<PreviewNote>& diff);

    // "No part selected" state.
    void clear();

    void paint (juce::Graphics& g) override;

private:
    struct AssignedRow
    {
        juce::String trackName;
        juce::uint32 swatch = 0;
        int transposeSemitones = 0;
    };

    bool hasSelection = false;
    std::vector<AssignedRow> assignedRows;
    juce::String instrumentName;
    juce::String rangeText;
    int totalNotes = 0;
    int droppedNotes = 0;
};

} // namespace lotro
