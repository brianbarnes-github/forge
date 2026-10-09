#pragma once

#include "MidiImportPlan.h"
#include "RawMidi.h"
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
// distinguishes the first import from a later one; see appendImport's
// definition of isFirstImport in the .cpp):
//   * TEMPO_MAP/METER_MAP belong to the FIRST import only, unless
//     options.tempo is TempoMode::replace, in which case a later import
//     replaces them (and the conductor events) wholesale. Otherwise a later
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
//     import that rescaled at least one note or one event/endTick tick
//     (a trackless import emits none; the rounded-value count includes
//     event ticks, the zeroed-note count is notes only): Severity::Info if every rescaled value was exact and no
//     note was zeroed; Severity::Warning otherwise, naming the
//     rounded-value count and, if any, the zeroed-note count. Same-PPQ
//     imports are not rescaled and emit no rescale Diagnostic.
void appendImportedSong (SongDocument& doc, const Song& imported, int importBatch,
                         Diagnostics& diagnostics);

// Raw-aware import. On a planning failure (format 2, parser disagreement)
// appends one Error diagnostic (source "SongModelBridge"), leaves the
// document unchanged and returns false. On success, appends
// `importerDiagnostics` (with trackIndex remapped to the document row --
// the MIDI_TRACK's SOURCE_MIDI child index), then the plan's diagnostics,
// then the bridge's own, and returns true. `options` default to today's
// behaviour (keep the tempo map, expand the tracks).
bool appendImportedMidi (SongDocument& doc, const Song& imported, const RawMidiFile& raw,
                         int importBatch, Diagnostics& diagnostics,
                         const Diagnostics& importerDiagnostics = {},
                         const ImportOptions& options = {});

// Opens `midiFile` (sourceName = the file's stem, matching the CLI's ad-hoc
// path in Source/Main.cpp), parses with both importMidi and readMidiBytes and
// appends via appendImportedMidi; returns false (Error diagnostic, document
// unchanged) if either parser fails or they disagree. Sets
// SONG.inputMidiPath only if it is currently empty (first import's filename
// wins). Never touches the UndoManager (bulk import is not a user-undoable
// edit). An unopenable file likewise appends a Severity::Error Diagnostic
// (source "SongModelBridge") and returns false. `options` default to today's
// behaviour (keep the tempo map, expand the tracks).
bool importMidiFile (SongDocument& doc, const juce::File& midiFile, int importBatch,
                     Diagnostics& diagnostics, const ImportOptions& options = {});

// A-R1: default-part synthesis. For every assignable MIDI_TRACK (not the conductor, not note-less) not yet referenced by
// any ASSIGNMENT on any PART, adds one PART (instrumentName =
// displayName(LotroInstrument::Drums) when the track's sourceMidiChannel ==
// 10, else displayName(LotroInstrument::LuteOfAges); empty label) with one
// ASSIGNMENT (that track, transpose 0, volume 0, "octaveShift"). Tracks
// already referenced anywhere are left alone, so this is safe to call on a
// partially-arranged document (e.g. after the user has manually assigned
// some tracks). This is a user-initiated action, so it IS undoable — the
// whole call is exactly ONE undo transaction (one doc.undo() removes every
// part/assignment it added), via SongDocument::addPart/addAssignment's
// newTransaction=false parameter.
void synthesiseDefaultParts (SongDocument& doc);

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

// Full-export helper (MainWindow::runConversion only — not the scoped
// preview path, which deliberately never calls validateConfig). Drops any
// ConfigInstrument with an empty `sources` array — a part with no assigned
// tracks, e.g. one freshly added via the part strip's "+ Add" and not yet
// dragged a track onto — since forge_core's validateConfig rejects that
// outright and would otherwise abort the whole export over one unfinished
// part. Appends one Severity::Warning Diagnostic per dropped instrument,
// naming it by label (falling back to its instrument name).
void dropUnassignedInstruments (Config& config, Diagnostics& diagnostics);

} // namespace lotro
