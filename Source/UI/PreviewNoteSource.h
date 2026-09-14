#pragma once

#include "PianoRollNoteSource.h"
#include "PreviewNoteDiff.h"

#include <vector>

// Phase 6 — the third PianoRollNoteSource implementation, wrapping the
// output of diffPreviewNotes for the preview roll. See the Songsmith plan's
// "Ghost-note / out-of-range / dropped-note diff" section: the roll always
// positions a note's rectangle at its PRE-fold pitch (prePitch); the ghost
// destination (postPitch) is drawn separately by PianoRollComponent's
// preview-only paint path, not by repositioning this rect.
namespace lotro
{

class PreviewNoteSource : public PianoRollNoteSource
{
public:
    explicit PreviewNoteSource (std::vector<PreviewNote> notesIn);

    int getNumNotes() const override;
    PianoRollNote getNote (int index) const override;

    // Empty source returns a zero-length Range at 0 for both, matching
    // SourceTrackNoteSource's convention.
    juce::Range<int> getTickRange() const override;
    juce::Range<int> getPitchRange() const override;

private:
    std::vector<PreviewNote> notes;
};

} // namespace lotro
