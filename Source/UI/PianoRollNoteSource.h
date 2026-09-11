#pragma once

#include <juce_core/juce_core.h>

// Abstract note-source interface so PianoRollGeometry/PianoRollComponent
// never depend on ValueTree directly. Phase 6 adds a second implementation
// (PreviewNoteSource, over PreviewResult) — this seam is why the interface
// exists rather than PianoRollComponent reading a ValueTree straight.
namespace lotro
{

struct PianoRollNote
{
    int pitch = 0;
    int startTick = 0;
    int durationTicks = 0;
    juce::uint32 colourArgb = 0;
};

class PianoRollNoteSource
{
public:
    virtual ~PianoRollNoteSource() = default;

    virtual int getNumNotes() const = 0;
    virtual PianoRollNote getNote (int index) const = 0;

    // Tick/pitch extent of every note in this source, used to drive
    // PianoRollGeometry::fitToContent. Pitch range's upper bound is
    // exclusive (highest pitch + 1), matching tick range's upper bound
    // (last note's startTick + durationTicks) — both describe a half-open
    // [start, end) span. An empty source (no notes) returns a Range with
    // start == end at each source's discretion.
    virtual juce::Range<int> getTickRange() const = 0;
    virtual juce::Range<int> getPitchRange() const = 0;
};

} // namespace lotro
