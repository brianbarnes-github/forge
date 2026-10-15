# Songsmith transport strip: user-chosen command buttons (left area) — design

Date: 2026-10-14. Status: Draft, awaiting review.

## Goal

Fill the empty left area of the `TransportStrip` (TODO item 4 in the
transport-controls spec) with a user-built row of buttons. A `+` button at the
far left opens a menu of app commands; picking one adds a text button that runs
that command. Together with saved dock layouts (View ▸ Layouts) this gives the
user a recallable workspace plus one-click access to the commands they use
most.

Everything is UI-side; nothing is added to `Source/Core/` (see the
`forge-engine-ui-boundary` skill).

## Agreed decisions

| Question | Decision |
|---|---|
| Where are the buttons stored? | `AppSettings` (machine-wide, all songs), key `strip.buttons`, like saved layouts. Not the `.songsmith` file. |
| Which commands can be added? | The curated list below (about 27 fixed commands) **plus one "apply layout" entry per saved layout**. |
| What happens to a layout button when its layout is renamed or deleted? | It **follows the layout**: rename updates the button's name, delete removes the button. Buttons never go stale. |
| Duplicates | Each fixed command and each layout can be on the strip once; already-added entries are greyed out in the picker. |
| Removal | Right-click a button ▸ "Remove from strip". |
| Reordering | Not in v1 (remove and re-add). |
| Labels | Short text, never icons. |
| Overflow (left third too narrow) | Buttons that do not fit move into a `»` overflow menu at the right edge of the area. Rejected: plain clipping (hides buttons unreachably), a scroll bar (extra gesture, fights the resizable dividers). |
| Command dispatch | Reuse `MainWindow::menuItemSelected` through a `runCommand(int)` callback. No second dispatcher. |
| Enabled state | Same as the main menu: a button is disabled when its command is disabled in `MenuState`. |

## Pickable commands

Grouped like the main menu; the picker uses one submenu per group.

| Group | Offered | Deliberately left out |
|---|---|---|
| File | New, Open…, Save, Save As…, Import MIDI…, Export MIDI…, Export ABC…, Preferences… | Quit (too easy to hit by accident) |
| Edit | Undo, Redo, Split, Delete, Quantize, Tempo/Meter…, Select All, Select All Tracks, Select All Sections | Grid-size items (a radio submenu, not an action) |
| Song | Default Parts, Run Converter, SoundFont… | none |
| Transport | Rewind One Bar, Clear Marker | Play/Pause, Stop, Go to Start/End (the centre area already has them) |
| View | Source, MIDI Editor, LOTRO Preview, Tracks, Parts, Mixer, Diagnostics, Export ABC panel (toggles); Save Layout; Manage Layouts… | none |
| Apply Layout ▸ | one entry per saved layout | none |
| Help | none | About |

Panel-toggle buttons show a "pressed" look while the panel is visible,
matching the menu tick. If that proves fiddly in the plan it may be dropped
without changing the rest of the design.

## Components

### 1. `StripButtons` (pure data, JUCE-free)

New `Source/UI/Dock/StripButtons.{h,cpp}`, modelled on `SavedLayouts`.

```cpp
namespace lotro::dock {

struct StripButton
{
    enum class Kind { command, applyLayout };
    Kind kind = Kind::command;
    std::string key;   // command: stable command key ("file.exportMidi"); applyLayout: the layout's name
};

using StripButtons = std::vector<StripButton>;

bool contains (const StripButtons&, const StripButton&);          // same kind and key (layout names compare case-insensitively)
bool addButton (StripButtons&, StripButton);                      // false (no change) if already present
void removeButton (StripButtons&, std::size_t index);
void renameLayoutButton (StripButtons&, const std::string& oldName, const std::string& newName);
void removeLayoutButton (StripButtons&, const std::string& name);

std::string toJson (const StripButtons&);
StripButtons stripButtonsFromJson (const std::string&);           // never throws; empty/corrupt -> {}; unknown kinds dropped
}
```

- Fixed commands are stored by a **stable string key**, not the numeric
  `MenuCommandId`, so reordering the enum cannot corrupt saved settings.
  `stripButtonsFromJson` keeps unknown command keys (a newer build's setting
  must survive a downgrade round-trip); the UI skips buttons whose key it does
  not know.
- Cap: `maxStripButtons = 10`; `addButton` refuses beyond it (the picker greys everything out once the strip is full). Only about 4–6 fit in the default left area; the rest sit behind `»`.
- `AppSettings` gains `stripButtons()` / `setStripButtons()` (key
  `strip.buttons`), like `savedLayouts()`.

### 2. Pickable-command table (`MenuModel.h`)

A constexpr table of `{ group, label, key, MenuCommandId }` for the fixed
commands above. It is the single source for the picker menu, the key↔id
mapping and button labels. A test asserts every table id is in
`allCommandIds` and every key is unique.

Enabled/pressed state for a command button is read from the existing menu
model for the current `MenuState` (find the item by id), so there is no second
source of truth. Layout buttons are always enabled while the layout exists.

### 3. `StripButtonsBar` (JUCE component)

New `Source/UI/Playback/StripButtonsBar.{h,cpp}`, owned by `TransportStrip`,
placed inside `leftArea` (a sibling of `TransportControls`; `TransportStrip`
itself stays thin).

- Constructed with callbacks, no knowledge of `MainWindow`:
  - `std::function<void (const StripButtons&)> onChanged` — user added/removed a button.
  - `std::function<void (const StripButton&)> onActivate` — a button was clicked.
  - `std::function<ButtonView (const StripButton&)> viewOf` — label, enabled, pressed.
  - `std::function<std::vector<std::string> ()> layoutNames` — for the picker.
- Layout: `+` at the far left, then the buttons in order. Buttons are sized
  to their text; any that do not fit are replaced by a `»` button whose menu
  lists them (and runs them).
- `+` opens the grouped `PopupMenu`; already-added entries are disabled.
- Right-click a button: popup with "Remove from strip".
- `refresh()` re-reads label/enabled/pressed for every button; called whenever
  `MainWindow` refreshes menu state.

### 4. `MainWindow` wiring

- Holds `StripButtons stripButtons`, loaded from `AppSettings` at startup and
  saved on every `onChanged` (same failure-notice path as `persistSavedLayouts`).
- `onActivate`: command button → `menuItemSelected (id, 0)`; layout button →
  look the layout up by name and call `applySavedLayout (index)`; a name with
  no match does nothing.
- **Follow-the-layout rule:** `ManageLayoutsPanel` gains `onRenamed (oldName,
  newName)` and `onRemoved (name)` callbacks (today it only has a single
  persist callback and cannot say which change happened). `MainWindow` forwards
  them to `renameLayoutButton` / `removeLayoutButton`, then persists and
  refreshes the bar.
- The bar is refreshed from the same place the menu state is rebuilt, so
  enabled/pressed state stays current.

## Error handling

- Corrupt or missing `strip.buttons` → empty strip; the `+` still works.
- Unknown command key → button not shown, but kept in the stored list.
- Layout button whose layout no longer exists (e.g. hand-edited settings) →
  shown disabled; right-click still removes it.

## Testing (TDD, integration-leaning)

- `StripButtons_tests`: add/duplicate refusal/cap of 10, remove, rename and remove
  layout button (case-insensitive), JSON round-trip, corrupt input, unknown
  kind dropped, unknown command key kept.
- `MenuModel_tests`: pickable table ids ⊂ `allCommandIds`, keys unique, no
  Quit/About/transport-centre ids.
- `AppSettings_tests`: `strip.buttons` default, round-trip.
- `StripButtonsBar_tests` (message-thread, no window shown): `+` menu content
  and disabled-when-present; adding calls `onChanged`; clicking calls
  `onActivate`; right-click remove; overflow moves trailing buttons to `»`
  when the bar is narrowed.
- `MainWindow`-level: a command button dispatches the right
  `MenuCommandId`; a layout button applies the layout; renaming and deleting a
  layout in the Manage dialog updates/removes its button and persists.
- The GUI is never launched by agents; appearance and the overflow feel are
  verified by the user.

## Docs

`docs/ARCHITECTURE.md` §9.19/§9.20 (new "Strip buttons" bullet),
`docs/UI_GUIDE.md` (transport strip left area), and this spec's status line.

## Out of scope

Reordering by drag, per-song buttons, icons, user-defined macros or
keyboard shortcuts for buttons, and the transport divider persistence item.
