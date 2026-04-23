# Config-editor UI (`converter_ui`) — design

**Status:** Design accepted; implementation pending.
**Author:** Brian Barnes, with Claude, during a 2026-04-23 brainstorming session.

## Summary

Add a second binary `converter_ui` alongside `converter` that provides a basic graphical editor for converter config files. The UI lets a tester drag a MIDI onto the window, automatically synthesises a starter config from that MIDI's tracks, and provides a master-detail editor for tweaking each output instrument (X: index, LOTRO instrument name, label, sources, transpose, volume, drum-map). A `Run Converter` button calls the conversion in-process and surfaces the structured `Diagnostic` list and the generated ABC text in a right-hand pane. Configs save in any of the three supported formats (JSON, TOML, XML).

## Goals

- A self-contained tool for the project owner and a small cohort of testers (~100 people running ~10 MIDIs each) to exercise the converter against real songs and report bugs that lab tests would miss.
- Tight tweak-and-rerun loop: edit the config, click Run, see diagnostics + ABC immediately, repeat.
- Cross-platform-portable design (Linux/WSL build now; Windows build later).

## Non-goals

- Production-quality polish (animations, theming, accessibility audits, i18n).
- Replacement of the CLI as the primary conversion entry point — the CLI stays the source of truth; the UI is a wrapper.
- A full DAW-style MIDI editor. The UI does not edit notes, only the conversion config.
- Distribution mechanisms (installer, packaging, auto-update) — out of scope for v0.1; testers run from a build directory.
- A `--dump-config` style CLI generator. The UI replaces that need for the cohort.

## Guiding principles inherited

- **MIDI is the source of truth.** The UI presents the MIDI's actual tracks (count, name, channel, note-count) when populating the editor. It does not hide MIDI detail behind heuristics.
- **Diagnostics are the contract.** The diagnostics pane shows full structured `Diagnostic` records exactly as the converter emits them (severity, source, message, tick, pitch, sourceTrackIndex, sourceEventIndex). The UI does not reinterpret or filter what the converter says happened.
- **Forced decisions only.** The UI does not add editorial steps the converter wouldn't have performed.

## Platform and toolkit

- **Toolkit: JUCE GUI** (`juce_gui_basics`, `juce_gui_extra` for `FileDragAndDropTarget`).
  - Already in the toolchain; zero new dependencies.
  - Allows in-process calls to `converter_core` — no subprocess, no temp files, no stderr parsing. The `Diagnostic` vector reaches the UI as native objects.
  - Cross-platform free (Linux/Windows/macOS from the same code).
- **Build target: Linux/WSL with GCC.** Windows packaging deferred (matches the CLI's posture per CLAUDE.md).
- **Linux runtime deps for the GUI target only:** `libfreetype6-dev`, `libfontconfig1-dev`, `libx11-dev`, `libxrandr-dev`, `libxinerama-dev`, `libxcursor-dev`, `libasound2-dev`. The CLI build does not need any of these — JUCE GUI modules link only into the new target.

## Architecture

### Build targets

```
converter_core (static lib, JUCE-free public API)
       ▲                ▲                ▲
       │                │                │
converter        converter_tests    converter_ui   (NEW — links GUI modules)
   (CLI)            (Catch2)        (JUCE GUI app)
```

`converter_ui` links `converter_core` plus `juce_gui_basics` and `juce_gui_extra`. The CLI and test binaries do not change.

### In-process conversion path

The Run button calls into `converter_core` directly. Workflow:

```
File → Open MIDI ──► MidiImporter::importMidi
                           │
                           ▼
                     synthesiseConfig    (extracted from Main.cpp into Core)
                           │
                           ▼
                     EditorPane.config  ◄────  user edits via the three sub-views
                           │
                           ▼
              [Run Converter clicked]
                           │
                           ▼
        validateConfig → assembleInstruments → runPipeline → writeAbc
                           │                   (extracted from Main.cpp into Core)
                           ▼
                  (diagnostics, abcText)
                           │
                           ▼
                    DiagnosticsPane.show(...)
```

### Window shape

Single window with two top-level panes split by a draggable splitter. Editor on the left, diagnostics on the right.

```
┌────────────────────────────────────────────────────────┐
│ File  Edit                                             │
├──────────────────────────┬─────────────────────────────┤
│ Editor (left pane)       │ Diagnostics (right pane)    │
│                          │                             │
│  Global settings         │  DiagnosticListView         │
│  ──────────────────────  │  ┌──────────────────────┐   │
│  InstrumentsTable        │  │ severity│source│msg  │   │
│  ┌────────────────────┐  │  └──────────────────────┘   │
│  │ X │ Name    │Sources│ │  ────────── splitter ────── │
│  │ 1 │ Lute    │ 0, 2  │ │  AbcPreviewView             │
│  │ 2 │ Theorbo │ 1     │ │   (read-only monospace)     │
│  └────────────────────┘  │                             │
│  [+ Add] [- Remove]      │                             │
│                          │                             │
│  InstrumentDetailForm    │                             │
│   (form for selected)    │                             │
│                          │                             │
│  [ Run Converter ]       │                             │
└──────────────────────────┴─────────────────────────────┘
```

The splitter between the diagnostic list and the ABC preview is also draggable.

### Empty state

App launches with no MIDI loaded. Editor pane is mostly disabled (greyed-out fields, empty instruments table, disabled Run button). A central message reads:

> *Drop a MIDI file here, or use File → Open MIDI…*

Once a MIDI is loaded, the editor enables.

## Components

Five distinct units inside `converter_ui`. Each has a single responsibility.

### `MainWindow` (`juce::DocumentWindow`)

The top-level window. Owns the menu bar, the splitter layout, instances of `EditorPane` and `DiagnosticsPane`, and the Run callback wiring. Implements `juce::FileDragAndDropTarget` to receive dropped files.

### `EditorPane` (`juce::Component`)

The left pane. Holds `GlobalSettingsView`, `InstrumentsTable`, `InstrumentDetailForm`, and the `Run Converter` button. Owns the in-memory `lotro::Config` and the in-memory `lotro::Song` (the imported raw MIDI). All sub-views read from and write to that one `Config` instance via callbacks. Tracks a dirty-bit (set on any change, cleared on Save / Open).

### `InstrumentsTable` (`juce::TableListBoxModel`)

Renders the table of instruments with columns: X, Name, Label, Sources. Selecting a row drives which instrument the `InstrumentDetailForm` shows. Provides Add and Remove via the buttons below it. Sources column displays a comma-separated list of MIDI track indices for visibility into which instruments are merges.

### `InstrumentDetailForm` (`juce::Component`)

Editor for a single `ConfigInstrument`:

- `name` — dropdown of valid LOTRO instrument names from `allInstrumentNames()`.
- `label` — text field.
- `sources` — multi-select list of the loaded MIDI's tracks, labelled with their MIDI track name, channel, and note count, e.g. `"0: Drums (chan 10, 142 notes)"`. Checking 2+ boxes is how you create a merge.
- `transposeSemitones` — integer input.
- `volumePercent` — integer input (≥ 1).
- `drumMap` — file path with a Browse button, **enabled only when** `name == "Drums"`.

Edits push back into the underlying `ConfigInstrument` and flip the dirty bit.

### `DiagnosticsPane` (`juce::Component`)

The right pane. Holds `DiagnosticListView` on top and `AbcPreviewView` below, separated by a draggable splitter. `show(diagnostics, abc)` replaces the content of both views (replace, not append — see "Run lifecycle" below).

#### `DiagnosticListView`

`juce::TableListBox` columns: Severity, Source, Tick, Pitch, Track, Message.

- Severity column shows a coloured marker: red dot for `Error`, yellow for `Warning`, blue for `Info`.
- Tick / Pitch / Track render `—` for unset (negative-one) fields.
- Click a column header to sort.
- Empty list shows: *"No diagnostics from the last run."*

#### `AbcPreviewView`

`juce::TextEditor`, read-only, monospaced, multi-line, no word-wrap, horizontal scrollbar on. Standard select / copy / Ctrl+F. Status line at the bottom shows summary stats: bytes generated, bar count (`% bar` markers), part count (`X:` markers).

Example status line: *"5,824 bytes · 184 bars · 3 parts"*

### What is deliberately NOT a separate component

- File menu actions are direct `MainWindow` methods — no `FileController` abstraction.
- `Config` is owned by `EditorPane` directly — no `ConfigViewModel` indirection.
- No event bus / signal router. Sub-views call back to `EditorPane` via lambdas.

YAGNI: layered architecture would slow iteration on a single-window personal tool.

## Refactor: extract `synthesiseConfig` and `runPipeline` into Core

`Source/Main.cpp` currently contains both helpers. The UI needs both. To avoid duplication, move them into a new Core helper unit:

- `Source/Core/Pipeline.h` — declares `synthesiseConfig(opts, raw)` and `runPipeline(song, diagnostics)`.
  - `synthesiseConfig` keeps its current signature with one tweak: instead of taking `lotro::CliOptions`, it takes the data fields it actually needs (`std::optional<double> tempo, int transpose, std::map<int, LotroInstrument> instrumentOverrides`) so the UI doesn't depend on `Cli/CliOptions.h`. CLI-side wraps in a one-line adapter.
- `Source/Core/Pipeline.cpp` — implementation lifted from `Main.cpp`.
- `Source/Main.cpp` — uses the new helpers; CLI behavior unchanged.

This is a small refactor, justified by reuse, no behavior change. Pinned by the existing `EndToEnd_tests.cpp` and `EndToEndConfig_tests.cpp`.

## File operations

### Menus

```
File
  Open MIDI…              Ctrl+O
  Open Config…             Ctrl+Shift+O
  ─────────
  Save Config              Ctrl+S
  Save Config As…          Ctrl+Shift+S
    JSON (.json)
    TOML (.toml)
    XML  (.xml)
  ─────────
  Quit                     Ctrl+Q

Edit
  Add Instrument           Ctrl+N
  Remove Selected          Ctrl+Backspace
```

Two menus only. No `Help`, `View`, etc.

### File dialogs

JUCE's `juce::FileChooser`. Save As submenu items each open a chooser pre-filtered to one extension; the format the file saves in is determined by which menu item you picked, not by the typed extension. This sidesteps the "user typed `.json` but wanted `.toml`" ambiguity.

Open Config auto-detects format from the file extension via the existing `ConfigLoader::detectFormat`.

### Drag-and-drop

`MainWindow` implements `juce::FileDragAndDropTarget`. When a `.mid` / `.midi` file is dropped: treat as `File → Open MIDI…`. When a `.json` / `.toml` / `.xml` is dropped: treat as `File → Open Config…`. Multiple files: first one only. Highlight the editor pane during a hover.

### Unsaved-changes warning

Loading a new MIDI / config or quitting while the dirty bit is set pops a "Discard unsaved changes?" confirm dialog (`juce::AlertWindow` or `juce::NativeMessageBox`).

### Save writers

The Save flow needs to **write** Configs in JSON / TOML / XML. The existing `ConfigLoader` only reads. New `ConfigWriter` unit:

- `Source/Core/ConfigWriter.h` — declares `writeConfigToFile(path, format, config) → string` (empty on success, error message otherwise) and `writeConfigToString(format, config, out) → string`.
- `Source/Core/ConfigWriter.cpp` — three writers (`writeJson`, `writeToml`, `writeXml`), each producing canonical output of the same data model the loader reads.

Each writer gets a save-then-load round-trip test.

## Run lifecycle

1. Click `Run Converter`.
2. Editor pane disables briefly (~1s on a typical song); spinner appears next to the button.
3. UI runs on the JUCE message thread — conversion is millisecond-fast for typical songs; not worth a worker thread until proven needed.
4. Steps performed:
   1. `validateConfig(cfg, raw.tracks.size())` — on error, populate diagnostics with a single Error row, clear the ABC preview, return.
   2. `assembleInstruments(raw, cfg, diagnostics)` — produces the assembled Song.
   3. `runPipeline(song, diagnostics)` — runs the constraint pipeline + `applyTempoCollapseToSongMaps`.
   4. `writeAbc(song)` — produces the ABC text.
5. `DiagnosticsPane.show(diagnostics, abcText)` replaces both views' content. Editor re-enables.
6. Status line at the bottom of the diagnostics list briefly flashes: *"Run at 14:32:05 · 47ms"*.

### Failure modes

- **Validation error:** no Run; single Error row in diagnostics; ABC cleared.
- **Pipeline exception** (defensive only — shouldn't happen): caught at the Run handler; surfaced as Error diagnostic with the exception message.
- **MIDI re-import error** (file moved/deleted between Open and Run): same single-Error pattern.

## Testing

Calibrated to the audience: the UI is a focused tool, not a polished product. Tests catch the regressions that would mislead a tester, not exhaustive coverage.

### Unit tests (extends `converter_tests`)

- **`synthesiseConfig` post-extraction:** given a Song with N tracks, produces a Config with N instruments, x in `1..N`, auto-picked instrument names, instrument overrides honoured. Pins behavior during the Main.cpp → Core extraction.
- **Save round-trips:** for each of JSON, TOML, XML, build a Config in-memory → `writeConfigToString` → `loadConfigFromString` → assert the loaded Config matches the original. Catches loader/writer asymmetries.

### What we don't test automatically

- Widget rendering, layout, drag-and-drop behavior, button-disable affordances — manual smoke test on real MIDIs is sufficient.
- Cross-platform behavior (Windows) — out of scope until that build target is taken on.

### Smoke test checklist (manual)

- Build cleanly on a fresh WSL after installing the GUI dev packages.
- Drop `midi/Barnes Brothers Band - Pull The Wires.mid` on the window; auto-config populates with the expected number of instruments.
- Click Run; diagnostics appear, ABC preview shows the expected first part.
- Save the config in JSON, TOML, XML; reopen each; editor state matches.
- Toggle a Drums instrument's drumMap field; verify it disables for non-Drums and enables back when reset.
- Tweak `volumePercent`, click Run, see the dynamic markings change.

## Out of scope (deferred)

- Diagnostic filtering UI (filter by severity / source). Sortable columns cover the common case.
- Run history (each Run replaces).
- "Save Output ABC" file menu — copy from the preview is sufficient.
- Jump-to-source from a diagnostic into a future MIDI editor — that's a different project.
- Worker-thread Run for long songs — add when proven needed.
- Theme / preferences / settings — single-window personal tool; nothing to configure.
- Windows packaging.

## File plan

### New files

| Path | Responsibility |
|---|---|
| `Source/UI/MainWindow.h` / `.cpp` | Window + menu + splitter + drag-drop |
| `Source/UI/EditorPane.h` / `.cpp` | Left pane container; owns Config and Song |
| `Source/UI/GlobalSettingsView.h` / `.cpp` | Top-of-editor form for top-level Config fields |
| `Source/UI/InstrumentsTable.h` / `.cpp` | TableListBoxModel for the instruments list |
| `Source/UI/InstrumentDetailForm.h` / `.cpp` | Detail form for the selected instrument |
| `Source/UI/DiagnosticsPane.h` / `.cpp` | Right pane container |
| `Source/UI/DiagnosticListView.h` / `.cpp` | TableListBox for the Diagnostic list |
| `Source/UI/AbcPreviewView.h` / `.cpp` | Read-only TextEditor for the generated ABC |
| `Source/UI/Main.cpp` | UI binary entry point (small JUCE app boilerplate) |
| `Source/Core/Pipeline.h` / `.cpp` | Extracted `synthesiseConfig` + `runPipeline` |
| `Source/Core/ConfigWriter.h` / `.cpp` | Save Configs in JSON / TOML / XML |
| `Tests/ConfigWriter_tests.cpp` | Round-trip tests for each format |
| `Tests/Pipeline_tests.cpp` | `synthesiseConfig` pin-test |

### Modified files

| Path | Change |
|---|---|
| `CMakeLists.txt` | New `juce_add_gui_app(converter_ui …)` target linking `converter_core` + `juce_gui_basics` + `juce_gui_extra`. |
| `Source/Main.cpp` | Use the extracted `Pipeline.h` helpers; remove duplicated `synthesiseConfig` / `runPipeline` definitions. |
| `Tests/CMakeLists.txt` | Add `ConfigWriter_tests.cpp` and `Pipeline_tests.cpp`. |
| `CLAUDE.md` | Add a short paragraph in the Architecture section describing the new UI binary and its build dependencies. |

## Open questions

None at spec completion. Anything ambiguous surfaced during implementation should be captured back here as a delta.

## Rollout

Phase-1 (single commit series):

1. Extract `synthesiseConfig` and `runPipeline` into `Source/Core/Pipeline.{h,cpp}`. Update `Main.cpp` to use them. Add a focused unit test. Verify `EndToEnd*` tests still pass.
2. Add `ConfigWriter` (`writeJson` / `writeToml` / `writeXml`) with round-trip tests.
3. Wire the new `converter_ui` CMake target with a minimal "Hello UI" window. Verify it builds and launches.
4. Implement `MainWindow`, `EditorPane`, splitter, menus.
5. Implement `InstrumentsTable` and `InstrumentDetailForm` against a synthetic `Song`.
6. Wire MIDI loading + auto-`synthesiseConfig` to populate the editor.
7. Implement `DiagnosticsPane` (`DiagnosticListView` + `AbcPreviewView`).
8. Wire Run → `assembleInstruments` → `runPipeline` → `writeAbc` → display.
9. Implement file dialogs (Open MIDI, Open Config, Save As) and drag-drop.
10. Implement dirty-bit + unsaved-changes warning.
11. Update `CLAUDE.md`.
