# CLAUDE.md

Guidance for Claude Code (claude.ai/code) when working in this repository.

## Status: v0.1 CLI complete; Songsmith GUI through Phase 7 + polish; MIDI fidelity, the `.songsmith` Song file, source-MIDI playback (TinySoundFont, transport, mute/solo), track sections (split/move/resize/delete, `S`/`Delete`/Ctrl+A, multi-track selection), a Preferences dialog (`File → Preferences…`, two confirmation toggles, a Playback page to browse/clear the SoundFont, and Appearance and Editing pages for window-placement restore, default grid and playhead follow) MIDI import options (tempo map keep/replace, expand/merge tracks, Preferences ▸ Import) and instrument changes (segments on the track row, band menu, All-events merge) and the redesigned track head (colour picker, rename, mute/solo icons, saved playback volume) and the tempo/meter data layer (derived maps, `TempoEdit`, multi-meter ruler/grid) and its Conductor-row display (markers, labels, tempo tint) and the Tempo/Meter editor window (Edit menu, Conductor double-click / head menu) and its tempo graph and meter strip (phase 4) and dockable MIDI-editor / LOTRO-preview / Parts panels (fixed-width part cards) (dock or float, View menu, layout remembered in AppSettings) and the dockable Mixer panel (per-track pan override, volume, mute/solo, session-only master gain) and the loop range (band drag, ruler brackets, sample-accurate engine wrap, Esc to clear) and transport controls (loop selection, meter/tempo at the playhead, playback rate) and the transport strip's user-chosen command buttons and the resizable track-head column (divider drag, 200–400 px, `trackList.headWidth`) and the MIDI editor panel's own menu bar (File / Edit / Navigate / Options / View; File → Export to New MIDI File (conductor + that track) and Close Editor; Navigate → Move Marker Left / Right by Grid (arrow keys) and Move Up / Down a Semitone (Up/Down; a canvas click picks the highlighted active row, which follows a dragged note) and Select Note Nearest Cursor / Add Note Nearest Cursor to Selection, and Select Previous / Next Note (any pitch, chords lowest first, marker moves to the note's start) and, with exactly one note selected, Select Previous / Next / All Notes of the Same Pitch, and with a selection Add Previous / Next Note to Selection (marker follows) and Move Cursor to Start of Selection; Options → Snap to Grid, shared with Preferences ▸ Editing, snaps the editor's marker clicks, note moves and resizes and double-click inserts (Alt for free), and Options → Snap to Key ▸ Enabled / Root / Type (scales and Chords ▸), session-only and starting from the song key, refuses double-click note creation off the chosen scale or chord (`ScaleSnap`), and Options → Note Preview (insert / edit / velocity checkboxes, saved; a live preview path on reserved synth channels), and Options → Grid Length (Off, 1/1–1/64) / Grid Division (Straight, Triplet, Dotted, Swing, a REAPER-style swung grid with a strength slider on the bottom bar — pairs restart per bar), session-only, set the editor's grid (`GridLength.h`) and draw its lines in the roll; Edit ▸ Grid Size sets the length and keeps the division) and the MIDI editor's fixed bottom bar (`EditorBottomBar`, strip-high, holding the Swing strength slider) and the restyle (charcoal/copper palette, `SongsmithLookAndFeel`, `ThemePaint` notes/headers/logo, copper playhead with ruler cap, ivory marker/loop) and the detected key on the Conductor head (`KeyDetector`, Krumhansl-Schmuckler, set per MIDI import; Song ▸ Set Key detects again or picks one of the 24 keys; saved as `SOURCE_MIDI.songKey`) implemented

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
| `docs/superpowers/specs/2026-10-03-songsmith-playback-design.md` | Design spec for Songsmith playback (source MIDI through a SoundFont, shared transport/playhead, mute/solo); code walkthrough: `docs/ARCHITECTURE.md` §9.14 |
| `docs/superpowers/specs/2026-10-08-songsmith-preferences-design.md` | Design spec for the Preferences dialog and `AppSettings`; code walkthrough: `docs/ARCHITECTURE.md` §9.16 |
| `docs/superpowers/specs/2026-10-09-songsmith-preferences-hardening-design.md` | Design spec for Preferences hardening (dialog lifetime, settings-save failure notice, minimum size, `validateLoaded` SECTION checks, import dialog hidden before importing); code walkthrough: `docs/ARCHITECTURE.md` §9.13 (SECTION validation), §9.16 (Preferences lifetime, notice, minimum size) and §9.17 (import dialog) |
| `docs/superpowers/specs/2026-10-09-songsmith-playback-preferences-design.md` | Design spec for the Preferences ▸ Playback page (SoundFont Browse/Clear, `PreferencesServices`, bundled `SongSmith.sf2` rename); code walkthrough: `docs/ARCHITECTURE.md` §9.16 |
| `docs/superpowers/specs/2026-10-09-songsmith-appearance-editing-preferences-design.md` | Design spec for the Preferences ▸ Appearance and Editing pages (window-placement restore, default grid, playhead follow; `applyViewSettings`); code walkthrough: `docs/ARCHITECTURE.md` §9.16 |
| `docs/superpowers/specs/2026-10-08-songsmith-import-options-design.md` | Design spec for MIDI import options (tempo map keep/replace, expand/merge tracks, Preferences ▸ Import); code walkthrough: `docs/ARCHITECTURE.md` §9.17 |
| `docs/superpowers/specs/2026-10-10-songsmith-instrument-changes-design.md` | Design spec for instrument (Program Change) segments on the track row, the band right-click menu (auto split, set track instrument) and the Notes-only / All-events cross-track Move/Copy preference; code walkthrough: `docs/ARCHITECTURE.md` §9.15 and §9.16 |
| `docs/superpowers/specs/2026-10-10-songsmith-track-head-design.md` | Design spec for the redesigned track head (200 px two-row `TrackHeadComponent`: colour-picker swatch, right-click rename, drawn mute/solo icons, saved per-track `playbackVolume` slider; note-count line removed); code walkthrough: `docs/ARCHITECTURE.md` §9.10 (head, in-place refresh), §9.13 (`playbackVolume`) and §9.14 (snapshot gain) |
| `docs/superpowers/specs/2026-10-10-songsmith-merge-tracks-design.md` | Design spec for Alt-drag merging of a track's notes into another track (`NoteMerge`, merge gesture); code walkthrough: `docs/ARCHITECTURE.md` §9.15 |
| `docs/superpowers/specs/2026-10-10-songsmith-tempo-meter-design.md`, `docs/superpowers/plans/2026-10-10-songsmith-tempo-meter-phase1.md` | Design spec and phase 1 plan for tempo/meter editing (tempo and meter live only in the conductor's events, `TEMPO_MAP`/`METER_MAP` derived, `TempoEdit` data layer, multi-meter ruler/grid/Rewind One Bar); phase 2 plan `docs/superpowers/plans/2026-10-11-songsmith-tempo-meter-phase2.md` (Conductor row markers, labels and tempo tint); phase 3 plan `docs/superpowers/plans/2026-10-11-songsmith-tempo-meter-phase3.md` (the Tempo/Meter editor window); phase 4 plan `docs/superpowers/plans/2026-10-11-songsmith-tempo-meter-phase4.md` (tempo graph and meter strip); code walkthrough: `docs/ARCHITECTURE.md` §9.18 |
| `docs/superpowers/specs/2026-10-11-songsmith-dock-panels-design.md`, `docs/superpowers/plans/2026-10-11-songsmith-dock-panels.md` | Design spec and plan for dockable panels (`Source/UI/Dock/`, `TrackEditorPanel`, `dock.layout` setting, View menu items); code walkthrough: `docs/ARCHITECTURE.md` §9.19 |
| `docs/superpowers/specs/2026-10-12-songsmith-mixer-design.md`, `docs/superpowers/plans/2026-10-12-songsmith-mixer.md` | Design spec and plan for the Mixer panel (`Source/UI/Mixer/`, `playbackPan`, the engine pan override and master gain, `MixerTarget` seam); code walkthrough: `docs/ARCHITECTURE.md` §9.13, §9.14 and §9.19 |
| `docs/superpowers/specs/2026-10-12-songsmith-loop-range-design.md`, `docs/superpowers/plans/2026-10-12-songsmith-loop-range.md` | Design spec and plan for the loop range (`LoopRange`/`LoopSpanCell`, `PlaybackController::setLoopRange`, the engine's sample-accurate wrap, `LoopRangeOverlay`, ruler brackets, band drag, `escapePressed`); code walkthrough: `docs/ARCHITECTURE.md` §9.14 (and §9.10 for the band drag) |
| `docs/superpowers/specs/2026-10-14-songsmith-transport-controls-design.md`, `docs/superpowers/plans/2026-10-14-songsmith-transport-controls.md` | Design spec and plan for the transport strip's right area (`TransportControls`, `TransportText`, `RateMapping`; Selection boxes, meter/tempo at the playhead, session-only rate knob, the engine's clock scaling); code walkthrough: `docs/ARCHITECTURE.md` §9.20 (and §9.14 for the rate) |
| `docs/superpowers/specs/2026-10-14-songsmith-strip-buttons-design.md`, `docs/superpowers/plans/2026-10-14-songsmith-strip-buttons.md` | Design spec and plan for the transport strip's user-chosen command buttons (`StripButtons` data and `strip.buttons` setting, `stripCommands` in `MenuModel.h`, `StripButtonsHost` / `StripButtonsBar` with `+` picker and `»` overflow, layout buttons following Manage Layouts renames/deletes); code walkthrough: `docs/ARCHITECTURE.md` §9.20 |
| `docs/superpowers/specs/2026-10-15-songsmith-restyle-design.md`, `docs/superpowers/plans/2026-10-15-songsmith-restyle.md` | Design spec and plan for the restyle (palette retune, `SongsmithLookAndFeel`, `ThemePaint`, `PianoRollPalette.h`, copper playhead glow and ruler cap, ivory marker/loop, `PlayButton`, anvil logo and window icon); code walkthrough: `docs/ARCHITECTURE.md` §9.21 |
| `docs/superpowers/specs/2026-10-16-songsmith-note-preview-design.md`, `docs/superpowers/plans/2026-10-16-songsmith-note-preview.md` | Design spec and plan for the MIDI editor's Options ▸ Note Preview (`NotePreview.h`, `PreviewRequest`, the engine's FIFO and reserved preview channels 256–261, `SourceRollEditor::setNoteChangeListener`, `editing.previewOn*` settings and the Editing page toggles); code walkthrough: `docs/ARCHITECTURE.md` §9.14, the TrackEditorPanel bullet and §9.16 |
| `docs/superpowers/specs/2026-10-16-songsmith-editor-swing-design.md`, `docs/superpowers/plans/2026-10-16-songsmith-editor-swing.md` | Design spec and plan for the MIDI editor's Swing grid (`EditorGrid`, swung lines, the bottom bar's Swing slider); code walkthrough: `docs/ARCHITECTURE.md` §9.19 |
| `docs/superpowers/specs/2026-10-06-songsmith-sections-design.md` | Design spec for track sections (split/move/resize/delete, multi-track selection); code walkthrough: `docs/ARCHITECTURE.md` §9.15 |
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
instead. The File menu is now New / Open… / Save / Save As… /
Import ▸ MIDI… / Export ▸ MIDI…, ABC… / Preferences… / Quit over a binary `.songsmith`
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
/mnt/c/Apps/SongSmith/ && mkdir -p /mnt/c/Apps/SongSmith/resources && cp
resources/soundfonts/SongSmith.sf2 /mnt/c/Apps/SongSmith/resources/` (the SoundFont is a local-only file; first-time
setup is in `docs/BUILD.md`).

## Git

Atomic commit history starting from `1f24708`. Conventional-commit
prefixes (`feat:`, `fix:`, `refactor:`). Never push without explicit
approval.

## Licensing guardrails

- JUCE's license tier depends on distribution; re-check before any
  public release.
- `SongSmith.sf2` is the GPL-2 TimGM6mb bank renamed (md5
  `1f1ad87ae6f87033d9a591eca567d919`) and is never committed or shipped by CI — it is
  a git-ignored local file under `resources/soundfonts/` copied next to
  the exe by a CMake post-build step. TinySoundFont (MIT) is vendored at
  `Source/ThirdParty/tinysoundfont/`.
- `drummaps/*.txt` are AGPL-licensed reference data only — see
  `docs/REFERENCE.md`.
