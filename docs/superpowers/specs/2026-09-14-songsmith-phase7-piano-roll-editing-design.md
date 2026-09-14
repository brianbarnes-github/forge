# Songsmith Phase 7: piano roll editing — design

**Status:** Design accepted; implementation pending.
**Author:** Brian Barnes, with Claude, during a 2026-09-14 brainstorming session.

## Summary

Phase 7 of the Songsmith plan (`/home/brian/.claude/plans/lets-talk-about-how-optimized-marble.md`)
adds direct mouse-driven note editing to the **source** piano roll —
create, move, resize, delete — plus quantize as a batch grid-snap
operation, all committed through `SongDocument`'s `UndoManager` with one
transaction per gesture. This is the first Songsmith phase where the user
can actually reshape the imported MIDI rather than only viewing/assigning
it. The preview roll remains fully read-only, unchanged from Phase 6.

Two carried-over items from the Phase 5/6 whole-branch reviews become
load-bearing this phase and are addressed as prerequisites:
`PianoRollGeometry`'s `xForTick`/`tickForX` round-trip asymmetry, and
`PianoRollComponent`'s growing `Role`-branching debt.

## Goals

- Create, move, resize, and delete notes on the source piano roll via
  mouse gestures.
- Multi-note selection (click / shift-click / rubber-band) sufficient to
  drive quantize, move, and delete over an arbitrary set of notes.
- Quantize: grid-snap both `startTick` and `durationTicks` of the
  selected notes to a toolbar-chosen grid size.
- Every edit is undoable and redoable, one transaction per gesture
  (one keypress, one drag, one quantize invocation — regardless of how
  many notes it touches).
- Basic Ctrl+Z / Ctrl+Y (or Ctrl+Shift+Z) keyboard wiring so undo/redo is
  actually usable this phase, not blocked on Phase 8's fuller
  keyboard-shortcut pass.
- Fix the `xForTick`/`tickForX` round-trip so pixel→tick hit-testing for
  drag/resize is exact, not off-by-one at non-integer zoom ratios.

## Non-goals

- Song/Track/Group/time-range selection (a DAW-style multi-level
  selection and marquee-across-tracks model). Explicitly deferred — see
  "Deferred: advanced selection model" below.
- Any change to the preview roll's read-only behavior.
- Any change to `forge_core` — quantize and all other edits happen
  entirely on `SongDocument`'s `NOTE` nodes before `SongModelBridge` ever
  builds a `Config`. See `forge-engine-ui-boundary`: quantize is a UI
  editing action, never a pipeline parameter.
- Snap-while-dragging (magnetic grid snap during a live move/resize
  drag). Quantize stays the one explicit grid-snap tool; free drags stay
  continuous.
- Note velocity editing, note splitting/merging, copy/paste.
- Full keyboard-shortcut pass (menu items, other accelerators) — that
  stays in Phase 8; Phase 7 wires only undo/redo.
- Cursor-anchored zoom (pre-existing Phase 5 simplification, untouched).

## Guiding principles inherited

- **MIDI is the source of truth; the converter makes no timing
  decisions.** Quantize/move/resize are user-driven UI edits to
  `SongDocument`, never a `Config`/pipeline concept. `forge_core` only
  ever sees final tick values.
- **One gesture = one undo transaction** (`juce-valuetree-conventions`).
- **Synthetic ids, never positional indices**, for anything that outlives
  a single bridge call (unaffected by this phase — no new id-bearing node
  types are introduced).

## Prerequisite: coordinate math fix

`PianoRollGeometry::xForTick`/`tickForX` currently each independently
`std::lround`, so they are not exact inverses at non-1:1 zoom ratios
(confirmed by hand: `ticksPerQuarter=480, pixelsPerQuarterNote=479,
tick=240` round-trips to `241`). This phase depends on exact pixel↔tick
conversion for two different purposes, handled two different ways:

- **"Which note did I click/drag?"** — never round-trips through ticks.
  Hit-testing compares the mouse point directly against each note's
  already-computed pixel rect (as painting already does today).
- **"What tick does this pixel correspond to?"** (placing a new note,
  computing a move/resize delta) — `tickForX` becomes the single source
  of truth; `xForTick` is redefined as its exact algebraic left-inverse
  (derived from the same scale factor, not independently rounded), so
  round-tripping a tick through both functions is lossless by
  construction rather than by coincidence.

## Component architecture

`PianoRollComponent` already branches on `Role` in three places (range
band, ghost/dropped-note overlays, preview-specific border colours) —
flagged in the plan as the point to reconsider before a fourth branch is
added for mouse handling. Three options were considered:

1. Add editing as a 4th role-branch inside `PianoRollComponent` directly
   — least new code, but grows the exact debt the plan flagged.
2. Split into a base class (shared paint/viewport/zoom) plus
   `SourcePianoRollComponent`/`PreviewPianoRollComponent` subclasses —
   clean separation, but forces every call site
   (`SongsmithMainComponent`) to know which concrete type it holds.
3. **Chosen:** keep one concrete `PianoRollComponent` type — no call site
   changes — but introduce a new `SourceRollEditor` class that the roll
   owns only when constructed with `Role::Source`, and forwards
   `mouseDown`/`mouseDrag`/`mouseUp`/key events to. `PianoRollComponent`
   gains exactly one delegation point instead of a fourth branch.
   `SourceRollEditor` depends only on the note-source abstraction and
   `SongDocument`'s mutation API, so it is unit-testable with zero JUCE
   painting involved.

## Gestures (source roll only; preview roll unchanged, read-only)

- **Select** — click selects a single note; shift/ctrl-click toggles a
  note into/out of the current selection; dragging on empty canvas draws
  a rubber-band rect that selects every note it intersects.
- **Create** — double-click an empty grid cell creates a new note at the
  clicked pitch/tick; default `durationTicks` = the current toolbar grid
  size, falling back to a quarter note if the grid is set to "off."
- **Move** — dragging a note's body moves it on both axes: horizontal
  drag changes `startTick`, vertical drag changes `pitch`.
- **Resize** — dragging within an edge-threshold zone (~6px) of a note's
  left or right edge resizes it: the right edge changes `durationTicks`
  only; the left edge changes `startTick` and `durationTicks` together so
  the note's end tick stays fixed (standard DAW trim behavior).
- **Delete** — Delete/Backspace removes every selected note; a right-click
  context menu offers Delete as well.

All gesture-driven mutations go through `SongDocument`'s `UndoManager`,
one `beginNewTransaction()` per gesture (on mouse-up for drag/resize, once
per Delete keypress or quantize invocation regardless of how many notes
are touched) — per `juce-valuetree-conventions`.

## Quantize

A toolbar grid-size selector (transient UI state — e.g. "1/16" — not
persisted in the document, per the plan) plus a quantize action that:

- Acts on the current selection only. An empty selection is a no-op —
  there is no implicit "nothing selected means everything" behavior.
  (Select-all, once it exists, is the way to quantize an entire track.)
- Snaps **both** `startTick` and `durationTicks` of each selected note to
  the grid: `snapped = std::lround(tick / gridTicks) * gridTicks`.
- Is one undo transaction regardless of how many notes it touches.

## Undo / redo

Every mutation (`setProperty` for move/resize/quantize, `addChild`/
`removeChild` for create/delete) goes through `SongDocument`'s real
`UndoManager`, never `nullptr` (the one exception across the whole
document remains bulk MIDI import, unaffected by this phase). Phase 7
additionally wires minimal keyboard handling directly on the source
roll/editor: Ctrl+Z calls `undoManager.undo()`, Ctrl+Y (or Ctrl+Shift+Z)
calls `undoManager.redo()`. This is deliberately pulled forward from
Phase 8's "keyboard shortcuts" polish item, since Phase 7 is the first
phase with anything to undo — without it, editing would ship with no
way to trigger undo/redo at all until Phase 8 lands. Phase 8 still does
the fuller keyboard-shortcut pass (menu items, other accelerators).

## Testing

- **`SourceRollEditor`** gets direct Catch2 unit tests against a
  hand-built `ValueTree` — no JUCE painting involved — covering
  hit-testing, each gesture's resulting mutation, transaction boundaries,
  and quantize's snap math.
- **`PianoRollComponent` wiring** gets one test in the style of
  `Tests/SongsmithMainComponent_tests.cpp` (real component construction,
  real `ValueTree` mutation, a real pumped JUCE message loop) proving
  mouse events actually reach `SourceRollEditor` end to end — not just
  that the editor's internal logic is correct in isolation.
- Manual golden-path verification per the plan's existing Phase 4-8
  section: `run-ui.sh`, exercise create/move/resize/delete/quantize/undo/
  redo, confirm the preview roll updates and stays read-only. Per the
  project's standing policy, a Linux `run-ui.sh` pass does not count as
  this user's real verification — a Windows CI `.exe` pass is still
  required before this phase is considered actually verified.

## Deferred: advanced selection model

The user asked for Song/Track/Group/time-range selection (DAW-style:
select an entire track, a named group of notes, or a time range spanning
multiple tracks/parts) — explicitly deferred out of Phase 7 rather than
half-built, since it implies persisted grouping concepts not in the
current schema and cross-track marquee semantics that need their own
design pass. Phase 7 ships only single-roll note selection (click/
shift-click/rubber-band) — enough to drive quantize/move/delete over an
arbitrary set of notes on the currently-displayed track.

**Candidate landing spot:** fold into Phase 8 ("Polish"), or spin out as
its own Phase 9, once it's clearer how much schema/UI work it actually
needs. Revisit before Phase 7 is considered fully superseded by later
work.

## Open items carried into implementation

- Exact edge-threshold pixel value (~6px) for resize-zone hit-testing —
  tune during implementation against actual note heights at typical zoom.
- Whether `SourceRollEditor`'s key handling should be a JUCE
  `KeyListener` or `ApplicationCommandTarget` — implementation detail,
  decide when wiring it against `SongsmithMainComponent`'s existing
  command/menu setup (if any).
