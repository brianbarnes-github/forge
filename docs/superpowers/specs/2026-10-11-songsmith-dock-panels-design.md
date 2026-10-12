# Songsmith dockable panels — design

Date: 2026-10-11. Status: draft, awaiting review.

## Goal

Make the **MIDI editor** and the **LOTRO preview** panels that can each be docked on the
main page or floated into their own window, with View-menu items that show or hide them.
The mechanism is generic (a panel registry and dock zones), so later controls such as a
Mixer are one registration each. All code lives in `Source/UI/`; nothing in `Source/Core/`.

## Decisions (from the brainstorm)

- Each panel can **dock or float**; no free drag-and-drop rearranging, tabs or IDE-style drop
  targets in v1.
- Dock/float state, visibility, zone, sizes and floating bounds are **remembered across
  sessions** in `AppSettings` (not in the `.songsmith` file).
- **First-run defaults reproduce today's page exactly:** LOTRO preview docked and visible at the
  bottom (where it is now); MIDI editor hidden, and when first opened (track double-click or
  View menu) it opens **floating**, as today. After that it re-opens however it was last left.
- The editor's docked home is a resizable slot between the track list and the lower region.
- Approach: one reusable `DockPanel` host that **re-parents** a single content instance between
  the page and a floating window (no duplicated instances to keep in sync).

## Components

- `DockPanelDef` (plain struct): panel id (`"midiEditor"`, `"lotroPreview"`), title, default
  zone, default size, minimum size. `DockRegistry` is the list of these; the View menu and the
  saved state are generated from it.
- `DockLayoutState` (plain data, JUCE-free): per panel `mode` (docked | floating), `visible`,
  `zone`, `order`, `size`, `floatBounds`. Pure functions: show, hide, float, dock, move to zone,
  `normalise` (drop unknown ids, add missing defaults, clamp sizes, reject bad bounds), and
  JSON round-trip.
- `DockPanel` (`juce::Component`): thin header (title, Float/Dock button, "Dock to" zone menu,
  Close) wrapping one content component. Floating moves the same content into a
  `DockFloatWindow` (`DocumentWindow`); docking moves it back.
- `DockHost`: owned by `SongsmithMainComponent`; owns the panels, applies a `DockLayoutState`,
  reports changes via `onLayoutChanged` (saved through `AppSettings`).
- Panel contents: `midiEditor` hosts the existing `TrackEditorWindow::Content` (transport strip,
  ruler, source roll); `TrackEditorWindow` becomes a content controller rather than a window.
  `lotroPreview` hosts the existing `PreviewRegion`.

## Layout

- Zones (enum, extensible): `belowTracks` (editor default) and `bottom` (LOTRO preview default,
  the slot it occupies today). A `right` column is out of scope; adding it later is a contained
  change.
- The page is one vertical stack: track list, `belowTracks` panels, transport and part strip,
  `bottom` panels, diagnostics list. Panels in a zone stack by `order`. Every boundary between
  visible items has a draggable divider. An empty zone has zero height and no divider; floating
  or hiding a panel returns its space to the neighbours (as the diagnostics list does).
- `computeStack(items, totalHeight)` is a pure function: stored sizes, minimum heights and
  visibility in, bounds out, clamped so every visible item keeps its minimum and the total fits.
  A divider drag updates the two adjacent items' sizes in `DockLayoutState`.
- The main page stops using the two-component `SplitterComponent` for this stack; the
  Tempo/Meter window and the export panel are unaffected.
- A floating panel uses its saved `floatBounds`, clamped onto a visible screen on restore (as the
  window-placement preference does). Closing the float window hides the panel; it does not
  re-dock it.

## Behaviour

- **View menu:** existing "Export ABC panel" and "Diagnostics list" stay; "MIDI Editor" and
  "LOTRO Preview" are added, ticked while visible. A ticked item hides the panel; an unticked
  one shows it in its remembered mode and zone.
- **MIDI editor:** still a single instance that follows the opened track. A track double-click
  shows the panel in its remembered mode and loads the track. Docked with no track it shows
  "Double-click a track to edit it". Title shows the track name. Space, quantize, grid size,
  follow playhead and ghost tracks keep working via the content controller. Closing hides the
  panel and keeps the loaded track.
- **LOTRO preview:** behaves as today (including part selection) but can be hidden, docked in
  either zone or floated. The LOTRO range band is part of its content and stays always shown.
- **Persistence:** one `dock.layout` key in `AppSettings`, a small JSON string, saved on every
  layout change (a failed save is reported like other settings). On load the state is
  normalised; a corrupt or missing value yields the defaults above.

## Testing

- `DockLayoutState` operations, normalisation and JSON round-trip, including corrupt input and
  the first-run defaults.
- `computeStack` (minimums, hidden items, overflow).
- `AppSettings` round-trip of `dock.layout`.
- Float/dock re-parenting keeps a single content instance and its state (zoom, selection).
- Menu items and tick state from the registry (`MenuModel_tests`).
- A track double-click shows and loads the editor; first run opens it floating.
- First-run layout of the main page matches the pre-change layout.
- Integration tests are preferred over unit tests for the behaviour above.

## Out of scope

Drag-to-rearrange, tabbed groups, a right-hand zone, per-song layouts, a Mixer panel itself.
