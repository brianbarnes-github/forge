# Songsmith Song file — design

Status: draft, awaiting review. Project 2 of 3 (1 = MIDI fidelity, done; 3 = Playback).

## Intent

The Song is Songsmith's governing document. A user can save it, close it and
reopen it later with everything intact: every track, note and event (including
edits), parts, assignments, title and transcriber. MIDI tracks are components
of a Song, not the thing being edited; Songsmith is a Song editor, not a MIDI
editor. Export ▸ MIDI remains as an optional output.

Success: save → close → open reproduces the document exactly, and a damaged or
foreign file is refused with a specific message without touching the open Song.

## Decisions (all settled with the user)

- **Embed, don't link.** The file holds all MIDI data including edits.
  `SONG.inputMidiPath` stays as provenance only; nothing re-reads that file.
- **Binary format**, not XML/JSON: the file must not be hand-editable outside
  the tool. Debug tooling uses the app's own display mechanisms.
- **Extension `.songsmith`.**
- **No autosave, no recovery files.** Saving is always an explicit user action.
- **Standard unsaved-changes prompt** (Save / Don't Save / Cancel).
- **Config items removed** from the menu (Open Config, Save Config As) along
  with the config drag-and-drop stub. Config import/export may return later.
- `Source/Core/` is untouched (see `forge-engine-ui-boundary`).

## File layer — `Source/UI/SongFile.{h,cpp}`

Container (little-endian):

| Field | Size | Notes |
|---|---|---|
| magic | 4 | `"SGSM"` |
| formatVersion | uint32 | starts at 1 |
| payloadLength | uint64 | bytes of payload |
| checksum | 32 | SHA-256 of the payload |
| payload | n | gzip of `ValueTree::writeToStream(SONG)` |

API:
- `writeSongBytes(const ValueTree&) -> MemoryBlock`
- `readSongBytes(const MemoryBlock&) -> ValueTree` (throws `SongFileError`)
- `saveSongFile(const SongDocument&, const File&)` — builds the whole buffer
  first, then temp file + atomic replace; a failed save never touches the
  existing file.
- `loadSongFile(const File&) -> ValueTree`

`SongFileError` (custom type, plain-English message per kind):
`NotASongFile`, `UnsupportedVersion`, `Truncated`, `ChecksumMismatch`,
`Corrupt`, `InvalidStructure`. A loader rejects a newer major version; older
versions run migrations keyed on `formatVersion`.

`InvalidStructure` is a post-load semantic check: root is `SONG`; conductor is
`SOURCE_MIDI` child 0; trackIds and partIds unique; every ASSIGNMENT references
an existing track; `nextTrackId`/`nextPartId` exceed all existing ids. The
checksum catches damage; this catches a file written by a buggy version.

Not saved: undo history. `nextTrackId`/`nextPartId` live in the tree, so ids are
never re-minted after a reload.

Plan-time check: confirm no `var` type in the tree other than int/int64/double/
bool/String/`MemoryBlock` is present (those round-trip through `var`'s stream).

## Document and session

`SongDocument::replaceContents(const ValueTree& loaded)`:
- Components hold listener handles on the long-lived child nodes
  (`PartStripComponent` → `PARTS`, `TrackListComponent` → `SOURCE_MIDI`,
  `SongsmithMainComponent` → watched part/track nodes), so the tree is not
  swapped. The existing `SONG`, `SOURCE_MIDI`, `PARTS`, `TEMPO_MAP`, `METER_MAP`
  nodes are kept; their children and properties are cleared and the loaded ones
  copied in non-undoably (same bulk style as import). Listeners see ordinary
  child-added/removed events and rebuild.
- Ends with `undoManager.clearUndoHistory()`.
- **New** = `replaceContents` with an empty Song (fresh conductor, reset
  counters): one code path for New and Open.

`SongSession` (`Source/UI/SongSession.{h,cpp}`): current file (empty =
untitled), dirty flag, display name.
- A `ValueTree::Listener` on the root sets dirty on any change after the last
  save/load, including non-undoable imports.
- Save and load clear it; `replaceContents`' own notifications are suppressed.
- Undoing back to the saved state still reads as dirty (simple, safe).

Provenance and export:
- Export ▸ MIDI default filename = Song file stem + `.mid` (`Untitled.mid` if
  unsaved), replacing the `inputMidiPath` stem. This removes the "overwrites the
  original imported .mid" hazard.
- A typed name lacking the expected extension gets it appended **before** the
  overwrite check (fixes the silent-overwrite bug in `exportMidiAs` and
  `saveAbcAs`, `MainWindow.cpp:~110-111`).

## Menu and flows (`MainWindow`)

- **File:** New, Open…, Close, Save, Save As… (Ctrl+N / O / S / Shift+S);
  Import ▸ MIDI…; Export ▸ MIDI…, ABC…; Quit.
- Close and New both leave an empty untitled Song. Save is disabled when clean
  with a path; Save As is always enabled. Export ▸ ABC replaces "Save ABC As".
- Dropped `.mid` imports as today; dropped `.songsmith` opens through the guarded
  Open path. Import ▸ MIDI keeps current semantics (first import owns the
  timeline; later imports rescaled and added).
- `confirmDiscardChanges(onProceed)` wraps New, Open, Close, Quit, window close
  and `.songsmith` drops. If dirty: Save / Don't Save / Cancel. Save proceeds
  only if the save succeeds.
- Title bar: `<name> — Songsmith`, `*` when dirty.

## Error handling

- Load: fully validated into a separate tree before `replaceContents`; failure
  shows a message box with the `SongFileError` message and leaves the open Song
  untouched.
- Save: failure shows a message box; destination intact (temp + replace).

## Testing (TDD, integration-first)

- Round trip over each tracked `midi/*.mid` fixture: import, edit, save to
  bytes, load, assert deep-equal tree; export MIDI from the loaded Song equals
  the pre-save export byte for byte.
- Corruption: wrong magic, truncation, flipped payload byte, bumped version,
  structurally invalid tree → each yields its specific `SongFileError` kind.
- `replaceContents`: listeners fire, undo cleared, node identity preserved.
- Dirty flag: import, edit, save, load, undo transitions.
- `MainWindow` is not compiled into `forge_tests`; keep its logic thin by
  pushing decisions (dirty state, filename/extension logic) into testable
  helpers. Menu and guard wiring is build-verified and review-checked.
- Test names prefixed `SongFile:` / `SongSession:` (`ctest -R` filters by name).

## Docs to update

`docs/UI_GUIDE.md`, `docs/ARCHITECTURE.md`, `docs/TESTING.md`, the `CLAUDE.md`
status line, and `docs/DELTAS_FROM_SPEC.md` (note that the strict MIDI reader
rejects some files the CLI still converts, with no notes-only fallback).

## Out of scope

Recent-files list, autosave, Config import/export, relinking MIDI, "mix
timelines" import, file association/OS integration.
