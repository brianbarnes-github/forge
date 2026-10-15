# Songsmith transport controls (right area) — design

Date: 2026-10-14. Status: draft for review.

## Goal

Fill the right area of the `TransportStrip` with numeric controls so the user
can type the loop range, the meter and tempo at the playhead, and set a
playback rate, without opening the Tempo/Meter editor window.

Everything is UI-side: nothing is added to `Source/Core/` (see the
`forge-engine-ui-boundary` skill). The only engine change is a playback rate.

## Agreed decisions

| Question | Decision |
|---|---|
| What is "Selection"? | The existing **loop range** (`PlaybackController::setLoopRange`), not a new selection model. |
| Which position do the meter and tempo boxes read and edit? | The **playhead**. |
| Rate range and persistence | **0.25 – 4.0**, logarithmic knob, **session only** (resets to 1.000 on launch, never in `AppSettings`). |
| How the rate reaches the engine | **Scale the engine clock** (approach A); pitch never changes. |
| Rejected | Rebuilding the snapshot with stretched times (glitches while playing); resampling output (changes pitch). |

## Layout (right area, left to right)

```
[Selection] [ start ] [ end ] [ length ]   [ 4/4 ]   [ ♩= 120 ]   (rate knob) 1.000
 └──────────── one group ───────────────┘   own        own          own
```

- **Selection group**: a "Selection" label and three text boxes, drawn as one
  bordered group.
  - `start` and `end`: position text `bar.beat.tick`, e.g. `1.1.0`.
  - `length`: a decimal number of **beats**, e.g. `88.5`.
- **Time signature box**: `n/d`, e.g. `6/8`.
- **Tempo box**: `♩=` prefix drawn as part of the box, then a BPM, e.g. `♩=120`.
- **Rate knob**: a rotary `juce::Slider` with a `1.000` readout beside it.
- The area is resizable (the splitter already exists). Controls keep a fixed
  width and are laid out left to right; those that do not fit are clipped, not
  squeezed, the way the middle area behaves.

## Behaviour

### Position text (`bar.beat.tick`)

- Bars and beats are 1-based, ticks 0-based and within the beat; the beat is
  the meter's denominator unit. Conversion uses the song's meter segments
  (`meterSegments`, `segmentAt`) and the PPQ from the tempo map, so
  multi-meter songs convert correctly.
- Parsing is a pure function: `parsePosition(text, segments, ppq)` returns a
  tick or a typed error. A short form is accepted (`5` = `5.1.0`, `5.3` =
  `5.3.0`). Bar 0 and beat or tick beyond the bar are rejected
  (`PositionParseError`).

### Selection group

- Display: shows the loop range while one exists; all three boxes are empty
  with none. Dragging the loop band, an `Esc` clear or an undo that moves it
  updates the boxes through `playbackLoopChanged`.
- Edit `start` or `end` and press Enter or leave the box: with an existing
  range, set that edge and keep the other. With none, the edit is held until
  both start and end hold valid values, then `setLoopRange` creates the range.
  Edges are ordered, clamped and minimum-length checked by `setLoopRange`; if
  it returns false the boxes revert to the current range.
- Edit `length` (beats from the start): sets `end = start + length`
  (converted through the meter segments); needs a start. With no start the
  length edit is rejected.
- Clearing all three boxes (select all, delete, Enter) clears the range.
- The boxes show the **ordered, clamped** range after every edit, never the
  raw typing.

### Time signature and tempo boxes

- Display the meter and BPM **in force at the playhead** (`listTempoMeterEvents`,
  `bpmAt`, the meter segment at the playhead tick). They refresh on
  `playbackPositionChanged` and when the tempo or meter events change. The
  tempo box uses `formatExactTempoText`. A box being edited is never
  overwritten by a refresh.
- Edit: `setMeter(doc, playheadTick, n, d)` (applied at the nearest bar start,
  as `TempoEdit` defines) and `setTempo(doc, playheadTick, bpm)`. Each is one
  undo step through `SongDocument`. A `TempoEditError` reverts the box and
  shows the reason as a tooltip-style notice on the box (`MeterOutOfRange`,
  `BpmOutOfRange`, `NoTimeBase`, …).
- `setTempo` at a tick that already holds a tempo edits it. Editing never
  moves the playhead.
- With no imported song (`NoTimeBase`) both boxes are disabled.

### Rate knob

- Value `r` in 0.25 – 4.0; slider position is linear in `log2(r)` so that 0.5
  and 2.0 sit equally far from 1.0. Readout `%.3f`.
- Double-click resets to 1.000. Typing a value in the readout is accepted
  (clamped).
- It changes playback speed only. It does not change the song's tempo, the
  tempo box, the ruler or any export.
- It is held by `PlaybackController` for the session and re-applied to a new
  engine state after `documentReplaced`. It is not written to
  `AppSettings` or the `.songsmith` file.

## Components and files

| Unit | Responsibility | Depends on |
|---|---|---|
| `Source/UI/Playback/BarPosition.{h,cpp}` | Pure `formatPosition` / `parsePosition` over meter segments + PPQ; `PositionParseError` | `MeterSegments` |
| `Source/UI/Playback/RateMapping.{h,cpp}` | Pure `rateToSlider` / `sliderToRate`, clamp, snap to 1.000 | none |
| `Source/UI/Playback/TransportControls.{h,cpp}` | The right-area component: the three groups, text boxes, knob; maps edits to the controller and `TempoEdit` | `PlaybackController`, `SongDocument`, the two units above |
| `TransportStrip` | Hosts `TransportControls` in `rightArea`; constructor also takes `SongDocument&` (it only receives the controller today, and `controller.document()` is const) | — |
| `PlaybackEngine` / `PlaybackController` | `setRate` / `rate()` | — |

`SongsmithMainComponent` and `MainWindow` change only where they construct
`TransportStrip`.

### Engine rate (approach A)

- `PlaybackEngine::setRate(double)` clamps to 0.25 – 4.0 and stores it in an
  atomic (like `master`); the audio thread reads it once per block.
- Per block the transport position advances `rate × blockSeconds`. An event at
  song time `T` is delivered at sample offset `(T − segPos) / rate`. Voices
  render at true pitch, so pitch is unchanged; note lengths follow because the
  note-off is scheduled on the same clock.
- The loop wrap (`nextLoopStep`) works in song seconds: its remaining-samples
  argument is converted to song seconds with the rate, and the sample count
  returned for the stretch before the wrap is converted back. Held-note
  retrigger at the wrap is unchanged.
- `rate == 1.0` must be bit-identical to today's path (the same arithmetic),
  so every existing engine test stays valid.
- `PlaybackController::setRate` forwards and notifies a new
  `Listener::playbackRateChanged`.

## Error handling

Custom error types, no string errors:
- `PositionParseError` (`Empty`, `BadFormat`, `BarOutOfRange`, `BeatOutOfRange`,
  `TickOutOfRange`).
- `TempoEditError` is reused unchanged for tempo and meter.
- A rejected edit never changes the document or the loop range; the box
  reverts to the current value.
- Time signature text accepts `n/d` only; `TempoEdit` validates the ranges.

## Testing (TDD; integration tests first for the business logic)

1. `BarPosition` round trips for single-meter and multi-meter songs; parse
   errors; short forms.
2. `RateMapping`: 1.000 maps to the centre, 0.5 and 2.0 are symmetric, clamp,
   snap.
3. Engine: at `rate = 2` a note on at T sounds at half the sample offset and
   a held note ends in half the samples; pitch input to the sink is unchanged;
   `rate = 1` is identical to the existing tests' expectations; loop wrap at
   `rate = 0.5` and `rate = 4` wraps at the exact sample; seek and stop at a
   non-unit rate; changing the rate while playing does not skip or repeat an
   event.
4. `TransportControls` integration: with a real `SongDocument` and
   `PlaybackController`, typing start and end creates the loop range; dragging
   the range updates the boxes; typing length sets the end; clearing the boxes
   clears the loop; an invalid entry reverts; meter and tempo edits at the
   playhead create undoable events and the boxes follow the playhead; no song
   disables the boxes; the knob reaches the engine and double-click resets it.
5. `TransportStrip` layout: existing tests keep passing; the new controls
   appear in `rightArea`, and narrowing the area clips them without
   overlapping the middle buttons.

## Out of scope

- Persisting the rate, or the Selection boxes, anywhere.
- A new time selection separate from the loop range, or a selected-section
  editor.
- Editing tempo or meter events at positions other than the playhead (the
  Tempo/Meter window stays the place for that).
- Favourite commands in the left area (TODO item 4).
- Pitch-preserving or pitch-shifting rate modes.

## Open points for review

- Beat unit for the `length` box is the meter's denominator beat; if you would
  rather have quarter notes always, say so.
- Whether Enter or leaving the box commits a held start/end pair is as written
  above; change it if you want an explicit Apply.
