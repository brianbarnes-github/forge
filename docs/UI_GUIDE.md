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

(This is the classic editor's menu content; Phase 4 added an Edit/Song/View
menu set that's global across both modes — see "Songsmith (preview)" below.
In the classic tree, Add/Delete actions still live on the tree's right-click
context menus, not on Edit.)

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

## Songsmith (preview)

`View → Classic editor` (unchecked by default) toggles `MainWindow::Body`
between this view and the classic Editor/Diagnostics split described above.
**Songsmith is the default on launch** — checking "Classic editor" switches
back to the layout above; the classic layout stays fully functional (it is
only removed at the end of Phase 6).

```
┌──────────────────────────────────────────────────────────────────────┐
│ ▲ MIDI SOURCE · drag tracks down to assign                          │  ← header
├──────────────────────┬─────────────────────────────────────────────┤
│  TrackListComponent   │  "Source piano roll — Phase 5" (placeholder) │  ← 40% of height
│  (220px, scrollable)  │                                             │
├──────────────────────┴─────────────────────────────────────────────┤
│ PARTS · DROP TRACKS TO ASSIGN                                        │  ← PartStripComponent
│  [x:1 Lute "Lead"] [x:2 Drums ""] [+ Add]                            │     header + slots
├──────────────────────────────────────────────────────────────────────┤
│  DiagnosticsPane (this view's own instance — import diagnostics land  │
│  here, not in the classic pane)                                      │
└──────────────────────────────────────────────────────────────────────┘
```

- **Track row** (`TrackListComponent`/`TrackRowComponent`) — index, name,
  colour swatch, and a `"<n> notes · <lo>–<hi>"` (or `"· ch 10"` for drums)
  second line. Click selects; drag onto a part slot to assign (the drag
  payload is the track's synthetic id, not its row index).
- **Part slot** (`PartStripComponent`/`PartSlotComponent`) — `x:` index,
  instrument badge, label, and its assigned tracks as chips
  (`AssignmentChipComponent`: swatch, `Tk<n>`, transpose, `×` to unassign).
  Drop a track here to assign it (dropping an already-assigned track is a
  no-op — dedup is the document's job). Right-click for Instrument /
  Rename… / Remove part. "+ Add" appends a new, auto-selected slot.

New menus (global, but only meaningful with a `SongDocument`, i.e. in
Songsmith mode):

```
Edit
  Undo                                  ← songDocument.undo()
  Redo                                  ← songDocument.redo()
                                          (enabled per canUndo()/canRedo();
                                           no keyboard shortcuts yet)

Song
  Default parts from tracks            ← synthesiseDefaultParts(songDocument)
                                          (enabled only in Songsmith mode)

View
  Classic editor                       ← toggles the classic Editor/
                                           Diagnostics split back on
                                          (checkbox; unchecked by default)
```

In Songsmith mode, **File → Open MIDI…** and dropping a `.mid`/`.midi` file
both import into `songDocument` (via `importMidiFile`) and show the result
in this view's own `DiagnosticsPane`, instead of loading into the classic
`EditorPane`. Dropping a `.json`/`.toml`/`.xml` config file in Songsmith
mode shows a "Config files are not supported in Songsmith mode yet" message
box rather than opening it — Songsmith has no `Config`-editing surface yet.

Export/Run is not part of Songsmith yet (Phase 6) — there is no Run button
in this view.

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
