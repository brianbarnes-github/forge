#pragma once

#include "PianoRollNoteSource.h" // for NoteState — do not redefine the enum here
#include "PreviewPipeline.h"

#include <optional>
#include <vector>

namespace lotro
{

// One note's pre/post-pipeline identity for the preview roll's ghost/
// dropped-note overlays — see the Songsmith plan's "Ghost-note /
// out-of-range / dropped-note diff" section.
struct PreviewNote
{
    int sourceTrackIndex = -1;
    int sourceEventIndex = -1;
    int prePitch = 0;
    std::optional<int> postPitch;
    int startTick = 0;
    int durationTicks = 0;
    int velocity = 0;
    NoteState state = NoteState::Normal;
};

// Joins every note in `result.assembled` against `result.pipelined` by
// (sourceTrackIndex, sourceEventIndex). Known v1 limitation: this only
// diffs pre-assembly vs. post-full-pipeline, not pass-by-pass, so a note
// that folds and is later dropped shows as Dropped, not as a fold-ghost.
std::vector<PreviewNote> diffPreviewNotes (const PreviewResult& result);

} // namespace lotro
