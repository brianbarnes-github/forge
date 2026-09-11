#pragma once

#include "SongDocument.h"
#include "Core/Config.h"
#include "Core/Diagnostics.h"
#include "Core/Song.h"

#include <juce_core/juce_core.h>

#include <vector>

namespace lotro
{

// Import path: appends imported's tracks as new document state, minting
// fresh trackIds, copying Note fields verbatim. Does NOT touch
// Parts/Assignments, and does NOT touch SONG.inputMidiPath (the caller
// sets that separately).
//
// Time-base handling (only relevant once an import has already landed,
// i.e. TEMPO_MAP is not empty — that emptiness, not track count, is what
// distinguishes the first import from a later one; see appendImportedSong's
// definition of isFirstImport in the .cpp):
//   * TEMPO_MAP/METER_MAP belong to the FIRST import only. A later
//     import's tempo/meter map is never appended/concatenated. If the
//     later import's map (after rescaling its ticks below) differs from
//     what the document already holds, one Severity::Warning Diagnostic
//     is appended (source "SongModelBridge") saying the document's
//     timeline was kept; identical maps emit nothing.
//   * If `imported.ticksPerQuarter` differs from the document's PPQ
//     (fixed by the first import), every incoming note's startTick/
//     durationTicks is rescaled via
//     std::lround(tick * (double) docPpq / importedPpq) before being
//     written. pitch/velocity/isDrum/sourceTrackIndex/sourceEventIndex
//     are never touched. A lossy downscale can round a nonzero
//     durationTicks all the way to 0 — the bridge never clamps/invents a
//     duration to prevent that (a musical decision it isn't allowed to
//     make), it only reports it; DurationConstraint drops such notes
//     later in the pipeline. One Diagnostic is appended per rescaled
//     import that touched at least one note (a trackless import emits
//     none): Severity::Info if every rescaled value was exact and no
//     note was zeroed; Severity::Warning otherwise, naming the
//     rounded-value count and, if any, the zeroed-note count. Same-PPQ
//     imports are not rescaled and emit no rescale Diagnostic.
// `diagnostics`, when non-null, receives the above; pass nullptr to
// silently skip diagnostic collection (existing behavior).
void appendImportedSong (SongDocument& doc, const Song& imported, int importBatch,
                         Diagnostics* diagnostics = nullptr);

// Opens `midiFile`, runs forge_core's importMidi (sourceName = the
// file's stem, matching the CLI's ad-hoc path in Source/Main.cpp), and
// appends the result via appendImportedSong. Sets SONG.inputMidiPath
// only if it is currently empty (first import's filename wins). Never
// touches the UndoManager (bulk import is not a user-undoable edit). On
// an unopenable or malformed file (including a lotro::MidiImportError
// thrown by importMidi for content that opens fine but doesn't parse):
// appends a Severity::Error Diagnostic (source "SongModelBridge"),
// leaves the document unchanged, and returns false.
bool importMidiFile (SongDocument& doc, const juce::File& midiFile, int importBatch,
                     Diagnostics& diagnostics);

// Export path (also used later for scoped live preview). Builds a
// forge_core Config from Song-level metadata + the given parts (empty =
// all parts = full export; one or more entries = filtered subset, e.g.
// single-part preview in a later phase). Builds the Config and a
// flattened raw Song together, in lockstep, since Config.midiTrackIndex
// is positional into that raw Song.
struct BuiltConfigAndSong
{
    Config config;
    Song   rawSong;
};

BuiltConfigAndSong buildConfigAndRawSong (const SongDocument& doc,
                                          const std::vector<juce::int64>& partIds = {});

} // namespace lotro
