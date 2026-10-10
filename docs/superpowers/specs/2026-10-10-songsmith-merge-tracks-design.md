# Songsmith: merge a track's notes into another track — design

Date: 2026-10-10. Status: draft for review.

## Goal

On the main canvas, the user can drag a track's notes (by section) onto another
track's row and merge them in. A plain drop **moves** the notes; holding Ctrl
(Cmd on macOS) copies them. A merge never leaves one note inside another note
of the same pitch, and same-pitch notes that overlap are joined into one longer
note.

## Context and constraints

- Sections (`docs/ARCHITECTURE.md` §9.15) already own the strip drag: a plain
  press-drag moves sections in time and edge zones resize them. The merge
  gesture must not disturb that.
- Guiding principle (`CLAUDE.md`): the MIDI is the source of truth, so the
  merge changes only what the user asked for. In particular it never rewrites
  overlaps that already exist in the target track.
- Core/UI boundary: this is editing logic, so it lives in `Source/UI/`.
  `Source/Core/` is untouched.
- Tree rules (`juce-valuetree-conventions`): one undo transaction per call,
  no write when nothing changes, no tree mutation mid-drag.

## Decisions taken with the user

| Question | Decision |
|---|---|
| Gesture | **Alt**-drag on a note strip (vertical onto another row) |
| Copy modifier | **Ctrl** (Cmd on macOS), read live during the drag and at release |
| Scope | The selected sections if the pressed section is part of a multi-selection, otherwise only the pressed section |
| Overlap rule | Same-pitch notes only; contained → dropped, overlapping or touching → joined |

Ctrl rather than Shift: Ctrl-drag is the platform convention for copy, and Shift
already means range-select on press. Ctrl+Alt as a *start* gesture was rejected
because several Windows layouts report AltGr as Ctrl+Alt.

## Merge semantics (`NoteMerge`, pure document logic)

New files `Source/UI/NoteMerge.{h,cpp}`, next to `SectionEdit`, no UI and no
`Source/Core/` dependency.

```
struct MergeResult { int inserted = 0; int dropped = 0; int extended = 0; bool changed = false; };
MergeResult mergeSections (SongDocument& doc, const std::vector<SectionRef>& refs,
                           juce::int64 targetTrackId, bool copy);
```

1. **Carried notes.** Every note of the track belonging to each ref
   (`sectionIdOfNote` over `sectionsOf`). Absolute ticks are kept; nothing is
   shifted in time.
2. **Validity.** The target must be a non-conductor `MIDI_TRACK`, different from
   every source track. Refs on the target track itself are ignored. If nothing
   valid remains, return `changed == false` with no transaction.
3. **Pairing.** An incoming note is compared only with notes of the **same
   pitch** in the target. Different pitches coexist (chords).
   Interval is `[startTick, startTick + durationTicks)`.
4. **Resolution**, per incoming note, in start order:
   - fully contained in an existing same-pitch note → **dropped**;
   - overlapping **or touching** (end equals start) one or more same-pitch
     notes → those notes and the incoming one collapse into a single note that
     keeps the earliest-starting note's properties (velocity, channel, etc.)
     and spans the union; the surviving note is `markNoteTimingEdited`;
   - otherwise → **inserted** as a new note.
   Incoming notes of the same pitch that collide with each other are resolved by
   the same rules in start order.
5. **Existing target overlaps** between notes that were already in the target
   are never touched.
6. **Inserted notes** lose their raw-MIDI ordering (`onOrder`, `offOrder`,
   `sourceTrackIndex`, `sourceEventIndex`) so they export as new material, take
   the target's `defaultChannel` and drum flag (the editor-created-note rule),
   and are tagged with the target section that contains their start, or the
   nearest one (`sectionIdOfNote` rules). A target with no stored sections is
   materialised first (non-undoably, as for other section mutations).
7. **Move vs copy.** Move removes the carried notes from the source track after
   the target is updated; the source's sections are left as they are (an
   emptied source can still be opened, see `fix(ui): allow opening the editor
   on a track whose notes were all deleted`). Copy leaves the source untouched.
8. **Undo.** Exactly one undo transaction for the whole call. A call that would
   change nothing opens no transaction and writes nothing.

## Gesture and UI

- **Start.** An Alt + left-press on a strip starts a `MergeGesture` in
  `TrackListComponent` (beside `SectionGesture`) instead of a section move or
  resize. A press without Alt is unchanged. Carried sections follow the scope
  decision above; an unselected pressed section becomes the selection.
- **Preview, not mutation.** The drag only updates `SectionViewState::mergeDrag`
  (hover target row id, valid flag, copy flag). A 3 px threshold applies before
  the preview starts, as for section drags.
- **Target.** The row under the pointer. The conductor, the source row and the
  gaps are invalid targets.
- **Feedback.** Valid target: highlight outline and translucent ghosts of the
  carried sections at their original ticks, plus a "Move"/"Copy" label by the
  pointer that follows Ctrl live. Invalid target: not-allowed cursor, no
  highlight. Esc cancels.
- **Release.** Over a valid target: one call to `mergeSections`, with
  `copy` taken from the modifiers at release. The target track and its sections
  become the selection. Anywhere else: nothing happens.
- **Wiring.** `TrackNotePreview` already reports press/drag/release with
  modifiers; the press forwards the Alt flag (via `ModifierKeys`) and the drag
  callback gains the pointer's y so the list can find the row. `TrackRowComponent`
  forwards both unchanged. No new tree listener: rows rebuild on the usual
  change notification.

## Error handling

`mergeSections` is total: invalid input returns `changed == false`. No custom
exception is needed because there is no failure path that the user can act on;
the UI shows the not-allowed cursor before release.

## Testing (TDD, integration-first)

Document-level (`Tests/NoteMerge_tests.cpp`):
- move removes from source, copy keeps it; both undo in one step;
- no-op (nothing valid, or everything contained) opens no transaction;
- contained → dropped; overlap → extended; touching → joined; incoming bridging
  two existing notes → one note; different pitches coexist;
- earliest note's velocity wins; extended note is marked timing-edited;
- inserted notes lose raw ordering and take the target channel/drum flag;
- target without sections is materialised; note-less target works;
- source == target and conductor target are rejected;
- existing target overlaps are left untouched.

List-level (`Tests/TrackListComponent_tests.cpp` or a new file): Alt-press,
drag, release over another row commits once; Ctrl at release copies; release
over the source row, the conductor or empty space does nothing; Esc cancels;
a press without Alt still moves sections.

Manual (user, deployed build): Alt tap on Windows must not steal focus to the
menu bar mid-drag. If it does, swallow the Alt-up while a merge gesture is
active. This cannot be verified headlessly.

## Out of scope

Merging whole tracks from the header, merging tracks into the conductor, any
re-timing of carried notes, and a Preferences switch for the gesture.

## Implementation notes (2026-10-10)

- The Move/Copy label sits at the pointer's x on the target row, not floating by the cursor.
- The preview threshold is 3 px of distance in any direction (measured from the stored press position), not vertical only.
- Alt starts a merge from any non-empty hit, including the section edge zones; a press on empty strip or the conductor starts none.
- `MergeResult` counts each carried note in exactly one of inserted/dropped/extended.
- JUCE has no not-allowed cursor: an invalid target shows the normal cursor and no highlight.
