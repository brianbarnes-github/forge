# CLAUDE.md

Guidance for Claude Code (claude.ai/code) when working in this repository.

## Status: v0.1 CLI complete; Songsmith GUI through Phase 7 + polish; MIDI fidelity and the `.songsmith` Song file implemented

This repo (**Forge**) is a MIDI → LOTRO ABC converter shipped as a CLI
(`forge`) and a JUCE GUI (`forge_ui`, the **Songsmith** MIDI editor —
binary `song-smith`), both thin wrappers around a static library
(`forge_core`) that is JUCE-free at its public surface.

**Core/UI boundary:** editing, timing and display logic (quantize, snap,
selection, layout) lives in `Source/UI/`; `Source/Core/` only gets what
the conversion pipeline itself needs. Check the `forge-engine-ui-boundary`
skill before touching `Source/Core/`.

The original spec is `lotro-abc-converter-spec.md` (560 lines). Some of
its design decisions have been revised in practice — see
`docs/DELTAS_FROM_SPEC.md` before assuming a section is still
authoritative.

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

## Docs map

| Doc | Covers |
|---|---|
| `docs/ARCHITECTURE.md` | Build targets, source layout, full pipeline/emission-model walkthrough, CLI internals, GUI internals, end-to-end data flow |
| `docs/UI_GUIDE.md` | GUI named regions, field → Config-path table, context menus |
| `docs/BUILD.md` | Toolchain, platform rationale, dependencies, Windows CI packaging |
| `docs/DELTAS_FROM_SPEC.md` | Where the implementation intentionally diverges from `lotro-abc-converter-spec.md` |
| `docs/REFERENCE.md` | Drum-map source data, MIDI/ABC test fixtures, config-schema spec doc |
| `docs/TESTING.md` | Test count and what the notable test files pin down |
| `docs/superpowers/specs/2026-10-03-songsmith-song-file-design.md` | Design spec for the `.songsmith` Song file, session/dirty tracking and the unsaved-changes guard (code walkthrough: `docs/ARCHITECTURE.md` §9.13) |
| `docs/songsmith-ui-map.html`, `docs/Songsmith Arch.md`, `docs/Songsmith UI Guide.html` | Clickable `#N` map companion to `UI_GUIDE.md`; original Songsmith design notes and mock-up |

## Build / test commands

From the repo root:

```bash
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure
```

CLI binary: `build/forge_artefacts/Debug/forge`. UI binary:
`build/forge_ui_artefacts/Debug/song-smith` (or `./run-ui.sh`); it launches
into the Songsmith view (source region, part strip, LOTRO preview region
with range-band/ghost/dropped-note overlays, diagnostics) — the classic
Config-editing UI (`EditorPane`/`InstrumentsTree`/`PropertyPageHost` +
property pages) was deleted at the end of Phase 6; `View → Export ABC
panel` now toggles a separate full-export diagnostics/ABC-preview panel
instead. The File menu is now New / Open… / Close / Save / Save As… /
Import ▸ MIDI… / Export ▸ MIDI…, ABC… / Quit over a binary `.songsmith`
Song file; "Open Config" / "Save Config As" are gone. Do not launch the GUI from subagents; the user sees every
window. Single test: `ctest --test-dir build -R <name>
--output-on-failure`. First clone: `git submodule update --init
--recursive`. Details, toolchain rationale, and Windows CI packaging:
`docs/BUILD.md`.

**Local Windows cross-compile** (clang-cl + xwin + ciopfs + Wine — for
Songsmith GUI/pixel-comparison test work that Linux can't verify):
`./setup-windows-toolchain.sh` once per machine, then
`./build-windows.sh [forge|forge_ui|forge_tests|all]`. Dev-loop
accelerant only — `windows-2022` CI (real MSVC) is still the release
gate. Full details: `docs/BUILD.md`.

**Deploy for the user's manual testing:** `./build-windows.sh forge_ui &&
cp build-windows/forge_ui_artefacts/Release/song-smith.exe
/mnt/c/Apps/SongSmith/`.

## Git

Atomic commit history starting from `1f24708`. Conventional-commit
prefixes (`feat:`, `fix:`, `refactor:`). Never push without explicit
approval.

## Licensing guardrails

- JUCE's license tier depends on distribution; re-check before any
  public release.
- `drummaps/*.txt` are AGPL-licensed reference data only — see
  `docs/REFERENCE.md`.
