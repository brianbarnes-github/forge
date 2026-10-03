# Songsmith MIDI fidelity: every MIDI event lives in the song — design

**Status:** Design accepted; implementation pending.
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
- **Core/UI boundary** (`forge-engine-ui-boundary` skill). The lossless
  reader/writer is import/export fidelity, not editing, and Core is already
  the only place that reads MIDI files, so it goes in Core. Conversion types
  (`Song`, `Track`, `Note`, `Config*`, `Constraints/*`) gain no fields.
  Conductor rules, document storage and export assembly live in `Source/UI/`.
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

Both are reasonable for playback and wrong for an exact copy. So Core gets
its own small SMF reader/writer. `importMidi` keeps using JUCE unchanged, so
conversion can't move.

## Design

### Core: `Source/Core/RawMidi.{h,cpp}`

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

**Reader**
- Resolves running status: stored bytes always carry an explicit status.
- Keeps note-on at velocity 0 as written.
- Keeps F0 and F7 SysEx packets, and every meta type, including unknown ones.
- A track without End-of-Track gets `endTick` = its last event's tick.
- SMPTE time division is rejected, as `importMidi` does today.
- Malformed input throws `MidiImportError`: bad/short `MThd`, a chunk
  running past end of file, an unterminated variable-length number, a data
  byte with no running status, or truncated event data.
- Non-`MTrk` chunks are skipped.

**Writer**
- Writes `MThd` (format, track count, PPQ) and one `MTrk` per track.
- Writes events in stored order with delta times, then End-of-Track at
  `max(endTick, last event tick)`.
- Writes every status byte explicitly (no running-status compression).
  Event-level equality is the guarantee, not byte equality of the file.
- Writes meta and SysEx lengths as variable-length numbers.

### Document storage (`SongDocument` / `SongIDs`)

```
SONG
└─ SOURCE_MIDI  (ticksPerQuarter)
   ├─ MIDI_TRACK  isConductor=true   ← always child 0, exactly one
   │  ├─ NOTES   (always empty for the conductor)
   │  └─ EVENTS
   └─ MIDI_TRACK  (trackId, name, colorArgb, sourceMidiChannel, sourceProgram,
      │            importBatch, + sourceTrackIndex, endTick, isConductor=false)
      ├─ NOTES
      │  └─ NOTE  (pitch, startTick, durationTicks, velocity, isDrum,
      │            sourceTrackIndex, sourceEventIndex,
      │            + channel, offVelocity, offIsNoteOnZero,
      │              onOrder?, offOrder?, offSynthesized)
      └─ EVENTS
         └─ EVENT (tick, order, data: juce::MemoryBlock of RawMidiEvent::bytes)
```

- **New identifiers:** `NOTES`, `EVENTS`, `EVENT`, `data`, `order`,
  `channel`, `offVelocity`, `offIsNoteOnZero`, `onOrder`, `offOrder`,
  `offSynthesized`, `isConductor`, `endTick`. The existing `sourceTrackIndex`
  identifier is reused on `MIDI_TRACK`.
- **`order`, `onOrder`, `offOrder`:** the event's index within its *raw*
  source track. `onOrder`/`offOrder` are absent on notes created in the
  editor, and on notes whose timing was edited (see Editing).
- **`channel`:** 1–16, from the raw note-on. Format-0 and multi-channel
  tracks need this.
- **`SongDocument` helpers:** `getNotesNode(track)`, `getEventsNode(track)`,
  `getConductorTrack()`. `SongDocument()` creates the conductor `MIDI_TRACK`
  (with empty `NOTES`/`EVENTS`) at construction, so it exists before any
  import. `addTrack`/`addTrackBulk` create both containers.
- **Call sites migrated** from `track.getChild(i)`/`getNumChildren()`-as-notes
  to `getNotesNode(track)`: `SourceTrackNoteSource`, `SourceRollEditor`,
  `TrackRowComponent`, `TrackNotePreview`, `TrackListComponent`,
  `SongModelBridge`, plus any found by a full audit (`grep getChild`).
  Filtering by node type at each call site was rejected as fragile.
- **Track-index-based queries** (`getNumTracks`, `getTrack(i)`) keep
  counting every `MIDI_TRACK`, conductor included. Callers that mean
  "assignable tracks" must check `isConductor` and note count.
- **`TEMPO_MAP`/`METER_MAP` are unchanged**: derived at first import, still
  used by conversion and the timeline. The raw tempo/time-signature events
  also live in the conductor's `EVENTS`. This duplication is accepted
  because nothing edits tempo yet (see Follow-ups).
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
  `order` (sorting is by tick first, so mixed-track origins are fine).
  Everything else stays in its own track: SysEx, lyrics, text, track names
  and channel events. One Info diagnostic reports the number of relocated
  events.
- **Format 2:** treated like format 1 with no conductor (each pattern
  becomes a track), plus one Warning diagnostic saying format-2 sequencing
  semantics are not preserved.
- **Later imports (rule A, "first import owns the timeline"):**
  - If the file has a conductor, that whole track is dropped.
  - Otherwise its song-wide meta events are dropped from every track.
  - Everything else stays with its track, rescaled per the PPQ rules.
  - One Info diagnostic gives the count of dropped events.
  - The existing tempo/meter-mismatch Warning is unchanged.
- **The conductor is never part of conversion or arrangement:**
  - It can't be dragged onto a part, renamed or deleted.
  - `synthesiseDefaultParts` skips it.
  - `buildConfigAndRawSong` leaves it out of the `Song` given to
    `forge_core`.

### Import (`SongModelBridge::importMidiFile`)

1. Read the file once into memory. Parse it with `importMidi` (notes and
   diagnostics exactly as today) and with `readMidiFile` (raw).
2. Apply the conductor rules.
3. Every remaining raw track becomes a `MIDI_TRACK` (`sourceTrackIndex` = its
   raw index), including tracks with no notes. For a track `importMidi`
   produced, name, channel, program and notes come from it as today. For a
   note-less track:
   - name from its first 0x03 meta, else "Track N", matching `importMidi`
   - `sourceMidiChannel` from its first channel event (0 if none)
   - `sourceProgram` from its first program change (0 if none)
4. **Linking notes to raw events.** `importMidi` tags each note with the
   ordinal of its note-on among the track's note-ons (velocity > 0); JUCE's
   reordering never changes the relative order of note-ons. The k-th
   velocity>0 note-on in the raw track is the note-on of the note whose
   `sourceEventIndex == k`.
5. **Matching note-offs, replicating JUCE's pairing** (needed to reproduce
   which notes JUCE ended with an invented note-off):
   - Scan in JUCE's effective order: within a tick, note-offs (including
     note-on at velocity 0) come before note-ons.
   - For each velocity>0 note-on in that order, its off is the first later
     note-off on the same channel and key not already claimed, unless a
     note-on of the same channel and key comes first, in which case JUCE
     invented the off.
   - This runs over *all* note-ons, including ones `importMidi` later
     skipped, so claims match JUCE exactly.
   - For notes that became `NOTE`s: a real matched off sets `offOrder`,
     `offVelocity` and `offIsNoteOnZero`. An invented off sets
     `offSynthesized = true`.
   - **Cross-check:** the matched duration must equal `durationTicks`. On a
     mismatch, `jassertfalse` plus a Warning diagnostic (this would be a
     bug).
6. **Leftovers:** every raw event not claimed by a `NOTE` (on or off)
   becomes an `EVENT` with its `order`. That includes notes `importMidi`
   skipped (unmatched, zero-length) and note-offs nobody claimed, so nothing
   is lost.
7. **PPQ:** the existing rescale (`std::lround`) and LCM-raise rules apply
   to `EVENT.tick` and `MIDI_TRACK.endTick` as well as note ticks, for both
   incoming and (on LCM raise) existing tracks, including the conductor.
   The rescale diagnostics count these values too. Round-trip exactness is
   guaranteed only for a single import (or same-PPQ imports).
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
     ascending by that order
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
- Writes via `writeMidiFile`. Failures (unwritable path, stream error) throw
  a custom `MidiExportError`, shown in an `AlertWindow`.

### Editing interactions

- **New notes** (`SourceRollEditor` create gesture):
  - `channel` = the track's `sourceMidiChannel`, or 1 if that is 0
  - `offVelocity` = 64, `offIsNoteOnZero` = false
  - no `onOrder`/`offOrder`, `offSynthesized` = false
- **Move/resize/quantize of a note's timing** removes `onOrder`/`offOrder`
  and clears `offSynthesized` in the same undo transaction as the edit. The
  note then sorts as new material and gets a real note-off. Pitch and
  velocity edits keep them.
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

- **`RawMidi_tests.cpp` (Core):**
  - read → write → read gives equal `RawMidiFile`s for all 12 `midi/*.mid`.
  - Hand-built byte fixtures: running status; note-on velocity 0; F0 and F7
    SysEx; unknown meta type; missing End-of-Track; trailing silence
    (`endTick` > last event); format 0 with several channels;
    non-`MTrk` chunk skipped.
  - Malformed inputs throw `MidiImportError`: short header, chunk past end
    of file, unterminated variable-length number, orphan data byte,
    truncated event, SMPTE division.
- **`MidiFidelity_tests.cpp` (import → export integration):**
  - For each fixture: `importMidiFile` into a fresh `SongDocument`, then
    `buildRawMidiFile`, then compare with `readMidiFile` of the original.
    Files with a conductor are equal; the others are equal after applying
    the relocation rule, and the test asserts that rule.
  - `blue.mid` and `leah.mid` exercise `offSynthesized`, and their export
    reproduces the original exactly.
  - Every `NOTE`'s matched duration equals `importMidi`'s `durationTicks`
    across all fixtures (the cross-check never fires).
- **Conductor rules:**
  - Existing conductor kept verbatim.
  - Created for format 0 and for format 1 with notes in track 0.
  - Format-2 warning.
  - Later import drops song-wide events, with the Info diagnostic count.
  - Fresh `SongDocument` has an empty conductor.
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
- **Pins:** `SongsmithRoundTrip_tests` (CLI vs Songsmith ABC byte-identical)
  and the whole existing suite (329) pass unchanged apart from the
  mechanical `NOTES`-container migration in tests that build `MIDI_TRACK`
  children directly.

## Docs to update

- `docs/ARCHITECTURE.md`: RawMidi in the Core section, document schema, the
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
- **Tempo/meter editing:** must make the conductor's tempo/time-signature
  `EVENT`s authoritative and rebuild `TEMPO_MAP`/`METER_MAP` from them,
  removing today's duplication.
- **Master-track automation** in the conductor: intro/outro volume fades,
  song-wide volume or tempo adjustments over regions.
- **"Mix timelines" import preference:** default stays "first import keeps
  tempo, meter, etc."; the option would merge later imports' song-wide
  events.
- **Export of the LOTRO/ABC view as MIDI.**
- **Licensing guardrail:** add the bundled `TimGM6mb.sf2` (GPL-2) to
  `CLAUDE.md`'s licensing section when project 3 bundles it.
