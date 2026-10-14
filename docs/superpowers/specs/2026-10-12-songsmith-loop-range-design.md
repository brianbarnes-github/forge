# Songsmith time selection with looping — design

Status: implemented (2026-10-12; plan
`docs/superpowers/plans/2026-10-12-songsmith-loop-range.md`, code walkthrough
`docs/ARCHITECTURE.md` §9.14). TODO item #3. This document has been updated to
describe what was built; the points where the build refined the draft are
marked *As built*.

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
  snapped) creates nothing and leaves any existing range alone. *As built:* the
  minimum is `kMinLoopSeconds` (0.05 s, at the tempo at the range start). The
  range follows the pointer live. When the drag becomes too short, e.g. back in
  the anchor's grid cell, the range from before the drag is put back, or cleared
  if there was none. The drag starts only after 4 px, so a band click just
  selects the row. The snap grid is the MIDI editor's grid while it shows a
  track, else the Preferences ▸ Editing default grid (Off = unsnapped). The
  editor panel's own ruler snaps to its roll's grid.
- **Show:** a translucent band across all tracks and the editor roll (like
  `MarkerOverlay`, fully mouse-transparent), plus start / end handles in the
  timing bar.
- **Adjust:** dragging a handle in the timing bar moves that edge (same snap
  rules). Dragging inside an existing range in a band moves the whole range,
  keeping its length. A new drag that starts **outside** the existing range
  replaces it. *As built:* a ruler edge drag is anchored on the other edge as
  it was at the press, so dragging past it swaps the edges. A moved range's
  start snaps to the grid and stops at tick 0. At the Song end the controller
  clamps the end, so the range gets shorter.
- **Clear:** Esc clears the range, as does clicking an edge handle in the timing
  bar without dragging (as the marker triangle clears the marker today;
  *as built*, "without dragging" means the pointer never got 3 px from the
  press). New /
  Open clears it (`documentReplaced`). Editing the Song does not.
- **Bounds:** edges are clamped to `[0, endSeconds]`. A range that extends past
  the end of the Song after an edit is clamped when read (the stored ticks are
  not rewritten). *As built:* if the clamped span is shorter than 0.05 s, no
  loop is published; it returns when the Song grows. `getLoopRange()`, which the
  UI draws, still returns the stored ticks, so the shading can differ from what
  the engine loops. No notice fires when the span is dropped or restored.

### Playing

- **Range always loops.** There is no loop toggle: a selected range loops, no
  range means no loop. Clearing the range is how looping stops.
- **Play with a range:** seeks to the range start only when the playhead is
  outside `[start, end)`. When it is already inside the range, Play resumes
  from there. Either way it wraps from the range end back to the range start.
  The range overrides the start marker while it exists (the marker is untouched
  and applies again once the range is cleared). With no range, Play behaves
  exactly as today. *As built:* "a range exists" means a span is published to
  the engine. After an edit shortens the Song so much that the span is dropped,
  Play takes the normal marker/playhead path.
- **Stop** returns to the play-start: the range start, or the in-range position
  Play resumed from.
- **Seeks:** a seek or click that lands **inside** the range plays on from there
  and still wraps at the range end. A seek that lands **before** the range plays
  forward into it and then loops. A seek that lands **at or past the range end**
  plays on to the end of the Song with no loop-back (the playhead was never
  inside the range).
- **Edits while looping** keep working: a snapshot rebuild already re-chases
  from the current position, and the loop range is held in ticks so it follows
  tempo edits. *As built:* an edit that leaves a looping playhead at or past the
  new range end (the end dragged or the range moved before the playhead, or a
  faster tempo pulling the edges in) wraps it straight back to the range start;
  only a seek past the end plays on.
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
  normalises reversed arguments. `documentReplaced` clears it. *As built:*
  `setLoopRange` returns `bool`, true when the request was accepted (including
  when the result equals the current range). A result that is too short or lies
  wholly past the Song end is rejected and leaves the range unchanged.
- The range is stored in **ticks** and converted to seconds through the current
  snapshot's `TempoMap`; the controller republishes the seconds pair to the
  engine on every change and on every snapshot swap, so tempo edits move the loop
  edges with the music.
- `play()` seeks to the range start when a range exists and the playhead is
  outside it, else (no range) to the marker as today. `PlaybackController` owns
  this decision; the transport stays dumb.

### Engine (the wrap)

- The engine holds the loop in a `LoopSpanCell`. The controller writes it on
  the message thread and the audio thread reads it once per block. *As built*
  it is a seqlock (version counter plus start, end and active atomics) rather
  than two bare atomics, so a block never sees a start from one range and an end
  from another. A torn read falls back to the last consistent value.
- **Sample-accurate wrap inside `renderBlockUnscaled`.** If the block starts
  inside `[loopStart, loopEnd)` and would cross `loopEnd`, the engine renders up
  to `loopEnd`, sets the playhead to `loopStart`, chases there
  (`releaseAll`, `chase`, `firstEventAtOrAfter`) and renders the rest of the
  block from `loopStart`; a very short loop can wrap more than once in a block,
  so this is a loop with a safe iteration cap. *As built:* the cap is
  `kMaxWrapsPerBlock` = 4, and loops shorter than 0.05 s are rejected at
  `setLoopRange`. A wrapping segment delivers only events strictly before the
  loop end, so an event exactly on the end plays after the wrap. When the cap
  binds (a block longer than 4 × the loop length, more than ~9600 frames at the
  minimum; unreachable at normal buffer sizes), the rest of the block only lets
  the synth ring out, with no events, and the playhead stays at the loop start,
  so it never leaves the loop.
- The wrap uses the same `Transport` position update the end-of-block advance
  uses (*as built*, `Transport::advanceTo(from, to, end)`, which may move
  backwards; `advance` is now a wrapper for it), so the message thread still sees a continuously valid position and a
  concurrent message-thread seek still wins (the CAS drop path is kept).
- A playhead that starts the block at or past `loopEnd` (the "seek past the end"
  case) never wraps. *As built,* with one exception: the engine keeps an
  audio-thread latch, `insideLoopLastBlock`, set when the previous block left the
  playhead inside `[loopStart, loopEnd)`. If it is set, there was no seek (same
  seek generation, and the playhead is where the engine left it) and the block
  starts at or past `loopEnd`, the block wraps to `loopStart` at once. This
  covers a range narrowed or moved before a looping playhead and a tempo
  speed-up while looping (the playhead is in seconds, the edges in ticks). A
  snapshot swap keeps the latch; Stop/pause, a seek, a dropped commit and a
  cleared loop reset it.
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
  `onSetMarker` / `onSeek` / `onClearMarker`. *As built:* the ruler also has
  `onLoopEdgePressed (bool isStart)`, fired by the press that grabs a bracket,
  so its owner can capture the other edge as the drag's anchor. A loop bracket's
  hit box wins where it overlaps the marker's triangle. A ruler drag carries no
  modifiers, so its owner reads Alt from the live keyboard state.
- **Band:** `TrackRowComponent` forwards a left press / drag / release in
  `inInstrumentBand` (not popup-menu, not on the conductor row) as
  `onLoopRangeDragged (pressTick, tickAtX, finished, unsnapped)`. Every drag
  step reports `finished = false` once the pointer is 4 px from the press, and
  the release reports `finished = true` only after such a step. The right-click
  menu is untouched, and the press still selects the row. `TrackListComponent`
  decides at the first drag step whether this is a move (the press tick lies
  inside the existing range) or a draw. It converts x to ticks with its
  `TimelineViewState`, applies the grid snap (`snapLoopTick`, Alt = off) and
  calls the controller.
- **Overlay:** a `LoopRangeOverlay` (new class beside `MarkerOverlay` in
  `PlayheadOverlay.{h,cpp}`) draws the translucent band over the track canvas and
  the editor roll. Fully mouse-transparent; listens to
  `playbackLoopChanged`.
- **Keyboard:** Esc clears the range when no text editor is active. *As
  built:* `SourceRollEditor` turned out to have no Esc handling of its own, so
  nothing in the note editor consumes the key. `MainWindow::keyPressed` keeps
  its text-input guard and then calls `SongsmithMainComponent::escapePressed()`,
  which does one thing per press:
  1. It cancels a section or merge drag, keeping the range.
  2. Otherwise, during a note-editor drag (`isGestureActive()` chain), it does
     nothing and does not consume the key.
  3. Otherwise it clears the range.

  Known gap: Esc does not reach `MainWindow` from a floating MIDI-editor panel
  (`DockFloatWindow` has no `keyPressed`).
- **Menu / commands:** no new menu item (see Open questions, resolved);
  `allCommandIds` stays 34.

### Pure helpers (JUCE-free, unit-tested)

- `snapLoopTick (tick, gridTicks, altHeld)` — range-edge snapping.
- `normaliseLoopRange (a, b, endTick)` — ordering, clamping, minimum length.
- `LoopRange` with a `contains (tick)` and `toSeconds (TempoMap)` helper. *As
  built:* `LoopRange` is a plain `{startTick, endTick}` struct. The containment
  and seconds conversion live in `PlaybackController` (`effectiveLoopSpan`, which
  yields a `LoopSpan` in seconds) and in `TrackListComponent` (move vs draw).
- The engine wrap arithmetic (how many samples until `loopEnd`, remaining samples
  after the wrap) as a small pure function so the block-splitting logic is tested
  without a sink: `nextLoopStep`.

## Testing

Integration over unit where it counts, with `RecordingSink` as in the pan tests:

- Engine: a block that crosses `loopEnd` renders exactly up to the end, then the
  chase happens at `loopStart`, and the events after the wrap are those after
  `loopStart` (check sample offsets). A loop shorter than a block wraps correctly
  more than once. Starting at or past `loopEnd` never wraps. A seek inside the
  loop keeps wrapping; a seek past it does not. *As built,* also: narrowing the
  range past a looping playhead, a tempo speed-up while looping and a block over
  4 × the loop all leave the playhead inside the loop. A concurrent message-thread seek
  during a wrap block still wins. Mute / solo / pan / master apply across a wrap
  (*not built*: no test of its own; they are untouched by construction, since the
  wrap sits inside `renderBlockUnscaled`).
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

## Open questions (resolved)

- Transport-menu "Clear Loop Range" item: **not built**. Esc and the bracket
  click suffice; `allCommandIds` stays 34.
- Esc precedence: section/merge drag cancel first (range kept), then nothing
  during a note-editor drag, then clear the range (see Keyboard above).
- Auto-scroll while band-dragging past the visible edge: **not built**, the same
  as the marker drag in the ruler.

## Known gaps (as built)

- Esc from a floating MIDI-editor panel never reaches `MainWindow`.
- A block longer than 4 × the loop length (the `kMaxWrapsPerBlock` cap) stays
  silent after the fourth wrap, apart from tails, with the playhead held at the
  loop start. This is unreachable at normal buffer sizes.
- Notes sounding at the loop start are not retriggered on a wrap, the same as
  after a seek, as this spec accepts above.
- After a Song shrink drops the span, the overlay still shows the stored range.
