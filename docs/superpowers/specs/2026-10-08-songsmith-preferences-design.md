# Songsmith Preferences dialog — design

Date: 2026-10-08. Status: awaiting review.

## Intent

File ▸ **Preferences…** opens a fully modal dialog: a page tree on the left, a
splitter, and the selected page on the right. **General** is the first page and
holds two confirmation toggles. Later pages (Playback, Appearance, …) are
expected; adding one must be one registry entry plus its component.

Success: the user can switch the unsaved-changes prompt (and the replace-file
prompt) off and on from the dialog, the choice survives a restart, and nothing
else in the window is clickable while the dialog is open.

Said by the user: menu placement (own section between Export and Quit), modal,
tree + splitter + canvas, General first, the unsaved-changes prompt as the
first option, "find what else could be a setting". Chosen with the user:
**off means discard silently** (no auto-save); General ships with **both
confirmations** only.

## Scope

In: menu entry, `AppSettings`, `PreferencesDialog` (tree, splitter, page host),
General page (two checkboxes), wiring the two prompts to the settings.

Out (candidate later pages, found in the source, not built now): SoundFont path
with Browse/Clear (today Song ▸ SoundFont… has no way to clear), default grid
size, wheel sensitivity (track list 50 px/notch, row resize 4 px, pitch-row
resize 1 px), follow-the-playhead, show/hide the instrument band, restore window
placement on launch. No OK/Cancel/Apply, no reset-to-defaults, no import/export
of settings.

## Menu

- `FilePreferences` added to `MenuCommandId` and `allCommandIds` (array size 28 → 29).
- `MenuModel`: File = New, Open…, Save, Save As…, Import ▸, Export ▸, **separator,
  Preferences…, separator**, Quit. Always enabled, no shortcut.
- `MainWindow::menuItemSelected` handles it by opening the dialog.

## AppSettings

Header-only, `Source/UI/AppSettings.h`, over a `juce::PropertiesFile&` (the one
`MainWindow` already owns, `SongSmith.settings`). Typed accessors, key names and
defaults live in this one file.

| Field | Key | Default |
|---|---|---|
| `askToSaveUnsavedChanges` | `confirm.unsavedChanges` | true |
| `askBeforeReplacingFile` | `confirm.replaceFile` | true |

Setters write and call `saveIfNeeded()` at once (as the SoundFont path does), so
a crash after toggling still keeps the choice. The existing `soundFontPath` and
window-placement keys are untouched and not migrated.

Pure decision functions (no JUCE UI) so the hooks are testable:
`shouldPromptForUnsavedChanges (settings, isDirty)` and
`shouldConfirmReplace (settings)`.

## Applying the settings

- `MainWindow::guardHooks()` prompt hook: when the setting is off it answers
  `DiscardChoice::DontSave` without showing the box. New, Open, `.songsmith`
  drops and Quit all route through these hooks, so all four follow it.
- Save As overwrite check: when off, the file is written without the
  "Replace file?" box (the extension is still appended first).
- Read at the moment of use, so a toggle takes effect immediately, no restart.

## PreferencesDialog

`Source/UI/Preferences/PreferencesDialog.{h,cpp}`; launched like `AboutBox`
(`juce::DialogWindow::LaunchOptions::launchAsync`, which enters modal state), so
the main window and its menus are blocked until it closes. Native title bar,
centred over the main window, resizable, single instance (opening it again while
open is impossible because it is modal).

Layout, one content component:
- Left: `juce::TreeView` with a hidden root and one item per registered page.
  General is selected on open.
- Middle: the project's `SplitterComponent` (`leftRight`), initial fraction 0.25
  (its clamp is [0.15, 0.85]).
- Right: a page host showing the selected page's component, with a page title.
- A Close button bottom-right; Escape and the title-bar close also close it.

Pages are data: `struct PreferencesPage { juce::String name; std::function<std::unique_ptr<juce::Component> (AppSettings&)> make; }`
in a `preferencePages()` list. Adding a page = one entry + one component.

General page (`GeneralPreferencesPage`): two `ToggleButton`s bound to
`AppSettings`, each applied on click. The unsaved-changes one carries a muted
note: "When off, New, Open and Quit discard unsaved edits without asking."

## Boundary

All UI-only: `Source/UI/` plus `Tests/`. Nothing in `Source/Core/` changes
(`forge-engine-ui-boundary`). No new dependencies.

## Testing

- `AppSettings`: defaults; set/get round trip; persistence across a re-opened
  `PropertiesFile`.
- `shouldPromptForUnsavedChanges` / `shouldConfirmReplace` truth tables.
- `MenuModel`: Preferences sits between Export and Quit with separators around
  it; its id is in `allCommandIds`; always enabled.
- `PreferencesDialog` content (constructed directly, no window launch): tree has
  General; General is selected on open and its page is shown; toggling each
  checkbox writes through to `AppSettings`; the page registry drives the tree.
- Not covered headless: `MainWindow.cpp` is outside the test binary, so the menu
  dispatch, real modality and the two hook call sites are checked by hand
  (toggle off, then New/Quit on a dirty Song and Save As over an existing file).

## Risks

- Off = silent data loss is the requested behaviour; mitigated only by the
  on-page note and the default being on.
- `settings->saveIfNeeded()` on every toggle is a tiny file write; acceptable
  for a click-driven setting.
