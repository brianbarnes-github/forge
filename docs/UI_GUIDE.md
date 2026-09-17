# `forge_ui` — UI Guide

A reference for the Forge GUI's components and their relationships, so we can talk about specific parts unambiguously when reporting bugs or proposing changes.

Every named region below carries a `#N` tag, cross-referenced between the layout diagram and the naming table — say "#16" or "the Part slot" and either one is unambiguous. `docs/songsmith-ui-map.html` is a clickable visual companion to this file (open it directly in a browser) — the same `#N` numbering, click a marker or a legend row and the match highlights on both.

Historical note: earlier phases of this project had a second, form/tree-based
"classic" editor (`EditorPane`, `InstrumentsTree`, `PropertyPageHost` + three
property pages) toggleable alongside Songsmith. It was deleted at the end of
Phase 6 (`MainWindow::Body` now hosts Songsmith exclusively) — this guide
describes the current, Songsmith-only UI only.

## Layout overview

```
┌──────────────────────────────────────────────────────────────────────┐
│ Title bar (JUCE-drawn; "Forge")                                      │  #2
├──────────────────────────────────────────────────────────────────────┤
│ Menu bar    [File ▾] [Edit ▾] [Song ▾] [View ▾]                      │  #3  (24 px)
├──────────────────────────────────────────────────────────────────────┤
│ ▲ MIDI SOURCE · drag tracks down to assign                           │  #8  UpperRegion header
├──────────────────────────────────────────────────────────────────────┤
│  TrackListComponent — one row per MIDI track, full width              │  #9 (track list, rows #10)
│  index/name/note-range text, inline note-timeline preview            │  (inline preview under #10)
│  (shared zoom/scroll) + per-row ghost-visibility toggle               │  dbl-click row → editor (#28)
├══════════════════════ SplitterComponent (drag to resize, top/bottom) ═╡  #6  outer splitter
│ PARTS · DROP TRACKS TO ASSIGN                                         │  #15  PartStripComponent
│  [x:1 Lute "Lead"] [x:2 Drums ""] [+ Add]                             │  (slots are #16, chips #17)
├──────────────────────┬────────────────────────────────────────────────┤
│ ▼ LOTRO PREVIEW · how it will sound in-game                          │  #20  PreviewRegion header
│  PreviewAssignedPanel │  PianoRollComponent (Role::Preview)            │  #21 (preview assigned panel)
│  (160px): chips,      │  range band · ghost/dropped-note overlays ·   │  #22 (preview piano roll)
│  instrument/range,    │  scroll · ctrl+wheel zoom                     │
│  output stats         │                                               │
├══════ inner SplitterComponent (drag to resize, top/bottom) ══════════┤  #18  inner splitter
│  DiagnosticListView (import diagnostics)                              │  #23
└──────────────────────────────────────────────────────────────────────┘
```

`MainWindow::Body` (#4) also holds a second, full-screen child — the export
panel (`DiagnosticsPane`, #24, reused as-is from before Songsmith) — toggled
in place of everything above by **Song → Run Converter** or **View → Export
ABC panel**; only one of the two is ever visible at a time. Its own internal
layout (`DiagnosticListView` #25 + `AbcPreviewView` #26, split by an inner
`SplitterComponent`, plus a status line #27) is unchanged from when it was
the classic editor's sibling pane.

## Naming reference

When you say…       …I'll know you mean

| # | Name in this guide            | Code class / file                                              |
|---|--------------------------------|------------------------------------------------------------------|
| 1 | **Main window**                | `MainWindow` (`Source/UI/MainWindow.{h,cpp}`)                    |
| 2 | **Title bar**                   | JUCE-drawn window chrome, reads "Forge" |
| 3 | **Menu bar**                   | `juce::MenuBarComponent` inside `MainWindow`                     |
| 4 | **Body**                       | `MainWindow::Body` (inner class; hosts Songsmith + the toggleable export panel) |
| 5 | **Songsmith view**             | `SongsmithMainComponent` (`Source/UI/SongsmithMainComponent.{h,cpp}`) — everything below the menu bar when the export panel isn't shown |
| 6 | **Outer splitter**             | `SongsmithMainComponent`'s `splitter` (`SplitterComponent::Orientation::topBottom`) — between the upper (source) region and the lower region |
| 7 | **Upper region**               | `SongsmithMainComponent::UpperRegion` — source header + a full-width `TrackListComponent`; no embedded piano roll (moved to the floating Track editor window, #28) |
| 8 | **Upper region header**        | The "▲ MIDI SOURCE · drag tracks down to assign" label row only — the grid-size combo and Quantize button that used to sit here moved to the **Edit** menu (see #11/#12) |
| 9 | **Track list**                  | `TrackListComponent`/`TrackRowComponent` (`Source/UI/TrackListComponent.{h,cpp}`, `TrackRowComponent.{h,cpp}`) — spans the Upper region's full width; owns the shared `TimelineViewState` (`Source/UI/TimelineViewState.{h,cpp}`) that every row's note preview reads |
| 10 | **Track row**                  | `TrackRowComponent` — one row inside the track list: index/name/note-range text on the left, an inline `TrackNotePreview` (`Source/UI/TrackNotePreview.{h,cpp}`) plus a per-row ghost-visibility toggle on the right; double-click opens the floating Track editor window (#28) on this track |
| 11 | **Grid Size menu**              | **Edit → Grid Size** submenu (`Off`/`1/4`/`1/8`/`1/16`) in the main menu bar (`MainWindow`'s `EditGridSizeBase` items) — enabled only while the Track editor window (#28) is open; forwards to `SongsmithMainComponent::setActiveEditorGridSize (GridSize)`, translated to ticks by `GridSize.h`'s `gridSizeToTicks()` |
| 12 | **Quantize menu item**          | **Edit → Quantize** in the main menu bar (`MainWindow`'s `EditQuantize`) — enabled only while the Track editor window (#28) is open; forwards to `SongsmithMainComponent::quantizeActiveEditor()`, which calls the open editor's `SourceRollEditor::quantizeSelection()` |
| 13 | **Source piano roll**           | `PianoRollComponent` constructed with `Role::Source` (`Source/UI/PianoRollComponent.{h,cpp}`) — unchanged internally, but no longer embedded in the Upper region; now hosted inside the floating Track editor window (#28) |
| 14 | **Lower region**                | `SongsmithMainComponent::LowerRegion` — `PartStripComponent` + an inner splitter between the preview region and diagnostics |
| 15 | **Part strip**                  | `PartStripComponent`/`PartSlotComponent` (`Source/UI/PartStripComponent.{h,cpp}`, `PartSlotComponent.{h,cpp}`) |
| 16 | **Part slot**                   | `PartSlotComponent` — one slot in the strip: `x:` index, instrument badge, label, assigned-track chips (#17) |
| 17 | **Assignment chip**             | `AssignmentChipComponent` — swatch, `Tk<n>`, transpose, `×` to unassign, inside a part slot |
| 18 | **Inner splitter**              | Between the preview region (#19) and the diagnostics list (#23), inside the lower region |
| 19 | **Preview region**              | `SongsmithMainComponent::PreviewRegion` — preview header + `PreviewAssignedPanel` + preview-role `PianoRollComponent` |
| 20 | **Preview region header**       | The "▼ LOTRO PREVIEW · how it will sound in-game" label row |
| 21 | **Preview assigned panel**      | `PreviewAssignedPanel` (`Source/UI/PreviewAssignedPanel.{h,cpp}`) — assigned-track chips, instrument/range readout, range-policy label, output stats |
| 22 | **Preview piano roll**          | `PianoRollComponent` constructed with `Role::Preview` — adds the range band and ghost/dropped-note overlays |
| 23 | **Diagnostics list (Songsmith)** | `DiagnosticListView` hosted directly by `SongsmithMainComponent` — import diagnostics only, no ABC preview alongside it. **Hidden by default** — toggled via **View → Diagnostics list**; when hidden, the preview region (#19) takes the space it would otherwise share via the inner splitter (#18) |
| 24 | **Export panel**                | `DiagnosticsPane` (`Source/UI/DiagnosticsPane.{h,cpp}`) — the toggleable full-export view (`Song → Run Converter` / `View → Export ABC panel`) |
| 25 | **Diagnostic List View (export panel)** | `DiagnosticListView` inside `DiagnosticsPane` — the 6-column table |
| 26 | **ABC Preview View**            | `AbcPreviewView` (`Source/UI/AbcPreviewView.{h,cpp}`) — read-only text editor showing the generated ABC, inside `DiagnosticsPane` |
| 27 | **Status line**                 | The grey `juce::Label` at the bottom of `DiagnosticsPane` (`5,824 bytes · 184 bars · 3 parts`) |
| 28 | **Track editor window**         | `TrackEditorWindow` (`Source/UI/TrackEditorWindow.{h,cpp}`) — floating, single-instance `juce::DocumentWindow` opened by double-clicking a track row (#10); a second double-click on a different row re-points it (`setTrack`) rather than opening another window. Hosts the same `PianoRollComponent(Role::Source)`/`SourceRollEditor` pairing described under #13, unchanged. Supports translucent ghost-track overlays of other tracks, toggled per-row from the track list (#10) and never persisted. Owns its own zoom/scroll state, independent of the track list's shared `TimelineViewState` (#9) |

## Songsmith view

- **Track row** (#10, `TrackListComponent`/`TrackRowComponent`) — index, name,
  colour swatch, and a `"<n> notes · <lo>–<hi>"` (or `"· ch 10"` for drums)
  second line on the left; an inline `TrackNotePreview` (read-only) fills the
  rest of the row's width on the right, painted directly against the track
  list's shared `TimelineViewState` (#9) — so every row zooms/scrolls in
  lockstep. Ctrl/Cmd+scroll-wheel over any row zooms all rows horizontally
  around that point; a plain scroll wheel pans all rows. Each preview also
  has a per-row ghost-visibility toggle (an eye icon in its top-right
  corner): toggling it on/off is transient (never persisted) and adds/removes
  that track from the set of translucent ghost overlays shown in the Track
  editor window (#28), if one is open. Click a row to select it; drag onto a
  part slot to assign (the drag payload is the track's synthetic id, not its
  row index); double-click to open the Track editor window (#28) on that
  track. An empty document shows a muted placeholder ("No MIDI loaded — File
  → Open MIDI… or drop a .mid here") instead of a blank panel.
- **Track editor window** (#28, `TrackEditorWindow`) — a floating,
  single-instance window opened by double-clicking a track row (#10).
  Double-clicking a different row re-points the same window (`setTrack`)
  rather than opening a second one. It hosts the source piano roll (#13)
  and `SourceRollEditor` exactly as before this redesign — see that entry
  below for the editing gestures — plus translucent overlays of any tracks
  currently ghost-toggled on in the track list (#10). Closing the window
  clears the owning `SongsmithMainComponent`'s pointer to it (`onClosed`), so
  the next double-click opens a fresh instance.
- **Source piano roll** (#13, `PianoRollComponent`, `Role::Source`) — shows the
  track the Track editor window (#28) is currently pointed at (one track at
  a time), scrollable in both axes via an internal `juce::Viewport`. A
  pinned keyboard gutter on the left (C-note labels only) stays put while
  notes scroll underneath it; row shading follows the real piano
  black/white-key pattern, not plain semitone alternation. Vertical
  gridlines mark bar boundaries from the document's meter. Ctrl/Cmd+
  scroll-wheel zooms horizontally; a plain scroll wheel scrolls as usual —
  this zoom/scroll state belongs to the editor window and is independent of
  the track list's shared `TimelineViewState` (#9). Re-pointing the window
  at a different track, or any change to `SOURCE_MIDI` (e.g. a second MIDI
  import), re-fits the view to the newly-current track. Note editing (Phase
  7, via `SourceRollEditor`): click to select a note, shift/ctrl/cmd-click to
  add or remove one from the selection, drag on empty canvas to
  rubber-band-select; drag a selected note's body to move it (and every
  other selected note, together) or its left/right edge to resize it;
  double-click empty canvas to create a note there (duration from the Edit
  menu's Grid Size, or a quarter note if the grid is off); Delete/Backspace
  removes the selection; Ctrl+Z/Ctrl+Y (or Ctrl+Shift+Z) undo/redo. Every
  gesture is one undo transaction through the same `songDocument` as the
  Edit menu's Undo/Redo.
- **Grid Size menu (#11) and Quantize menu item (#12)** — **Edit → Grid
  Size**/**Edit → Quantize** in the main menu bar, enabled only while a
  Track editor window (#28) is open (disabled otherwise, since there is no
  editor to act on). Grid Size (`Off`/`1/4`/`1/8`/`1/16`) sets the grid the
  open editor's `SourceRollEditor` snaps to: it drives both Quantize's snap
  size and the duration a double-click-created note gets by default.
  Quantize snaps every currently-selected note in the open editor's start
  and duration to that grid, one undo transaction; a no-op with nothing
  selected or the grid off. Neither has a `Config` path — they drive
  `SourceRollEditor` state, not document/config fields.
- **Part slot** (#16, `PartStripComponent`/`PartSlotComponent`) — `x:` index,
  instrument badge, label, and its assigned tracks as chips
  (#17 `AssignmentChipComponent`: swatch, `Tk<n>`, transpose, `×` to unassign).
  Drop a track here to assign it (dropping an already-assigned track is a
  no-op — dedup is the document's job). Right-click for Instrument /
  Rename… / Remove part. "+ Add" appends a new, auto-selected slot. Slots
  have a 140px minimum width with a 1px gutter between them; once they no
  longer fit the available width the strip scrolls horizontally instead of
  squeezing. An empty document shows a muted placeholder ("No parts — +
  Add, or Song → Default parts from tracks"). A slot with no assignments
  yet is a normal, reachable state — `MainWindow::runConversion()` skips it
  (with a Warning diagnostic) rather than failing the whole export over it.
- **Preview region** (#19, `SongsmithMainComponent::PreviewRegion`) — appears
  once a part is selected in the strip. `PreviewAssignedPanel` (#21) on the left
  shows that part's assigned-track chips, instrument name + native MIDI
  range, the (currently fixed) "Octave shift" range policy, and output
  stats: total note count, a `will octave-shift` count (amber) for notes
  `RangeConstraint` folds into range, and a `dropped` count (red) for notes
  that don't survive the pipeline at all. The preview piano roll
  (#22, `Role::Preview`) on the right reuses the source roll's coordinate math
  and adds: a translucent range band (with red above/below-range wash)
  showing the target instrument's playable MIDI range; a dashed amber
  ghost outline at a folded note's post-range destination pitch (the solid
  rectangle stays at its pre-fold pitch, tinted red); and a hatched fill
  for dropped notes. Recomputes automatically (via `computePartPreview` +
  `diffPreviewNotes`, reusing the real pipeline) whenever the selected
  part or any of its assigned tracks changes.
- **Diagnostics** (#23, `DiagnosticListView`, hosted directly) — the bare list
  only, not the full `DiagnosticsPane`: import diagnostics land here; the
  ABC-preview half only exists in the export panel. Hidden by default;
  **View → Diagnostics list** shows/hides it, and hiding it hands its share
  of the lower region's height straight to the preview region (#19) instead
  of leaving a gap.

New menus (global — see "Menus" below):

```
Edit
  Undo                                  ← songDocument.undo()
  Redo                                  ← songDocument.redo()
                                          (enabled per canUndo()/canRedo();
                                           no keyboard shortcuts yet)
  Quantize                              ← quantizeActiveEditor() (#12)
  Grid Size ▸                           ← setActiveEditorGridSize (#11)
    Off
    1/4
    1/8
    1/16
                                          (Quantize and Grid Size are enabled
                                           only while a Track editor window
                                           (#28) is open)

Song
  Default parts from tracks            ← synthesiseDefaultParts(songDocument)
  Run Converter                        ← runConversion(), then shows the
                                           export panel

View
  Export ABC panel                     ← toggles the export panel
                                          (checkbox; unchecked by default)
  Diagnostics list                     ← toggles the Songsmith diagnostics
                                          list (#23) (checkbox; unchecked
                                          by default — hiding it gives its
                                          space to the preview region)
```

**File → Open MIDI…** and dropping a `.mid`/`.midi` file both import into
`songDocument` (via `importMidiFile`) and show the result in the Songsmith
view's own `DiagnosticListView` (#23). **File → Open Config…** and dropping a
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

| Node          | Right-click menu                                    |
|---------------|-------------------------------------------------------|
| Part slot (#16) | `Instrument ▸` (LOTRO instrument picker); `Rename…`; `Remove part` |

## Drag-drop targets

The whole Main window (#1) is a drag-drop target. Drop:

- `.mid` or `.midi` → same as **File → Open MIDI…**
- `.json`, `.toml`, or `.xml` → same as **File → Open Config…** (shows the
  "not supported yet" message box)

Within the Songsmith view itself, dragging a track row (#10) onto a part slot
(#16) assigns that track to that part (see "Songsmith view" above).

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
  DiagnosticsPane.show (diagnostics, abcText)   ← the export panel (#24)
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

When something doesn't work, please reference the named region — the `#N`
tag or the name both work:

> "The **Run Converter** menu item doesn't respond."
> "The **outer splitter** (#6) is fixed at 50/50 and won't drag."
> "The **preview piano roll** (#22)'s range band doesn't show up when I select a Drums part."
> "Right-clicking a **part slot** (#16) doesn't show the Rename… menu."
> "The **track list** (#9) placeholder text is wrong for an empty document."

That avoids any ambiguity about which of the half-dozen panels / rolls / lists we're talking about.
