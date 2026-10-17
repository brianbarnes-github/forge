# Songsmith note display: note shape and Color Note by — design

Date: 2026-10-16. Status: approved in conversation, awaiting plan.

## Intent

Users want to read more from the piano roll at a glance: drum-style
start-only notes, and note colours that show velocity, channel or pitch
instead of only the track. Both choices live in the MIDI editor's View menu,
after the existing Piano Notes / MIDI Numbers pair.

## Goals

- MIDI editor menu bar, **View**: after a separator below Piano Notes / MIDI
  Numbers, two submenus:
  - **Piano Roll Notes ▸** Rectangle · Diamond · Diamond + Length (one ticked).
  - **Color Note by ▸** Track · Velocity · Channel · Pitch (one ticked).
- Both always enabled (no track needed), both **saved** in `AppSettings`
  (like `view.keyboardLabels`) and pushed at startup by `applyViewSettings`.
  Absent or unknown values read the defaults: Rectangle, Track.
- Display only: nothing is written to the Song, nothing is undoable, the
  `.songsmith` file and export are untouched.

## Piano Roll Notes (shape) — MIDI editor roll only

- **Rectangle** (default): exactly today's `ThemePaint::paintNoteBody`.
- **Diamond**: a diamond one row tall and one row wide (a square on its
  point), centred on the note's start x and its row's centre. The note's
  length is not shown.
- **Diamond + Length**: the same diamond, plus a thin line (≈2 px, the row's
  centre) from the start out to the note's end x.
- Selected notes keep the white selection outline, drawn around the diamond
  (and the line, for Diamond + Length).
- Editing:
  - Diamond: pressing the diamond grabs the note (move, select, Alt-drag
    etc. as today). There is no resize handle — length can't be seen, so it
    can't be dragged.
  - Diamond + Length: the diamond and the line both grab the note; dragging
    the end of the line resizes it as the right edge does today.
- The main page keeps its 2 px note bars; the shape setting does not apply
  there. The LOTRO preview roll keeps rectangles (its fold/drop states are
  drawn on them).

## Color Note by — MIDI editor roll and main page note bars

Applies to the MIDI editor's roll and to every track row's note bars on the
main page (`TrackNotePreview`). Not the LOTRO preview roll, whose colours
show what happens to each note in LOTRO.

- **Track** (default): today — the track's `colorArgb`.
- **Velocity** (note-on velocity, 0..127): a heat map blended smoothly
  between these stops, evenly spaced from 0 to 127:
  `#2f5bd8` (blue), `#2d8fe0`, `#2fc0c8` (cyan), `#45c97a` (green),
  `#9ccf3e`, `#e6cf35` (yellow), `#f0a030` (orange), `#ec6a2c`, `#e52c32` (red).
- **Channel** (the NOTE's `channel`, 1..16): fixed colours —
  1 `#e5484d`, 2 `#f08a3c`, 3 `#e8cf3a`, 4 `#9ccf3e`, 5 `#45c97a`,
  6 `#3fb6c6`, 7 `#4f9ce8`, 8 `#4f6ae8`, 9 `#8a63e0`, 10 `#9a9ea6` (drums, grey),
  11 `#d65fc8`, 12 `#e86a96`, 13 `#b5835a`, 14 `#7fb3a0`, 15 `#c9b27a`, 16 `#a0a4e0`.
- **Pitch** (pitch class; every octave alike): naturals
  C `#e5484d`, D `#f08a3c`, E `#e8cf3a`, F `#5cc46a`, G `#3fb6c6`,
  A `#4f7ce8`, B `#a46be0`; each sharp is halfway between its two naturals:
  C# `#ea6944`, D# `#ecac3b`, F# `#4dbd98`, G# `#4799d7`, A# `#7973e4`.
- Colours follow live edits: changing a note's velocity, channel or pitch
  recolours it at once.
- The track's own colour still shows where it identifies the track: the head
  swatch and the selected row's accent bar.

## Non-goals

- No Preferences page entry for either choice (user declined the
  equivalent for Piano Notes / MIDI Numbers).
- No per-track override; one setting for the whole app.
- No new shapes beyond the three; no user-editable palettes.

## Anti-goals

- Must not change what is exported, saved or converted — the MIDI is the
  source of truth and this is purely how it is drawn.
- Must not make the LOTRO preview roll's fold/drop/out-of-range marking
  harder to read.

## Constraints

- Core/UI boundary: all of this is `Source/UI/` (see the
  `forge-engine-ui-boundary` skill); `Source/Core/` is untouched.
- Follow the Piano Notes / MIDI Numbers pattern: header-only enums with no
  JUCE (like `KeyboardLabels.h`), `AppSettings` getter/setter pair,
  `EditorMenuModel.h` ids appended after the existing ones, the
  `TrackEditorPanel` → `SongsmithMainComponent` → `MainWindow` change
  callbacks, `applyViewSettings` at startup.
- `MenuItemDesc` has no radio flag; ticked items are used, as for the
  keyboard-label pair.
- Strict TDD; pixel tests for the shapes and colours as the
  `[keyboard-labels]` tests do.

## Left to the builder

- Enum, setting-key and string-value names (e.g. `view.noteShape`,
  `view.noteColour`); menu id names.
- How the colour reaches the painters (e.g. a pure `noteColourFor(mode,
  note, trackColour)` function used by both the roll's note source and
  `TrackNotePreview`) and how `PianoRollNote` carries velocity/channel.
- How the main page gets the setting (through `SongsmithMainComponent` /
  `TrackListComponent`) and repaints on change.
- Diamond painting details (glow, gradient, small-row fast path) — keep
  them in the spirit of `paintNoteBody`.
- Rubber-band selection and nearest-note hit tests: what area a diamond
  note occupies. Claude's call: Diamond uses the diamond's bounds,
  Diamond + Length uses the diamond plus the note's full length.
- What a NOTE with no `velocity` or `channel` property reads as. Claude's
  call: follow playback's defaults (velocity 64, channel 1).
- Claude's call: a Diamond + Length note whose line ends inside its diamond
  has no resize handle — the diamond wins the press.
- Docs: CLAUDE.md status line, `docs/UI_GUIDE.md`, `docs/ARCHITECTURE.md`,
  `docs/TESTING.md` count.
