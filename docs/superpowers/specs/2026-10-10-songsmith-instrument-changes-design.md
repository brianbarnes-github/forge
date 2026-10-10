# Songsmith: instrument changes in a track — design

Date: 2026-10-10. Status: implemented.

## Goal

A MIDI track can change instrument part-way through (Program Change events).
Songsmith shows those changes on the track row, lets the user split the track
at them, lets the user force the whole track to one instrument, and lets an
Alt-drag Move/Copy to another track carry the section's events (including its
program changes) if the user chooses. The purpose is to cut a track at an
instrument change and move or copy the parts onto other existing tracks.

## Context and constraints

- Today the instrument band on each row (`TrackRowComponent`, 16 px) shows one
  label: the GM name of `sourceProgram`, the *first* Program Change. Later
  program changes live as raw `EVENT` blobs under the track's `EVENTS`
  container and play back correctly, but are invisible and not editable.
- Guiding principle (`CLAUDE.md`): the MIDI is the source of truth. Nothing here
  infers an instrument the file does not state. In particular, a section does
  not take "the instrument in effect at its start" with it; only events that
  lie inside it travel.
- Core/UI boundary: all of this is editing and display logic, so it lives in
  `Source/UI/`. `Source/Core/` is untouched.
- Tree rules (`juce-valuetree-conventions`): one undo transaction per call, no
  write when nothing changes, no tree mutation mid-gesture.
- Out of scope: creating new tracks, an instrument picker for individual
  program changes, bank-select (CC0/CC32) editing.

## Decisions taken with the user

| Question | Decision |
|---|---|
| Relation to sections | Show instrument changes; split on demand (no auto-split on import) |
| Menu | Right-click on the instrument band: **Auto split on instrument change**, **Set track instrument to ▸** |
| Set instrument default | The track's first instrument is ticked |
| Cross-track Move/Copy | A preference: **Notes only** or **All events** |
| "All events" | Literal. Events inside the section's range travel; nothing is synthesized for the instrument in effect before the section |
| New tracks | Not in this feature |
| Preference default | **Notes only** (today's behaviour) — confirmed |

## Program changes (`ProgramChanges`, pure document logic)

New `Source/UI/ProgramChanges.{h,cpp}`, no UI, no Source/Core.

```cpp
struct ProgramChange { int tick; int channel; int program; juce::ValueTree event; };
std::vector<ProgramChange> programChangesOf (const juce::ValueTree& track);
```

Parses each `EVENT` whose `data` is a `Cn pp` message (status 0xC0–0xCF, two
bytes) and returns them in tick order, ties by `order`. Never mutates. The
conductor has none.

**Instrument segments** for display: one segment per program change, from its
tick to the next program change's tick (the last runs to the track's `endTick`).
Consecutive changes to the same program are merged into one segment. The span
before the first change, when the first change is after tick 0, is shown with
the existing `sourceProgram` label, as today. A track with at most one distinct
program has one segment and looks as it does now.

## Display

`TrackRowComponent` paints the band as the row of segments across the canvas
at the current zoom: each segment is filled with its GM family colour
(`gmFamilyFor`) and labelled with the GM name (`gmProgramName`), clipped to the
segment, with a boundary line at each change. Segment geometry comes from a
pure function over `programChangesOf` and the pixels-per-tick, so it is testable
without painting. Drum tracks (channel 10) keep "Drum Kit".

## Menu actions (`InstrumentEdit`, pure document logic)

Right-click on the band opens a `juce::PopupMenu`.

- **Auto split on instrument change.** Calls `splitAt` at each distinct
  program-change tick above 0, in one undo transaction (a new multi-tick
  `splitAtAll` so it stays one step). Disabled when the track has fewer than two
  segments. Each piece is an ordinary section.
- **Set track instrument to ▸ P.** The submenu lists the 128 GM programs in
  the 16 family groups; the track's first instrument carries a tick mark. In one
  undo transaction it (a) rewrites the program of the first program change on
  each channel the track has to P, or inserts one at tick 0 on the track's
  `defaultChannel` when there is none, (b) deletes the later program changes,
  and (c) sets `sourceProgram` to P. Nothing else is touched. A call that would
  change nothing opens no transaction.

## Cross-track Move/Copy preference

`AppSettings` gains `moveCopyScope()` / `setMergeScope()` with a
`MergeScope { notesOnly, allEvents }` enum, stored under
`editing.mergeScope` (`"notes"` / `"all"`; absent or unrecognised reads
`notesOnly`). Preferences ▸ Editing gets a radio pair labelled "When moving or
copying sections to another track: Notes only / All events".

`mergeSections` gains a `MergeScope` parameter. With `notesOnly` it behaves
exactly as today. With `allEvents`, after the notes are merged, every non-note
`EVENT` on a source track whose `tick` lies in the referenced section's
`[startTick, endTick)` is carried too:

- inserted on the target at the same tick with fresh `order` values after the
  target's existing events, `relocatedFrom` unset;
- a channel message's channel nibble is rewritten to the target track's channel
  (the notes inserted by the merge already take the target's channel), so the
  event stays paired with the notes it travels with;
- on Move the originals are removed from the source; on Copy they stay;
- never carried: End-of-Track and track-name metas, note-on/off events (notes
  and the importer's stray pairs follow the existing merge rules).

`TrackListComponent` reads the setting when the gesture is built; the drag label
may read "Move (all events)" / "Copy (all events)" in that mode.

## Error handling

All operations are total: invalid input returns without changes and opens no
undo transaction. No custom exception is needed, since no failure path is
actionable by the user.

## Testing (TDD, integration-first)

- `ProgramChanges_tests`: parsing, ordering, segments, merged duplicates,
  leading span before the first change, conductor and note-less tracks.
- `InstrumentEdit_tests`: auto split yields one section per segment and one undo
  step; disabled below two segments; set-instrument rewrite/insert/delete,
  `sourceProgram`, no-op opens no transaction, undo restores everything.
- `NoteMerge_tests` (`[merge]`): `notesOnly` unchanged; `allEvents` carries
  in-range events only, rewrites the channel, removes on Move, keeps on Copy,
  skips EOT/track-name, and does not synthesize a program change for an
  instrument set before the section.
- `AppSettings_tests`: round-trip, default, corrupt value.
- `TrackRowComponent`/`TrackListComponent` tests: segment geometry, band
  right-click menu items and enabled state.
- Manual: segments follow zoom/scroll; menu on a multi-instrument file.

## Open points for review

- Preference default `notesOnly`.
- Channel rewrite for carried channel messages (alternative: keep the original
  channel byte, which can sound on the wrong virtual channel in the target).
- Whether "Set track instrument to" should also colour the track by the new GM
  family (not proposed: `colorArgb` is the user's).

## Implementation notes

- `ProgramChange` carries the `EVENT` node (`juce::ValueTree event`) instead of an index, and the scope enum is `MergeScope` in `Source/UI/MergeScope.h`.
- `splitAtTicks` (one track, several ticks) replaces the spec's `splitAtAll`; the per-track body of `splitAt` is shared as `splitTrackAt`.
- `setTrackInstrument` also moves a kept first change that lies after tick 0 to tick 0 (otherwise the span before it would still play program 0), and gives inserted or moved changes an `order` below every event and imported note so the export places them before tick-0 notes. The single-segment band label reads the first segment's program, not `sourceProgram`.
- The Editing radios each act only when they are the button turned on: turning one radio on also notifies the one it turns off, which would otherwise save and apply twice.
- Open points confirmed by the user (2026-10-10): default `notesOnly`, channel rewrite on carried channel messages, no recolour on Set instrument.
- A stretch before a track's first program change is labelled with `sourceProgram` even when that program is not the one playing (program 0). Accepted: after a Move the stretch holds no notes, and a late first change in an imported file is rare.
