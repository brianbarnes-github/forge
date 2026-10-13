# Songsmith time selection with looping — design

Status: draft for review (2026-10-12). TODO item #3.

## Goal

Select a span of time on the timeline and have playback repeat it. It is an
audition aid for practising or checking a passage while editing: nothing in it
touches the Song's MIDI, the ABC export, the undo stack or the saved
`.songsmith` file.

## Background

- Playback renders the source MIDI through `PlaybackEngine::renderBlock`
  (§9.14). `Transport` is the one clock (atomic playhead in seconds,
  play / pause / stop / seek, a seek generation). The engine **chases** — release
  voices, reset channels, replay Program / Control / PitchBend events before the
  position — whenever it sees a seek-generation change, a snapshot swap, a sink
  replacement or a playhead position it did not itself leave.
- A session-only **start marker** already exists on `PlaybackController`
  (`setMarkerTick` / `clearMarker` / `getMarkerTick`, cleared by
  `documentReplaced`). `play()` seeks to it first. A `MarkerOverlay` draws it
  down both canvases and `TimelineRuler` draws its triangle handle in the timing
  bar. In the ruler, a left press / drag sets the marker and a right press / drag
  moves the playhead.
- Each track row has a 16 px **instrument-name band** across the top of its
  canvas side (`TrackRowComponent::instrumentBandHeight`, `inInstrumentBand`). Its
  only interaction today is a right-click menu (auto split, set track
  instrument), so a left-drag there collides with nothing.

## Behaviour

### Creating and editing the range

- **Create:** left-drag in the instrument band of any non-conductor track row
  (conductor row excluded: it has no band menu today and its band is a plain
  label). The range is global time, not per track: dragging on any track's band
  selects that span for the whole Song. The press sets one edge and the drag
  sets the other, in either direction.
- **Snap:** both edges snap to the current grid (the same grid-size setting that
  note editing uses). Holding Alt drags unsnapped, to the exact tick under the
  pointer. A drag that ends with zero length (or shorter than one grid step when
  snapped) creates nothing and leaves any existing range alone.
- **Show:** a translucent band across all tracks and the editor roll (like
  `MarkerOverlay`, fully mouse-transparent), plus start / end handles in the
  timing bar.
- **Adjust:** dragging a handle in the timing bar moves that edge (same snap
  rules). Dragging inside an existing range in a band moves the whole range,
  keeping its length. A new drag that starts **outside** the existing range
  replaces it.
- **Clear:** Esc clears the range, as does clicking an edge handle in the timing
  bar without dragging (as the marker triangle clears the marker today). New /
  Open clears it (`documentReplaced`). Editing the Song does not.
- **Bounds:** edges are clamped to `[0, endSeconds]`. A range that extends past
  the end of the Song after an edit is clamped when read (the stored ticks are
  not rewritten).

### Playing

- **Range always loops.** There is no loop toggle: a selected range loops, no
  range means no loop. Clearing the range is how looping stops.
- **Play with a range:** starts at the range start and wraps from the range end
  back to the range start. The range overrides the start marker while it exists
  (the marker is untouched and applies again once the range is cleared). With no
  range, Play behaves exactly as today.
- **Stop** returns to the play-start, which with a range is the range start.
- **Seeks:** a seek or click that lands **inside** the range plays on from there
  and still wraps at the range end. A seek that lands **before** the range plays
  forward into it and then loops. A seek that lands **at or past the range end**
  plays on to the end of the Song with no loop-back (the playhead was never
  inside the range).
- **Edits while looping** keep working: a snapshot rebuild already re-chases
  from the current position, and the loop range is held in ticks so it follows
  tempo edits.
- **Muting / mixer changes** behave as during normal playback.

### Scope

Session-only: held on `PlaybackController`, not stored in the Song, not undoable,
reset by New / Open, never exported. It belongs to the transport, not to the
source-MIDI side, so the later LOTRO playback target (see the mixer spec's
`MixerTarget` seam) can honour the same range.

## Design

### Data and controller

- `PlaybackController` gains `setLoopRange (startTick, endTick)`, `clearLoopRange`,
  `getLoopRange` (`std::optional<LoopRange>` with `startTick < endTick`) and
  `Listener::playbackLoopChanged()`. `setLoopRange` is ignored while there is
  nothing to play (the same `endSeconds() > 0` test as `setMarkerTick`) and
  normalises reversed arguments. `documentReplaced` clears it.
- The range is stored in **ticks** and converted to seconds through the current
  snapshot's `TempoMap`; the controller republishes the seconds pair to the
  engine on every change and on every snapshot swap, so tempo edits move the loop
  edges with the music.
- `play()` seeks to the range start when a range exists, else to the marker as
  today. `PlaybackController` owns this decision; the transport stays dumb.

### Engine (the wrap)

- The engine holds the loop as two atomics, `loopStartSeconds` and
  `loopEndSeconds` (negative = off), written by the controller on the message
  thread and read once per block.
- **Sample-accurate wrap inside `renderBlockUnscaled`.** If the block starts
  inside `[loopStart, loopEnd)` and would cross `loopEnd`, the engine renders up
  to `loopEnd`, sets the playhead to `loopStart`, chases there
  (`releaseAll`, `chase`, `firstEventAtOrAfter`) and renders the rest of the
  block from `loopStart`; a very short loop can wrap more than once in a block,
  so this is a loop with a safe iteration cap (a block can never wrap more than
  its length in samples allows, and a degenerate loop shorter than one sample is
  rejected at `setLoopRange`).
- The wrap uses the same `Transport` position update the end-of-block advance
  uses, so the message thread still sees a continuously valid position and a
  concurrent message-thread seek still wins (the CAS drop path is kept).
- A playhead that starts the block at or past `loopEnd` (the "seek past the end"
  case) never wraps.
- Master gain, pan override and mute / solo apply unchanged because the wrap
  sits inside `renderBlockUnscaled`, below the master-gain wrapper.
- Notes held across the wrap are released by the chase's `releaseAll` (tails ring
  out as for any seek); notes that started before `loopStart` and are still
  sounding at that point of the music are not re-triggered, exactly as with a
  seek to `loopStart`.

### UI

- **Ruler:** `TimelineRuler` draws the two edge handles and a shaded span for the
  range (rows 1-2) and routes handle press / drag / click-to-clear to new
  callbacks (`onLoopEdgeDragged`, `onClearLoop`), next to the existing
  `onSetMarker` / `onSeek` / `onClearMarker`.
- **Band:** `TrackRowComponent` forwards a left press / drag / release in
  `inInstrumentBand` (not popup-menu, not on the conductor row) as
  `onLoopRangeDragged (tickAtX, finished)`, and a drag that starts on an existing
  range inside the band as a move. The right-click menu is untouched.
  `TrackListComponent` converts x to ticks with its `TimelineViewState`, applies
  the grid snap (shared snap helper, Alt = off) and calls the controller.
- **Overlay:** a `LoopRangeOverlay` (new class beside `MarkerOverlay` in
  `PlayheadOverlay.{h,cpp}`) draws the translucent band over the track canvas and
  the editor roll. Fully mouse-transparent; listens to
  `playbackLoopChanged`.
- **Keyboard:** Esc clears the range when the main window or a panel has focus and
  no text editor, popup or drag is active (it must not steal Esc from the note
  editor's cancel-gesture handling; the precise precedence is settled in the plan
  against `SourceRollEditor`'s existing Esc handling).
- **Menu / commands:** no new menu item is required. A Transport-menu
  "Clear Loop Range" entry is optional (see Open questions).

### Pure helpers (JUCE-free, unit-tested)

- `snapLoopTick (tick, gridTicks, altHeld)` — range-edge snapping.
- `normaliseLoopRange (a, b, endTick)` — ordering, clamping, minimum length.
- `LoopRange` with a `contains (tick)` and `toSeconds (TempoMap)` helper.
- The engine wrap arithmetic (how many samples until `loopEnd`, remaining samples
  after the wrap) as a small pure function so the block-splitting logic is tested
  without a sink.

## Testing

Integration over unit where it counts, with `RecordingSink` as in the pan tests:

- Engine: a block that crosses `loopEnd` renders exactly up to the end, then the
  chase happens at `loopStart`, and the events after the wrap are those after
  `loopStart` (check sample offsets). A loop shorter than a block wraps correctly
  more than once. Starting at or past `loopEnd` never wraps. A seek inside the
  loop keeps wrapping; a seek past it does not. A concurrent message-thread seek
  during a wrap block still wins. Mute / solo / pan / master apply across a wrap.
- Controller: `setLoopRange` / `clearLoopRange` / `documentReplaced`, ignored when
  nothing is loaded, reversed arguments normalised, `play()` starts at the range
  start and the marker applies again after clearing, the tick range follows a
  tempo edit, `playbackLoopChanged` fires once per change.
- Snap / normalise helpers: grid snap, Alt unsnapped, zero-length drag creates
  nothing, clamping to `[0, end]`.
- UI: a left-drag in a track band creates the range (and not on the conductor row,
  not for a right-click), dragging a handle moves one edge, dragging inside the
  range moves it, a drag from outside replaces it, click-on-handle and Esc clear
  it, and the right-click instrument menu still works. The overlay and ruler
  paint the span (pixel checks in the style of the marker tests).

## Non-goals

- A loop on / off toggle, count-in, a loop count, or per-track ranges.
- Saving the range in the Song, undo / redo of the range, exporting it.
- LOTRO-preview playback (only the seam: the range lives on the transport side).
- Audio-thread allocation or locking of any kind.

## Open questions (for the plan, not blockers)

- Whether to add a Transport-menu "Clear Loop Range" item (and its `MenuCommandId`;
  `allCommandIds` would become 35) in addition to Esc and the handle click.
- Exact Esc precedence against the editor's gesture-cancel handling.
- Whether the band drag should auto-scroll the canvas when dragging past the
  visible edge (the marker drag in the ruler does not today).
