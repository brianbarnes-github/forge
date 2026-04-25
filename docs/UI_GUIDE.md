# `forge_ui` — UI Guide

A reference for the Forge GUI's components and their relationships, so we can talk about specific parts unambiguously when reporting bugs or proposing changes.

## Layout overview

```
┌──────────────────────────────────────────────────────────────────────┐
│ Title bar (JUCE-drawn; "Forge")                                     │
├──────────────────────────────────────────────────────────────────────┤
│ Menu bar              [File ▾]                                       │  ← MENU BAR (24 px)
├─────────────────────────────────────┬────────────────────────────────┤
│                                     │                                │
│  EDITOR PANE                        │  DIAGNOSTICS PANE              │
│                                     │                                │
│  ┌─ InstrumentsTree ─────────────┐  │  ┌─ DiagnosticListView ───┐   │
│  │  📄 Song title                  │  │  │                         │   │
│  │  ├─ 🎵 X:1 LuteOfAges  Lead     │  │  └─────────────────────────┘   │
│  │  │   ├─ 🎹 MIDI 0: Drums…       │  │  ──── INNER SPLITTER ────     │
│  │  │   └─ 🎹 MIDI 2: Guitar…      │  │  ┌─ AbcPreviewView ───────┐   │
│  │  └─ 🎵 X:2 Theorbo              │  │  │  (ABC text)             │   │
│  │      └─ 🎹 MIDI 1: Bass…        │  │  │                         │   │
│  └─────────────────────────────────┘  │  └─────────────────────────┘   │
│  ┌─ PropertyPageHost ────────────┐  │                                │
│  │   (shows Song / Instrument /    │  │                                │
│  │    Source page based on         │  │                                │
│  │    tree selection)              │  │                                │
│  └─────────────────────────────────┘  │                                │
│                                     │                                │
│  [ Run Converter ]                  │                                │
│                                     │                                │
└─────────────────────────────────────┴────────────────────────────────┘
                                     ↑
                            OUTER SPLITTER
                          (vertical, drag to resize)
```

## Naming reference

When you say…       …I'll know you mean

| Name in this guide              | Code class / file                              |
|---------------------------------|------------------------------------------------|
| **Main window**                 | `MainWindow` (`Source/UI/MainWindow.{h,cpp}`)  |
| **Menu bar**                    | `juce::MenuBarComponent` inside `MainWindow`   |
| **Body**                        | `MainWindow::Body` (inner class, holds the two panes + outer splitter) |
| **Outer splitter**              | `MainWindow::Body::Splitter` — vertical bar between Editor and Diagnostics panes |
| **Editor pane**                 | `EditorPane` (`Source/UI/EditorPane.{h,cpp}`)  |
| **Diagnostics pane**            | `DiagnosticsPane` (`Source/UI/DiagnosticsPane.{h,cpp}`) |
| **Inner splitter**              | `DiagnosticsPane::Body::HSplitterBar` — horizontal bar between Diagnostic List and ABC Preview |
| **InstrumentsTree**             | `Source/UI/InstrumentsTree.{h,cpp}` — the treeview itself |
| **SongItem / InstrumentItem / SourceItem** | Private inner classes of `InstrumentsTree` |
| **Song node**                   | The root `SongItem`; always one of them |
| **Instrument node**             | An `InstrumentItem` child of the Song node |
| **Source node**                 | A `SourceItem` child of an Instrument node |
| **PropertyPageHost**            | `Source/UI/PropertyPageHost.{h,cpp}` — the page switcher |
| **Song property page**          | `SongPropertyPage` — shown when the Song node is selected |
| **Instrument property page**    | `InstrumentPropertyPage` — shown for Instrument selection |
| **Source property page**        | `SourcePropertyPage` — shown for Source selection |
| **Run Converter button**        | `juce::TextButton` at the bottom of the Editor pane |
| **Diagnostic List View**        | `DiagnosticListView` (`Source/UI/DiagnosticListView.{h,cpp}`) — the 6-column table at the top of the Diagnostics pane |
| **ABC Preview View**            | `AbcPreviewView` (`Source/UI/AbcPreviewView.{h,cpp}`) — the read-only text editor showing the generated ABC |
| **Status line**                 | The grey `juce::Label` at the very bottom of the Diagnostics pane (`5,824 bytes · 184 bars · 3 parts`) |

## Field reference (Editor pane)

### Song property page (root selected)

| Field             | Control        | Config path        |
|-------------------|----------------|--------------------|
| Input MIDI        | read-only      | `Config::input`    |
| Output ABC        | read-only      | `Config::output`   |
| Title             | text           | `Config::title`    |
| Transcriber       | text           | `Config::transcriber` |
| Tempo (BPM)       | numeric        | `Config::tempo`    |
| Global transpose  | numeric        | `Config::transpose` |

### Instrument property page (Instrument selected)

| Field         | Control          | Config path                                |
|---------------|------------------|--------------------------------------------|
| X: index      | numeric          | `ConfigInstrument::x`                      |
| Name          | dropdown         | `ConfigInstrument::name`                   |
| Label         | text             | `ConfigInstrument::label`                  |
| Drum map      | text + Browse    | `ConfigInstrument::drumMap` (Drums only)   |

### Source property page (Source selected)

| Field                  | Control          | Source path                                     |
|------------------------|------------------|-------------------------------------------------|
| MIDI track (read-only) | label            | `ConfigSource::midiTrackIndex` + `Song.tracks[]` |
| Transpose semitones    | numeric          | `ConfigSource::transposeSemitones`               |
| Volume %               | numeric          | `ConfigSource::volumePercent` — adjustment in percent; `0` = no change, `+10` = +10 % louder, `-20` = -20 % quieter |

## Field reference (Diagnostics pane)

### Diagnostic List View

| Column   | Source                       |
|----------|------------------------------|
| Severity | `Diagnostic::severity` (Info / Warning / Error) — coloured dot + label |
| Source   | `Diagnostic::source` (e.g. `RangeConstraint`, `VolumeScale`, `Pipeline`) |
| Tick     | `Diagnostic::tick` (`--` if unset)         |
| Pitch    | `Diagnostic::pitch` (`--` if unset)        |
| Track    | `Diagnostic::trackIndex` (`--` if unset)   |
| Message  | `Diagnostic::message`                       |

### ABC Preview View

- **Editor area** — read-only monospaced text showing the generated ABC.
- **Status line** — bytes / bar count / part count.

## Menus

```
File
  Open MIDI…              Ctrl+O      ← FileChooser, .mid/.midi
  Open Config…            Ctrl+Shift+O ← FileChooser, .json/.toml/.xml
  ─────────
  Save Config As…
    JSON (.json)                       ← writeConfigToFile (JSON)
    TOML (.toml)                       ← writeConfigToFile (TOML)
    XML  (.xml)                        ← writeConfigToFile (XML)
  Save ABC As…                         ← writes the last Run's ABC output
                                         (greyed until Run Converter has
                                         produced something)
  ─────────
  Quit                                  ← systemRequestedQuit
```

(There is no Edit menu — Add / Delete actions live on the tree's right-click context menus.)

## Context menus

Right-click a tree node for the actions available to it. Left-click always
just selects the node (which swaps the property page).

| Node        | Right-click menu                                           |
|-------------|------------------------------------------------------------|
| Song        | `Add Instrument`; `Clear All Instruments` (disabled when empty; confirmation prompt before wipe) |
| Instrument  | `Add Source ▸` (submenu of unused MIDI tracks); `Delete Instrument` |
| Source      | `Delete Source`                                            |

## Drag-drop targets

The whole Main window is a drag-drop target. Drop:

- `.mid` or `.midi` → same as **File → Open MIDI…**
- `.json`, `.toml`, or `.xml` → same as **File → Open Config…**

## Data flow

```
[Open MIDI / drag-drop a .mid]
       │
       ▼
  MidiImporter::importMidi  ──►  raw Song (read-only after this point)
       │
       ▼
  synthesiseConfig          ──►  starter Config (one instrument per track)
       │
       ▼
  EditorPane::loadFromMidi
       │
       ▼
[user edits via tree selection + property pages; tree context menus
 mutate Config::instruments and Config::instruments[i].sources]
       │
       ▼
[Run Converter clicked]
       │
       ▼
  validateConfig
       │
       ▼
  assembleInstruments  ──►  assembled Song
       │
       ▼
  runPipeline                (Range → Chord → Duration → Tempo →
       │                      Collision → Dynamic → applyTempoCollapseToSongMaps)
       ▼
  writeAbc                  ──►  ABC text
       │
       ▼
  DiagnosticsPane.show (diagnostics, abcText)
```

## Bug-report shorthand

When something doesn't work, please reference the named region:

> "The **Run Converter button** doesn't respond."
> "The **inner splitter** is fixed at 50/50 and won't drag."
> "The **drum-map field** stays disabled even when I select Drums in the **Name** dropdown."
> "Right-clicking a **Source node** doesn't show the Delete Source menu."
> "The **Add Source** submenu is empty even though I have unused MIDI tracks."

That avoids any ambiguity about which of the half-dozen buttons / fields / tree nodes / tables we're talking about.
