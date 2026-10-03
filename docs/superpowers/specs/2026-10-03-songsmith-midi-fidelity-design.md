# Songsmith MIDI fidelity: every MIDI event lives in the song — design

**Status:** Design accepted; revised after an independent spec review
(Fable 5.1, 2026-10-03); implementation pending.
**Author:** Brian Barnes, with Claude, during a 2026-10-02/03 brainstorming session.

## Summary

Today Songsmith keeps only notes, each track's first program change, and the
tempo/meter maps; every other MIDI event (controllers, pitch bend, later
program changes, aftertouch, SysEx, text/lyric/marker meta events, note-less
tracks such as the conductor track) is discarded at import. This design makes
the song's MIDI a true representation of the imported MIDI file: every event
of every track is kept in `SongDocument`, a conductor track always exists,
and a new **File → Export MIDI…** writes the song's MIDI back out
event-for-event.

The `forge_core` conversion path is untouched: `importMidi`, `Song`/`Track`,
`Config` and every constraint pass stay as they are, and ABC output stays
byte-identical.

## Context: the three-project roadmap

This is project 1 of 3, agreed in the same session. Order: 1 → 2 → 3.

1. **MIDI fidelity** (this spec).
2. **Song file.** The song becomes the governing document. File menu becomes
   New Song / Open Song / Close Song / Save Song, then Import ▸ MIDI…,
   Config…, and Export ▸ MIDI…, ABC…, Config…. Save Song persists the whole
   `SongDocument` (tracks with all MIDI events, parts, assignments, LOTRO
   settings, later mute/solo and layout). To confirm in that spec: the song
   file *contains* all MIDI data, with imported files' paths kept as
   provenance only, never re-read.
3. **Playback.** Settled so far:
   - TinySoundFont (MIT, single header) synth in our own audio callback.
   - Bundled `TimGM6mb.sf2` (GPL-2, 6 MB) as the default GM bank. A
     "SoundFont…" setting points at another bank.
   - The LOTRO bank is user-supplied and never bundled: it is 249 MB and
     its copyright field says Standing Stone Games. LOTRO playback is a
     later pass.
   - One playback engine and one song position. Transport bar (Play/Pause,
     Stop, Rewind, Go to beginning, Go to end; no record, no MIDI in) in
     the main window and in the track editor. Docking is deferred.
   - A playhead. Syncing it across the top canvas, bottom canvas and
     editor roll is deferred.
   - Mute/solo on track rows in the main screen. Solo is additive, mute
     beats solo, nothing set means everything plays. These settings govern
     the track editor too: the editor is an overlay with no solo of its
     own.
   - New code; no reuse of earlier player codebases.

Project 3 plays from the event model this spec builds, which is why it comes
after.

## Goals

- Every event of every track in an imported MIDI file is kept in the song,
  in its original order, with its original bytes.
- Every song has exactly one conductor track, pinned first in the track list.
- File → Export MIDI… writes the song's source MIDI (with the user's edits).
- A file that already has a conductor track survives import → export with
  identical events: same tracks, same absolute ticks, same order within a
  tick, same bytes. A file without one survives with its song-wide meta
  events relocated into a created conductor track (rules below).
- No change to ABC output, the CLI, or `forge_core`'s conversion types.

## Non-goals

Each is listed under Follow-ups:

- Editing controllers, bends, meta events or SysEx (only notes stay editable).
- Tempo/meter editing.
- The song file and File-menu restructure (project 2).
- Playback (project 3).

## Guiding principles inherited

- **The MIDI is the source of truth** (`CLAUDE.md`). Nothing is cleaned up,
  merged or dropped at import except what the rules below state explicitly.
  Every drop is reported as a Diagnostic.
- **Core/UI boundary** (`forge-engine-ui-boundary` skill; `CLAUDE.md`:
  "`Source/Core/` only gets what the conversion pipeline itself needs").
  Nothing in conversion uses the lossless reader/writer, so it lives in
  `Source/UI/` (decided after review; it is JUCE-free, so moving it to Core
  later is trivial if the CLI ever needs it). `forge_core` is not changed at
  all: conversion types (`Song`, `Track`, `Note`, `Config*`,
  `Constraints/*`) gain no fields. Conductor rules, document storage and
  export assembly also live in `Source/UI/`.
- **ValueTree conventions** (`juce-valuetree-conventions` skill). Synthetic
  ids are used, never positional indices. Import stays non-undoable (bulk
  path); user edits stay one undo transaction per gesture.

## Survey of the test MIDIs (`midi/*.mid`, 12 files)

Measured with a throwaway parser during brainstorming:

- **Controllers:** up to 6,383 CC events per file. Seen: 0, 1, 5, 6, 7, 10,
  11, 17, 32, 38, 64, 65, 71–78, 91, 93, 94, 98–101, 120, 121, 123.
- **Pitch bend:** up to 5,285 events (`angels.mid`).
- **SysEx:** 3–4 messages in 4 files.
- **Meta events:** lyrics (0x05, up to 509), markers (0x06), text (0x01),
  copyright (0x02), port prefix (0x21), sequencer-specific (0x7F), SMPTE
  offset (0x54), key signature (0x59). `syn5.mid` has 440 tempo events.
- **Note-less tracks:** every file has at least one; `angels`, `land` and
  `tellit` have two.
- **Note-off encoding:** some files use real note-offs, others note-on at
  velocity 0.
- **Same-key overlaps** (a key struck again before its note-off): 12 in
  `blue.mid`, 14 in `leah.mid`.
- **Not present:** every file is format 1 with one channel per track.
  Format 0 and multi-channel tracks need synthetic fixtures.

## Why not `juce::MidiFile` for the lossless path

Verified in `JUCE/modules/juce_audio_basics/midi/juce_MidiFile.cpp`:

- `MidiFile::readNextTrack` always calls `reorderNoteOnsAfterNoteOffs` on each
  tick group, so within-tick order is not preserved.
- `updateMatchedPairs` (when `createMatchingNoteOffs` is true, which
  `importMidi` uses) inserts a synthetic note-off when a key is re-struck
  before its note-off.

Precisely (`juce_MidiFile.cpp` `reorderNoteOnsAfterNoteOffs`): per tick
group it finds the *first* note-on, finds the *last* note-off of the same
channel and key later in the group, swaps the two, and continues after the
first note-on's slot; it stops for that group as soon as a first note-on has
no matching off. This can move a note-on past other note-ons, so the order
of note-ons is *not* preserved. `updateMatchedPairs` pairs each note-on with
the next same-channel/key note-off, or, if a same-channel/key note-on comes
first, *inserts* a synthetic note-off into the list at that point.

Both are reasonable for playback and wrong for an exact copy. So the project
gets its own small SMF reader/writer. `importMidi` keeps using JUCE
unchanged, so conversion can't move.

## Design

### `Source/UI/RawMidi.{h,cpp}`

JUCE-free public surface and JUCE-free implementation.

```cpp
namespace lotro
{
struct RawMidiEvent
{
    int                       tick = 0;   // absolute
    std::vector<std::uint8_t> bytes;      // channel msg: status + data (status always explicit)
                                          // meta:  FF type <data>   (length not stored)
                                          // sysex: F0 <data> / F7 <data> (length not stored)
};

struct RawMidiTrack
{
    std::vector<RawMidiEvent> events;     // file order; End-of-Track excluded
    int                       endTick = 0; // tick of End-of-Track (keeps trailing silence)
};

struct RawMidiFile
{
    int                       format          = 1;
    int                       ticksPerQuarter = 480;
    std::vector<RawMidiTrack> tracks;
};

RawMidiFile readMidiFile  (std::istream& input, std::string_view sourceName); // throws MidiImportError
void        writeMidiFile (const RawMidiFile& file, std::ostream& output);
}
```

**Reader** (track structure matches `juce::MidiFile::readFrom`, so track
indices line up with `importMidi`'s `sourceTrackIndex`)
- Resolves running status: stored bytes always carry an explicit status.
  Meta and SysEx events do *not* cancel running status (JUCE's `readTrack`
  behaviour; the SMF spec says they should, but matching JUCE matters more
  here).
- Keeps note-on at velocity 0 as written.
- Keeps F0 and F7 SysEx packets, and every meta type, including unknown ones.
- Reads exactly the header's track count of chunks. A non-`MTrk` chunk
  consumes one of those slots and is skipped, as in JUCE. A track's index is
  the ordinal of its `MTrk` chunk.
- `endTick` is the tick of the track's first End-of-Track; a track without
  one gets its last event's tick. Events after an End-of-Track are kept in
  `events` in file order (JUCE keeps reading them too). Such malformed files
  are not guaranteed to round-trip exactly, because export writes one
  End-of-Track at the end.
- SMPTE time division is rejected, as `importMidi` does today.
- Malformed input throws `MidiImportError`: bad/short `MThd`, a chunk
  running past end of file, an unterminated variable-length number, a data
  byte with no running status, or truncated event data. JUCE is more
  tolerant of some of these (it silently stops reading the track); see
  Import step 1 for what happens when the two parsers disagree.

**Writer**
- Writes `MThd` (format, track count, PPQ) and one `MTrk` per track.
- Writes events in stored order with delta times, then End-of-Track at
  `max(endTick, last event tick)`.
- Writes every status byte explicitly (no running-status compression).
  Event-level equality is the guarantee, not byte equality of the file.
- Writes meta and SysEx lengths as variable-length numbers.
- Checks the output stream's state after writing and throws
  `MidiExportError` (declared in `RawMidi.h`; the reader reuses Core's `MidiImportError`) on
  failure.

### Document storage (`SongDocument` / `SongIDs`)

```
SONG
└─ SOURCE_MIDI  (ticksPerQuarter)
   ├─ MIDI_TRACK  isConductor=true   ← always child 0, exactly one
   │  ├─ NOTES   (always empty for the conductor)
   │  └─ EVENTS
   └─ MIDI_TRACK  (trackId, name, colorArgb, sourceMidiChannel, sourceProgram,
      │            importBatch, + sourceTrackIndex, endTick, defaultChannel,
      │            isConductor=false)
      ├─ NOTES
      │  └─ NOTE  (pitch, startTick, durationTicks, velocity, isDrum,
      │            sourceTrackIndex, sourceEventIndex,
      │            + channel, offVelocity, offIsNoteOnZero,
      │              onOrder?, offOrder?, offSynthesized)
      └─ EVENTS
         └─ EVENT (tick, order, relocatedFrom?,
                   data: juce::MemoryBlock of RawMidiEvent::bytes)
```

- **New identifiers:** `NOTES`, `EVENTS`, `EVENT`, `data`, `order`,
  `channel`, `offVelocity`, `offIsNoteOnZero`, `onOrder`, `offOrder`,
  `offSynthesized`, `isConductor`, `endTick`, `defaultChannel`,
  `relocatedFrom` (conductor `EVENT`s moved from another track). The existing
  `sourceTrackIndex` identifier is reused on `MIDI_TRACK`.
- **`defaultChannel`** (1–16): the channel of the track's first channel
  event at import, else 1. Used only for notes created in the editor.
  `sourceMidiChannel` is left exactly as today (`importMidi` sets it only
  for drum tracks), so nothing that reads it changes.
- **`order`, `onOrder`, `offOrder`:** the event's index within its *raw*
  source track. `onOrder`/`offOrder` are absent on notes created in the
  editor, and on notes whose timing was edited (see Editing).
- **`channel`:** 1–16, from the raw note-on. Format-0 and multi-channel
  tracks need this.
- **`SongDocument` helpers:** `getNotesNode(track)`, `getEventsNode(track)`,
  `getConductorTrack()`, `isAssignableTrack(track)` (not the conductor and
  has at least one `NOTE`), `getNumAssignableTracks()`, and an undoable
  `removeProperty(tree, id, newTransaction = true)` mirroring `setProperty`.
  `SongDocument()` creates the conductor `MIDI_TRACK` (with empty
  `NOTES`/`EVENTS`) at construction, so it exists before any import. It gets
  a normal minted `trackId` and `importBatch = 0`. `removeTrack` refuses it
  (no-op, returns without opening a transaction). `addTrack`/`addTrackBulk`
  create both containers.
- **Call sites migrated** from `track.getChild(i)`/`getNumChildren()`-as-notes
  to `getNotesNode(track)`: `SourceTrackNoteSource`, `SourceRollEditor`,
  `TrackRowComponent`, `TrackNotePreview`, `TrackListComponent`,
  `PianoRollComponent` (ghost tracks, and the `getTrackNode().getChild(i)`
  that ties note-source index to track child index), and `SongModelBridge`
  (including the LCM-raise loop), plus any found by a full audit
  (`grep getChild` in `Source/UI` and `Tests`). Three sites already filter
  `hasType(NOTE)`; they move to `getNotesNode` too, so there is one way to
  reach notes.
- **Track-index-based queries** (`getNumTracks`, `getTrack(i)`) keep
  counting every `MIDI_TRACK`, conductor included. Callers that mean
  "assignable tracks" use `isAssignableTrack`/`getNumAssignableTracks`.
  Known production callers to update:
  - `TrackListComponent` empty-state message (`getNumTracks() == 0` can
    never be true again): use "no non-conductor tracks".
  - Track colour assignment in `SongModelBridge` (family counting): skip the
    conductor and note-less tracks so existing shades don't shift.
  - Track numbering: the conductor row is unnumbered; other rows are
    numbered 1… skipping it (`TrackListComponent` row labels,
    `AssignmentChipComponent` `Tk<n>`).
  - `DiagnosticListView` shows `importMidi`'s `trackIndex` (index after
    empty-track filtering), which no longer matches row numbers once
    note-less tracks are rows: the bridge remaps it to the document row
    number at import.
- **`TEMPO_MAP`/`METER_MAP` are unchanged**: derived at first import from
  *every* track (as `importMidi` does), still used by conversion and the
  timeline. The raw tempo/time-signature events live in the conductor's
  `EVENTS` when they came from the conductor, and in other tracks'
  `EVENTS` when they came from there (e.g. `right.mid` has a 0x58 in
  track 1; `Pull The Wires` has a 0x59 in all ten note tracks). This
  duplication is accepted because nothing edits tempo yet (see
  Follow-ups).
- **Scale:** the largest fixture (`land.mid`) produces roughly 9.9k `NOTE`
  and 6.5k `EVENT` nodes. That is the same order of magnitude as today's
  `NOTE` count.

### Conductor rules (pure function in `Source/UI/`)

- **Song-wide meta events:** tempo (0x51), time signature (0x58), key
  signature (0x59), SMPTE offset (0x54), marker (0x06), copyright (0x02).
- **"Has a conductor track":** the file is format 1 and its first track
  contains no note-on with velocity > 0.
- **First import, file has a conductor:** that raw track's events (all of
  them, including its SysEx, text and sequence name) go into the song's
  conductor `EVENTS` unchanged. Its `endTick` becomes the conductor's
  `endTick`. The track does not also become a regular `MIDI_TRACK`.
- **First import, file has no conductor** (format 0, or format 1 with notes
  in its first track): every song-wide meta event, from any track, moves
  into the song's conductor at its original tick, keeping its original
  `order` plus a `relocatedFrom` source-track index. Conductor sort key for
  relocated events: (tick, relocatedFrom, order).
  Everything else stays in its own track: SysEx, lyrics, text, track names
  and channel events. One Info diagnostic reports the number of relocated
  events.
- **Format 2:** rejected: one Error diagnostic, document unchanged. No
  fixture or user has one; supporting it can come later.
- **Later imports (rule A, "first import owns the timeline"):**
  - If the file has a conductor, that whole track is dropped.
  - Otherwise its song-wide meta events are dropped from every track.
  - Everything else stays with its track, rescaled per the PPQ rules.
  - One Info diagnostic gives the count of dropped events.
  - The existing tempo/meter-mismatch Warning is unchanged.
- **The conductor is never part of conversion or arrangement:**
  - It can't be dragged onto a part, renamed or deleted.
  - `synthesiseDefaultParts` and `assignTrackToPart` skip it (and skip
    note-less tracks).
  - `buildConfigAndRawSong` leaves out the conductor and every track with
    zero `NOTE`s that has no `ASSIGNMENT`. (A track the user emptied but
    kept assigned still goes in, as today.) `trackIdToIndex` is assigned
    from `rawSong.tracks.size()` at push time, not from the document child
    index, so `midiTrackIndex` stays correct. This keeps both halves of the
    `SongsmithRoundTrip_tests` pin: ABC bytes, and the diagnostic count
    (`InstrumentAssembly`'s `emitUnreferencedDiags` would otherwise emit an
    Info per extra note-less track).

### Import (`SongModelBridge::importMidiFile`)

`importMidiFile` gains the raw path. `appendImportedSong(doc, Song, …)`
stays as the raw-less entry point (used by tests with synthetic `Song`s):
it creates `NOTE`s only, no `EVENT`s, and leaves the conductor untouched.
New notes it creates have no orders, so they export as new material. A new
`appendImportedMidi(doc, const Song&, const RawMidiFile&, importBatch,
diagnostics)` is the raw-aware path; `importMidiFile` calls it.

1. Read the file once into memory. Parse it with `importMidi` (notes and
   diagnostics exactly as today) and with `readMidiFile` (raw). If either
   parser fails, or they disagree on the track count, append one Error
   diagnostic (source "SongModelBridge") and leave the document unchanged.
   The same applies to format 2.
2. Apply the conductor rules.
3. Every remaining raw track becomes a `MIDI_TRACK` (`sourceTrackIndex` = its
   raw index), including tracks with no notes. For a track `importMidi`
   produced, name, channel, program and notes come from it as today. For a
   note-less track:
   - name from its *last* 0x03 meta, else "Track N", matching `importMidi`
     (which overwrites the name on each 0x03)
   - `sourceMidiChannel` from its first channel event (0 if none)
   - `sourceProgram` from its first program change (0 if none)

   Every imported track also gets `defaultChannel` from its first channel
   event (else 1).
4. **Linking notes to raw events, via an exact replica of JUCE's note
   view** (`Source/UI/JuceNoteReplica.{h,cpp}`, a pure function over one
   `RawMidiTrack`):
   - Copy the raw events, each tagged with its raw index.
   - `std::stable_sort` by tick (a no-op for valid files, kept for parity).
   - Per tick group, run the exact `reorderNoteOnsAfterNoteOffs` loop
     described above (first note-on, last matching note-off in the group,
     swap, continue after the first note-on's slot, stop the group when a
     first note-on has no match).
   - Run the exact `updateMatchedPairs` loop, including list insertion of a
     synthetic note-off (tagged "no raw index") before a re-striking
     note-on.
   - Use JUCE's predicates: `isNoteOn()` excludes velocity 0; `isNoteOff()`
     includes note-on at velocity 0; matching is by channel and note number.
   - Result: in the replica's sequence, the k-th `isNoteOn()` event is the
     note-on of the note whose `sourceEventIndex == k` (this is exactly how
     `importMidi` counts). Its paired off is either a raw event (its index
     becomes `offOrder`, with `offVelocity` and `offIsNoteOnZero`) or
     synthetic (`offSynthesized = true`). The note-on's raw index becomes
     `onOrder`; its raw bytes give `channel`.
5. **Cross-check** every linked `NOTE` against the replica in source-PPQ
   ticks, *before* any rescale: pitch, channel, velocity, `startTick` and
   `durationTicks` must all match `importMidi`'s note. On a mismatch,
   `jassertfalse` plus a Warning diagnostic (this would be a bug in the
   replica).
6. **Leftovers:** every raw event not claimed by a `NOTE` (on or off)
   becomes an `EVENT` with its `order`. That includes note-ons `importMidi`
   skipped (unmatched, zero-length), the offs the replica paired with those
   skipped notes, and note-offs nobody claimed, so nothing is lost.
7. **PPQ:** the existing rescale (`std::lround`) and LCM-raise rules apply
   to `EVENT.tick` and `MIDI_TRACK.endTick` as well as note ticks, for both
   incoming and (on LCM raise) existing tracks, including the conductor.
   The rescale diagnostics count these values too. Round-trip exactness is
   guaranteed only for a single import (or same-PPQ imports): after a
   rescale, `lround(start) + lround(duration)` can differ from
   `lround(offTick)` by one tick.
8. Import remains non-undoable (bulk path), as today.

### Export (`Source/UI/MidiExport.{h,cpp}`)

`RawMidiFile buildRawMidiFile (const SongDocument&)` — a pure function.

- Format 1; PPQ = `SOURCE_MIDI.ticksPerQuarter`.
- Track 0 is the conductor; then every other `MIDI_TRACK` in document order.
- **Per track, emitted items:** each `EVENT`; each `NOTE`'s note-on; each
  `NOTE`'s note-off unless `offSynthesized`.
  - The note-on is built as `0x90|ch-1, pitch, velocity`.
  - The note-off is built as `0x80|ch-1, pitch, offVelocity` (default 64),
    or `0x90|ch-1, pitch, 0` when `offIsNoteOnZero`.
  - The note-off tick is `startTick + durationTicks`.
- **Sort:** stable, by tick, then by group:
  1. note-offs of notes without `offOrder`
  2. everything with an original order (`order`/`onOrder`/`offOrder`),
     ascending by (`relocatedFrom` if present, else -1; then that order)
  3. note-ons of notes without `onOrder`

  This reproduces the original stream exactly for unedited imports, and
  puts new material at safe positions within a tick.
- **End of track:** `max(endTick, last emitted tick)`.
- Exports the source MIDI only. LOTRO assignments, transpose and volume
  never touch it.

**File → Export MIDI…** (added to the current File menu; project 2 moves it
under Export ▸):
- Enabled when the song has at least one non-conductor track.
- Uses the `FileChooser` save dialog, defaulting to the
  `SONG.inputMidiPath` stem + `.mid`.
- Writes via `writeMidiFile`. Failures (unwritable path, stream error)
  surface as `MidiExportError`, shown with
  `NativeMessageBox::showMessageBoxAsync`, matching `MainWindow`'s existing
  error dialogs.

### Editing interactions

- **New notes** (`SourceRollEditor` create gesture):
  - `channel` = the track's `defaultChannel` (1 for a track with none)
  - `offVelocity` = 64, `offIsNoteOnZero` = false
  - no `onOrder`/`offOrder`, `offSynthesized` = false
- **Move/resize/quantize of a note's timing**: when `startTick` or
  `durationTicks` actually changes, remove `onOrder`/`offOrder` (via
  `SongDocument::removeProperty`) and clear `offSynthesized`, in the same
  undo transaction as the edit. The note then sorts as new material and
  gets a real note-off. `offVelocity` and `offIsNoteOnZero` are kept. A drag
  that changes only pitch (`SourceRollEditor` sets `startTick` and `pitch`
  in one transaction) keeps the orders, as do velocity edits.
- **Deleting a note** removes its `NOTE` only. `EVENT`s are untouched.
- `EVENT`s are not editable in this project.

### Track list (`TrackListComponent` / `TrackRowComponent`)

- **Conductor row:** pinned first, muted style, label "Conductor",
  second line "N events". Not draggable; double-click does nothing (no
  event editor yet).
- **Note-less non-conductor rows:** second line "0 notes · N events". Not
  draggable onto parts (`assignTrackToPart` also refuses them);
  double-click does nothing until an event editor exists.
- Note counts and pitch ranges read from `NOTES`.

## Testing

Strict TDD. Integration tests are preferred for business logic.

- **`RawMidi_tests.cpp`:**
  - read → write → read gives equal `RawMidiFile`s for all 12 `midi/*.mid`.
  - Hand-built byte fixtures: running status; note-on velocity 0; F0 and F7
    SysEx; unknown meta type; missing End-of-Track; trailing silence
    (`endTick` > last event); format 0 with several channels;
    non-`MTrk` chunk skipped.
  - Malformed inputs throw `MidiImportError`: short header, chunk past end
    of file, unterminated variable-length number, orphan data byte,
    truncated event, SMPTE division.
- **`JuceNoteReplica_tests.cpp` (differential):**
  - Seeded random tracks (same-tick on/off mixes in every order, re-struck
    keys, velocity-0 note-ons, several channels, unmatched on/offs) are
    written to SMF bytes and read both by `juce::MidiFile::readFrom(…, true)`
    and by `readMidiFile` + the replica. Every note-on matches: ordinal,
    pitch, channel, velocity, start, matched-off tick, synthetic-or-not.
  - Hand-built cases from the review: tick 0 `[on60, on62, off60]` with
    later `off62@10, off60@20` (JUCE moves on62 ahead of on60); tick 0
    `[on60 v100, off60, on60 v50, off60]` with later `off60@10` (JUCE puts
    the v50 on first).
  - Every fixture in `midi/*.mid` agrees too.
- **`MidiFidelity_tests.cpp` (import → export integration):**
  - For each fixture: `importMidiFile` into a fresh `SongDocument`, then
    `buildRawMidiFile`, then compare with `readMidiFile` of the original.
    Files with a conductor are equal; the others are equal after applying
    the relocation rule, and the test asserts that rule.
  - `blue.mid` and `leah.mid` exercise `offSynthesized`, and their export
    reproduces the original exactly.
  - The two hand-built review cases above import and export exactly.
  - The cross-check never fires across all fixtures.
- **Conductor rules:**
  - Existing conductor kept verbatim.
  - Created for format 0 and for format 1 with notes in track 0.
  - Format 2 and parser disagreement produce an Error diagnostic and leave
    the document unchanged.
  - Later import drops song-wide events, with the Info diagnostic count.
  - Fresh `SongDocument` has an empty conductor; `removeTrack` refuses it.
  - The conductor is skipped by `synthesiseDefaultParts`,
    `assignTrackToPart` and `buildConfigAndRawSong`; note-less tracks are
    skipped by `synthesiseDefaultParts` and `assignTrackToPart`.
- **PPQ:** a mixed-PPQ second import rescales `EVENT` ticks and `endTick`,
  including on LCM raise of existing tracks.
- **Editing:**
  - A new note exports with a real note-off at the right position.
  - A timing edit on an `offSynthesized` note clears the flag and its
    orders in one undo step, and undo restores them.
  - Deleting a note leaves `EVENT`s intact.
- **UI:** conductor and note-less rows refuse drags; conductor row label and
  event count.
- **Pins:** `SongsmithRoundTrip_tests` (CLI vs Songsmith ABC
  byte-identical, and equal post-import diagnostic counts) keeps passing.
- **Existing tests that must change** (not just the mechanical `NOTES`
  migration), because the conductor exists from construction and
  note-less tracks are now kept:
  - `SongDocument_tests.cpp`: `getNumTracks() == 0` assertions.
  - `SongModelBridge_tests.cpp`: track counts, and colour-by-index asserts
    (`colourOf(0)` is now the conductor).
  - `SongsmithRoundTrip_tests.cpp`: track counts, and the positional
    alignment `doc.getTrack(tracksAfterFirst + t)` ↔ `directSong.tracks[t]`
    (note-less tracks now interleave; `angels`, `land` and `tellit` have
    one at raw index 1). Align by assignable tracks instead.
  - `TrackListComponent_tests.cpp`: row counts.

  Each change keeps the test's original intent; the plan lists them
  explicitly.

## Docs to update

- `docs/ARCHITECTURE.md`: RawMidi in the GUI/Songsmith section, document schema, the
  import/export walkthrough. `MidiImporter` is no longer the only MIDI
  reader; update that sentence.
- `docs/UI_GUIDE.md`: Export MIDI menu item, conductor row.
- `docs/TESTING.md`: count and new files.
- `docs/DELTAS_FROM_SPEC.md`, if the original spec says anything about
  dropping non-note events.

## Follow-ups (out of scope)

- **Song file and File-menu restructure** (project 2). It must serialise
  `EVENT.data` (MemoryBlock → base64 in XML is JUCE's default).
- **Playback** (project 3).
- **Event/controller editor**, including opening the conductor track.
- **Tempo/meter editing:** must make tempo/time-signature `EVENT`s
  authoritative and rebuild `TEMPO_MAP`/`METER_MAP` from them, removing
  today's duplication. Note that today's maps are built from *every* track,
  so "rebuild from the conductor only" would change behaviour for files
  with tempo/meter events outside the conductor.
- **Master-track automation** in the conductor: intro/outro volume fades,
  song-wide volume or tempo adjustments over regions.
- **"Mix timelines" import preference:** default stays "first import keeps
  tempo, meter, etc."; the option would merge later imports' song-wide
  events.
- **Export of the LOTRO/ABC view as MIDI.**
- **Format 2 import support.**
- **Licensing guardrail:** add the bundled `TimGM6mb.sf2` (GPL-2) to
  `CLAUDE.md`'s licensing section when project 3 bundles it.
