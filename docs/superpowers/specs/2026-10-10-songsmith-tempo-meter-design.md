# Songsmith tempo and meter editing — design

Date: 2026-10-10. Status: approved; phase 1 implemented (plan `docs/superpowers/plans/2026-10-10-songsmith-tempo-meter-phase1.md`).

## Goal

Let the user see the song's tempo map and time-signature (meter) changes and
edit them: add, change and remove tempo events and meter events. The
Conductor track is the home for this. Edits are undoable and reach playback,
the ruler and the bar grid, the ABC export and — exactly — File ▸ Export ▸ MIDI.

## Requirements

1. **Export equals editor.** What the editor shows is what Export MIDI writes,
   for tempo and meter, with no possible disagreement. This is a hard
   requirement, met by construction (below), not by discipline.
2. **Notes stay on their ticks.** A tempo edit changes how long ticks last in
   seconds; it never moves or rewrites notes, sections or events. (The MIDI is
   the source of truth; a "keep the sound timing" mode is out of scope.)
3. Every edit is undoable; a drag is one undo step.
4. No change to `forge_core`: `TempoCollapse` already turns mid-song tempo
   changes into LOTRO's single `Q:`, and `% tempo:` / `% meter:` comments
   already mark them (`docs/DELTAS_FROM_SPEC.md`).

## What exists today

- Tempo and meter are stored twice: as `TEMPO_MAP` / `METER_MAP` nodes
  (`TEMPO_CHANGE {tick, bpm}`, `METER_CHANGE {tick, numerator, denominator}`),
  read by playback (`tempoMapFromDocument`), the ruler, the ABC export
  (`buildConfigAndRawSong`); and as raw `FF 51` / `FF 58` `EVENT`s on the
  Conductor track, read by Export MIDI (`buildRawMidiFile`). Both are filled at
  import and nothing keeps them in step afterwards.
- Every consumer of the meter assumes one meter: `rulerGridFromDocument`
  (first `METER_CHANGE` only), the roll's grid lines
  (`setNoteSource (…, doc.getMeterMapNode())`) and
  `PlaybackController::rewindOneBar` ("first entry only").
- The Conductor row shows only a name; it has no notes and every edit command
  skips it.
- "First import" is detected as an empty `TEMPO_MAP` (`appendImport`).

## Design

### 1. One source of truth: the conductor's events

The conductor's `FF 51` (tempo) and `FF 58` (time signature) `EVENT`s are the
only stored truth. `TEMPO_MAP` and `METER_MAP` become a **derived view**,
rebuilt from those events and never written by anything else.

- **`TempoMapSync`** (`Source/UI/TempoMapSync.{h,cpp}`): a pure function
  `deriveMaps (conductorEvents) -> {tempoMap, meterMap}` and a
  `juce::ValueTree::Listener` owned by `SongDocument` on the conductor's
  `EVENTS` node. Any add, remove or change of a tempo or meter event rebuilds
  both maps, written with a null `UndoManager` (the maps are never separately
  undoable; undo restores the events and the listener rebuilds the maps). The
  rebuild writes only when the result differs, so `PlaybackController`'s
  listener on `TEMPO_MAP` does not rebuild its snapshot needlessly.
- **Derivation mirrors the importer exactly.** Events are taken in
  `(tick, order)` order; a tempo is `60e6 / microsecondsPerQuarter` BPM; a
  meter is `nn` and `2^dd`. A differential test asserts `deriveMaps` equals
  what `importMidi` produced for every fixture MIDI, so existing songs behave
  identically.
- **Tempo/meter always live in the conductor.** A file that has its own conductor
  track may still hold `FF 51` / `FF 58` inside a note track (the importer's maps
  include them). Import now moves those into the conductor like it already does
  for conductor-less files (counted in the "Moved N song-wide event(s)" Info; on a
  later import with the tempo map kept they are dropped). Export MIDI therefore
  writes them in the conductor track — musically identical, but not event-for-event
  for that one file shape. Alt-drag (All events) never carries `FF 51` / `FF 58`.
- **Defaults.** With no tempo event the map is the single default
  `{0, 120 BPM}`; with no meter event `{0, 4/4}` — as `importMidi` seeds
  today. Export MIDI writes no event for a default; the first edit writes an
  explicit event at tick 0.
- **Bulk operations rebuild once.** Import (`appendImport`), a tempo Replace and
  loading a `.songsmith` file call `rebuildMaps (doc)` once after their bulk
  writes, with the listener suppressed meanwhile. `appendImport` no longer
  writes `TEMPO_CHANGE` / `METER_CHANGE` nodes itself, and its "file's tempo
  map differs" warning compares the file's maps to the (derived) Song maps as
  before.
- **First-import flag.** Because the map is now non-empty-or-default whatever
  the events, "has anything been imported" can no longer be "`TEMPO_MAP` is
  empty". `SOURCE_MIDI` gets a saved `timeBaseSet` boolean, set by the first
  import; loading an older file sets it when its `TEMPO_MAP` has children.
- **`.songsmith` files** keep storing the maps (no format change) but rebuild
  them from the events on load, so an older or hand-edited file cannot carry a
  disagreement into a session. `validateLoaded` gains no new failure mode.

### 2. Edit operations (`Source/UI/TempoEdit.{h,cpp}`)

Pure, headless-testable functions over `SongDocument`, each one undo
transaction, each writing only conductor `EVENT`s:

- `addTempo (doc, tick, bpm)`, `setTempo (doc, eventId, tick, bpm)`,
  `removeTempo (doc, eventId)`;
- `addMeter (doc, tick, numerator, denominator)`, `setMeter (…)`,
  `removeMeter (…)`;
- `describeTempoMeterEvents (doc)` for the views: the ordered list of events
  with tick, kind, value and a stable id.

Rules:

- **Encoding.** A typed BPM is rounded to whole microseconds per quarter note
  (`FF 51 03 tt tt tt`); the event, and so the map, holds the rounded value and
  the UI shows it. BPM range: whatever the 24-bit value can encode
  (≥ 3.58 BPM) up to 1000; out-of-range input is rejected with a message, not
  silently clamped.
- **Meter.** Numerator 1–32, denominator one of 1, 2, 4, 8, 16, 32. A new
  `FF 58` event is written with 24 clocks-per-click and 8 32nds-per-quarter;
  editing an existing meter keeps its original `cc`/`bb` bytes.
- **Position.** A tempo may sit on any tick (the graph snaps to the beat, the
  table is exact). A meter change snaps to the start of a bar, computed with the
  meters in force before it.
- **Tick 0.** The event at tick 0 can be edited but not removed; if no event is
  there, it is the implicit default, and adding one at tick 0 is allowed.
- **No duplicates.** At most one tempo and one meter event per tick; adding at
  an occupied tick edits it instead.
- Other conductor events (key signature, markers, copyright, SysEx) are never
  touched. A new event gets `order` after the existing events at its tick and
  no `relocatedFrom`.
- **Notes are never touched.**

### 3. Meter-aware consumers

Phase 1 makes every reader walk the whole meter map: the ruler's bar numbering
(`rulerGridFromDocument`, `TimelineRulerMarks`), the roll's bar grid lines
(`GridLines`), Rewind One Bar and Split/grid snapping where they use the bar,
and the `previousBarTick` helper. Bars restart their count at each meter
change's tick. A pure `meterSegments (meterMap, ticksPerQuarter)` helper is the
single place that turns the map into per-segment bar lengths. Tests pin the
single-meter results to today's.

### 4. Conductor row

The row stays unmuteable and non-editable as a track. It now draws, aligned
with the ruler and the tracks, a marker at each tempo event (`♩=120`) and each
meter event (`3/4`), read from the derived maps, and a faint change of
background at each. A single-click does nothing new; **double-click opens the
Tempo/Meter editor window**; the window is also reachable from a new
Edit ▸ Tempo and Meter… menu item and the Conductor head's right-click menu.

### 5. Tempo/Meter editor window

`TempoMeterEditorWindow` (a `juce::DocumentWindow`, owned by `MainWindow` like
the Track editor), one window per document. It reads only the derived maps and
`describeTempoMeterEvents`, so it refreshes on any change, including undo.

- **Graph (top).** Time runs along x on the shared timeline zoom/scroll. The
  tempo is a stepped line (BPM on y); each tempo event is a draggable point.
  Click empty space to add a point, drag horizontally to move it (snapped to the
  beat) and vertically to change BPM, Delete removes the selected point. A meter
  strip underneath shows each meter change as a block labelled `3/4`; click
  adds, drag moves (snapped to bars), Delete removes. A drag is one undo
  transaction committed on mouse-up; the window shows the in-progress value.
- **Table (bottom).** One row per event: kind, position (`bar.beat` and tick),
  value (BPM or `n/d`). Cells are typed; Add Tempo, Add Meter and Remove
  buttons. Selecting a row selects the graph point and vice versa.
- Both views call `TempoEdit`; neither writes the tree directly.
- Undo/Redo are the application's Edit commands (the window forwards the
  shortcuts).

### 6. Phases (each its own plan, in this order)

1. **Data layer:** `TempoMapSync` + the importer/loader/first-import-flag
   migration, `TempoEdit`, and the meter-aware consumers (§1–§3). No new UI.
2. **Conductor row display** (§4).
3. **Table editor window** (§5, table only; the window opens, edits, refreshes).
4. **Graph** (§5 graph and meter strip).

## Testing

TDD, integration tests first (project convention):

- Differential: `deriveMaps` equals `importMidi`'s maps for every fixture.
- The invariant: after any sequence of add/set/remove, undo/redo, import (keep
  and Replace) and save/load, the maps equal `deriveMaps (events)`, and
  `buildRawMidiFile` contains exactly the maps' tempo and meter changes.
- Edit rules: rounding, range rejection, bar snapping of meters, no duplicate
  tick, tick-0 removal refused, other conductor events and all notes untouched.
- Import: first-import detection with the new flag; a file with no tempo events;
  Replace; the `startOffsetTicks` placement (2026-10 feature) still shifts the
  conductor's later events and the rebuilt maps follow.
- Meter-aware consumers: multi-meter ruler marks, grid lines, rewind-one-bar;
  single-meter results unchanged.
- Playback: an edit changes `ticksToSeconds`; no snapshot rebuild when the
  rebuild produced the same maps.
- UI: the editor window's table/graph refresh on edit and on undo; window
  wiring in `MainWindow` is compile-checked and covered by a manual list, as
  other window wiring is.

## Decisions made for the user

(Veto any of these.) Tempo range up to 1000 BPM with rejection, not clamping;
meter numerator 1–32 and denominators 1–32; meter changes snap to bar starts,
tempo to the beat in the graph and exact in the table; one tempo and one meter
event per tick; the tick-0 event cannot be removed; the editor window is
reached by double-clicking the Conductor row, Edit ▸ Tempo and Meter…, and the
head's right-click menu.

## Out of scope

Gradual tempo ramps (MIDI has none; a ramp would be many tempo events); editing
key signatures, markers or other conductor events; "keep the sound timing"
edits that move notes; a tempo/meter change inside the ABC beyond what
`TempoCollapse` already does.

## Docs to update on delivery

`docs/ARCHITECTURE.md` (§9.2 data model, §9.13 load, §9.14 playback, a new
section for `TempoEdit` / `TempoMapSync` / the window), `docs/UI_GUIDE.md`
(Conductor row, window, menu item), `docs/TESTING.md`, CLAUDE.md status line.
