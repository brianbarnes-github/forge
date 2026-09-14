#pragma once

#include <juce_core/juce_core.h>

#include <optional>

// Abstract note-source interface so PianoRollGeometry/PianoRollComponent
// never depend on ValueTree directly. Phase 6 adds a second implementation
// (PreviewNoteSource, over PreviewResult) — this seam is why the interface
// exists rather than PianoRollComponent reading a ValueTree straight.
namespace lotro
{

// General note-source concept (not preview-specific), so PreviewNoteDiff.h
// can reuse it via this header instead of redefining it.
enum class NoteState
{
    Normal,
    WillFold,
    Dropped
};

struct PianoRollNote
{
    int pitch = 0;
    int startTick = 0;
    int durationTicks = 0;
    juce::uint32 colourArgb = 0;

    // Provenance — mirrors lotro::Note's field names exactly; the join key
    // for PreviewNoteDiff and for mapping a clicked rectangle back to a
    // NOTE node. -1 means "unknown" (matches lotro::Note's sentinel).
    int sourceTrackIndex = -1;
    int sourceEventIndex = -1;

    // Preview-only state. A source-role note (SourceTrackNoteSource) never
    // folds/drops, so these stay at their defaults there. postPitch is only
    // meaningful when state == WillFold (the post-range-fold destination
    // pitch, for ghost-note rendering).
    NoteState state = NoteState::Normal;
    std::optional<int> postPitch;
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
