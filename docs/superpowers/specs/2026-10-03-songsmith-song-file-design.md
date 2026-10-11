# Songsmith Song file — design

Status: Implemented (branch worktree-song-file, 2026-10-03). Project 2 of 3
(1 = MIDI fidelity, done; 3 = Playback).

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
- **Integrity via our own CRC-32 plus stored lengths, no SHA-256.** `juce::SHA256` needs the
  `juce_cryptography` module, which no target links; gzip is in `juce_core`.
  No new dependency or module is added.
- **`nextImportBatch` is persisted** on `SONG` with the other id counters.
- **Overwrite confirmation is our own** `file.exists()` prompt, run after the
  extension is appended.
- `Source/Core/` is untouched (see `forge-engine-ui-boundary`).

## File layer — `Source/UI/SongFile.{h,cpp}`

Container (little-endian):

| Field | Size | Notes |
|---|---|---|
| magic | 4 | `"SGSM"` |
| formatVersion | uint32 | starts at 1; one number covers container and tree schema |
| uncompressedLength | uint64 | bytes of the serialized tree before gzip |
| crc32 | uint32 | CRC-32 of the uncompressed payload |
| payloadLength | uint64 | bytes of the gzip payload that follow |
| payload | n | gzip of `ValueTree::writeToStream(SONG)` |

Read order (nothing is parsed until the bytes are proven intact — Debug
`ValueTree::readFromStream` `jassertfalse`s on garbage):
1. Magic, then `formatVersion`: any version greater than the reader's →
   `UnsupportedVersion`; older → migration chain keyed on `formatVersion`.
2. `payloadLength` must equal the remaining bytes (shorter → `Truncated`,
   longer → `Corrupt`).
3. Decompress fully via `GZIPDecompressorInputStream` (gzip format). JUCE's
   stream does not expose a failed zlib trailer check, so integrity is checked
   by the decompressed byte count (`uncompressedLength`) **and** our own
   `crc32` of the decompressed bytes → else `ChecksumMismatch`.
4. Only then `ValueTree::readFromStream`, then the semantic check below.

Any schema change bumps `formatVersion`.

API:
- `writeSongBytes(const ValueTree&) -> MemoryBlock`
- `readSongBytes(const MemoryBlock&) -> ValueTree` (throws `SongFileError`)
- `saveSongFile(const SongDocument&, const File&)` — builds the whole buffer
  first, then `File::replaceWithData` (sibling temp file + atomic replace); a
  failed save never touches the existing file.
- `loadSongFile(const File&) -> ValueTree`

`SongFileError` (custom type, plain-English message per kind):
`NotASongFile`, `UnsupportedVersion`, `Truncated`, `ChecksumMismatch`,
`Corrupt`, `InvalidStructure`.

`InvalidStructure` is a post-load semantic check. `nextTrackId`/`nextPartId`
are private identifiers in `SongDocument.cpp`, so the check lives in
`SongDocument` (`validateLoaded(const ValueTree&) -> std::optional<SongFileError>`),
called by the loader and by `replaceContents`. It verifies: root is `SONG`;
exactly one MIDI_TRACK has `isConductor=true` and it is `SOURCE_MIDI` child 0;
every MIDI_TRACK has `NOTES` and `EVENTS` children (`getNotesNode`/
`getEventsNode` and `isAssignableTrack` assume them); trackIds and partIds are
unique; every ASSIGNMENT references an existing track; `nextTrackId`,
`nextPartId` and `nextImportBatch` exceed all existing ids / batches.

Not saved: undo history. `nextTrackId`, `nextPartId` and `nextImportBatch` live
in the tree, so none is re-minted after a reload.

Verified: every `var` type present in the tree (int, int64, double, bool,
String, `MemoryBlock` for `EVENT.data`) has a write/read marker in JUCE's
`var` stream; no Array/Object/Method vars are set anywhere in `Source/UI`.

## Document and session

`SongDocument::replaceContents(const ValueTree& loaded)`:
- Components hold listener handles on the long-lived child nodes
  (`PartStripComponent` → `PARTS`, `TrackListComponent` → `SOURCE_MIDI`,
  `PianoRollComponent` → `METER_MAP`, `SongsmithMainComponent` → watched
  part/track nodes), so the tree is not swapped. The existing `SONG`,
  `SOURCE_MIDI`, `PARTS`, `TEMPO_MAP`, `METER_MAP` nodes are kept; properties
  are replaced (`copyPropertiesFrom`) and children cleared and re-added
  (`removeAllChildren` + `addChild(createCopy)`) with a `nullptr` UndoManager.
  Listeners see ordinary child-added/removed events and rebuild.
- Ends with `undoManager.clearUndoHistory()`.
- **New** = `replaceContents` with an empty Song (fresh conductor, reset
  counters including `nextImportBatch` = 1): one code path for New and Open.

**Session reset** (state outside the tree that `replaceContents` cannot reach).
`MainWindow` calls `SongsmithMainComponent::documentReplaced()` after every
New/Open/Close, which:
- closes the track editor window (`trackEditorWindow`; its `SourceRollEditor`
  and `SourceTrackNoteSource` hold the old MIDI_TRACK handle with no listener,
  so left open it would paint and edit an orphaned node);
- clears `ghostedTrackIds`, `PartStripComponent::selectedPartId`,
  `TrackListComponent::selectedTrackId` and `selectedPreviewPartId`, and
  re-binds the preview (ids persist across songs, so a stale id would
  re-select a *different* song's part);
- refits the timeline (`fitTimelineToDocument`, today called only from
  `openMidiFromPath`).
`MainWindow` also clears `lastAbc` (else Export ABC would write the previous
Song's ABC), the Diagnostics contents, and hides the export panel. The reset
must not write to the tree, or a freshly opened Song would read as dirty.
`SongsmithMainComponent` is compiled into `forge_tests`, so this is testable.

`SongSession` (`Source/UI/SongSession.{h,cpp}`): current file (empty =
untitled), dirty flag, `displayTitle()`.
- It holds its own persistent `juce::ValueTree rootNode` member for
  `addListener`/`removeListener` (`SongDocument::getTree()` returns a temporary
  copy handle; same convention as `TrackListComponent.h:120-129`).
- A listener on the root sets dirty on any change after the last save/load,
  including non-undoable imports (notifications bubble to all ancestors).
- Save and load clear it; the session goes dirty during `replaceContents` and `MainWindow` clears it with `markClean`/`markNew` immediately after.
- Undoing back to the saved state still reads as dirty (simple, safe).
- Verified: nothing in `Source/UI` writes to the tree on mere viewing; the only
  writers are user gestures (`PartSlotComponent`, `SourceRollEditor`).

Provenance and export:
- Export ▸ MIDI and Export ▸ ABC default filename = Song file stem + `.mid` /
  `.abc` (`Untitled` if unsaved), replacing the `inputMidiPath` stem. This
  removes the "overwrites the original imported .mid" hazard.
- Extension and overwrite fix (`saveAbcAs` at `MainWindow.cpp:408-409`,
  `exportMidiAs` at `MainWindow.cpp:439-440`): the native chooser warns on the
  typed name before our callback runs, so after appending the missing extension
  we run our own `file.exists()` confirmation (async message box) before
  writing.
- `config.input` and the raw `Song`'s title derive from `inputMidiPath`;
  `validateConfig` rejects an empty `config.input` but never opens it, so Run
  Converter still works on a loaded Song whose original `.mid` has moved.

## Menu and flows (`MainWindow`)

- **File:** New, Open…, Close, Save, Save As… (Ctrl+N / O / S / Shift+S);
  Import ▸ MIDI…; Export ▸ MIDI…, ABC…; Quit.
- Close and New both leave an empty untitled Song. Save is disabled when clean
  with a path; Save As is always enabled. Export ▸ ABC replaces "Save ABC As".
- Drag-and-drop: `isInterestedInFileDrag`/`filesDropped` (`MainWindow.cpp:240-264`)
  drop `.json/.toml/.xml` and accept `.mid`/`.midi` (import, as today) and
  `.songsmith` (guarded Open). Import ▸ MIDI keeps current semantics (first
  import owns the timeline; later imports rescaled and added; first-import
  detection is TEMPO_MAP emptiness, so a loaded Song correctly treats the next
  import as a later one; since the 2026-10-10 tempo/meter spec it is the saved
  `SOURCE_MIDI.timeBaseSet` flag instead). Imported tracks get `importBatch` from the persisted
  `nextImportBatch`.
- **Unsaved-changes guard is asynchronous.** `confirmDiscardChanges(onProceed)`
  is a callback chain — prompt → (Save As chooser if untitled) → save →
  `onProceed` — never a modal loop (`JUCE_MODAL_LOOPS_PERMITTED` is defined only
  for `forge_tests`). It wraps New, Open, Close, Quit, window close and
  `.songsmith` drops. If dirty: Save / Don't Save / Cancel; Save proceeds only
  if the save succeeds.
- **Quit routing:** `closeButtonPressed` and File ▸ Quit currently call
  `JUCEApplication::systemRequestedQuit()` (`MainWindow.cpp:135-138, 218`) and
  `UiApp::systemRequestedQuit` calls `quit()` immediately (`UiMain.cpp:17`).
  Both change to `MainWindow::requestQuit()`, which runs the guard and calls
  `JUCEApplication::quit()` only from `onProceed`; `UiApp::systemRequestedQuit`
  (OS/session end) routes to the same path.
- Title bar: `SongSession::displayTitle()` = `<name> — Songsmith`, `*` when
  dirty; `MainWindow` applies it via `setName` on dirty/path changes (the
  window title is currently the hard-coded `"Forge"`, `MainWindow.cpp:76`).
- Command-line `.songsmith` (`UiApp::initialise` ignores its argument today,
  `UiMain.cpp:15`): in scope — opened unguarded at startup via `MainWindow::openSongOnStartup` (nothing is open yet).

## Error handling

- Load: fully validated into a separate tree before `replaceContents`; failure
  shows a message box with the `SongFileError` message and leaves the open Song
  untouched.
- Save: failure shows a message box; destination intact (temp + replace).

## Testing (TDD, integration-first)

- Round trip over the 9 tracked `midi/*.mid` fixtures (reuse the list at
  `MidiFidelity_tests.cpp:85-86`; `angels/state/syn5` are untracked): import,
  edit, save to bytes, load, assert deep-equal tree (`isEquivalentTo`); export
  MIDI from the loaded Song equals the pre-save export byte for byte.
- Corruption: wrong magic, truncation, trailing bytes, flipped payload byte,
  stored `uncompressedLength` mismatch, bumped version → each yields its
  specific `SongFileError` kind. The "structurally invalid tree" case is built
  from a valid serialization of a bad tree (never garbage bytes, which would
  hit Debug asserts), and covers: two conductors, missing `NOTES`/`EVENTS`,
  duplicate ids, dangling assignment, counters not above existing ids.
- `replaceContents`: listeners fire, undo cleared, node identity preserved,
  `nextImportBatch` restored.
- Session reset: after `documentReplaced()` the editor is closed and
  selection/ghost/preview state is cleared, and no tree write occurs (session
  stays clean).
- Dirty flag: import, edit, save, load, undo transitions; `displayTitle()`.
- `MainWindow` is not compiled into `forge_tests`; keep its logic thin by
  pushing decisions (dirty state, filename/extension logic, guard sequencing)
  into testable helpers. Menu, drop and quit wiring is build-verified and
  review-checked. Run the suite under Wine (`./build-windows.sh forge_tests`)
  to cover the Windows atomic-replace path.
- Test names prefixed `SongFile:` / `SongSession:` (`ctest -R` filters by name).

## Docs to update

`docs/UI_GUIDE.md`, `docs/ARCHITECTURE.md`, `docs/TESTING.md`, the `CLAUDE.md`
status line, and `docs/DELTAS_FROM_SPEC.md` (note that the strict MIDI reader
rejects some files the CLI still converts, with no notes-only fallback).

## Out of scope

Recent-files list, autosave, Config import/export, relinking MIDI, "mix
timelines" import, OS file association / installer registration.
