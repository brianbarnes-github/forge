#pragma once

#include "PianoRollNoteSource.h"
#include "SongDocument.h"

namespace lotro
{

// PianoRollNoteSource over a single MIDI_TRACK ValueTree. Queries NOTE
// children live on every call (no internal cache) — v1 tracks don't gain
// notes after import, but reading live off the tree is simpler and can't go
// stale, matching this project's ValueTree-as-source-of-truth style.
class SourceTrackNoteSource : public PianoRollNoteSource
{
public:
    explicit SourceTrackNoteSource (juce::ValueTree trackNode);

    int getNumNotes() const override;
    PianoRollNote getNote (int index) const override;

    // Empty track (no NOTE children) returns a zero-length Range at 0 for
    // both — start == end, not a garbage/default-constructed Range.
    juce::Range<int> getTickRange() const override;
    juce::Range<int> getPitchRange() const override;

private:
    // False once `track` has been removed from the document (e.g. via
    // SongDocument::removeTrack): the ValueTree object itself stays valid
    // (refcounted, and possibly still referenced by undo history) but is no
    // longer reachable from the document, so its notes are stale and must
    // not be reported.
    bool isTrackLive() const;

    juce::ValueTree track;
};

} // namespace lotro
