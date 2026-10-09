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
├══════════════════════ SplitterComponent (drag to resize, top/bottom) ═╡  #6  outer splitter
│            [|<] [<<] [Play] [Stop] [>|]   TransportStrip (28 px, centred) │  #30  top row of the lower region
│ PARTS · DROP TRACKS TO ASSIGN                                         │  #15  PartStripComponent
│  [x:1 Lute "Lead"] [x:2 Drums ""] [+ Add]                             │  (slots are #16, chips #17)
├──────────────────────┬────────────────────────────────────────────────┤
│ ▼ LOTRO PREVIEW · how it will sound in-game                          │  #20  PreviewRegion header
│  PreviewAssignedPanel │  PianoRollComponent (Role::Preview)            │  #21 (preview assigned panel)
│  (160px): chips,      │  range band · ghost/dropped-note overlays ·   │  #22 (preview piano roll)
│  instrument/range,    │  wheel scroll · ctrl+wheel zoom              │
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
| 1 | **Main window**                | `MainWindow` (`Source/UI/MainWindow.{h,cpp}`) — reopens where it was closed (position, size, maximised), or centred on the primary monitor if that spot is no longer on any connected monitor (`WindowPlacement`) |
| 2 | **Title bar**                   | JUCE-drawn window chrome, reads `<name>[*] — Songsmith` (e.g. "Untitled — Songsmith"; `*` = unsaved changes) |
| 3 | **Menu bar**                   | `juce::MenuBarComponent` inside `MainWindow`                     |
| 4 | **Body**                       | `MainWindow::Body` (inner class; hosts Songsmith + the toggleable export panel) |
| 5 | **Songsmith view**             | `SongsmithMainComponent` (`Source/UI/SongsmithMainComponent.{h,cpp}`) — everything below the menu bar when the export panel isn't shown |
| 6 | **Outer splitter**             | `SongsmithMainComponent`'s `splitter` (`SplitterComponent::Orientation::topBottom`) — between the upper (source) region and the lower region |
| 7 | **Upper region**               | `SongsmithMainComponent::UpperRegion` — source header + a full-width `TrackListComponent`; no embedded piano roll (moved to the floating Track editor window, #28) |
| 8 | **Upper region header**        | The "▲ MIDI SOURCE · drag tracks down to assign" label row only — the grid-size combo and Quantize button that used to sit here moved to the **Edit** menu (see #11/#12) |
| 9 | **Track list**                  | `TrackListComponent`/`TrackRowComponent` (`Source/UI/TrackListComponent.{h,cpp}`, `TrackRowComponent.{h,cpp}`) — spans the Upper region's full width; owns the shared `TimelineViewState` (`Source/UI/TimelineViewState.{h,cpp}`) that every row's note preview reads |
| 10 | **Track row**                  | `TrackRowComponent` — one row inside the track list: index/name/note-range text on the left, an inline `TrackNotePreview` (`Source/UI/TrackNotePreview.{h,cpp}`) plus a per-row ghost-visibility toggle on the right; double-click opens the floating Track editor window (#28) on this track (assignable tracks only; conductor and note-less rows ignore double-click) |
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
| 28 | **Track editor window**         | `TrackEditorWindow` (`Source/UI/TrackEditorWindow.{h,cpp}`) — floating, single-instance `juce::DocumentWindow` (native title bar with minimise/maximise/close) opened by double-clicking a track row (#10); a second double-click on a different row re-points it (`setTrack`) rather than opening another window. Hosts the same `PianoRollComponent(Role::Source)`/`SourceRollEditor` pairing described under #13, plus the playback transport strip (#30), a seek ruler (#32) and the playhead overlay (#33) above/over the roll (the window is 42 px taller than before playback). Supports translucent ghost-track overlays of other tracks, toggled per-row from the track list (#10) and never persisted. Owns its own zoom/scroll state, independent of the track list's shared `TimelineViewState` (#9) |
| 29 | **About dialog**              | **Help → About...** (`MainWindow`'s `HelpAbout`) → `showAboutDialog` (`Source/UI/AboutBox.{h,cpp}`): a modal `DialogWindow` centred over the main window, showing `AboutComponent`'s "SongSmith", "Created by Vydor", `Version <x.y.z>` and `Build <commit count> (<short hash>)` — `-dirty` after the hash if built from uncommitted changes, `Build unknown` if built without git. Ask testers for the Build line to know exactly which commit they're running |
| 30 | **Transport strip**          | `TransportStrip` (`Source/UI/Playback/TransportStrip.{h,cpp}`) — 28 px row of `\|<` (go to start), `<<` (back one bar), Play/Pause, Stop, `>\|` (go to end); centred in the bar across the top of the lower region of the Songsmith view (just under the MIDI canvas, above the part strip; it rides along when the outer splitter is dragged) and at the top of the Track editor window (#28), both bound to the one `PlaybackController` |
| 31 | **Mute / Solo buttons**      | `TrackRowComponent`'s `M` / `S` toggle buttons at the right end of the 180 px info column of every non-conductor row (#10); session-only, never saved or undoable |
| 32 | **Timing bar** (seek ruler)  | `TimelineRuler` (`Source/UI/Playback/TimelineRuler.{h,cpp}`, marks from `TimelineRulerMarks.{h,cpp}`) — two-row, 28 px strip above the main track canvas (#9) and above the Track editor's roll (#13). Beat-1 bar lines cross both rows: bar number on top, that line's clock time below (`m:ss`, or `m:ss.mmm` when labelled bars are ≥ 80 px apart). Zoomed far enough (beats ≥ 24 px apart) beat ticks appear in the top row, labelled bar.beat (`17.2`), with no clock time, and bar labels read `17.1`; zoomed out, bar lines thin to every 1, 2, 5, 10… bars (labels ≥ 48 px apart). A beat is one note of the first meter entry's denominator (6/8 = six eighth-note beats); clock times follow the tempo map. Left-click or drag sets the start marker at the exact tick under the pointer (its amber line runs through both rows with a down-pointing triangle at the top, continuing down the canvas; pressing the triangle clears it); right-click or drag moves the playhead instead |
| 33 | **Playhead**                 | `PlayheadOverlay` (`Source/UI/Playback/PlayheadOverlay.{h,cpp}`) — mouse-transparent vertical line over the note previews (main) or over the roll below the keyboard gutter (editor) |

## Songsmith view

- **Track row** (#10, `TrackListComponent`/`TrackRowComponent`) — index, name,
  colour swatch, and a `"<n> notes · <lo>–<hi>"` (or `"· ch 10"` for drums)
  second line fill a fixed 180px-wide left column; the inline
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
  The first row is always the song's **conductor** (`isConductor`): no
  index number, name "Conductor", muted text, and a second line of just
  `"<N> events"` (the song-wide tempo/meter/key/marker events and anything
  else it holds). A track with no notes (e.g. only controllers) shows
  `"0 notes · <N> events"`. Neither kind is draggable onto a part slot or
  openable in the Track editor window (double-click does nothing) — only
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
  wheel over the track heads (left column) scrolls the rows vertically (wheel
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
  instrument ("Drum Kit" on channel 10, blank for the conductor); it is extra height, so the
  notes area keeps its size: toggling it on/off is
  transient (never persisted) and adds/removes that track from the set of
  translucent ghost overlays shown in the Track
  editor window (#28), if one is open. Click a row to select it; drag onto a
  part slot to assign (the drag payload is the track's synthetic id, not its
  row index); double-click an assignable track to open the Track editor window (#28) on
  it (conductor and note-less rows ignore double-click). An empty document shows a muted placeholder ("No MIDI loaded — File
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

- **Track editor window** (#28, `TrackEditorWindow`) — a floating,
  single-instance window opened by double-clicking a track row (#10). It
  pops up centred over the main window (kept on that window's monitor), not
  on the primary monitor.
  Double-clicking a different row re-points the same window (`setTrack`)
  rather than opening a second one. It hosts the source piano roll (#13)
  and `SourceRollEditor` exactly as before this redesign — see that entry
  below for the editing gestures — plus translucent overlays of any tracks
  currently ghost-toggled on in the track list (#10). Closing the window
  clears the owning `SongsmithMainComponent`'s pointer to it (`onClosed`), so
  the next double-click opens a fresh instance.
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
  add or remove one from the selection, drag on empty canvas to
  rubber-band-select; right-click a key, or anywhere in its row, to select
  every note of that MIDI pitch (replacing the selection; shift/ctrl/cmd+
  right-click adds to it instead); drag a selected note's body to move it (and every
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
- **Playback** (#30–#33) — plays the **source MIDI** (not the LOTRO preview,
  which has no playback) through a SoundFont. Play starts from the playhead
  from either window; Pause keeps the position; Stop returns to where play
  started; `|<` goes to 0; `<<` steps back one bar (to the start of the
  current bar, or the previous one when exactly on a bar line, using the first meter-map entry only, 4/4 when
  there is none); `>|` goes to
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
  app settings. At startup the stored path is tried, then `TimGM6mb.sf2` next
  to the exe; with neither the app still starts and Play shows a "No
  SoundFont" dialog. The audio device opens on the first Play, not at
  startup. The Track editor window is 56 px taller than it was before
  playback (strip 28 + timing bar 28) when playback is attached.
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

Song
  Default parts from tracks            ← synthesiseDefaultParts(songDocument)
  Run Converter                        ← runConversion(), then shows the
                                           export panel
  ────────────────
  SoundFont...                         ← FileChooser, *.sf2; loads it and
                                           remembers the path (see Playback)

View
  Export ABC panel                     ← toggles the export panel
                                          (checkbox; unchecked by default)
  Diagnostics list                     ← toggles the Songsmith diagnostics
                                          list (#23) (checkbox; unchecked
                                          by default — hiding it gives its
                                          space to the preview region)
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

Song
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

## Context menus

| Node          | Right-click menu                                    |
|---------------|-------------------------------------------------------|
| Part slot (#16) | `Instrument ▸` (LOTRO instrument picker); `Rename…`; `Remove part` |

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
> "The **outer splitter** (#6) is fixed at 50/50 and won't drag."
> "The **preview piano roll** (#22)'s range band doesn't show up when I select a Drums part."
> "Right-clicking a **part slot** (#16) doesn't show the Rename… menu."
> "The **track list** (#9) placeholder text is wrong for an empty document."

That avoids any ambiguity about which of the half-dozen panels / rolls / lists we're talking about.


## Preferences dialog

`File → Preferences…`. Modal: the main window, its menus and shortcuts do nothing until it is closed (Close button, Escape or the title-bar X). Changes apply immediately; there is no OK/Cancel.

- **Left:** a tree of pages (General, selected by default; then Import). **Splitter** between tree and page (drag to resize).
- **General page:**
  - *Ask about unsaved changes* (default on) — off: New / Open / drops / Quit discard unsaved edits silently (no auto-save).
  - *Ask before replacing an existing file* (default on) — off: Save As and Export MIDI / ABC overwrite without the Replace box.
- **Import page:** one dropdown, *Import Track Options* — *Ask* (default: every import shows the options dialog) or *Import Expanded Always* (no dialog; keeps the existing tempo map and expands the file into separate tracks). Merging into one track is only available through Ask.
- All settings persist in the per-user settings file across launches.

## Import options dialog

`File → Import ▸ MIDI…` or a dropped `.mid`/`.midi`, when Import Track Options is *Ask*. Modal, titled "MIDI File Import" and naming the file. Two radio groups:

- **Tempo map:** *Keep existing tempo map* (default) / *Replace existing tempo map* (the file's tempo and meter maps and conductor events replace the Song's; existing tracks then play at the new tempo, and an Info diagnostic says so). Replace also removes the markers, copyright, key signatures and other conductor events from earlier imports. Greyed out when the Song has no tempo map yet, where the file's tempo map is used anyway.
- **Tracks:** *Expand into separate tracks* (default) / *Merge into one track* (all of the file's note tracks become one track named "<file> (merged)"; each note keeps its MIDI channel, so Export ▸ MIDI writes it back on the original channel).
- **OK** imports with the chosen options; **Cancel**, Escape and the title-bar X import nothing and leave the Song untouched.
