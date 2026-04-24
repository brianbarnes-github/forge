# CLAUDE.md

Guidance for Claude Code (claude.ai/code) when working in this repository.

## Status: v0.1 CLI complete, editor-ready Core library

This repo is a **CLI MIDI → LOTRO ABC converter** whose Core layer is
designed to also back a future **MIDI editor + converter GUI**. The CLI
(`converter`) is a thin wrapper around a static library (`converter_core`)
that is JUCE-free at its public surface.

The original spec is `lotro-abc-converter-spec.md` (560 lines). Some of
its design decisions have been revised in practice; see "Deltas from spec"
below before assuming a section is still authoritative.

## Guiding principle

**The MIDI is the source of truth; the converter makes as few decisions
for the user as possible.** The ABC should reflect the input MIDI as
literally as LOTRO's format allows. Transformations are only acceptable
when LOTRO's parser forces our hand (range clamp, 6-note chord cap,
single `Q:`/`M:` per part, single dynamic per instrument). No smoothing,
hysteresis, quantization, or "clean-up" passes — if the MIDI has detail,
the ABC should show it. Diagnostic `%` comments are freely added; they
don't change audio. See `findings/dynamics.md` for a concrete case where
this principle rules out tempting improvements.

## Target platform and toolchain

- **Native Linux/WSL with GCC.** Spec §1/§4.3 name Windows + MSVC; both
  are overridden. MSVC was rejected outright. MinGW-w64 cross-compile was
  tried and abandoned because JUCE explicitly does not support MinGW
  (`juce_TargetPlatform.h` has `#error "MinGW is not supported"` and
  `JUCE_64BIT` is gated on `_MSC_VER`). Output `.abc` is plain text and
  platform-agnostic, so the conversion math is fully provable on Linux.
  Windows packaging is deferred.
- CMake ≥ 3.22, C++17, Ninja.
- JUCE (submodule at `./JUCE`) — modules linked: `juce_audio_basics`,
  `juce_audio_formats`, `juce_core`. Console app via `juce_add_console_app`
  — no GUI modules.
- The `converter_ui` GUI binary additionally links `juce_gui_basics`
  and `juce_gui_extra`. Linux/WSL build needs system packages
  `libfreetype-dev`, `libfontconfig-dev`, `libx11-dev`, `libxrandr-dev`,
  `libxinerama-dev`, `libxcursor-dev`, `libasound2-dev`. The CLI build
  does not need these.
- Catch2 (submodule at `./Tests/Catch2`, tracking `devel`).
- `cmake/mingw-w64-toolchain.cmake` exists but is unused; kept for
  reference. Revisit with `llvm-mingw` + clang if we ever retry Windows
  cross-compile.

## Build / test commands

From the repo root:

```bash
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure
```

Converter binary: `build/converter_artefacts/Debug/converter`.

UI binary: `build/converter_ui_artefacts/Debug/converter_ui`.

Run a single Catch2 test: `ctest --test-dir build -R <name> --output-on-failure`,
or invoke `build/Tests/converter_tests` directly with `"[tag]"` / `"test name"`.

First-time clone only: `git submodule update --init --recursive`.

## Architecture

### Build targets (CMakeLists.txt)

```
converter_core   (static library)  ─────────────► converter_tests (Catch2 exe)
       ▲                                          ▲
       │ depends on                               │
       ├──────────────────────────────────────────┤
       │                                          │
converter (CLI exe)                       converter_ui (JUCE GUI exe)
```

The Core library compiles once and is linked by both the CLI and the
test binary. A future GUI will link the same library.

### Source layout

```
Source/
├── Core/                        JUCE-free public API at the header surface
│   ├── Diagnostics.h            Severity + Diagnostic (source, tick, pitch,
│   │                            sourceTrackIndex, sourceEventIndex, trackIndex)
│   ├── Note.h                   POD; carries sourceTrackIndex/sourceEventIndex
│   ├── Track.h                  POD; std::string name, vector<Note>, …
│   ├── Song.h                   POD; includes a DrumMap member
│   ├── LotroInstrument.{h,cpp}  enum + InstrumentRange lookup; std::string API
│   ├── DrumMap.{h,cpp}          DrumMap class (set/clear/lookup); defaultDrumMap()
│   ├── MidiImporter.{h,cpp}     takes std::istream; juce::MidiFile *internally*
│   ├── AbcWriter.{h,cpp}        std::string writeAbc(Song). Internal ChordEmitter
│   │                            class encapsulates the emit loop's state.
│   └── Constraints/
│       ├── RangeConstraint      clamp pitches to instrument's ABC envelope
│       ├── ChordConstraint      group by startTick; trim chords > 6 by velocity
│       ├── DurationConstraint   drop zero-duration notes (currently a near-no-op)
│       ├── TempoCollapse        scale note startTick + durationTicks to stream-tick
│       │                        space; also exposes applyTempoCollapseToSongMaps
│       │                        which rescales tempoMap and meterMap ticks
│       ├── CollisionGuard       trim same-pitch overlaps
│       └── DynamicMapper        velocity → +dynamic+ markings (loudest-per-tick)
├── Cli/
│   ├── CliOptions.{h,cpp}       hand-rolled arg parser (juce::String internally)
│   └── DrumMapLoader.{h,cpp}    JSON parser for --drum-map (uses juce::JSON)
├── UI/                          JUCE GUI app — converter_ui binary.
│   ├── UiMain.cpp               JUCE app entry point
│   ├── MainWindow.{h,cpp}       Window, menus, splitter, drag-drop
│   ├── EditorPane.{h,cpp}       Left pane: tree + property page host + Run
│   ├── InstrumentsTree.{h,cpp}  Three-level treeview (Song/Instrument/Source)
│   ├── PropertyPageHost.{h,cpp} Swaps the visible property page on selection
│   ├── SongPropertyPage.{h,cpp}      Song-node property page
│   ├── InstrumentPropertyPage.{h,cpp} Instrument-node property page
│   ├── SourcePropertyPage.{h,cpp}    Source-node property page
│   ├── DiagnosticsPane.{h,cpp}       Right pane container
│   ├── DiagnosticListView.{h,cpp}    Diagnostic table
│   └── AbcPreviewView.{h,cpp}        Read-only ABC text + status line
└── Main.cpp                     wires import → synthesise default config →
                                  overrides → pipeline → writer, converts
                                  at boundaries
```

### Pipeline (in `Main.cpp::runPipeline`)

Per-track loop:
```
  Range → Chord → Duration → Tempo → Collision → Dynamic
```

Then once per song, after every track has been through the per-track
loop: `applyTempoCollapseToSongMaps(song)` rescales `tempoMap[i].tick`
and `meterMap[i].tick` into stream-tick space so the emitter's bar
labels, `% tempo:` comments, and `% meter:` comments line up with the
already-rescaled note ticks.

Finally: `AbcWriter::writeAbc(song)`.

**Order is load-bearing** — do not reorder without re-running all tests.

Only `MidiImporter.cpp` touches `juce::MidiFile`. Every constraint takes a
`Track&` and mutates it in place. All Core public headers are `juce::`-free
(verified: `grep juce Source/Core/*.h Source/Core/**/*.h` returns nothing).

### Per-instrument pitch letters

LOTRO ABC letters are strictly `C, .. c'` (3 octaves) for every instrument.
LOTRO applies a per-instrument pitch shift on playback so the same letter
sounds at a different MIDI pitch depending on the instrument: `C,` is MIDI
24 on Theorbo, 36 on Lute, 48 on Clarinet, 60 on Flute. The
`midiLow..midiHigh` table in `LotroInstrument.cpp` records those sounding
ranges. `AbcWriter::emitNotePart` shifts the absolute MIDI pitch by
`(48 - midiLow)` before converting to a letter, so the written text
always lands in `[C,..c']` — no `,,` or `''` notation, whatever the
instrument. `abcPitchToken(int)` keeps standard ABC semantics (Clarinet
is the identity shift); `abcPitchToken(int, LotroInstrument)` lets
callers get the per-instrument letter.

### Emission model — the thing worth understanding

`AbcWriter` does **not** use tied notes to express polyphony. Instead it
uses the **cluster-at-boundary with z-pulse** trick (see the ~30-line
design comment at the top of `AbcWriter.cpp`):

- Walk MIDI notes by start-tick cluster (all notes starting at the same
  tick become one emission).
- Each cluster emits one chord token `[note1dur1 note2dur2 ...]` where
  each note has its own duration. The **shortest inner element**
  controls how far the stream advances; longer voices keep ringing as
  subsequent tokens play on top.
- When a voice extends past the next cluster's start, we add a `z` rest
  **inside the chord brackets** as the pulse. Non-standard ABC but LOTRO
  and Maestro both accept it (verified against 2011-era reference output
  `rideintochetwood.abc`).
- The z-pulse is **capped at the remaining bar space**, so each
  `% bar N` comment covers exactly one bar of tokens. Held notes keep
  their full MIDI ring duration via the inner-per-note-duration
  mechanism; the cap only limits the chord's *advance*, not any note's
  duration.

`BarAlignment_tests.cpp` pins this invariant. The `ChordEmitter` class
inside the `AbcWriter.cpp` anonymous namespace owns the emit-loop state
(body buffer, stream-tick cursor, bar-label state, and reported-index
trackers for mid-song tempo/meter annotations); `emitBody` is a short
walk over note clusters feeding the emitter.

`ChordEmitter` is **meter-aware**: it takes `song.meterMap` and looks
up the current bar length per bar boundary, so `% bar N` labels track
mid-song meter changes even though the emitted `M:` in the header is
only the first one. At each bar boundary it also emits `% tempo: N bpm`
and `% meter: n/d` comments for any tempo/meter changes that fall in
that bar — purely informational markers (LOTRO ignores them, since it
only honours the first `Q:` and `M:` per part).

### Diagnostics

`Diagnostic` is a struct with `severity`, `source` (e.g.
"RangeConstraint"), `message`, `trackIndex`, `tick`, `pitch`, plus the
stable-identity pair `sourceTrackIndex` / `sourceEventIndex` that points
back to the originating MIDI event. `formatDiagnostic(d)` renders a
one-line display. The CLI's `-v` flag prints these; a GUI will render
them as clickable list items and use the source IDs to jump-to-source.

## CLI shape

```
converter [OPTIONS] INPUT.mid [OUTPUT.abc]
  --config PATH         Load a config file (JSON/TOML/XML)
  --config-format FMT   Override format detection (json|toml|xml)
  --instrument N=NAME   Assign instrument to track N (config-less mode only)
  --tempo BPM           Override detected main tempo
  --transpose N         Global semitone transpose (pre range-clamp)
  --drum-map PATH       Load drum-map JSON file, merged onto defaults
  --list-tracks         Print track table and exit
  --list-instruments    Print valid instrument NAME values and exit
  -v, --verbose         Log Diagnostics to stderr
  -h, --help            Print this help and exit
```

### Config mode

`--config PATH` loads a JSON, TOML, or XML config that
authoritatively describes the output: which MIDI tracks become which ABC
parts, per-instrument transpose and volume scaling, X: index assignment,
track merging, and per-instrument drum-map overrides. Without `--config`,
the converter synthesises an equivalent internal `Config` from the CLI
flags so existing one-shot invocations keep working bit-for-bit.

See `docs/superpowers/specs/2026-04-23-config-driven-conversion-design.md`
for the full schema.

**Defaults:**
- Instrument per track defaults to `LuteOfAges` for every non-drum
  track. The converter does **not** auto-pick based on note ranges —
  the MIDI-is-source-of-truth principle means we don't second-guess
  the song writer. The user is expected to pick the real instrument
  via `--instrument N=NAME` (CLI) or the Instrument property page
  dropdown (GUI).
- MIDI channel-10 tracks are imported as `Drums` in `MidiImporter`
  because General MIDI declares channel 10 as percussion; this is
  reading the MIDI, not a converter heuristic.
- Output path defaults to `<input-stem>.abc` next to the input.
- Drum mappings default to the spec §2.6 + extended-GM-percussion set
  in `defaultDrumMap()`. `--drum-map` merges overrides on top; unlisted
  pitches keep their defaults.

## Deltas from the spec

These are intentional and verified against test fixtures:

- **Spec §2.4 "quantize to 1/16 grid" was removed.** We preserve exact
  MIDI tick positions. See `findings/drum-timing.md` for context —
  empirical testing showed quantization introduced audible drift; the
  user-requested "preserve exact ticks" emission proved accurate when
  playback was verified against the MIDI source.
- **Spec §2.4 "split long notes at bar lines with ties"**: we don't
  split note tokens. `DurationConstraint` passes notes through
  unchanged except for dropping zero-duration ones. Long sustains
  survive intact inside cluster chord tokens.
- **Ties** (`-`): removed entirely. Not needed with the z-pulse
  emission model.
- **PolyphonyFlatten**: existed briefly, removed. The z-pulse trick
  supersedes slice-based flattening and eliminates the re-articulation
  problem for held voices.
- **ABC `L:1/8`** is still the fixed default length. `Q:` `M:` `K:`
  headers match the spec format.
- **Mid-song tempo changes**: LOTRO honours only the first `Q:` per
  part, so `TempoCollapse` absorbs all tempo changes into scaled
  `startTick` + `durationTicks` values under a single fixed `Q:`.
  Rest gaps stretch or compress with the tempo just like notes do.
- **Mid-song meter changes**: LOTRO also honours only the first `M:`.
  `applyTempoCollapseToSongMaps` rescales `meterMap[i].tick` into
  stream-tick space, and `ChordEmitter` looks up the current bar
  length per bar boundary so `% bar N` labels track the real bar
  structure even with a single emitted `M:`.
- **`% tempo:` and `% meter:` comments**: emitted at the bar where a
  change takes effect (LOTRO ignores comments; purely diagnostic for
  humans).
- **Dynamics**: `DynamicMapper` does loudest-per-tick bucketing. This
  is the correct end state under LOTRO's one-dynamic-per-instrument
  constraint, not a workaround. See `findings/dynamics.md` — the
  previously-proposed smoothing/hysteresis/centroid passes are
  retired under the MIDI-is-truth principle.

## Reference material in this repo

- `drummaps/default.txt`, `drums.txt`, `cymbals.txt` — **Maestro-format
  drum maps, AGPL-licensed reference data only**. Read for GM → LOTRO
  drum-note mappings; do not ship verbatim. The defaults live in
  `Source/Core/DrumMap.cpp`'s `specDefaults` array; users can override
  at runtime via `--drum-map drum_map.json`.
- `drum_map.json` (repo root) — sample JSON reproducing the built-in
  defaults. Edit to customise.
- `midi/*.mid` — test fixtures. `Barnes Brothers Band - Pull The Wires.mid`
  is the end-to-end reference used by `EndToEnd_tests.cpp`.
- `correct right.abc`, `rideintochetwood.abc` — reference outputs from
  other LOTRO converters (Maestro / 2011-era) kept as comparison
  targets. `rideintochetwood.abc` is the origin of the z-pulse trick.

## Git

Atomic commit history starting from `1f24708`. Conventional-commit
prefixes (`feat:`, `fix:`, `refactor:`). Never push without explicit
approval.

## Testing notes

- Test count: **134/134**.
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

## Licensing guardrails

- **Maestro** (github.com/NikolaiVChr/maestro, github.com/digero/maestro)
  is AGPL-3.0. Treat it as a spec reference for algorithms and data
  tables. Do not paste its source.
- JUCE's license tier depends on distribution; re-check before any
  public release.
