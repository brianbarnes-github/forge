# Testing notes

- Test count: **831** (`ctest --test-dir build -N | tail -1`).
- `AppSettings_tests.cpp` pins the two confirmation keys, their defaults (on), the "anything but 0 is on" read rule and write-through save; `PreferencesDialog_tests.cpp` pins the page tree / splitter / page layout, the page registry, the Close button (`triggerClick()` is asynchronous, so the test pumps the dispatch loop) and the General page toggles. `AppSettings_tests.cpp` also pins the save-failure reporting (a failed save returns false, keeps the value for the session and clears on the next good save); `PreferencesDialog_tests.cpp` pins the failure notice (from either page, visible on open when the last save already failed, hidden again after a later good save) and the minimum size. `SongDocument_tests.cpp` pins `validateLoaded` accepting well-formed sections and rejecting damaged `SECTION` nodes, and `SectionEdit_tests.cpp` that documents built with the real split/move/resize operations (overlap included) pass it.
- `ImportOptions_tests.cpp` (`[import-options]`) drives `importMidiFile` with `ImportOptions`: tempo replace swaps the tempo/meter maps and conductor events (keep leaves them), rescales the file's ticks to the Song's time base including an LCM raise at a different PPQ, and clears the old meter map; merged import yields one track whose notes keep their channels and export losslessly (same-tick events in (raw track, original index) order), a single note track imports unchanged, and a diagnostic from a folded-in track lands on the merged row; `previewImportTrackCount` counts the tracks an import adds (conductor excluded, note-less included, equal to what `importMidiFile` adds) and gives none for a missing or malformed file. `MidiImportPlan_tests.cpp` also pins the planner's replace/merge cases (`writesConductor`, `mergedTrack`, renumbered orders, fewer than two note tracks). `ImportOptionsDialog_tests.cpp` pins the dialog defaults, the Expand label (shows the track count from 2 up, plain for 1 or unknown), the radios, the disabled tempo group on a first import, and OK delivering options / Cancel delivering none. `AppSettings_tests.cpp` also pins `import.trackOptions` (Ask default, round trip, unrecognised value reads Ask) and `PreferencesDialog_tests.cpp` the Import page (second in the tree, dropdown reflects and writes the setting).
- `AppSettings_tests.cpp` also pins the SoundFont path accessors (`soundFontPath` empty by default, `setSoundFontPath` / `clearSoundFontPath` round-trip and persist under the unchanged `soundFontPath` key, failed saves reported). `PlaybackPreferencesPage_tests.cpp` (`[playback-page]`) drives the page against a fake `PreferencesServices`: the label shows the active SoundFont and Clear is disabled while no choice is saved; a chosen file that loads updates the label, enables Clear and fires `onChanged`; a file that fails to load shows the error and changes nothing; Clear returns to the bundled SoundFont; Clear with a missing or unloadable bundled file reports it but still forgets the saved path; a failed settings save is visible while the SoundFont still loads. `PreferencesDialog_tests.cpp` now expects the pages General, Import, Playback and that Playback is the third page.
- `MenuModel_tests.cpp` pins the menu bar structure (labels, shortcut hints,
  order, enabled/ticked per state, Transport as the 4th menu, no Close,
  unique command ids all in `allCommandIds`).
- `BarAlignment_tests.cpp` verifies bar-tick sums — regression catch
  for the day bar alignment was off in track 5.
- `Provenance_tests.cpp` verifies source-track/event IDs survive the
  full pipeline.
- `TempoCollapse_tests.cpp` covers unit-level tempo/meter scaling:
  duration rescaling, startTick rescaling, and
  `applyTempoCollapseToSongMaps` on both maps.
- `TempoPipeline_tests.cpp` covers pipeline-level behaviour across a
  mid-song tempo or meter change: rest-gap stretch, `% tempo:` and
  `% meter:` annotation emission, bar-label positioning.
- End-to-end test reads `midi/Barnes Brothers Band - Pull The Wires.mid`
  from disk, runs the full pipeline, checks structural ABC invariants.
- `SongDocument_tests.cpp`/`SongModelBridge_tests.cpp` cover Songsmith's
  `ValueTree` document schema and its translation layer to/from
  `forge_core`'s `Config`/`Song` structs — see `docs/ARCHITECTURE.md`
  §9.7/§9.8. The bridge tests include synthetic multi-import cases
  pinning the LCM time-base raise (existing notes and tempo/meter ticks
  rescaled exactly, undo history cleared), the over-cap lossy fallback's
  rounding mode and zero-duration Warning, the first-import timeline rule,
  `synthesiseDefaultParts` as one undo transaction, and the track-colour
  cycle. The document tests pin `assignTrackToPart` dedup, `PART.x`
  minting as max+1, and `removeTrack` cascading its assignments in one
  undo step.
- `SongsmithRoundTrip_tests.cpp` (`ctest -R SongsmithRoundTrip`; ctest
  filters by test name, not Catch2 tag) imports every tracked `midi/*.mid`
  fixture (120/480/960 PPQ) through `importMidiFile` → `SongDocument` →
  `buildConfigAndRawSong` → pipeline → `writeAbc` and requires
  byte-identical ABC to the direct `importMidi` → `synthesiseConfig` CLI
  path, plus real-file multi-import rescale, malformed-file, and
  first-import-wins cases.

- `RawMidi_tests.cpp` covers the lossless SMF reader/writer: running
  status, meta/SysEx, End-of-Track handling, format 0, non-`MTrk` chunks,
  malformed input, writer rejections, and write/read round-trips of every
  tracked fixture (with JUCE's track count).
- `JuceNoteReplica_tests.cpp` is a differential test of the JUCE note-pairing
  replica against `juce::MidiFile` itself (reordering within a tick,
  invented note-offs) on hand-built cases, 500 seeded random files and every
  tracked fixture.
- `MidiImportPlan_tests.cpp` covers `planMidiImport`: conductor rules
  (own conductor vs relocated song-wide events, later imports dropping
  theirs), note-to-raw-event links, and which events become `EVENT` nodes.
- `MidiImport_tests.cpp` covers the document side of a raw-aware import
  (`appendImportedMidi`): conductor/`EVENTS`/note-link properties written,
  note-less tracks, PPQ rescale and LCM raise of events and `endTick`.
- `MidiFidelity_tests.cpp` pins the whole feature: import then export
  reproduces each tracked `midi/*.mid` fixture (all 9) event-for-event,
  plus the pairing edge cases, format-0 and conductor-less files, and edited
  songs re-importing as the same notes.
- `SongFile_tests.cpp` pins the `.songsmith` container: byte round trip,
  and every `SongFileError` kind from a specifically damaged buffer (bad
  magic, newer and zero version, truncation and trailing bytes, a damaged
  payload re-wrapped as valid gzip, an intact container holding an invalid
  tree), a real-file save/load, a missing file, and a failed save leaving
  the existing file intact.
- `SongSession_tests.cpp` covers dirty tracking (any tree change, including
  non-undoable ones; undoing back still dirty; `replaceContents` then
  `markClean` clean), the `<name>*` title, `onChanged` firing only on a
  flip, `withExtensionIfMissing` and `defaultExportFile`.
  `DiscardGuard_tests.cpp` drives the Save / Don't Save / Cancel callback
  chain with fake hooks, including a failed or cancelled save.
- `SongDocument_tests.cpp` also pins `mintImportBatch`, `validateLoaded`
  rejections, `replaceContents` (equivalence, dropped `SONG` properties,
  node identity and listener notification kept, undo cleared, throw leaves
  the document untouched) and `resetToEmpty`.
  `SongsmithMainComponent_tests.cpp`, `TrackListComponent_tests.cpp` and
  `PartStripComponent_tests.cpp` (tag `[session-reset]`) cover the UI state
  that lives outside the tree being reset when a Song is replaced
  (`documentReplaced`, `clearSelection`).
- `SongFileRoundTrip_tests.cpp` imports each of the 9 tracked fixtures,
  arranges and edits it, saves and loads it and requires an equivalent
  tree and an identical Export MIDI byte stream; plus importing into a
  loaded Song (fresh track ids, batch number continues) and undo after a
  load. MainWindow is not in `forge_tests`, so menu wiring, shortcuts,
  native dialogs and quit routing are only build-verified.

- Playback (`Source/UI/Playback/`, see `docs/ARCHITECTURE.md` §9.14):
  `PlaybackError_tests.cpp` (error kinds and messages),
  `TempoMap_tests.cpp` (tick/seconds conversion, 120 BPM default),
  `PlaybackSnapshot_tests.cpp` (`buildSnapshot`: seconds timing, event
  ordering (Control, Program, PitchBend, NoteOff, NoteOn within a tick),
  conductor ignored, per-(track, channel) virtual channels, drum
  bank, the 64-track and 256-channel caps, degenerate data),
  `MuteSoloState_tests.cpp` (additive solo, mute beats solo),
  `Transport_tests.cpp` (play/pause/stop/seek, auto-stop and restart, the
  dropped stale `advance`, `previousBarTick`), `HandOff_tests.cpp`
  (publish/acquire/retire, message-thread free, every republish pattern),
  `PlaybackEngine_tests.cpp` (device-free, through a recording sink: block
  timing, tempo change, chase, seek / snapshot swap / sink replacement /
  playhead move without a seek-generation bump, mute release, auto-stop;
  `resetChannel` for every snapshot channel before each chase and never
  during continuous playback; a bank-select CC reaching the sink before the
  Program at the same tick), `SynthVoice_tests.cpp` (`resetChannel` is
  harmless without a font and restores a CC7 = 0 channel with the real font),
  `PlaybackController_tests.cpp` (real pumped message loop: coalesced
  rebuilds, cosmetic and parts changes not rebuilding, mid-play swap,
  mute/solo kept across removal and undo, `documentReplaced` including a
  Stop that does not jump to the previous Song's play-start, veto,
  flush-before-play, rewind), `TransportStrip_tests.cpp` and
  `PlayheadOverlay_tests.cpp` (the overlay and `TimelineRuler` seek), `TimelineRulerMarks_tests.cpp` (the pure bar/beat/clock mark computation, `formatClock`, and the ruler's two-row paint). `GridLines_tests.cpp` pins the pure grid-line computation (zoom thresholds, coarsest-level wins, meter-driven bars, 6/8, range clipping, odd PPQ); grid paint is checked in `TrackNotePreview_tests.cpp` and `PianoRollComponent_tests.cpp`.
  `TrackListComponent_tests.cpp` (tag `[wheel]`) pins the pointer-region wheel
  rules through `handleWheel` and the `WheelViewport` (canvas zoom with and
  without a vertical scrollbar, heads scroll / no-op / never zoom, Ctrl+wheel
  row resize with 4 px steps, clamps, sub-pixel accumulation, pointer
  anchoring, survival across `rebuild()`, Shift from either region), and
  `TrackRowComponent_tests.cpp` the layout at the minimum and maximum heights.
  `PianoRollComponent_tests.cpp` (tags `[wheel]`, `[row-height]`) pins the
  Source editor's mapping through the real `Canvas::mouseWheelMove` and the
  `handleWheel` decision: canvas zoom, gutter vertical scroll (clamps, no-op
  when the range fits, never zooms), Ctrl+wheel pitch-row resize (1 px steps,
  sub-pixel accumulation, clamps discarding excess, pointer-pitch anchoring,
  content height, `SourceRollEditor` hit-testing at min/max/in-between
  heights, survival across `setNoteSource`/resize, per-instance isolation),
  Shift pan, and the unchanged Preview mapping.
  `TrackRowComponent_tests.cpp`, `TrackListComponent_tests.cpp`,
  `PianoRollComponent_tests.cpp` and `TrackEditorWindow_tests.cpp` also gained
  the M/S buttons, ruler, playhead, follow and editor-window transport
  cases. `SynthVoice_tests.cpp`: the missing-file, garbage-bytes and no-font
  cases always run; the three smoke tests that need real audio return early
  with a `WARN` (and still pass) when the local, untracked
  `resources/soundfonts/SongSmith.sf2` is absent — always the case on CI.
  `MainWindow`, `AudioOutput`, the Song-menu item and the Space shortcut
  are build-verified only.
- `PianoRollGeometry_tests.cpp`/`SourceTrackNoteSource_tests.cpp` cover
  Phase 5's headless piano-roll coordinate math (tick/pitch↔x/y
  round-tripping, `fitToContent`, `isBlackKey`'s real-piano-key pattern)
  and the source-track note reader (live reads, empty-track ranges,
  orphaned-track defence). `TrackListComponent_tests.cpp` covers
  selection-clearing on track removal, re-fitting a still-live selection
  after a PPQ-raising second import, and — critically — a real
  `ValueTree::Listener → AsyncUpdater → rebuild()` end-to-end test using
  a real pumped JUCE message loop (`MessageManager::runDispatchLoopUntil`,
  needs `JUCE_MODAL_LOOPS_PERMITTED=1` on `forge_tests`), added after a
  Phase 4-era bug (a `ValueTree` listener registered on a temporary
  handle, silently dropped) was found via a live Windows build: the
  track list and part strip never updated after a real MIDI import or
  drag-and-drop assignment, invisible to every test that drives
  `rebuild()` directly via friend access. `PartStripComponent` got the
  same fix but has no automated regression test yet for the same
  real-listener path — see the plan's "Deferred design decisions".

- Track sections (`docs/ARCHITECTURE.md` §9.15; tag `[sections]`):
  `SectionEdit_tests.cpp` pins the read model (virtual vs stored sections,
  note ownership, edge hit-testing) and every mutation
  (`splitAt` including a straddling note, `moveSections` clamping,
  `resizeSections`/`resizeSectionsBy` delta semantics with differing ends,
  `deleteSections`), each as one undo step with no-op calls opening none.
  `PreviewNoteDiff_tests.cpp` pins the pairing passes for split notes.
  `TrackListComponent_tests.cpp` pins the independent head and canvas
  selections (a head click leaves the canvas alone; canvas clicks mirror onto
  the heads one way; Ctrl toggle, Shift same-start range, multi-selection kept on
  press and collapsed on a click release, empty-strip click, `selectAllCanvases`
  / `selectAllHeads`, per-selection pruning in `rebuild()`), press/drag/
  release gestures (drag is preview-only, commits once, jitter is a click,
  rebuild cancels a drag), and the keys: `S` at the pointer tick, falling
  back to the marker, then nothing; tracks owning a selected section vs the pointer's track (heads ignored);
  edge, conductor, unknown-track and repeated-press no-ops that open no undo
  step; `Delete` over several tracks in one undo step and clearing the
  selection; Ctrl+A (both variants) skipping the conductor. `SongsmithMainComponent_tests.cpp`
  pins the key forwarders doing nothing when there is nothing to act on.
  `MainWindow::keyPressed` itself (including the text-focus guard) is not
  unit-constructible and is verified manually.

See `docs/BUILD.md` for how to run these.
