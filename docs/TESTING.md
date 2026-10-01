# Testing notes

- Test count: **324/324**.
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

See `docs/BUILD.md` for how to run these.
