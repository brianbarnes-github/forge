# Songsmith upper region: track-timeline redesign — design

**Status:** Design accepted; implementation pending.
**Author:** Brian Barnes, with Claude, during a 2026-09-15 brainstorming session.

## Summary

Replaces the Upper region's current model — one embedded `PianoRollComponent`
(`Role::Source`) that repoints to whichever track is selected in
`TrackListComponent` — with a Reaper-inspired arrangement view: every MIDI
track gets its own row with an always-visible, zoomable/scrollable inline
note-timeline preview, and double-clicking a row opens a dedicated floating
editor window for that track. The floating editor reuses Phase 7's
`PianoRollComponent`/`SourceRollEditor` pairing unchanged, re-hosted in a new
window instead of embedded in the main layout, and adds the ability to show
other tracks' notes as translucent, non-editable "ghosts" in the same roll.

This is a restructuring of the Upper region only (`docs/UI_GUIDE.md` #7).
It does not touch the Part Strip (#15/#16), Preview region (#18-22), or
Diagnostics (#23), and is unrelated to the still-undesigned "LOTRO
instrument pane rebuild" noted as the next open thread in
`HANDOFF-songsmith-part-strip-and-instrument-pane-2026-09-15.md`.

## Goals

- Every MIDI track visible in the Upper region shows its own note timeline
  inline, at all times — no more single-track-at-a-time embedded roll.
- The inline timeline is zoomable (horizontal) and scrollable, with all
  rows sharing one synchronized timeline position/zoom (Reaper's
  arrangement-view convention: scrolling/zooming any row moves them all).
- Double-clicking a row opens a floating editor window scoped to that
  track, reusing the existing Phase 7 gesture/undo-tested editing surface
  unchanged.
- The floating editor is single-instance: double-clicking a different row
  re-points the same window at the new track rather than opening a second
  window.
- While editing one track, other tracks' notes can optionally be shown
  translucently in the background of the same roll (non-editable), toggled
  per track from the row's own UI.
- Grid-size and Quantize controls move out of the Upper region's header
  (they have no meaning until a track is being edited) into menu entries
  that act on whichever floating editor is currently open.

## Non-goals

- Any change to `SourceRollEditor`'s public API, gesture behavior, or
  undo-transaction boundaries — all ~20 `SourceRollEditor_tests.cpp` cases
  must keep passing untouched. This phase only re-hosts the existing
  editor/roll pairing in a new window; it does not modify it.
- Any change to the Preview region, Part Strip, or Diagnostics.
- Persisting ghost-track visibility to `SongDocument`/`Config` — it is
  transient UI state (see "Ghost tracks" below).
- Independent per-row zoom/scroll (explicitly rejected in favor of one
  shared timeline).
- Syncing the floating editor's zoom/scroll to the shared row-timeline
  state — the editor keeps its own independent zoom, matching Reaper's
  arrangement-view-vs-MIDI-editor separation.
- A full `ApplicationCommandManager`/`ApplicationCommandTarget` setup —
  unnecessary given the single-instance window model (see "Window
  lifecycle & menu wiring").
- The LOTRO instrument-pane rebuild (Part Strip `X`/drum-map fields) — a
  separate, already-flagged, not-yet-designed thread.

## Guiding principles inherited

- **MIDI is the source of truth.** Nothing here changes what data is
  shown or how it's interpreted — only how it's laid out and which
  component renders it. No new `forge_core`/`Config` surface.
- **One gesture = one undo transaction**, unaffected: all mutation still
  flows through the existing `SourceRollEditor` → `SongDocument`
  `UndoManager` path, just reached via a different window.
- **Synthetic ids, never positional indices** — ghost-track references and
  the "currently edited track" pointer are held as `MIDI_TRACK` ValueTree
  node references (or their synthetic ids), never row indices, so
  reordering/adding/removing tracks doesn't invalidate them.

## Component architecture

### Upper region layout change

`UpperRegion` (`SongsmithMainComponent.h:68-81`, `.cpp:12-33`) drops
`sourceRoll` entirely. Its header keeps only the region title (grid-size
combo and Quantize button are removed — see "Menu wiring"). Below the
header, `TrackListComponent` expands to fill the full region width (no
more roll pane alongside it).

### `TimelineViewState`

A new small value type owned by `TrackListComponent`: time-per-pixel +
horizontal scroll offset. Passed by const-reference to every row's
`TrackNotePreview` for painting, and mutated by one shared zoom/scroll
control (mouse-wheel+ctrl to zoom, drag/scrollbar to pan) rendered once
at the bottom of the track list — not per row. This keeps all rows in
lockstep without needing per-row `Viewport`s; each row just repaints
against the same shared transform.

### `TrackNotePreview`

New per-row component (one per `TrackRowComponent`, replacing the current
label-only row content alongside it). Reads its track's `MIDI_TRACK`
ValueTree and the shared `TimelineViewState`, paints note rectangles
directly — no `PianoRollComponent` involved, confirmed as the lighter-weight
option consistent with `PartSlotComponent`'s existing custom-painting style.
Also renders the per-track ghost-visibility toggle (eye icon).

### `TrackEditorWindow`

New `juce::DocumentWindow` subclass, created on first double-click and
reused thereafter. Internally re-hosts the *existing*
`PianoRollComponent::Role::Source` + `SourceRollEditor` pairing exactly as
it works today inside `UpperRegion` — only the parent changes, not the
pairing's construction or behavior. Owns its own independent
zoom/scroll state (unrelated to `TimelineViewState`). Has its own menu bar
with grid-size + Quantize entries (see "Menu wiring").

Gains a new rendering capability: accepts a set of "ghost" `MIDI_TRACK`
node references, drawn as translucent, non-interactive note rectangles
behind/around the actively-edited track's notes. This requires a small,
additive API change to `PianoRollComponent` (Role::Source variant only) —
not a change to its existing Role-branch structure, which Phase 7 already
resolved into `SourceRollEditor` delegation.

### Window lifecycle & menu wiring

`SongsmithMainComponent` holds one nullable
`std::unique_ptr<TrackEditorWindow>`. Double-clicking a `TrackNotePreview`
row:

- if the pointer is null, constructs a new `TrackEditorWindow` for that
  track;
- if non-null, calls the existing `setEditableTrack`-equivalent to
  re-point the same window at the new track (the same mechanism
  `sourceRoll.setEditableTrack` uses today), rather than
  destroying/recreating the window.

Both the floating window's own menu and the main app's menu bar expose
grid-size + Quantize entries that forward directly to this pointer,
disabled/grayed out when it's null. Because there is at most one editor
window ever open, there's no ambiguity about "which window is active" —
so no `ApplicationCommandManager`/`ApplicationCommandTarget` routing
infrastructure is needed; both menus simply call through the same pointer.

### Ghost tracks

Each `TrackNotePreview` row exposes a toggle (eye icon) controlling
whether that track's notes appear as ghosts in the currently-open
`TrackEditorWindow`. The set of "ghosted" tracks is:

- scoped to *any* other MIDI track (not limited to same-part groupings),
- opt-in per track, off by default,
- transient UI state — held in memory (e.g. on `SongsmithMainComponent` or
  the coordinator that owns `TrackEditorWindow`), not written to
  `SongDocument`/`Config`, not part of undo history, and reset on
  restart — consistent with how current row-selection state already
  behaves.

## Testing

- **`SourceRollEditor_tests.cpp`** — no changes expected; this spec does
  not touch that class's API or behavior. All ~20 existing cases must stay
  green as a regression check that re-hosting didn't disturb gesture/undo
  semantics.
- **`SongsmithMainComponent_tests.cpp`** — updated/added cases: header no
  longer contains grid-size/Quantize widgets; double-clicking a row
  creates the floating window on first use and re-points (not recreates)
  it on subsequent double-clicks of other rows; main-menu grid/Quantize
  entries are disabled when no window is open and forward correctly when
  one is.
- **New: `TimelineViewState` tests** — zoom/scroll math (time-per-pixel
  conversions, clamping), no JUCE painting involved.
- **New: `TrackNotePreview` tests** — given a hand-built `MIDI_TRACK`
  ValueTree and a `TimelineViewState`, paints the expected note rectangles
  (verified the way existing painting tests in this codebase are, e.g. via
  captured `Graphics` calls or pixel comparison, matching whatever
  convention `PartSlotComponent`'s tests already use).
- **New: `PianoRollComponent` ghost-rendering test** — given a set of
  ghost track nodes, renders them translucently and does not route mouse
  gestures to them (only the actively-edited track is interactive).
- Manual golden-path verification via `run-ui.sh` for layout/zoom/scroll/
  double-click/ghost-toggle, per project policy that a Linux run does not
  substitute for the Windows CI `.exe` pass as this user's real
  verification.

## Open items carried into implementation

- Exact zoom/scroll control affordance for the shared row timeline
  (mouse-wheel+ctrl vs. a visible scrollbar+zoom slider, or both) —
  implementation detail, decide against actual row heights/usability.
- Ghost-track rendering style (opacity level, color treatment vs. the
  active track's notes) — tune visually during implementation.
- Whether `TrackEditorWindow`'s own menu bar is a full JUCE `MenuBarModel`
  or a simpler popup menu on a toolbar button — implementation detail,
  decide when wiring against this project's existing menu conventions.
- `TrackNotePreview` and `PianoRollComponent`'s ghost rendering will need
  to share note-rectangle-painting logic without duplicating it wholesale
  — worth a quick look during implementation at whether a small shared
  helper (e.g. a free function taking a note ValueTree + a rect + a
  `Graphics&`) is warranted, or whether the two contexts differ enough
  (thumbnail scale vs. full editing scale) that duplication is actually
  fine. Not decided here since it's a small-enough seam to resolve during
  implementation rather than up front.
