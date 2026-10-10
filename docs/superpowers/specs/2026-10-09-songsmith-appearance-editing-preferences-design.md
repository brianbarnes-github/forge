# Songsmith Preferences ▸ Appearance and Editing pages — design

Date: 2026-10-09. Status: draft, awaiting review (sub-project 3 of 3; follows
`2026-10-09-songsmith-playback-preferences-design.md`).

Sub-projects: 1 hardening (done), 2 Playback page (done), **3 Appearance and
Editing pages (this spec)**.

## Intent

File ▸ Preferences… gains two pages, **Appearance** and **Editing**, holding four
settings that today are hard-wired or not remembered. Each default equals today's
behaviour, so nobody who never opens the pages sees a change.

Success: the user can switch the instrument band on or off and the change shows at
once; switch follow-the-playhead on or off and playback obeys at once; choose the
grid a newly opened editor starts on; and choose whether the main window reopens
where it was closed. All four survive a restart.

Said by the user: all of the previously deferred candidates **except wheel
sensitivity** (dropped: a user-tunable wheel speed can lead to a bad setting that
is hard to recover from). Chosen with the user: push settings from `MainWindow`
into the views (not views reading `AppSettings`, not a shared prefs struct);
pages reach the app through `PreferencesServices` (sub-project 2).
Assumed (not said): which setting sits on which page; the grid as a dropdown.

## Scope

In: four typed `AppSettings` entries; `PreferencesServices::applyViewSettings`;
`AppearancePreferencesPage` and `EditingPreferencesPage`; plain setters on the
views and on `SongsmithMainComponent`; `MainWindow` applying the settings at
startup and on change; the launch-time placement switch.

Out: wheel speed; reset-to-defaults; OK/Cancel/Apply; settings import/export;
any change to `Source/Core/` (all of this is UI behaviour); any other page.

| Page | Setting | Key | Default (= today) | Applies |
|---|---|---|---|---|
| Appearance | Show instrument band | `appearance.showRangeBand` | on | live |
| Appearance | Restore window placement | `appearance.restoreWindowPlacement` | on | next launch |
| Editing | Default grid size | `editing.defaultGrid` | Off | when an editor opens |
| Editing | Follow playhead | `editing.followPlayhead` | on | live |

## 1. `AppSettings`

Typed accessors in the style of `soundFontPath`, each setter returning the save
result (and updating `lastSaveFailed()` via the existing `save()`):

- `bool showRangeBand() const` / `bool setShowRangeBand (bool)`
- `bool restoreWindowPlacement() const` / `bool setRestoreWindowPlacement (bool)`
- `bool followPlayhead() const` / `bool setFollowPlayhead (bool)`
- `GridSize defaultGrid() const` / `bool setDefaultGrid (GridSize)`

Booleans reuse the existing `read`/`write` helpers, so an absent key reads **on**.
The grid is stored as `"off" | "quarter" | "eighth" | "sixteenth"`; absent or
unrecognised reads `GridSize::Off`. `GridSize` is the existing header-only enum
(`Source/UI/GridSize.h`), so `AppSettings.h` includes it; no JUCE dependency added.

## 2. `PreferencesServices`

One new member:

```cpp
std::function<void ()> applyViewSettings;   // re-read the settings and push them to the views
```

A page writes through `services.settings`, then calls `services.applyViewSettings()`
and its `onChanged` (the existing save-failure notice refresh), from its click
handlers only. The rule for pages is unchanged: no use of services in a destructor.

## 3. Views and `MainWindow`

Plain setters, no `AppSettings` dependency in any view:

- `PianoRollComponent::setShowRangeBand (bool)` — when off, `drawRangeBand` paints
  nothing (the band and its red out-of-range zones). The band's data
  (`setPreviewRangeBand`) is still set, so turning it back on repaints correctly.
  Ghost and dropped-note overlays are unaffected.
- `PianoRollComponent::setFollowPlayhead (bool)` and
  `TrackListComponent::setFollowPlayhead (bool)` — when off, `followPlayhead (…)`
  returns before moving the view.
- `SongsmithMainComponent::setShowRangeBand`, `setFollowPlayhead`,
  `setDefaultGridSize` — apply to the existing views, and remember the values so a
  track editor window opened later (its roll is created on demand) gets them too.
  A newly opened editor starts on `setDefaultGridSize`'s grid via the same
  `gridSizeToTicks` path Edit ▸ Grid size uses. Edit ▸ Grid size still overrides
  it for that window. An already-open editor keeps its grid when the default
  changes.

`MainWindow` implements `applyViewSettings` as: read the four-setting subset it
pushes (band, follow, default grid) from `appSettings` and call the three
`SongsmithMainComponent` setters. It calls the same function once at startup, after
the body exists.

**Restore window placement** is read once in the `MainWindow` constructor: when off,
the window is centred at its default size and the saved placement and maximised
state are not applied. Quit still saves the placement, so switching the setting
back on restores the last-closed position. The setting takes effect next launch;
the page says so.

## 4. Pages

Both follow `PlaybackPreferencesPage`'s shape: a component constructed over
`PreferencesServices&`, widgets set from the settings in the constructor, writes in
click/changed handlers, then `applyViewSettings` and `onChanged`.

- **Appearance:** two checkboxes, "Show the instrument range band in the LOTRO
  preview" and "Restore the window position and size on launch" (with a note that
  it applies at the next launch).
- **Editing:** a dropdown "Default grid size" (Off, 1/4, 1/8, 1/16, with a note
  that it applies to editors opened afterwards) and a checkbox "Follow the
  playhead during playback".

The registry gains two entries in the order General, Import, Playback, Appearance,
Editing. Testing accessors (`…ForTesting`) follow the Playback page's pattern.

## Testing

TDD, headless where possible:

- `AppSettings_tests`: each accessor's default, round trip, persistence under its
  key, unrecognised grid value reads Off, failed-save reporting (the existing
  blocked-directory pattern).
- Page tests with a fake `PreferencesServices`: controls reflect the settings; a
  click writes the setting, calls `applyViewSettings` and `onChanged` once; a failed
  save still applies.
- `PianoRollComponent` / `TrackListComponent`: with follow off, `followPlayhead`
  leaves the view alone (extend the existing `followPlayheadForTesting` cases); with
  the band off the band is not painted (pixel or paint-count check in the existing
  style for the preview roll).
- `SongsmithMainComponent`: a track editor opened after `setDefaultGridSize`
  starts on that grid; a setting pushed before the editor existed reaches it.
- `PreferencesDialog_tests`: the page list is General, Import, Playback,
  Appearance, Editing.
- Not headless (`MainWindow.cpp` is outside the test binary): startup apply and the
  launch-time placement switch. Covered by a `forge_ui` Windows build and a manual
  checklist: (a) band off hides it at once and back on restores it; (b) follow off
  stops page-flipping during playback in both views; (c) default grid applies to the
  next editor opened, the menu still overrides; (d) placement off launches
  centred, on restores the last position; (e) all four survive a restart.

## Risks

- A view missed by a setter keeps old behaviour: covered by tests on each view and
  the manual checklist.
- `MainWindow.cpp` changes are not headless-tested: kept to the one
  `applyViewSettings` function and the placement `if`.
- Grid default vs. the menu: documented behaviour above; the menu never writes the
  setting.
