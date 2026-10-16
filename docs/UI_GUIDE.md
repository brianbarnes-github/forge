# `forge_ui` — UI Guide

A reference for the Forge GUI's components and their relationships, so we can talk about specific parts unambiguously when reporting bugs or proposing changes.

Every named region below carries a `#N` tag, cross-referenced between the layout diagram and the naming table — say "#16" or "the Part slot" and either one is unambiguous. `docs/songsmith-ui-map.html` is a clickable visual companion to this file (open it directly in a browser) — the same `#N` numbering, click a marker or a legend row and the match highlights on both.

Historical note: earlier phases of this project had a second, form/tree-based
"classic" editor (`EditorPane`, `InstrumentsTree`, `PropertyPageHost` + three
property pages) toggleable alongside Songsmith. It was deleted at the end of
Phase 6 (`MainWindow::Body` now hosts Songsmith exclusively) — this guide
describes the current, Songsmith-only UI only.

## Theming

Colours come from `Source/UI/SongsmithColours.h` (and the piano roll's own `Source/UI/Theme/PianoRollPalette.h`); never hard-code a colour in a component. Stock JUCE widgets take the look from the app-wide `SongsmithLookAndFeel`; custom-painted components use `ThemePaint` — notes go through `ThemePaint::paintNoteBody`, headers and strips through `ThemePaint::paintPanelGradient`. Copper (`accentCopper`) is the accent and the playhead (with a cap in the timing bar, #32); the start marker and loop range are ivory (`markerIvory`). Walkthrough: `docs/ARCHITECTURE.md` §9.21.

## Layout overview

```
┌──────────────────────────────────────────────────────────────────────┐
│ Title bar (JUCE-drawn; "<name>[*] — Songsmith")                      │  #2
├──────────────────────────────────────────────────────────────────────┤
│ Menu bar    [File ▾] [Edit ▾] [Song ▾] [View ▾] [Help ▾]             │  #3  (24 px)
├──────────────────────────────────────────────────────────────────────┤
│ ▲ MIDI SOURCE · drag tracks down to assign                           │  #8  UpperRegion header
├──────────────────────────────────────────────────────────────────────┤
│  timing bar (28 px) above the track canvas; playhead line over rows  │  #32, #33
│  TrackListComponent — one row per MIDI track, full width             │  #9 (track list, rows #10)
│  (each non-conductor row ends its info column with [M] [S])          │  #31
│  index/name/note-range text, inline note-timeline preview            │  (inline preview under #10)
│  (shared zoom/scroll) + per-row ghost-visibility toggle              │  dbl-click row → editor (#28)
├══════════ DockStack divider (drag to resize; panels may dock here) ═══╡  #6  top dock stack divider
│     (empty)   ║  [|<] [<<] [Play] [Stop] [>|]  ║   (empty)   TransportStrip (28 px) │  #30  top row of the lower region
│ PARTS · DROP TRACKS TO ASSIGN                                         │  #15  PartStripComponent
│  [x:1 Lute "Lead"] [x:2 Drums ""] [+ Add]                             │  (slots are #16, chips #17)
├──────────────────────┬────────────────────────────────────────────────┤
│ ▼ LOTRO PREVIEW · how it will sound in-game   [Float] [...] [x]      │  #20  DockPanel header
│  PreviewAssignedPanel │  PianoRollComponent (Role::Preview)            │  #21 (preview assigned panel)
│  (160px): chips,      │  range band · ghost/dropped-note overlays ·   │  #22 (preview piano roll)
│  instrument/range,    │  wheel scroll · ctrl+wheel zoom              │
│  output stats         │                                               │
├══════ inner SplitterComponent (drag to resize, top/bottom) ══════════┤  #18  inner splitter
│  DiagnosticListView (import diagnostics)                              │  #23
└──────────────────────────────────────────────────────────────────────┘
```

The **MIDI editor** (#28) and the **LOTRO preview** (#19/#20) are dockable panels (see
*Dockable panels* below). Docked, the editor sits in its own `DockPanel` between the track
list's divider and the lower region; the preview sits at the bottom of the lower region.
Either can be floated into its own window or hidden from the View menu.

`MainWindow::Body` (#4) also holds a second, full-screen child — the export
panel (`DiagnosticsPane`, #24, reused as-is from before Songsmith) — toggled
in place of everything above by **Song → Run Converter** or **View → Export
ABC panel**; only one of the two is ever visible at a time. Its own internal
layout (`DiagnosticListView` #25 + `AbcPreviewView` #26, split by an inner
`SplitterComponent`, plus a status line #27) is unchanged from when it was
the classic editor's sibling pane.

## Dockable panels

The MIDI editor (#28), the LOTRO preview (#20), the Parts strip (#15) and the Mixer (#34) are panels (`Source/UI/Dock/`). Each has a
20 px header with its title and three controls: **Float** / **Dock** (toggles between the page
and its own window), **...** (the "Dock to" menu: *Below track list* or *Bottom*, plus a ticked *Always on top* item while the
panel floats; right-clicking anywhere on the header, over its buttons too, opens it) and **x** (hide). View → MIDI Editor / LOTRO Preview / Tracks / Parts / Mixer show or hide them. **View → MIDI Source** (ticked by default) shows or hides the whole MIDI source region (header, track list, ruler, conductor row); hidden, the transport strip and the docked panels take its space. **View → Layouts** (last item of the menu): *Save Layout* stores the current dock arrangement (which panels are shown, docked or floating, their positions and sizes, the splitter between the upper and lower regions) as "Layout 1", "Layout 2", …; each saved layout is listed beneath it, and choosing one applies it; *Manage Layouts…* opens a list where a layout is renamed (button or double-click) or deleted (button or Delete key). Layouts are kept in the app settings, so they are the same for every song
again, remembering whether each was docked or floating.

- **Zones.** *Below track list* (between the track list and the lower region; the editor's
  default) and *Bottom* (inside the lower region, above diagnostics; the preview's default).
  Dragging the divider bars resizes neighbours; there is no drag-to-rearrange and no tabs.
- **First run** looks like before (the split can differ by a few pixels): preview docked at the bottom, editor hidden. The
  editor's first open is floating.
- **Floating windows** are native-title-bar windows; their position and size are remembered. A
  remembered position that is no longer on any display is replaced by a reachable one.
  *Always on top* (header menu, floating panels only) keeps that window above the app's other windows;
  it is remembered per panel and kept while the panel is docked.
- **Remembered across sessions** in the app settings (key `dock.layout`, JSON), not in the
  `.songsmith` file. A missing or corrupt value gives the first-run layout.
- If nothing is docked at the bottom and diagnostics are off, the lower region shrinks to the
  transport strip; if diagnostics are on, they fill it.

## Naming reference

When you say…       …I'll know you mean

| # | Name in this guide            | Code class / file                                              |
|---|--------------------------------|------------------------------------------------------------------|
| 1 | **Main window**                | `MainWindow` (`Source/UI/MainWindow.{h,cpp}`) — reopens where it was closed (position, size, maximised), or centred on the primary monitor if that spot is no longer on any connected monitor (`WindowPlacement`) |
| 2 | **Title bar**                   | JUCE-drawn window chrome, reads `<name>[*] — Songsmith` (e.g. "Untitled — Songsmith"; `*` = unsaved changes) |
| 3 | **Menu bar**                   | `juce::MenuBarComponent` inside `MainWindow`                     |
| 4 | **Body**                       | `MainWindow::Body` (inner class; hosts Songsmith + the toggleable export panel) |
| 5 | **Songsmith view**             | `SongsmithMainComponent` (`Source/UI/SongsmithMainComponent.{h,cpp}`) — everything below the menu bar when the export panel isn't shown |
| 6 | **Top dock stack divider**     | `SongsmithMainComponent::topStack` (`dock::DockStack`) — the draggable bar between the upper (source) region and whatever sits below it: the docked MIDI editor panel (when shown) or the lower region. Replaces the old outer `SplitterComponent`; default weights 0.55 / 0.45 |
| 7 | **Upper region**               | `SongsmithMainComponent::UpperRegion` — source header + a full-width `TrackListComponent`; no embedded piano roll (moved to the floating Track editor window, #28) |
| 8 | **Upper region header**        | The "▲ MIDI SOURCE · drag tracks down to assign" label row only — the grid-size combo and Quantize button that used to sit here moved to the **Edit** menu (see #11/#12) |
| 9 | **Track list**                  | `TrackListComponent`/`TrackRowComponent` (`Source/UI/TrackListComponent.{h,cpp}`, `TrackRowComponent.{h,cpp}`) — spans the Upper region's full width; owns the shared `TimelineViewState` (`Source/UI/TimelineViewState.{h,cpp}`) that every row's note preview reads |
| 10 | **Track row**                  | `TrackRowComponent` — one row inside the track list: index/name/note-range text on the left, an inline `TrackNotePreview` (`Source/UI/TrackNotePreview.{h,cpp}`) plus a per-row ghost-visibility toggle on the right; double-click opens the floating Track editor window (#28) on this track (assignable tracks only; a note-less row ignores double-click, and double-clicking the conductor row opens the Tempo and Meter window instead); the conductor row's strip shows the tempo / meter markers, and a plain click on a marker (its line, or its label in its own half: tempo on top, meter below) selects that event's row in the Tempo and Meter window when it is already open (without moving the start marker; the window comes to the front); with the window closed, and anywhere else on the strip, a click behaves as on any row, and a double-click opens the window (on that marker's event if it landed on one) |
| 11 | **Grid Size menu**              | **Edit → Grid Size** submenu (`Off`/`1/4`/`1/8`/`1/16`) in the main menu bar (`MainWindow`'s `EditGridSizeBase` items) — enabled only while the Track editor window (#28) is open; forwards to `SongsmithMainComponent::setActiveEditorGridSize (GridSize)`, translated to ticks by `GridSize.h`'s `gridSizeToTicks()` |
| 12 | **Quantize menu item**          | **Edit → Quantize** in the main menu bar (`MainWindow`'s `EditQuantize`) — enabled only while the Track editor window (#28) is open; forwards to `SongsmithMainComponent::quantizeActiveEditor()`, which calls the open editor's `SourceRollEditor::quantizeSelection()` |
| 13 | **Source piano roll**           | `PianoRollComponent` constructed with `Role::Source` (`Source/UI/PianoRollComponent.{h,cpp}`) — unchanged internally, but no longer embedded in the Upper region; now hosted inside the floating Track editor window (#28) |
| 14 | **Lower region**                | `SongsmithMainComponent::LowerRegion` — transport strip fixed on top, a 6 px draggable bar under it (drags the dock divider above), then the bottom `DockStack` (the docked Parts and LOTRO preview panels) and an inner splitter to diagnostics. Collapses to just the transport strip when the bottom zone is empty and diagnostics are hidden |
| 15 | **Part strip**                  | `PartStripComponent`/`PartSlotComponent` (`Source/UI/PartStripComponent.{h,cpp}`, `PartSlotComponent.{h,cpp}`) — the `parts` dock panel (docked first in the bottom zone by default, above the preview; can float or be hidden) |
| 16 | **Part slot**                   | `PartSlotComponent` — one slot in the strip: `x:` index, instrument badge, label, assigned-track chips (#17) |
| 17 | **Assignment chip**             | `AssignmentChipComponent` — swatch, `Tk<n>`, transpose, `×` to unassign, inside a part slot |
| 18 | **Inner splitter**              | Between the preview region (#19) and the diagnostics list (#23), inside the lower region |
| 19 | **Preview region**              | `SongsmithMainComponent::PreviewRegion` — `PreviewAssignedPanel` + preview-role `PianoRollComponent` |
| 20 | **Preview panel header**       | The `DockPanel` header (20 px) of the LOTRO preview: title "▼ LOTRO PREVIEW · how it will sound in-game" plus Float/Dock, a `...` menu (Dock to: Below track list / Bottom, Always on top while floating; also right-click anywhere on the header) and a close button |
| 21 | **Preview assigned panel**      | `PreviewAssignedPanel` (`Source/UI/PreviewAssignedPanel.{h,cpp}`) — assigned-track chips, instrument/range readout, range-policy label, output stats |
| 22 | **Preview piano roll**          | `PianoRollComponent` constructed with `Role::Preview` — adds the range band and ghost/dropped-note overlays |
| 23 | **Diagnostics list (Songsmith)** | `DiagnosticListView` hosted directly by `SongsmithMainComponent` — import diagnostics only, no ABC preview alongside it. **Hidden by default** — toggled via **View → Diagnostics list**; when hidden, the preview region (#19) takes the space it would otherwise share via the inner splitter (#18) |
| 24 | **Export panel**                | `DiagnosticsPane` (`Source/UI/DiagnosticsPane.{h,cpp}`) — the toggleable full-export view (`Song → Run Converter` / `View → Export ABC panel`) |
| 25 | **Diagnostic List View (export panel)** | `DiagnosticListView` inside `DiagnosticsPane` — the 6-column table |
| 26 | **ABC Preview View**            | `AbcPreviewView` (`Source/UI/AbcPreviewView.{h,cpp}`) — read-only text editor showing the generated ABC, inside `DiagnosticsPane` |
| 27 | **Status line**                 | The grey `juce::Label` at the bottom of `DiagnosticsPane` (`5,824 bytes · 184 bars · 3 parts`) |
| 28 | **Track editor panel**          | `TrackEditorPanel` (`Source/UI/TrackEditorPanel.{h,cpp}`) — the single-instance MIDI editor, a dock panel (`midiEditor`) that is hidden until a track row is double-clicked (#10), then opens **floating** the first time and in whichever mode (docked below the track list, or floating) you last used. A second double-click on a different row re-points it (`setTrack`). With no track loaded (after New / Open) it shows "Double-click a track to edit it". Hosts the same `PianoRollComponent(Role::Source)`/`SourceRollEditor` pairing described under #13, plus its own menu bar (File, Edit, Navigate, Options, View; File has Export to New MIDI File and Close Editor; Navigate has Move Marker Left / Right by Grid, Move Up / Down a Semitone, Select Note Nearest Cursor and Add Note Nearest Cursor to Selection; Options has Snap to Grid, Snap to Key ▸ and Note Preview ▸) across the top, the playback transport strip (#30), a seek ruler (#32) and the playhead overlay (#33) above/over the roll. Supports translucent ghost-track overlays of other tracks, toggled per-row from the track list (#10) and never persisted. Owns its own zoom/scroll state, independent of the track list's shared `TimelineViewState` (#9) |
| 29 | **About dialog**              | **Help → About...** (`MainWindow`'s `HelpAbout`) → `showAboutDialog` (`Source/UI/AboutBox.{h,cpp}`): a modal `DialogWindow` centred over the main window, showing `AboutComponent`'s "SongSmith", "Created by Vydor", `Version <x.y.z>` and `Build <commit count> (<short hash>)` — `-dirty` after the hash if built from uncommitted changes, `Build unknown` if built without git. Ask testers for the Build line to know exactly which commit they're running |
| 30 | **Transport strip**          | `TransportStrip` (`Source/UI/Playback/TransportStrip.{h,cpp}`) — 28 px row of `\|<` (go to start), `<<` (back one bar), Play/Pause, Stop, `>\|` (go to end); in the middle of three areas (equal thirds to start, resized by dragging the two thin dividers; session-only; the left area holds your command buttons, below; the right area holds the transport controls, below) across the top of the lower region of the Songsmith view (just under the MIDI canvas, above the Parts panel; it rides along when the top dock divider (#6) is dragged) and at the top of the Track editor panel (#28), both bound to the one `PlaybackController`. The right area (`TransportControls`, ARCHITECTURE §9.20) holds, left to right: a **rate knob** (a round dial with a fixed mark at the top; 0.25-4.0, log scale, snaps to 1.000 at the top mark, double-click resets, typed readout beside it; session only; right-click for: set rate back to 1.000, set first tempo to current tempo, or set playhead tempo to current tempo, where current = tempo x rate; the first item resets the rate to 1.000, the playhead item leaves it); **Selection** start / end / length boxes (the loop range as `bar.beat.tick`, length in beats of the meter's denominator; with no loop they read `1.1.0`, `1.1.0`, `0`; type a range or drag the band, clear all three to remove it); and a **time-signature** box and a **tempo** box that show and edit the values at the playhead (the meter edit goes on the start of the bar containing the playhead, the bar the box shows; each is one undo step; re-entering the value already in force edits nothing). Edits commit on Enter or focus loss; Enter then hands keyboard focus back to the window (so Space, Esc, S, Delete and undo work again) and Esc reverts the box to the true value. A bad entry reverts and its reason appears in a bubble over the box for about 2 s (the box outline also turns red). To remove the loop, empty each Selection box and leave it (an emptied box is held while the loop stays, and the loop clears once all three are empty); pressing Enter in an emptied box while others still hold values restores the true range; the controls keep their fixed layout and are clipped, not squeezed, when the right area is narrower than 520 px (the tempo, then the time-signature box, are cut off first; the knob stays visible). The Selection, meter and tempo boxes are disabled with no Song. The **left area** (`StripButtonsBar`, ARCHITECTURE §9.20) starts with a **+** button whose menu lists commands in groups (File, Edit, Song, Transport, View) plus **Apply Layout** (one entry per saved layout); picking one adds a text button that runs that command or applies that layout. Entries already on the strip are greyed out, and so is everything once there are 10 buttons (the cap). A button is disabled when its command is disabled in the menus, and a panel toggle (Mixer, Parts…) shows as pressed while its panel is visible. Right-click a button for *Remove from strip*. Buttons that do not fit the area move behind a **»** button placed directly after the last visible button, whose menu runs them. A layout button follows its layout: renaming it in Manage Layouts renames the button, deleting it removes the button. The buttons are kept in the app settings (same for every song), appear in both the main strip and the MIDI editor's strip, and cannot be reordered (remove and re-add) |
| 31 | **Mute / Solo buttons**      | drawn speaker (mute) and headphone (solo) toggle icons on the second line of the track head (`TrackHeadComponent`, see #10) of every non-conductor row (#10); session-only, never saved or undoable |
| 32 | **Timing bar** (seek ruler)  | `TimelineRuler` (`Source/UI/Playback/TimelineRuler.{h,cpp}`, marks from `TimelineRulerMarks.{h,cpp}`) — two-row, 28 px strip above the main track canvas (#9) and above the Track editor's roll (#13). Beat-1 bar lines cross both rows: bar number on top, that line's clock time below (`m:ss`, or `m:ss.mmm` when labelled bars are ≥ 80 px apart). Zoomed far enough (beats ≥ 24 px apart) beat ticks appear in the top row, labelled bar.beat (`17.2`), with no clock time, and bar labels read `17.1`; zoomed out, bar lines thin to every 1, 2, 5, 10… bars (labels ≥ 48 px apart). A beat is one note of the denominator of the meter in force (6/8 = six eighth-note beats); bars and beats are laid out per meter, so bar numbers continue across meter changes and the bar grid in the roll follows them too; clock times follow the tempo map. Tempo and meter are edited in the Tempo and Meter window (see below). Left-click or drag sets the start marker at the exact tick under the pointer (in the Track editor, the nearest grid line while Options ▸ Snap to Grid is on and a grid is set; its ivory line runs through both rows with a down-pointing triangle at the top, continuing down the canvas; pressing the triangle clears it); right-click or drag moves the playhead instead. A loop range (#35) is shaded through both rows with an ivory bracket at each edge: drag a bracket to move that edge (snapped to the grid, Alt for free), or click it without dragging to clear the range |
| 33 | **Playhead**                 | `PlayheadOverlay` (`Source/UI/Playback/PlayheadOverlay.{h,cpp}`) — mouse-transparent copper line with a soft glow over the note previews (main) or over the roll below the keyboard gutter (editor); a copper cap marks it in the timing bar (#32) |
| 34 | **Mixer panel**              | `MixerPanelComponent` (`Source/UI/Mixer/`) — the `mixer` dock panel (hidden by default; View → Mixer; last in the bottom zone). A pinned Master strip (volume fader, 0-100, session-only) sits left of a horizontally scrolling row of per-track `MixerStripComponent`s: pan knob (moving it overrides the file's pan, double-click returns to the file's own pan), a volume fader filling the height beneath it (double-click resets to 100), the mute and solo icons and, at the bottom, the track number and name. A small "MIDI" label above the Master fader names the active playback target. The mouse wheel does nothing on the faders and knob. Volume and mute/solo stay in step with the track heads; pan lives only here |
| 35 | **Loop range**               | `LoopRangeOverlay` (`Source/UI/Playback/PlayheadOverlay.{h,cpp}`) plus brackets in the timing bar (#32); held on `PlaybackController` — a translucent ivory span with an edge line at each end, drawn across every track row and the Track editor's roll. Left-drag in a track's instrument band (#10) to draw it; while it exists playback loops it. Session-only: not saved, not undoable, cleared by New / Open and by Esc. See "Loop range" under Playback below |
| 36 | **Tracks bar**               | `TrackChipBar` (`Source/UI/TrackChipBar.{h,cpp}`) — the `tracks` dock panel (hidden by default; View → Tracks; docks first in the bottom zone, just above Parts and below the transport strip, and can be floated or moved like any panel). A thin horizontally scrolling row of chips, one per track that has notes (the conductor and empty tracks get none), each showing a colour swatch, the track's number (as on its head) and its name. Drag a chip onto a part card (#15) to assign the track, exactly like dragging a track row; click one to select that track in the list (Ctrl/Cmd and Shift work as on the heads). Rebuilt when a number, name or colour changes, not on note edits |

## Songsmith view

- **Track row** (#10, `TrackListComponent`/`TrackRowComponent`) — a left
  column 200px wide by default (`TrackRowComponent::trackInfoWidth`; 2px of it
  is the column divider) holds the **track head**; a wider column gives the
  name and volume slider the extra room. Drag the divider line
  between the heads and the note strips (the pointer turns into a left-right
  arrow over it) to widen the column up to 400px or narrow it back to 200px;
  the width is remembered across sessions (`trackList.headWidth` in the
  settings file, not the Song). The head
  (`TrackHeadComponent`, `Source/UI/TrackHeadComponent.{h,cpp}`) has two rows:
  index, colour swatch and name on top; drawn mute (speaker) and solo
  (headphones) icon toggles and a playback volume slider below. There is no
  note-count / pitch-range line. The head takes no clicks itself, so
  selection, drag-to-part and double-click still work on it; only its own
  controls react. The inline
  `TrackNotePreview` (read-only, notes drawn in the track's colour) fills
  everything to its right — the note
  data is the primary content, so it grows with the window instead of being
  pinned to a small fixed width — painted directly against the track list's
  shared `TimelineViewState` (#9) — so every row zooms/scrolls in lockstep.
  A 1px `trackDivider` line along each row's bottom edge separates tracks.
  Track colour (`colorArgb`) is set on import from the track's GM instrument
  family — its first Program Change `/ 8`, or Drums for channel 10 — with
  successive same-family tracks cycling through 4 shades of that family's
  hue; the swatch, preview notes, assignment chips and ghost overlays all
  use it.
  **Head controls.** Click the swatch to pick a new track colour (a
  `ColourSelector` in a callout; one undo step per picker session, none if the
  colour is not changed). Right-click the head for a menu with **Rename…**: an
  inline editor over the name where Enter commits, Escape or losing focus
  cancels, and a blank or unchanged name is rejected (one undo step on commit).
  The volume slider runs 0–100 (default 100) and shows the value as `NN%` in
  a popup while dragging; double-click resets it to 100; one drag is one undo
  step, and a lone change (arrow keys, reset) is one step each. The slider
  ignores the mouse wheel, which keeps the track list's meaning below. Volume is a saved `MIDI_TRACK` property (`playbackVolume`,
  absent = 100, removed when set back to 100); it scales the velocity of notes
  that start after the change during playback only and never reaches MIDI or
  ABC export. Mute / solo stay session-only. Editing the name, colour or
  volume refreshes the head in place; other property changes still rebuild the
  rows; a name or colour change (including its undo / redo) also repaints the
  part strip's assignment chips and retitles / repaints an open Track editor
  window. Known limit: an open rename editor does not follow an undo.
  The first row is always the song's **conductor** (`isConductor`): no
  index number, name "Conductor", muted text, and a head that shows the name
  and, on its second row, the song's average tempo (`♩= 118`: each tempo weighted by the ticks it is
  in force, rounded to a whole BPM; while a timeline marker is set it shows the exact tempo at the playhead while playing, or at the marker when stopped, instead (up to two decimals, e.g. `♩= 93.75`), and returns to the average when the marker is cleared), followed on the same row by the song's
  detected key (`key: G major`, `key: E♭ minor`; Krumhansl-Schmuckler over every non-drum note's
  duration, worked out when a MIDI is imported and again on **Song ▸ Set Key ▸ Detect Key**;
  the same submenu sets it by hand from all 24 keys, with the current one ticked. The key is
  saved in the `.songsmith` file; setting it from the menu (or Detect Key) is one undo step,
  while the detection that comes with an import is part of the import. Note edits do not
  update it; nothing is detected for a drums-only song). It has no swatch picker, rename, mute / solo or volume. Its note strip shows the song's
  tempo (copper, top half) and meter (cyan, bottom half) changes as 1 px lines with labels
  (`120`, `3/4`; labels that would overlap are hidden, the lines stay), and the background
  shade alternates at every tempo change (top half) and every meter change (bottom half). Double-clicking the
  conductor row, or choosing **Tempo and Meter…** from its head's right-click menu, opens the
  Tempo and Meter window (below). A track with no
  notes (e.g. only controllers) has the normal head. Neither kind is draggable onto a part slot or
  openable in the Track editor window (double-clicking a note-less row does nothing; double-clicking the
  conductor opens the Tempo and Meter window instead) — only
  tracks with at least one note are assignable. They stay in the document
  and are written by File → Export ▸ MIDI….
  On import, the shared zoom auto-fits so the longest track's notes span the
  full preview width (`TrackListComponent::fitTimelineToDocument()`, called
  from `MainWindow::openMidiFromPath`) — it is not recomputed on every edit,
  so a deliberate zoom/scroll survives routine note edits. Mouse wheel (the
  zoom/pan mapping is shared with the track editor, #28), decided purely by
  where the pointer is, never by whether a vertical scroll bar is showing:
  a plain wheel over a note strip or the ruler zooms all rows horizontally —
  first bringing the start marker to the middle of the preview strip and
  zooming about it (with no marker, about the middle of the view); a plain
  wheel over the track heads (left column, volume sliders included) scrolls the rows vertically (wheel
  up = towards the first row) and does nothing when the rows fit — it never
  zooms; Shift+wheel anywhere pans all rows horizontally, stopping at the
  song's end; Ctrl/Cmd+wheel anywhere resizes every track row (head and
  strip together), 4 px per notch, wheel up taller, between 30 and 120 px
  (default 34), keeping the content under the pointer in place. The row
  height is session-only: not saved, not undoable, reset only by restarting. Until you zoom by hand,
  resizing the window refits so the whole song stays visible; after a manual
  zoom, resizing keeps the zoom. A horizontal scroll bar under the note
  previews (spanning only the preview column) scrolls all rows together and
  auto-hides whenever the whole song fits. Each preview also has a per-row ghost-visibility
  toggle (an eye icon in its top-right corner). Across the top of each row's canvas side
  runs a 16 px instrument band (like Reaper's item label bar) naming the track's General MIDI
  instrument ("Drum Kit" on channel 10, blank for the conductor); when the track changes instrument
  part-way (Program Change events) it shows a coloured, named segment per instrument at the
  current zoom. Right-click the band for **Auto split on instrument change** (disabled with
  fewer than two instruments) and **Set track instrument to ▸** (a General MIDI list; the track's
  first instrument is ticked; the whole track becomes that one instrument, undoable in one
  step). Left-drag along the band (any non-conductor row) to draw or move the loop range
  (#35, see Playback below); it never starts a drag to a part, and the press still selects the
  row. It is extra height, so the
  notes area keeps its size: toggling it on/off is
  transient (never persisted) and adds/removes that track from the set of
  translucent ghost overlays shown in the Track
  editor window (#28), if one is open. Click a row to select it; drag onto a
  part slot to assign (the drag payload is the track's synthetic id, not its
  row index); double-click an assignable track to open the Track editor window (#28) on
  it (a note-less row ignores double-click; double-clicking the conductor row opens the Tempo and Meter window). An empty document shows a muted placeholder ("No MIDI loaded — File
  → Import ▸ MIDI… or drop a .mid here") instead of a blank panel.
- **Track selection and sections.** Two independent selections. The **head
  selection** is the highlighted track rows: clicking a row's info column selects
  that track; Ctrl/Cmd+click toggles a track, Shift+click selects the range from
  the last-clicked row (Ctrl/Cmd+Shift extends instead of replacing). A head click
  never changes which sections are selected. The **canvas selection** is the
  bright sections on the note strips, built only by clicking a strip. Each
  note-bearing track is divided into **sections** (initially one, from tick 0 to
  its last note end). A plain click on a section selects just it; Ctrl/Cmd+click
  toggles a section; Shift+click selects, on every track between the last-clicked
  section's track and this one, the section starting at the same tick as that
  anchor section (tracks without one are skipped; Ctrl/Cmd+Shift extends instead of
  replacing); a plain click on empty strip clears the section selection. Every
  canvas selection change is mirrored onto the heads (they then highlight exactly
  the tracks owning a selected section), but never the other way round, so you can
  have, say, two heads highlighted and three sections selected. Pressing a section
  that is part of a multi-selection keeps it: drag the body to move all selected
  sections and their notes (clamped at tick 0, counting member notes that lie
  outside the block) or a section's left or right edge (5 px grab zone) to resize
  them; releasing without dragging collapses the selection to the pressed section.
  Each edge moves by the dragged delta, shrinking
  deletes or trims the notes it gives up, growing touches no note. A few
  pixels of jitter is a click. The drag is a preview only and commits once on
  mouse-up as one undo step; **Esc** during the drag cancels it (nothing is
  committed, the press-time selection stays).
  **Alt-drag** from a section onto another track's row merges its notes into that
  track (the selected sections if the pressed one is selected, else just the
  pressed one): the target row is outlined with ghosts of the carried sections and
  a **Move** label by the pointer; hold **Ctrl/Cmd** (read live, also at release)
  to **Copy** instead. Same-pitch notes that overlap or touch are joined and
  contained ones dropped. Over the source row, the conductor or no row nothing is
  highlighted and nothing happens on release; **Esc** cancels. One undo step.
  Keys on the track canvas (never while a text field has focus; Ctrl/Cmd+S
  stays Save): **S** splits at the tick under the pointer when it is over a
  track's note strip (else at the start marker, else nothing — the key is not
  consumed) on the tracks that own a selected section, or just the pointer's
  track when no section is selected (the head selection is not used);
  **Delete**/**Backspace** removes the selected sections and their
  notes (nothing when none is selected); **Ctrl/Cmd+A** acts on the region under the
  pointer: over the note strips it selects every section of every non-conductor track
  (and so highlights all heads), anywhere else it selects all heads only.
  A note drawn in the Track editor far from every section still belongs to the
  nearest section and moves or deletes with it.
  The conductor and note-less tracks have no sections. Each of these is one
  undo step, and a press that changes nothing opens none.

- **Track editor panel** (#28, `TrackEditorPanel`) — the single MIDI editor,
  a dock panel opened by double-clicking a track row (#10). It starts
  hidden; the first open is a floating window, centred over the main window
  (kept on that window's monitor). After that it opens in the mode you last
  left it (docked below the track list, or floating) and wherever you left
  it. Double-clicking a different row re-points the same panel (`setTrack`)
  rather than opening a second one. Across its top is its own menu bar —
  File, Edit, Navigate, Options, View — shown docked or floating. Menus gain
  items feature by feature; an empty one does nothing when clicked. **File →
  Export to New MIDI File...** (needs a loaded track) saves the conductor (tempo
  map, meters) plus this track alone to a new MIDI file; the Save dialog
  suggests `<Song> - <Track>.mid` beside the Song (`Untitled - <Track>.mid` in
  Documents for an unsaved Song) and asks before replacing a file. **File →
  Close Editor** hides the panel, like its x. **Options → Snap to Grid**
  (ticked when on; default on) is the same setting as Preferences ▸ Editing ▸
  *Snap to grid* and is saved at once: while on, a click in this editor's roll or
  timing bar that drops the start marker lands on the nearest grid line (Edit ▸
  Grid size, counted from the song start); off, or with the grid Off, it lands
  exactly where clicked. Notes snap too: a dragged note's start lands on the
  nearest line (the rest of a selection moves with it, keeping its spacing), a
  dragged edge lands on the nearest line but never closer than one line to the
  other edge, and a double-click inserts its note at the start of the grid cell
  under the pointer. Hold Alt on the double-click or while dragging to place
  freely. **Options → Snap to Key ▸** holds **Enabled** (a
  checkbox, off at launch), **Root ▸** (C♭ B B♭ A♯ A A♭ G♯ G G♭ F♯ F E E♭ D♯ D
  D♭ C♯ C, the current one ticked) and **Type ▸** (Major, Natural minor, Melodic
  minor (rising form), Harmonic minor, Major / Minor pentatonic, Major / Minor
  blues, and **Chords ▸** Major / Minor triad, Major / Minor / Dominant seventh,
  Diminished triad / seventh, Augmented fifth triad). While Enabled, a
  double-click on a pitch outside that scale or chord adds nothing (no note, no
  selection change, no undo step); dragging, nudging and every other edit are
  not filtered, and existing notes are left alone. Root and Type start from the
  song key (Song ▸ Set Key, shown on the Conductor head) and follow it until you
  pick one, which then stays for the session (it is not saved, and New / Open
  start following the new song's key again). With no song key and nothing
  picked, Enabled and Type are greyed and nothing is blocked; picking a Root
  then uses Major. **Options → Note Preview ▸** holds three checkboxes, all
  off by default, saved at once and the same settings as Preferences ▸
  Editing's note preview toggles: **Preview notes when inserting** sounds a
  note you add by double-click; **Preview notes when editing** sounds the
  dragged note(s) each time a body drag lands them on a new pitch row (each new
  row cuts off the last), or once on release when the drag only moved them in
  time (resizing never sounds); **Preview notes when velocity changes** is
  saved but nothing triggers it yet (there is no velocity editing). A preview
  plays only the changed note(s) — one note, or a chord of up to six that all
  start together (anything else stays silent) — through the playback SoundFont
  as playback would at the note's start (the channel's instrument, controllers,
  the track's volume and Mixer pan, the master level), for the note's length but
  at most one second. It sounds even on a muted track, while the song plays,
  and does nothing without a SoundFont. **Navigate → Move Marker Left by Grid** / **Right by
  Grid** (`Left` / `Right` while the editor has keyboard focus) move the start
  marker to the previous / next grid line whether snap is on or off (from between
  two lines, to the nearer one on that side; Left stops at the song start; with no
  marker set, the step starts from the playhead, which does not move). They are
  greyed with the grid Off or no track loaded, and the arrow keys then scroll the
  roll as before. A click anywhere on this editor's roll (on a note or not) makes
  the semitone row under it the **active row**: painted a lighter grey
  (`#3A3A3A`, the same for sharps/flats and naturals) across the full width, with
  its keyboard key tinted and named as on hover. Dragging a note moves the active
  row with it, row by row (with several selected, it follows the note you
  grabbed); resizing a note or dragging a selection box leaves it on the row you
  pressed. It is a highlight, not a
  selection: not saved, not undoable, no sound; it stays when the editor switches
  track and goes when the editor has no track. **Navigate → Move Up a Semitone** /
  **Move Down a Semitone** (`Up` / `Down`) move it one semitone (stopping at MIDI
  127 / 0), scrolling just enough to keep it in view; they are greyed until a row
  is active, and the arrow keys then scroll the roll. The LOTRO preview roll has
  no active row. **Navigate → Select Note Nearest Cursor** (no shortcut) selects
  only the note whose start is nearest the cursor's time (the start marker, else
  the playhead), before or after it, on any pitch; when several start equally
  close, the one nearest the active row wins, then the higher pitch. A note still
  sounding under the cursor counts by its start. The view scrolls just enough to
  show the note; the marker and active row stay where they are, and there is no
  undo step. It is greyed with no track or a track with no notes. **Navigate →
  Add Note Nearest Cursor to Selection** (no shortcut) works the same way but
  adds the nearest note that is not selected yet to the selection, keeping the
  rest, so each use grows the selection outward from the cursor (with nothing
  selected it acts like Select). It is greyed once every note is selected.
  **Navigate → Select Next Note / Select Previous Note** (no shortcut) select
  only the next / previous note in time from the cursor, on any pitch, and move
  the start marker to that note's exact start (not snapped to the grid), so each
  use carries on from there. Notes that start together are stepped one at a
  time, lowest first going forward and highest first going back. The active row
  moves to the note's pitch and the view scrolls to show it; there is no undo
  step. With nothing selected at the marker, Next picks the first note starting
  at or after it and Previous the last starting before it. Each is greyed with
  no track, no notes, or no note left that way.
  **Navigate → Select Previous / Next Note of the Same Pitch** (no shortcut)
  work from the one selected note: each selects only the nearest earlier / later
  note of that pitch and moves the start marker to its exact start, scrolling it
  into view. **Select All Notes of the Same Pitch** selects every note of that
  pitch on the track and leaves the marker and the view where they are. All three
  are greyed unless exactly one note is selected (nothing selected or a
  multi-selection turns them off), and when no other note of that pitch is left
  that way; none adds an undo step.
  **Navigate → Add Previous / Next Note to Selection** (no shortcut) add the
  note right before the first selected note / right after the last one, on any
  pitch and in the same order as Select Previous / Next Note, and move the start
  marker to that note's exact start; repeated use grows the selection outward.
  The active row moves to the note's pitch and the view scrolls to show it.
  **Move Cursor to Start of Selection** moves the start marker to the earliest
  selected note's exact start and scrolls it into view, keeping the selection.
  All three are greyed with nothing selected or no track, and the adds also
  when no note is left that way; none adds an undo step. It hosts the source piano roll (#13)
  and `SourceRollEditor` exactly as before this redesign — see that entry
  below for the editing gestures — plus translucent overlays of any tracks
  currently ghost-toggled on in the track list (#10). The loop range (#35), when
  one is set, is shaded over its roll too, with brackets in its own timing bar. Closing it only hides
  it (View → MIDI Editor shows it again); New / Open clears the loaded track
  but keeps the panel's mode and visibility.
- **Source piano roll** (#13, `PianoRollComponent`, `Role::Source`) — shows the
  track the Track editor window (#28) is currently pointed at (one track at
  a time), scrollable in both axes via an internal `juce::Viewport`. It
  always lays out the whole MIDI pitch range, 0–127 (C-1 to G9), however
  narrow the track's own range, so a note can be drawn or dragged to any
  pitch; the vertical scroll bar is therefore effectively always shown, and
  the view opens scrolled so the track's notes sit mid-view (middle C for an
  empty track). The horizontal scroll bar appears only while the timeline is
  wider than the view: the track opens fitted to the width and keeps
  refitting as the window is resized or maximised, until the first
  wheel zoom, after which the zoom is kept and the bar appears. A
  pinned keyboard gutter on the left, drawn as a real piano keyboard —
  full-width white keys, shorter black keys for the sharps/flats, divider
  lines where keys meet, C-note labels only — stays put while notes scroll
  underneath it. Hovering over the keyboard or any note row (including while
  dragging) faintly tints that row's key and names it in light grey (e.g.
  `F#4`, sharps only, pitch 60 = C4), so a row can be identified without
  counting from the nearest C; row shading follows the real piano
  black/white-key pattern, not plain semitone alternation. Vertical
  gridlines mark bar boundaries from the document's meter, plus whole-note down to 1/64-note divisions (each appears once its lines are ≥ 8 px apart, fainter the finer it is; same grid on the main-view track strips and the LOTRO preview roll). Mouse
  wheel, the same model as the main track list but for pitch rows,
  decided by where the pointer is: a plain wheel over the notes zooms
  horizontally (centring the start marker first, or zooming about the middle
  of the view with no marker); a plain wheel over the keyboard gutter scrolls
  vertically (wheel up = higher pitches) and does nothing when the whole
  range fits — it never zooms; Shift+wheel anywhere pans horizontally;
  Ctrl/Cmd+wheel anywhere resizes the pitch rows (1 px per notch, wheel up
  taller, between 10 and 40 px, default 14), keeping the pitch under the
  pointer in place. The row height is session-only for that editor window
  (not saved, not undoable) and survives switching track. (The LOTRO
  preview roll is unchanged: Ctrl/Cmd+wheel zooms horizontally, a plain wheel
  scrolls vertically.) This zoom/scroll state belongs to the editor window and is independent of
  the track list's shared `TimelineViewState` (#9). Re-pointing the window
  at a different track, or any change to `SOURCE_MIDI` (e.g. a second MIDI
  import), re-fits the view (and re-arms refit-on-resize) for the
  newly-current track. Note editing (Phase
  7, via `SourceRollEditor`): click to select a note, shift/ctrl/cmd-click to
  add or remove one from the selection (a plain click on empty canvas clears
  it); right-drag, from empty canvas or a note, to lasso (rubber-band) select
  every note the box touches (replacing the selection; shift/ctrl/cmd+
  right-drag adds to it instead); right-click a key on the keyboard to select
  every note of that MIDI pitch (replacing the selection; shift/ctrl/cmd+
  right-click adds to it instead) -- a right-click on the roll itself, without
  dragging, selects nothing; a left-drag on empty canvas selects nothing; drag a selected note's body to move it (and every
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
  are a fixed 180px wide (`PartStripComponent::slotWidth`), left-aligned,
  with a 1px gutter between them; they never stretch or shrink with the
  panel, and once they no longer fit the strip scrolls horizontally. An empty document shows a muted placeholder ("No parts — +
  Add, or Song → Default parts from tracks"). A slot with no assignments
  yet is a normal, reachable state — `MainWindow::runConversion()` skips it
  (with a Warning diagnostic) rather than failing the whole export over it.
- **Preview region** (#19, `SongsmithMainComponent::PreviewRegion`) — appears
  once a part is selected in the strip. `PreviewAssignedPanel` (#21) on the left
  shows that part's assigned-track chips, instrument name + native MIDI
  range, the (currently fixed) "Octave shift" range policy, and output
  stats: total note count, a `will octave-shift` count (copper) for notes
  `RangeConstraint` folds into range, and a `dropped` count (red) for notes
  that don't survive the pipeline at all. The preview piano roll
  (#22, `Role::Preview`) on the right reuses the source roll's coordinate math
  and adds: a translucent range band (with red above/below-range wash)
  showing the target instrument's playable MIDI range; a dashed copper
  ghost outline at a folded note's post-range destination pitch (the solid
  rectangle stays at its pre-fold pitch, tinted red); and a hatched fill
  for dropped notes. Recomputes automatically (via `computePartPreview` +
  `diffPreviewNotes`, reusing the real pipeline) whenever the selected
  part or any of its assigned tracks changes.
- **Playback** (#30–#33) — plays the **source MIDI** (not the LOTRO preview,
  which has no playback) through a SoundFont. Play starts from the playhead
  from either window; Pause keeps the position; Stop returns to where play
  started; `|<` goes to 0; `<<` steps back one bar (to the start of the
  current bar, or the previous one when exactly on a bar line, following every
  meter change); `>|` goes to
  the end. At the last note-off playback stops and the playhead stays at the
  end; Play there restarts from 0. An empty Song's Play does nothing. Click
  or drag the seek ruler (#32) to move the playhead; the ruler spans the full
  width, including the info column, so a click over the info column seeks to
  a tick that is scrolled out of view. While playing, the view page-flips to
  keep the playhead visible (it stays in fitted mode when it cannot scroll
  further). **Space** is Play/Pause in the main window and in the Track editor
  window. The playhead is kept in seconds, so a tempo edit during playback
  moves the musical position; before the first tempo change the tempo is
  120 BPM, `SONG.tempoBpm` and the conductor's events are ignored. **M** mutes
  a track and **S** solos it (#31): solo is additive, mute beats solo, flags
  are keyed by track id and survive a track's removal and undo, and New /
  Open clear them. They are never saved and never undoable. The
  Track editor window has no M/S controls but plays what they allow. Edits,
  undo and redo during playback take effect without a restart (a held note is
  cut). **Song → SoundFont…** chooses a `.sf2`; the path is stored in the
  app settings. At startup the stored path is tried, then `SongSmith.sf2` next
  to the exe; with neither the app still starts and Play shows a "No
  SoundFont" dialog. The audio device opens on the first Play, not at
  startup. The Track editor window is 56 px taller than it was before
  playback (strip 28 + timing bar 28) when playback is attached.
- **Loop range** (#35) — repeats a stretch of time while you edit. It is an
  audition aid only: never saved in the Song, never undoable, never exported.
  New / Open clear it; editing the Song does not.
  - **Draw.** Left-drag in the instrument band of any non-conductor track row (#10). The range covers
    every track, not just the one you dragged on. The press is one edge and the pointer the
    other, in either direction. Both edges snap to the grid: the MIDI editor's grid while it
    shows a track, otherwise the default grid from Preferences ▸ Editing (Off = no snapping).
    Hold **Alt** to drag freely. The drag only starts after 4 px, so a click in the band just
    selects the row. While you drag, the range follows the pointer. If it ends shorter than
    0.05 s, for example back in the grid cell you started in, nothing is created and any earlier
    range is left as it was.
  - **Move.** A band drag that starts inside an existing range moves the whole range, keeping
    its length (its start snaps to the grid, Alt for free). It stops at the start of the Song;
    at the end of the Song the range is cut short.
  - **Replace.** A band drag that starts outside the range draws a new one in its place.
  - **Adjust.** Drag either bracket in a timing bar (#32), in the main view or in the Track
    editor, to move that edge. Dragging it past the other edge swaps them. The edge snaps to the
    grid; Alt drags it freely (in the editor the grid is that roll's grid).
  - **Clear.** Click a bracket without dragging (moving less than 3 px), or press **Esc**. Esc
    does one thing per press: during a section or merge drag it cancels that drag and keeps
    the range; during a note drag in the Track editor it does nothing; otherwise it clears the
    range. Esc does nothing while a text field has focus. It also does not reach the main
    window from a floating MIDI editor panel, so click into the main window first.
  - **Play.** While a range exists, playback always loops it: from the range end it jumps back
    to the range start, with no gap. Play starts at the range start, unless the playhead is
    already inside the range, in which case it carries on from there. The start marker is
    ignored while a range exists and applies again once the range is cleared. Stop returns to
    where Play started. A seek inside the range keeps looping, and a seek before it plays into
    it and then loops. A seek at or past the range end plays on to the end of the Song.
    Moving the range end before a looping playhead, or a faster tempo that does the same,
    jumps the playhead back to the range start instead.
  - **While looping.** Mute, solo, volume, pan and the master fader work as usual. Notes,
    tempo and meter can be edited; the range is held in musical time, so it follows a tempo
    change. A note that is still sounding from before the range start is not restarted on each
    pass, the same as after a seek. If an edit shortens the Song past the range, the loop is cut
    at the Song end; if that leaves less than 0.05 s, playback stops looping until the Song
    grows again, but the shaded range stays on screen.
  - **Overlap.** Where the start marker's triangle overlaps a range bracket, the bracket wins;
    press the part of the triangle outside the bracket to clear the marker. The part of the triangle hidden under the track-info column cannot be pressed.
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
                                           Ctrl+Z, Ctrl+Y / Ctrl+Shift+Z work
                                           window-wide via HistoryKeys.h)
  Quantize                              ← quantizeActiveEditor() (#12)
  Grid Size ▸                           ← setActiveEditorGridSize (#11)
    Off
    1/4
    1/8
    1/16
                                          (Quantize and Grid Size are enabled
                                           only while a Track editor window
                                           (#28) is open)
  ─────────
  Tempo and Meter…                      ← openTempoMeterEditor() (enabled once
                                           a MIDI has been imported)

Song
  Set Key ▸                            ← enabled once the song has a track
    Detect Key                         ← detectSongKey(): detects from the notes
    ─────────
    C major … B major, C minor … B minor ← setSongKey(index); the song's key is ticked
  Default parts from tracks            ← synthesiseDefaultParts(songDocument)
  Run Converter                        ← runConversion(), then shows the
                                           export panel
  ────────────────
  SoundFont...                         ← FileChooser, *.sf2; loads it and
                                           remembers the path (see Playback; also Preferences ▸ Playback)

View
  Export ABC panel                     ← toggles the export panel
                                          (checkbox; unchecked by default)
  Diagnostics list                     ← toggles the Songsmith diagnostics
                                          list (#23) (checkbox; unchecked
                                          by default — hiding it gives its
                                          space to the preview region)
  MIDI Editor                          ← shows/hides the editor panel (#28)
                                          (ticked while shown; unchecked by default)
  LOTRO Preview                        ← shows/hides the preview panel (#20)
                                          (ticked while shown; checked by default)
  Parts                                ← shows/hides the Parts panel (#15)
                                          (ticked while shown; checked by default)
  Mixer                                ← shows/hides the Mixer panel (#34)
                                          (ticked while shown; unchecked by default)
```

**File → Import ▸ MIDI…** and dropping a `.mid`/`.midi` file both import into
`songDocument` (via `importMidiFile`) and show the result in the Songsmith
view's own `DiagnosticListView` (#23). Import adds to the open Song (it never
replaces it). With Preferences ▸ Import ▸ Import Track Options on *Ask* (the
default) an import first opens the **MIDI File Import** dialog (see "Import
options dialog"). The Song itself is saved and opened as a `.songsmith` file (see
"Menus"); there is no Config file path in the GUI any more — "Open Config…" and
"Save Config As…" were removed.

## Menus

```
File
  New                Ctrl+N          ← empty Song (guarded, see below)
  Open…              Ctrl+O          ← FileChooser, *.songsmith (guarded)
  ─────────
  Save               Ctrl+S          ← enabled when dirty or untitled;
                                        untitled → Save As
  Save As…           Ctrl+Shift+S    ← appends .songsmith if missing, then
                                        asks before replacing another file (unless switched off in Preferences)
  ─────────
  Import ▸ MIDI…                      ← FileChooser, .mid/.midi; then opens the options
                                        dialog when Preferences ▸ Import is Ask
  Export ▸ MIDI…                      ← the whole song as a format-1 .mid;
                                        disabled until something besides the
                                        conductor is imported. Independent
                                        of parts.
           ABC…                       ← the last Run Converter's ABC output
                                        (disabled until one has produced
                                        something)
  ─────────
  Preferences…                       ← opens the modal Preferences dialog; no
                                        shortcut, always enabled
  ─────────
  Quit               Ctrl+Q          ← guarded

Edit
  Undo               Ctrl+Z          ← enabled when there is something to undo
  Redo               Ctrl+Y          ← Ctrl+Shift+Z also works (one hint only)
  ─────────
  Split              S               ← at the start marker only (the pointer is
                                        over the menu); enabled when a marker
                                        falls strictly inside a section of a
                                        track that owns a selected section
  Delete             Del             ← the selected sections; enabled when any
                                        is selected
  Select All         Ctrl+A          ← same as the key; from the menu the pointer
                                        is off the strips, so it selects all heads
  Select All Tracks                   ← every track head
  Select All Sections                 ← every section on every track
  ─────────
  Quantize                            ← enabled while a track editor is open
  Grid Size ▸ Off, 1/4, 1/8, 1/16     ← likewise
  ─────────
  Tempo and Meter…                    ← opens the Tempo and Meter window; enabled
                                        once a MIDI has been imported

Song
  Set Key ▸ Detect Key, ─, C major … B major, C minor … B minor
  Default parts from tracks
  Run Converter
  ─────────
  SoundFont…

Transport
  Play / Pause       Space           ← "Pause" while playing; same path as Space
  Stop
  ─────────
  Go to Start
  Go to End
  Rewind One Bar
  ─────────
  Clear Marker                        ← enabled when a start marker is set

View
  Export ABC panel                    ← ticked while visible
  Diagnostics list                    ← ticked while visible
  MIDI Editor                         ← ticked while visible
  LOTRO Preview                       ← ticked while visible
  Parts                               ← ticked while visible
  Mixer                               ← ticked while visible

Help
  About…
```

Export defaults to the Song's own file name with the extension swapped
(`<song>.mid` / `<song>.abc`), or `Untitled.*` in the Documents folder for an
unsaved Song — never the imported MIDI's name. The shortcut hints are
Windows/Linux-style (`Ctrl`); there are no Mac `Cmd` labels.

**Title bar / unsaved changes.** The window title is
`<name>[*] — Songsmith` (`Untitled` before the first save). `*` means the Song
changed since the last save or load; any tree edit counts, including imports,
and undoing back to the saved state still shows `*`. New, Open, Quit (menu or Ctrl+Q),
the window's close request and dropping a `.songsmith` file all go through
the guard: when the Song is dirty a **Save / Don't Save / Cancel** prompt
appears (unless "Ask about unsaved changes" is off in Preferences, which
discards silently). Save writes (Save As for an untitled Song) and only then continues;
a cancelled chooser, a failed write or Cancel stops the action. Opening a
damaged or unsupported file shows an error and leaves the open Song as it was.
Passing a `.songsmith` path on the command line opens it at startup.

(The menu structure lives in `Source/UI/MenuModel.h`, pinned by `MenuModel_tests.cpp`; the menus are rebuilt from live state each time one opens.)

## Tempo and Meter window

**Edit ▸ Tempo and Meter…** (enabled once a MIDI has been imported), a double-click on the
Conductor row, or the Conductor head's right-click **Tempo and Meter…** opens one floating
window (a double-click on a tempo or meter marker also selects that event's row and scrolls it into view; a single click on a marker does that only while the window is already open;
re-fronted if already open; closed when a Song is replaced by File ▸ New or Open). A table lists every tempo and
meter event in order: **Kind**, **Position** (`bar.beat`, 1-based, counted through meter
changes), **Tick** and **Value** (`93.75` BPM or `3/4`). A single click on a Value cell, or a
Tick cell (not on tick 0), selects its row and starts editing; Enter commits. Garbage or an
out-of-range value shows a message under the table and leaves the document unchanged; a meter's
tick snaps to a bar start, and the Tick cell then shows the snapped tick. **Add Tempo** / **Add Meter** insert an event at
the first free bar start after the selected row, with the value already in force there;
**Remove** (or Delete; Backspace does nothing) removes the selected event(s) — Ctrl/Cmd+A selects every row, Ctrl/Shift-click extends the selection, and a multi-row removal is one undo step, and the event at tick 0
cannot be removed. Every edit is one undo step: Ctrl+Z / Ctrl+Y work in the window and in the main
window's Edit menu, and the Conductor row's markers, the ruler and the grid follow.

Selecting a row draws a green vertical line at that event's tick across every track row of the main
window's timeline (aligned with the ruler and following scroll and zoom), so you can see where the event
sits among the tracks. No selection (or a refresh that drops the selected event, closing the window, or
replacing the Song) removes the line. The window has minimise and close buttons but no maximise button
(double-clicking the title bar does not maximise it); it can still be resized, within a modest size range.

**Graph and meter strip.** Above the table, a tempo graph plots the BPM over time as a stepped
line with a point at each tempo event; between the graph and the table, a meter strip shows one
block per meter (`3/4`), each running to the next meter. Both share the main timeline's zoom and
scroll, so they line up with the ruler and the tracks. The graph and strip are shown whenever the
window is opened from the main window. Only the left mouse button acts, and selection is shared
with the table.

- *Graph:* click a point (within 6 px) to select it. Click on empty graph to add a tempo at the
  beat-snapped position with the whole BPM at the pointer; if that beat already has a tempo, the
  click selects it instead of adding. Drag a point to move it. Moving the pointer 3 px or more
  sideways from where you pressed changes its tick (beat-snapped). Moving it 3 px or more up or
  down changes its tempo (whole BPM). Each axis is checked separately and continuously: bring the
  pointer back within 3 px on an axis and that axis keeps its stored value (an off-beat tick or a
  fractional BPM is not rounded). The tempo at tick 0 moves vertically only. Dropping onto a tick
  that already has a tempo is rejected. The moved point stays selected.
- *Strip:* in an ordinary song there is a meter at tick 0, so blocks cover the whole strip.
  - A single click on a block selects it.
  - To move a meter, press at a block's left edge (within 6 px, or anywhere in the block's first
    40 px) and drag 3 px or more sideways. The meter moves to the nearest bar; drag back within
    3 px and it stays where it was. The meter at tick 0 only selects.
  - **Double-click** a block's body to add a meter at the nearest bar, with the meter in force
    there; if that bar already has a meter, the double-click selects it instead.
- Delete removes the selected event. A drag previews the move and commits on release as one undo
  step. A drag that ends where it started changes nothing. A rejected edit shows its message under
  the table. An undo during a drag that removes the dragged event (or, on the graph, changes its BPM)
  abandons the drag.

## Context menus

| Node          | Right-click menu                                    |
|---------------|-------------------------------------------------------|
| Part slot (#16) | `Instrument ▸` (LOTRO instrument picker); `Rename…`; `Remove part` |
| Track head (#10) | `Rename…` (disabled on the conductor); on the Conductor head only: `Tempo and Meter…` |

## Drag-drop targets

The whole Main window (#1) is a drag-drop target. Drop:

- `.mid` or `.midi` → same as **File → Import ▸ MIDI…** (including the options dialog)
- `.songsmith` → same as **File → Open…** (behind the unsaved-changes guard)

Within the Songsmith view itself, dragging a track row (#10) onto a part slot
(#16) assigns that track to that part (see "Songsmith view" above).

## Data flow

```
[Import ▸ MIDI / drag-drop a .mid]
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
> "The **top dock divider** (#6) is fixed at 50/50 and won't drag."
> "The **preview piano roll** (#22)'s range band doesn't show up when I select a Drums part."
> "Right-clicking a **part slot** (#16) doesn't show the Rename… menu."
> "The **track list** (#9) placeholder text is wrong for an empty document."

That avoids any ambiguity about which of the half-dozen panels / rolls / lists we're talking about.


## Preferences dialog

`File → Preferences…`. Modal: the main window, its menus and shortcuts do nothing until it is closed (Close button, Escape or the title-bar X). Changes apply immediately; there is no OK/Cancel.

- **Left:** a tree of pages (General, selected by default; then Import; then Playback; then Appearance; then Editing). **Splitter** between tree and page (drag to resize).
- **General page:**
  - *Ask about unsaved changes* (default on) — off: New / Open / drops / Quit discard unsaved edits silently (no auto-save).
  - *Ask before replacing an existing file* (default on) — off: Save As and Export MIDI / ABC overwrite without the Replace box.
- **Import page:** two dropdowns. *Place imported MIDI at* — *Start of song* (default) or *Position marker* (the file's first tick lands where the marker is, set by left-clicking the timing bar; with no marker set it goes at the start; a tempo-map Replace shifts the file's later tempo and meter changes with it, the opening ones stay at the start; the playhead is not used). *Import Track Options* — *Ask* (default: every import shows the options dialog) or *Import Expanded Always* (no dialog; keeps the existing tempo map and expands the file into separate tracks). Merging into one track is only available through Ask.
- **Playback page:** the active SoundFont ("Using bundled SongSmith.sf2", "Using <path>" or "No SoundFont loaded"), **Browse…** and **Clear**, with a status line. Browse opens a chooser for `.sf2` files and loads the pick at once (same as Song ▸ SoundFont…) and remembers it; if it cannot be loaded the status says so and the previous SoundFont stays. Clear (enabled only while a chosen SoundFont is remembered) forgets that choice and loads the bundled `SongSmith.sf2`; if the bundled file is missing or cannot be loaded the choice is still forgotten, the status explains, and the current SoundFont stays until you quit.
- **Appearance page:** one toggle (the instrument range band is always shown, since it is essential to making an ABC, so it has no switch).
  - *Restore the window position and size on launch* (default on) — off: the next launch opens centred at the default size. Takes effect the next time Songsmith starts; quitting still saves the position, so switching it back on restores the last-closed one.
- **Editing page:**
  - *Default grid size* (Off by default; 1/4, 1/8, 1/16) — the grid a Track editor starts with. Applies to editors opened afterwards; an open editor keeps its grid, and Edit ▸ Grid size still changes the open editor (it never changes this setting).
  - *Snap to grid* (default on) — the same setting as the Track editor's Options ▸ Snap to Grid; either one changes both. While on, a start-marker click in the Track editor lands on the nearest grid line, and note moves, edge resizes and double-click inserts snap to the grid (Alt for free).
  - *Follow the playhead during playback* (default on) — off: the track list and Track editor no longer scroll to keep the playhead in view. Applies at once.
  - *Note preview (in the MIDI editor)*: *Preview notes when inserting*, *Preview notes when editing* and *Preview notes when velocity changes* (all default off) — the same settings as the Track editor's Options ▸ Note Preview checkboxes; either one changes both.
  - *When moving or copying sections to another track (Alt-drag), carry* **Notes only** (default) / **All events** — All events also carries the controllers, program changes and pitch bend that lie inside the section (End-of-Track and track-name events never travel), re-addressed to the target track's channel; Move removes them from the source. Applies to the next Alt-drag.
- All settings persist in the per-user settings file across launches.

## Import options dialog

`File → Import ▸ MIDI…` or a dropped `.mid`/`.midi`, when Import Track Options is *Ask*. Modal, titled "MIDI File Import" and naming the file. Two radio groups:

- **Tempo map:** *Keep existing tempo map* (default) / *Replace existing tempo map* (the file's tempo and meter maps and conductor events replace the Song's; existing tracks then play at the new tempo, and an Info diagnostic says so). Replace also removes the markers, copyright, key signatures and other conductor events from earlier imports. Greyed out when the Song has no tempo map yet, where the file's tempo map is used anyway.
- **Tracks:** *Expand N tracks into separate tracks* (default; N is how many tracks the file will add, the conductor not counted; with one track or an unreadable file it reads *Expand into separate tracks*) / *Merge into one track* (all of the file's note tracks become one track named "<file> (merged)"; each note keeps its MIDI channel, so Export ▸ MIDI writes it back on the original channel).
- A file's tempo and meter events always land in the Conductor track, whether the file's own conductor track or the note tracks held them (a merge never carries them), so they can no longer travel with a note track.
- **OK** imports with the chosen options; **Cancel**, Escape and the title-bar X import nothing and leave the Song untouched.
