# `forge_ui` — UI Guide

A reference for the Forge GUI's components and their relationships, so we can talk about specific parts unambiguously when reporting bugs or proposing changes.

Historical note: earlier phases of this project had a second, form/tree-based
"classic" editor (`EditorPane`, `InstrumentsTree`, `PropertyPageHost` + three
property pages) toggleable alongside Songsmith. It was deleted at the end of
Phase 6 (`MainWindow::Body` now hosts Songsmith exclusively) — this guide
describes the current, Songsmith-only UI only.

## Layout overview

```
┌──────────────────────────────────────────────────────────────────────┐
│ Title bar (JUCE-drawn; "Forge")                                      │
├──────────────────────────────────────────────────────────────────────┤
│ Menu bar    [File ▾] [Edit ▾] [Song ▾] [View ▾]                      │  ← MENU BAR (24 px)
├──────────────────────────────────────────────────────────────────────┤
│ ▲ MIDI SOURCE · drag tracks down to assign    [Grid: Off ▾][Quantize]│  ← UpperRegion header
├──────────────────────┬───────────────────────────────────────────────┤
│  TrackListComponent   │  PianoRollComponent (Role::Source)            │
│  (220px, scrollable)  │  keyboard gutter · gridlines · notes ·        │
│                       │  scroll · ctrl+wheel zoom                     │
├══════════════════════ SplitterComponent (drag to resize, top/bottom) ═╡
│ PARTS · DROP TRACKS TO ASSIGN                                         │  ← PartStripComponent
│  [x:1 Lute "Lead"] [x:2 Drums ""] [+ Add]                             │
├──────────────────────┬────────────────────────────────────────────────┤
│ ▼ LOTRO PREVIEW · how it will sound in-game                          │  ← PreviewRegion header
│  PreviewAssignedPanel │  PianoRollComponent (Role::Preview)            │
│  (160px): chips,      │  range band · ghost/dropped-note overlays ·   │
│  instrument/range,    │  scroll · ctrl+wheel zoom                     │
│  output stats         │                                               │
├══════ inner SplitterComponent (drag to resize, top/bottom) ══════════┤
│  DiagnosticListView (import diagnostics)                              │
└──────────────────────────────────────────────────────────────────────┘
```

`MainWindow::Body` also holds a second, full-screen child — the export panel
(`DiagnosticsPane`, reused as-is from before Songsmith) — toggled in place of
everything above by **Song → Run Converter** or **View → Export ABC panel**;
only one of the two is ever visible at a time. Its own internal layout
(`DiagnosticListView` + `AbcPreviewView`, split by an inner
`SplitterComponent`) is unchanged from when it was the classic editor's
sibling pane.

## Naming reference

When you say…       …I'll know you mean

| Name in this guide            | Code class / file                                              |
|--------------------------------|------------------------------------------------------------------|
| **Main window**                | `MainWindow` (`Source/UI/MainWindow.{h,cpp}`)                    |
| **Menu bar**                   | `juce::MenuBarComponent` inside `MainWindow`                     |
| **Body**                       | `MainWindow::Body` (inner class; hosts Songsmith + the toggleable export panel) |
| **Songsmith view**             | `SongsmithMainComponent` (`Source/UI/SongsmithMainComponent.{h,cpp}`) — everything below the menu bar when the export panel isn't shown |
| **Outer splitter**             | `SongsmithMainComponent`'s `splitter` (`SplitterComponent::Orientation::topBottom`) — between the upper (source) region and the lower region |
| **Upper region**               | `SongsmithMainComponent::UpperRegion` — source header + `TrackListComponent` + source-role `PianoRollComponent` |
| **Lower region**                | `SongsmithMainComponent::LowerRegion` — `PartStripComponent` + an inner splitter between the preview region and diagnostics |
| **Preview region**              | `SongsmithMainComponent::PreviewRegion` — preview header + `PreviewAssignedPanel` + preview-role `PianoRollComponent` |
| **Track list**                  | `TrackListComponent`/`TrackRowComponent` (`Source/UI/TrackListComponent.{h,cpp}`, `TrackRowComponent.{h,cpp}`) |
| **Source piano roll**           | `PianoRollComponent` constructed with `Role::Source` (`Source/UI/PianoRollComponent.{h,cpp}`) |
| **Grid-size combo**             | `SongsmithMainComponent::gridSizeCombo` (`juce::ComboBox`) — `Off`/`1/4`/`1/8`/`1/16`, translated to ticks by `GridSize.h`'s `gridSizeToTicks()` |
| **Quantize button**             | `SongsmithMainComponent::quantizeButton` (`juce::TextButton`) — calls `sourceRoll.quantizeSelection()` |
| **Preview piano roll**          | `PianoRollComponent` constructed with `Role::Preview` — adds the range band and ghost/dropped-note overlays |
| **Part strip**                  | `PartStripComponent`/`PartSlotComponent` (`Source/UI/PartStripComponent.{h,cpp}`, `PartSlotComponent.{h,cpp}`) |
| **Assignment chip**             | `AssignmentChipComponent` — swatch, `Tk<n>`, transpose, `×` to unassign, inside a part slot |
| **Preview assigned panel**      | `PreviewAssignedPanel` (`Source/UI/PreviewAssignedPanel.{h,cpp}`) — assigned-track chips, instrument/range readout, range-policy label, output stats |
| **Diagnostics list (Songsmith)** | `DiagnosticListView` hosted directly by `SongsmithMainComponent` — import diagnostics only, no ABC preview alongside it |
| **Export panel**                | `DiagnosticsPane` (`Source/UI/DiagnosticsPane.{h,cpp}`) — the toggleable full-export view (`Song → Run Converter` / `View → Export ABC panel`) |
| **Diagnostic List View (export panel)** | `DiagnosticListView` inside `DiagnosticsPane` — the 6-column table |
| **ABC Preview View**            | `AbcPreviewView` (`Source/UI/AbcPreviewView.{h,cpp}`) — read-only text editor showing the generated ABC, inside `DiagnosticsPane` |
| **Status line**                 | The grey `juce::Label` at the bottom of `DiagnosticsPane` (`5,824 bytes · 184 bars · 3 parts`) |

## Songsmith view

- **Track row** (`TrackListComponent`/`TrackRowComponent`) — index, name,
  colour swatch, and a `"<n> notes · <lo>–<hi>"` (or `"· ch 10"` for drums)
  second line. Click selects; drag onto a part slot to assign (the drag
  payload is the track's synthetic id, not its row index). An empty
  document shows a muted placeholder ("No MIDI loaded — File → Open MIDI…
  or drop a .mid here") instead of a blank panel.
- **Source piano roll** (`PianoRollComponent`, `Role::Source`) — shows the
  selected track's notes only (one track at a time), scrollable in both
  axes via an internal `juce::Viewport`. A pinned keyboard gutter on the
  left (C-note labels only) stays put while notes scroll underneath it;
  row shading follows the real piano black/white-key pattern, not plain
  semitone alternation. Vertical gridlines mark bar boundaries from the
  document's meter. Ctrl/Cmd+scroll-wheel zooms horizontally; a plain
  scroll wheel scrolls as usual. Selecting a different track, or any change
  to `SOURCE_MIDI` (e.g. a second MIDI import), re-fits the view to the
  newly-current track. Note editing (Phase 7, via `SourceRollEditor`):
  click to select a note, shift/ctrl/cmd-click to add or remove one from
  the selection, drag on empty canvas to rubber-band-select; drag a
  selected note's body to move it (and every other selected note, together)
  or its left/right edge to resize it; double-click empty canvas to create
  a note there (duration from the grid-size combo below, or a quarter note
  if the grid is off); Delete/Backspace removes the selection; Ctrl+Z/
  Ctrl+Y (or Ctrl+Shift+Z) undo/redo. Every gesture is one undo transaction
  through the same `songDocument` as the Edit menu's Undo/Redo.
- **Grid-size combo box and Quantize button** — top-right of the upper
  region's header row, next to the source header label. The combo
  (`Off`/`1/4`/`1/8`/`1/16`) sets the grid `SourceRollEditor` snaps to: it
  drives both the Quantize button's snap size and the duration a
  double-click-created note gets by default. "Quantize" snaps every
  currently-selected source-roll note's start and duration to that grid, one
  undo transaction; a no-op with nothing selected or the grid off. Neither
  control has a `Config` path — they drive `SourceRollEditor` state, not
  document/config fields.
- **Part slot** (`PartStripComponent`/`PartSlotComponent`) — `x:` index,
  instrument badge, label, and its assigned tracks as chips
  (`AssignmentChipComponent`: swatch, `Tk<n>`, transpose, `×` to unassign).
  Drop a track here to assign it (dropping an already-assigned track is a
  no-op — dedup is the document's job). Right-click for Instrument /
  Rename… / Remove part. "+ Add" appends a new, auto-selected slot. Slots
  have a 140px minimum width with a 1px gutter between them; once they no
  longer fit the available width the strip scrolls horizontally instead of
  squeezing. An empty document shows a muted placeholder ("No parts — +
  Add, or Song → Default parts from tracks"). A slot with no assignments
  yet is a normal, reachable state — `MainWindow::runConversion()` skips it
  (with a Warning diagnostic) rather than failing the whole export over it.
- **Preview region** (`SongsmithMainComponent::PreviewRegion`) — appears
  once a part is selected in the strip. `PreviewAssignedPanel` on the left
  shows that part's assigned-track chips, instrument name + native MIDI
  range, the (currently fixed) "Octave shift" range policy, and output
  stats: total note count, a `will octave-shift` count (amber) for notes
  `RangeConstraint` folds into range, and a `dropped` count (red) for notes
  that don't survive the pipeline at all. The preview piano roll
  (`Role::Preview`) on the right reuses the source roll's coordinate math
  and adds: a translucent range band (with red above/below-range wash)
  showing the target instrument's playable MIDI range; a dashed amber
  ghost outline at a folded note's post-range destination pitch (the solid
  rectangle stays at its pre-fold pitch, tinted red); and a hatched fill
  for dropped notes. Recomputes automatically (via `computePartPreview` +
  `diffPreviewNotes`, reusing the real pipeline) whenever the selected
  part or any of its assigned tracks changes.
- **Diagnostics** (`DiagnosticListView`, hosted directly) — the bare list
  only, not the full `DiagnosticsPane`: import diagnostics land here; the
  ABC-preview half only exists in the export panel.

New menus (global — see "Menus" below):

```
Edit
  Undo                                  ← songDocument.undo()
  Redo                                  ← songDocument.redo()
                                          (enabled per canUndo()/canRedo();
                                           no keyboard shortcuts yet)

Song
  Default parts from tracks            ← synthesiseDefaultParts(songDocument)
  Run Converter                        ← runConversion(), then shows the
                                           export panel

View
  Export ABC panel                     ← toggles the export panel
                                          (checkbox; unchecked by default)
```

**File → Open MIDI…** and dropping a `.mid`/`.midi` file both import into
`songDocument` (via `importMidiFile`) and show the result in the Songsmith
view's own `DiagnosticListView`. **File → Open Config…** and dropping a
`.json`/`.toml`/`.xml` config file both go through `openConfigFromPath`,
which always shows a "Config files are not supported yet" message box —
there is still no path from a loaded Config file into `SongDocument`'s
`ValueTree`.

## Menus

```
File
  Open MIDI…              ← FileChooser, .mid/.midi
  Open Config…             ← FileChooser, .json/.toml/.xml (always shows
                              "not supported yet" — see above)
  ─────────
  Save Config As…                      ← disabled; no ValueTree → Config
    JSON (.json)                          translation exists yet
    TOML (.toml)
    XML  (.xml)
  Save ABC As…                         ← writes the last Run Converter's
                                          ABC output (disabled until one
                                          has produced something)
  ─────────
  Quit                                  ← systemRequestedQuit
```

(Edit/Song/View are covered in "Songsmith view" above.)

## Context menus

| Node      | Right-click menu                                    |
|-----------|-------------------------------------------------------|
| Part slot | `Instrument ▸` (LOTRO instrument picker); `Rename…`; `Remove part` |

## Drag-drop targets

The whole Main window is a drag-drop target. Drop:

- `.mid` or `.midi` → same as **File → Open MIDI…**
- `.json`, `.toml`, or `.xml` → same as **File → Open Config…** (shows the
  "not supported yet" message box)

Within the Songsmith view itself, dragging a track row onto a part slot
assigns that track to that part (see "Songsmith view" above).

## Data flow

```
[Open MIDI / drag-drop a .mid]
       │
       ▼
  MidiImporter::importMidi  ──►  raw Song (read-only after this point)
       │
       ▼
  SongModelBridge::appendImportedSong  ──►  SongDocument (ValueTree)
       │
       ▼
[user drags tracks onto parts, edits assignments/labels via the UI —
 all through SongDocument's UndoManager]
       │
       ▼
[Song → Run Converter clicked]
       │
       ▼
  SongModelBridge::buildConfigAndRawSong (doc)  ──►  Config + raw Song
       │
       ▼
  SongModelBridge::dropUnassignedInstruments      (skips zero-assignment
       │                                            parts, warns per skip)
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
  DiagnosticsPane.show (diagnostics, abcText)   ← the export panel
```

Live per-part preview (no `Config`/export involved) is a separate, parallel
path:

```
[part selected in the strip]
       │
       ▼
  SongModelBridge::buildConfigAndRawSong (doc, {partId})
       │
       ▼
  assembleInstruments  ──►  assembled Song   (kept, for the diff below)
       │
       ▼
  deep-copy, then runPipeline  ──►  pipelined Song
       │
       ▼
  PreviewNoteDiff::diffPreviewNotes (assembled, pipelined)
       │
       ▼
  PreviewAssignedPanel.setPreview / PianoRollComponent(Role::Preview).setNoteSource
```

## Bug-report shorthand

When something doesn't work, please reference the named region:

> "The **Run Converter** menu item doesn't respond."
> "The **outer splitter** is fixed at 50/50 and won't drag."
> "The **preview piano roll**'s range band doesn't show up when I select a Drums part."
> "Right-clicking a **part slot** doesn't show the Rename… menu."
> "The **track list** placeholder text is wrong for an empty document."

That avoids any ambiguity about which of the half-dozen panels / rolls / lists we're talking about.
