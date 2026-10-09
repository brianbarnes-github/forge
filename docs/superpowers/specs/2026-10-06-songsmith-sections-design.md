# Songsmith sections: split, move, resize and delete parts of a track

Date: 2026-10-06. Status: approved and implemented.

## Goal

On the main-screen MIDI canvas, let the user cut each track into **sections**
and edit them like Reaper items: split with `S`, select, move, resize by an
edge, delete. Edits apply to the selected tracks (one, several, or all).
Playback, MIDI export and the ABC preview follow the edited notes.

Splits are **destructive** (user decision): they change the notes in the
document; undo is the way back. Sections may **overlap** (their notes sound
together); a note under a split is **cut in two**.

## Decisions (all settled with the user unless marked)

- Data model B: flat `NOTES` per track, a `sectionId` on every `NOTE`, a
  `SECTIONS` list per track. Rejected: sections owning their notes (A, large
  blast radius) and one track per piece (C, breaks "pieces of a track").
- A split cuts a straddling note into two notes (same pitch and velocity).
- A move onto another section keeps both sections' notes (overlap allowed).
- Edits apply to the **selected tracks**; with no track selected, to the track
  under the pointer.
- `S` splits at the pointer when it is over a track's note strip, else at the
  start marker. A pointer over a strip but outside every section does nothing
  and does not fall through to the marker. With the pointer over nothing and no
  marker set, `S` does nothing at all (user decision).
- Non-note `EVENTS` (controllers, pitch bend, tempo) stay where they are when a
  section moves or is cut; only notes follow sections. Deleting a section also
  removes the stray note-on/note-off pairs the importer kept in `EVENTS` (a
  stacked duplicate or zero-length note has no Song note) whose note-on lies
  in the section; a note-off alone, paired with an on elsewhere, stays. Shrinking
  a section's edge does the same for the band it gives up, and moving a section
  shifts those pairs (on and off) by the same delta as its notes, in the same undo
  step. A pair belongs to the section whose range holds its note-on; where
  selected sections overlap it moves once.
- Out of scope: moving a section to another track, copy/paste/duplicate of
  sections, sections in the Track editor roll.

## Data model

```
MIDI_TRACK
  SECTIONS
    SECTION { sectionId:int64, startTick:int, endTick:int }   // startTick < endTick
  NOTES
    NOTE { ..existing.., sectionId:int64 }
```

- `sectionId` is a monotonic `int64` minted by `SongDocument` (same scheme as
  `trackId`/`partId`; never a child index). Minting is non-undoable, like the
  existing counters.
- Section ranges are half-open `[startTick, endTick)`. A note belongs to a
  section by its `sectionId`, not by position, so overlapped notes can be dragged
  apart again.
- Reads never mutate. `sectionsOf(track)` returns the stored sections; a
  non-conductor track with notes but no stored sections reads as ONE virtual
  section `{id 0, [0, last note end)}`; a track with no notes has no sections
  (nothing to edit). The first edit on a track *materialises* it, inside that
  edit's own undo transaction (so undoing the edit puts the track back to its
  virtual section; the minted-id counter itself is not rolled back): it writes
  the `SECTIONS` child with a real minted id for the virtual section and tags
  every untagged or dangling note. A note with a
  missing or dangling `sectionId` belongs to the **nearest** section: distance
  is measured from its `startTick` to the section's `[startTick, endTick)` (0
  inside; otherwise to the nearer edge, the exclusive end counting as one past
  the last tick so abutting sections never tie a note inside one); ties go to
  the earlier `startTick`, then stored order. Documented residual: a note drawn
  in the Track editor far from every section still belongs to the nearest
  section and moves or deletes with it (the editor roll does not tag notes; that
  is a non-goal). A selection that names id 0 is resolved
  to the track's first section after materialising. Existing `.songsmith` files
  and fresh imports therefore need no migration.
- `Source/Core` is untouched. `SongModelBridge`, playback, export and the
  editor keep reading the flat `NOTES`.
- Provenance: `sourceTrackIndex`/`sourceEventIndex` are never regenerated.
  Both halves of a cut note keep the original pair verbatim ("the same source
  note"). `PreviewNoteDiff` keys a map on that pair; the plan must pin its
  behaviour with duplicate keys in a test and fix it if a half is mis-flagged.

## Editing logic: `Source/UI/SectionEdit.{h,cpp}`

Pure functions over `SongDocument`; no JUCE UI, no `Source/Core`. All take an
`UndoManager` through the document and mutate in **one transaction per call**.

- `splitAt (tracks, tick)`: for each track, every section with
  `startTick < tick < endTick` becomes two: `[start, tick)` and a new
  `[tick, end)`. Its notes with `startTick >= tick` move to the new section.
  A note with `startTick < tick < startTick + durationTicks` is cut: the left
  part ends at `tick`, a new note (same pitch, velocity, drum flag, provenance,
  new section) runs from `tick` to the old end. A tick on an edge or outside every
  section changes nothing.
- `moveSections (sections, deltaTicks)`: shifts each range and each of its notes'
  `startTick` by the same delta. The delta is clamped so no range or member note
  starts below 0, including member notes that lie outside the block (drawn in a
  gap). Overlap is allowed.
- `resizeSectionsBy (sections, edge, deltaTicks)` is the gesture path: each
  section's edge moves by the same **delta** as the clicked edge, from where that
  section's edge is; each is clamped on its own to at least 1 tick wide, and the
  whole gesture is one transaction. (`resizeSections (sections, edge, tick)`, an
  absolute tick for every section, remains as a single-target helper; with
  companions of different lengths it would shrink the longer ones, so the gesture
  does not use it.) Only a shrinking edge touches notes. On the left, a member
  that starts in the band given up is deleted if it ends inside the band, else
  its start moves up to the new edge (its end is unchanged). On the right, a
  member that starts in the band given up (at or after the new edge, even if it
  runs past the old end) is deleted; a member that starts in the kept range and
  crosses the new edge has its end cut at the edge; a member that starts before
  the old start and crosses the new right edge is not touched. Growing only
  extends the range and never touches a note, including a member that lies
  outside the section. Notes are never shifted by a resize.
- `deleteSections (sections)`: removes the sections and their notes.
- A section that ends up with no notes stays (an empty section is still an
  editable block).

### Applying to selected tracks

- A gesture acts on the **canvas selection** (a set of sections, see below),
  in a single transaction: a press on a section of a multi-selection keeps the
  selection so one drag moves or resizes every selected section by the same
  delta; a press elsewhere selects just that section first. (An earlier design
  derived "companions" from the head selection; heads no longer influence the
  canvas.)
- Split applies, at the one tick, to every track that owns a selected section.

## Track selection

Two independent selections, both transient and not persisted:

- **Head selection** (`selectedTrackIds`, with `selectedTrackId` as the
  last-clicked anchor for Shift ranges): a click on a track head. Click: select
  only that track. Ctrl/Cmd+click: toggle. Shift+click: select the range from the
  anchor (Ctrl/Cmd+Shift extends). It never changes the canvas selection. Drag-to-part
  uses it.
- **Canvas selection** (`sectionView.selected`, the bright sections): built only by
  clicks on a note strip, described under "Canvas UI". Every change to it is
  mirrored one way onto the heads: `selectedTrackIds` becomes the non-conductor
  tracks that own a selected section and the clicked track becomes the anchor. The
  mirror runs only on user clicks (and Ctrl+A over the strips), never from
  `rebuild()`.
- `rebuild()` prunes each selection on its own: dead tracks leave the heads, dead
  sections (a virtual id 0 remapped to the first stored section) leave the canvas.

## Canvas UI

- `TrackNotePreview` paints each section of its track as a translucent block under
  the note bars, with a visible edge at each end; selected sections are highlighted.
  Painting uses the shared `TimelineViewState`, as the notes and grid do. While a
  selected section is being moved, its notes are drawn shifted by the drag delta
  (same clamp as the block), so the notes travel inside the block; a resize drag
  does not preview note changes.
- Keys (handled by the main window): `S` splits at the tick under the pointer
  when the pointer is over a track's note strip, else at the start marker, else
  does nothing; it applies to the tracks that own a selected section, or just the
  track under the pointer when no section is selected (the head selection is not
  used). `Delete`/`Backspace` delete the selected sections. Ctrl/Cmd+A acts on the
  region under the pointer: over the note strips it selects every section of every
  non-conductor track (and mirrors onto the heads); anywhere else it selects all
  heads only and leaves the canvas selection alone.
- Mouse: clicking a track strip still sets the start marker at that tick (as
  today) and edits the canvas selection, as a click in Reaper moves the edit
  cursor and selects the item. Plain click on a section: select just it (unless it
  is already in a multi-selection, which is kept on press and collapsed to it if
  the button is released without dragging). Ctrl/Cmd+click: toggle that section,
  no drag. Shift+click: select, on each non-conductor track between the anchor
  section's track and the clicked one (row order), the section with the same
  `startTick` as the anchor section (rows without one are skipped); plain Shift
  replaces, Ctrl/Cmd+Shift extends; with no anchor Shift acts as a plain click.
  The anchor is the last plain or Ctrl-clicked section. Plain click on empty strip:
  clear the canvas selection and select that head. Ctrl/Shift on empty strip does
  nothing. Drag a section's body to move the selection; drag within a
  few pixels of an edge to resize. Nothing in the document changes during a
  drag (any change rebuilds the rows and would destroy the component holding
  the mouse); the drag is a preview committed on release. A drag is one
  undo step, committed on mouse-up; the preview follows the pointer during the drag.
- Snap: deferred (user decision). Moves, resizes and `S` are free (exact tick)
  for now; snap will be added later as its own piece of work.
- The existing marker and ruler gestures are unchanged.

## Playback, export and preview

Nothing new: all three read the flat `NOTES`, so an edit shows up on the next
rebuild. The plan must confirm that a note cut in two does not change how the ABC
pipeline treats the two halves (two attacks, as the user accepted).

## Undo

One gesture, one transaction (`beginNewTransaction` on mouse-up or key press), and
every mutation passes the document's `UndoManager`. Section and note changes from
one gesture are undone together.

## File format

`SECTIONS`/`SECTION` and `NOTE.sectionId` are written by `SongFile` as ordinary
ValueTree nodes and properties. A file without them loads through the
normalisation above. No version bump is planned unless the plan finds `SongFile`
rejects unknown nodes.

## Testing (TDD, integration-first)

- `SectionEdit_tests.cpp`: split (inside a section, on an edge, outside, with a
  straddling note, with overlapping sections), move (overlap, clamp at 0, drag
  back apart), resize (shrink deletes and trims, grow, 1-tick minimum, edge
  order), delete, each as a single undo step.
- `SongDocument` normalisation: untagged tracks, dangling ids, empty tracks.
- `SongFile` round trip with sections; an old file without them.
- Pipeline integration: a split, move and delete reach `SongModelBridge`, the
  preview and MIDI export.
- `TrackListComponent` multi-select (click, toggle, range, select all).
- `TrackNotePreview` section painting and gestures (pixel checks as for the grid).
- `PreviewNoteDiff` with a duplicate provenance key.

## Docs to update when built

`docs/UI_GUIDE.md` (new keys and gestures), `docs/ARCHITECTURE.md` (a section on
sections and selection), `docs/TESTING.md`.

## Open questions

- None. Decided: both halves of a cut note keep the original provenance pair
  verbatim. `PreviewNoteDiff` matches notes that share a pair by start-tick
  order (implementation plan, Task 4); this also fixes editor-created notes,
  which all share the pair (-1, -1).
