# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Status: v0.1 CLI prototype under construction

Scope is a **CLI MIDI → LOTRO ABC converter**, no GUI. The project is defined by `lotro-abc-converter-spec.md` (560 lines). **Read the spec in full before making implementation decisions** — the constraint rules in §2 (note range, chord size, duration quantization, tempo collapse, drum mapping) are non-negotiable because LOTRO will reject ABC that violates them. Follow spec §10 implementation order, minus the GUI step.

## Target platform and toolchain

- **v0.1 prototype builds natively on WSL/Linux with GCC.** Spec §1/§4.3 name Windows + MSVC; both are overridden. MSVC was rejected outright. MinGW-w64 cross-compile was tried and abandoned because JUCE explicitly does not support MinGW (`juce_TargetPlatform.h` has `#error "MinGW is not supported"` and `JUCE_64BIT` is gated on `_MSC_VER`). Native Linux builds clean.
- Output `.abc` is plain text and platform-agnostic, so the conversion math is fully provable on Linux. Windows packaging is deferred.
- CMake ≥ 3.22, C++17, Ninja.
- JUCE (submodule at `./JUCE`) — linked modules: `juce_audio_basics`, `juce_audio_formats` (for `juce::MidiFile`), `juce_core`. Console app via `juce_add_console_app` — no GUI modules.
- Catch2 (submodule at `./Tests/Catch2`, tracking `devel`).
- `cmake/mingw-w64-toolchain.cmake` exists but is currently unused. Kept for reference. If we ever revisit Windows cross-compile, evaluate `llvm-mingw` + clang before retrying GCC-mingw.

## Build / test commands

From the repo root:

```bash
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure
```

Converter binary: `build/converter_artefacts/Debug/converter`.

Run a single Catch2 test: `ctest --test-dir build -R <test_name> --output-on-failure`, or invoke `build/Tests/converter_tests` directly with `"[tag]"` / `"test name"`.

First-time clone only: `git submodule update --init --recursive`.

## Architecture

Pipeline (spec §3.2): `MidiImporter` → `RangeConstraint` → `DurationConstraint` → `TempoCollapse` → `ChordConstraint` → `CollisionGuard` → `DynamicMapper` → `AbcWriter`. **Constraint order is load-bearing** — do not reorder without re-running all tests.

Only `MidiImporter` touches `juce::MidiFile`; every constraint takes a `Track&` and mutates it in place. `Source/Core/` contains all conversion logic; `Source/Cli/` and `Source/Main.cpp` are the thin CLI wrapper.

## CLI shape (v0.1)

```
converter [OPTIONS] INPUT.mid [OUTPUT.abc]
  --instrument N=NAME   Assign instrument to track N (repeatable)
  --tempo BPM           Override detected main tempo
  --transpose N         Global semitone transpose (pre range-clamp)
  --list-tracks         Print track table and exit
  --list-instruments    Print valid instrument NAME values and exit
  -v, --verbose         Log constraint warnings to stderr
```

Defaults: all tracks → `LuteOfAges`; MIDI channel-10 tracks auto-detect to `Drums`. Output path defaults to `<input-stem>.abc` next to the input.

## Reference material in this repo

- `drummaps/default.txt`, `drums.txt`, `cymbals.txt` — **Maestro-format drum maps, AGPL-licensed reference data only**. Read for GM → LOTRO drum-note mappings; do not ship verbatim. The spec §2.6 table is hardcoded in `Source/Core/DrumMap.cpp` for v0.1.
- `midi/Barnes Brothers Band - Pull The Wires.mid` — end-to-end fixture. Tiny unit-test MIDIs live under `Tests/fixtures/`.

## Licensing guardrails

- **Maestro** (github.com/NikolaiVChr/maestro, github.com/digero/maestro) is AGPL-3.0. Treat it as a spec reference for algorithms and data tables. Do not paste its source.
- JUCE's license tier depends on distribution; re-check before any public release.
