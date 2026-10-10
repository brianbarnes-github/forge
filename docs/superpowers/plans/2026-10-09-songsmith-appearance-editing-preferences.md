# Songsmith Preferences ▸ Appearance and Editing pages Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add Preferences ▸ Appearance (show instrument band, restore window placement) and ▸ Editing (default grid size, follow playhead) pages backed by four typed `AppSettings` entries, applied live where the spec says.

**Architecture:** `AppSettings` gets four typed accessors. `PreferencesServices` gets one hook, `applyViewSettings`. Views get plain setters (no `AppSettings` dependency); `SongsmithMainComponent` fans them out and remembers them for editor windows opened later; `MainWindow` implements the hook (read settings, call the three `SongsmithMainComponent` setters), calls it once at startup, and honours the placement switch in its constructor.

**Tech Stack:** C++20, JUCE, Catch2, CMake/Ninja. No new dependencies.

**Spec:** `docs/superpowers/specs/2026-10-09-songsmith-appearance-editing-preferences-design.md`

## Global Constraints

- Nothing in `Source/Core/` changes (`forge-engine-ui-boundary`); no new dependencies; wheel speed is **not** built.
- Keys and defaults, verbatim: `appearance.showRangeBand` on; `appearance.restoreWindowPlacement` on; `editing.defaultGrid` stored as `"off" | "quarter" | "eighth" | "sixteenth"`, absent/unrecognised → `GridSize::Off`; `editing.followPlayhead` on. Booleans use the existing `read`/`write` helpers (absent key reads on).
- Every default equals today's behaviour.
- Views never see `AppSettings`; only plain `bool` / `GridSize` setters.
- A page uses `PreferencesServices` / `AppSettings` only in its constructor and in click / changed handlers, never in its destructor.
- Applies: band and follow live; default grid when an editor opens (an already-open editor keeps its grid; Edit ▸ Grid size never writes the setting); window placement at next launch (quit still saves the placement).
- TDD wherever headless-testable. Types on everything; no `any`-like escapes.
- Conventional commits; trailer `Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>`. **Commit only when the user has authorised it** (confirm once at the start of execution). Never push without explicit approval. Never launch the GUI (`song-smith` is user-only); never touch `/mnt/c/Apps/SongSmith/` (deploy is the user's step).
- Build/test from the repo root: `cmake --build build && ctest --test-dir build --output-on-failure`. Baseline: 833/833. Windows compile check: `./build-windows.sh forge_ui; echo $?` (exit 0, no warnings in touched files).
- `CMakeLists.txt` (root, `forge_ui` sources ~line 155) and `Tests/CMakeLists.txt` (test files ~line 73, UI sources ~line 118) list files explicitly: new `.cpp` files need entries in **both**.
- Test binary direct run (ctest tags don't match test-case tags): `./build/Tests/forge_tests "<test case name>"`.

## Review Focus

1. A saved `editing.defaultGrid` that is garbage or from a future version reads as Off, never crashes or picks a random grid (Task 1 test).
2. Settings file unwritable when toggling or choosing on either page: the change still applies for the session and the existing save-failure notice shows (Task 4 test).
3. Band switched off then on again while a preview is displayed repaints exactly as before (Task 2 test).
4. Follow switched off then on mid-playback: the next position update page-flips as it does today (Task 2 tests).
5. A track editor already open when "follow" changes gets it live, and one open when the default grid changes keeps its grid (Task 3 tests); window placement off then on restores the last-closed position (Task 5 manual list).

---

### Task 1: `AppSettings` entries for the four settings

**Files:**
- Modify: `Source/UI/AppSettings.h`
- Test: `Tests/AppSettings_tests.cpp`

**Interfaces:**
- Produces: `bool AppSettings::showRangeBand() const` / `bool setShowRangeBand (bool)`; `bool restoreWindowPlacement() const` / `bool setRestoreWindowPlacement (bool)`; `bool followPlayhead() const` / `bool setFollowPlayhead (bool)`; `GridSize defaultGrid() const` / `bool setDefaultGrid (GridSize)`. Setters return the save result (existing `save()` also updates `lastSaveFailed()`).

- [ ] **Step 1: Write the failing tests** — append to `Tests/AppSettings_tests.cpp` (check the file already includes `UI/AppSettings.h`; `GridSize` comes in through it after step 3):

```cpp
TEST_CASE ("AppSettings: the view settings default to today's behaviour and round-trip", "[app-settings]")
{
    auto file = juce::File::createTempFile (".settings");
    const juce::ScopeGuard cleanup { [&] { file.deleteFile(); } };
    {
        juce::PropertiesFile props (file, {});
        AppSettings settings (props);
        CHECK (settings.showRangeBand());
        CHECK (settings.restoreWindowPlacement());
        CHECK (settings.followPlayhead());
        CHECK (settings.defaultGrid() == GridSize::Off);

        CHECK (settings.setShowRangeBand (false));
        CHECK (settings.setRestoreWindowPlacement (false));
        CHECK (settings.setFollowPlayhead (false));
        CHECK (settings.setDefaultGrid (GridSize::Eighth));
    }
    juce::PropertiesFile reopened (file, {});
    AppSettings settings (reopened);
    CHECK_FALSE (settings.showRangeBand());
    CHECK_FALSE (settings.restoreWindowPlacement());
    CHECK_FALSE (settings.followPlayhead());
    CHECK (settings.defaultGrid() == GridSize::Eighth);
    CHECK (reopened.getValue ("appearance.showRangeBand") == "0");
    CHECK (reopened.getValue ("appearance.restoreWindowPlacement") == "0");
    CHECK (reopened.getValue ("editing.followPlayhead") == "0");
    CHECK (reopened.getValue ("editing.defaultGrid") == "eighth");
}

TEST_CASE ("AppSettings: every grid size round-trips and an unrecognised stored grid reads Off", "[app-settings]")
{
    auto file = juce::File::createTempFile (".settings");
    const juce::ScopeGuard cleanup { [&] { file.deleteFile(); } };
    juce::PropertiesFile props (file, {});
    AppSettings settings (props);

    for (const auto size : { GridSize::Off, GridSize::Quarter, GridSize::Eighth, GridSize::Sixteenth })
    {
        CHECK (settings.setDefaultGrid (size));
        CHECK (settings.defaultGrid() == size);
    }

    props.setValue ("editing.defaultGrid", "thirtysecond");   // hand-edited / future value
    CHECK (settings.defaultGrid() == GridSize::Off);
    props.setValue ("editing.defaultGrid", "");
    CHECK (settings.defaultGrid() == GridSize::Off);
}

TEST_CASE ("AppSettings: the view-setting setters report a failed save but still apply", "[app-settings]")
{
    auto blocked = juce::File::createTempFile (".settings");
    REQUIRE (blocked.createDirectory().wasOk());
    const juce::ScopedJuceInitialiser_GUI juceInit;
    const juce::ScopeGuard cleanup { [&] { blocked.deleteRecursively(); } };
    juce::PropertiesFile props (blocked, {});
    AppSettings settings (props);

    CHECK_FALSE (settings.setShowRangeBand (false));
    CHECK (settings.lastSaveFailed());
    CHECK_FALSE (settings.showRangeBand());

    CHECK_FALSE (settings.setDefaultGrid (GridSize::Quarter));
    CHECK (settings.lastSaveFailed());
    CHECK (settings.defaultGrid() == GridSize::Quarter);
}
```

- [ ] **Step 2: Run to verify they fail** — `cmake --build build` → compile error (`showRangeBand` not a member). Expected.
- [ ] **Step 3: Implement** — in `AppSettings.h` add `#include "GridSize.h"` under the existing include; in the public section after `clearSoundFontPath()`:

```cpp
    // Preferences > Appearance / Editing. Defaults equal the behaviour before the pages existed.
    bool showRangeBand() const { return read (keyShowRangeBand); }
    bool setShowRangeBand (bool on) { return write (keyShowRangeBand, on); }

    bool restoreWindowPlacement() const { return read (keyRestorePlacement); }
    bool setRestoreWindowPlacement (bool on) { return write (keyRestorePlacement, on); }

    bool followPlayhead() const { return read (keyFollowPlayhead); }
    bool setFollowPlayhead (bool on) { return write (keyFollowPlayhead, on); }

    // What a newly opened track editor starts on. Absent or unrecognised reads Off.
    GridSize defaultGrid() const
    {
        const auto stored = file.getValue (keyDefaultGrid);
        if (stored == "quarter")   return GridSize::Quarter;
        if (stored == "eighth")    return GridSize::Eighth;
        if (stored == "sixteenth") return GridSize::Sixteenth;
        return GridSize::Off;
    }
    bool setDefaultGrid (GridSize size)
    {
        const char* name = "off";
        switch (size)
        {
            case GridSize::Quarter:   name = "quarter";   break;
            case GridSize::Eighth:    name = "eighth";    break;
            case GridSize::Sixteenth: name = "sixteenth"; break;
            case GridSize::Off:       break;
        }
        file.setValue (keyDefaultGrid, name);
        return save();
    }
```
and with the other keys: `static constexpr const char* keyShowRangeBand = "appearance.showRangeBand"; static constexpr const char* keyRestorePlacement = "appearance.restoreWindowPlacement"; static constexpr const char* keyFollowPlayhead = "editing.followPlayhead"; static constexpr const char* keyDefaultGrid = "editing.defaultGrid";`.
- [ ] **Step 4: Run** — `cmake --build build && ctest --test-dir build -R AppSettings --output-on-failure` → pass; full suite `ctest --test-dir build 2>&1 | tail -3` → 836 pass.
- [ ] **Step 5: Commit** (if authorised): `git add Source/UI/AppSettings.h Tests/AppSettings_tests.cpp && git commit -m "feat(ui): AppSettings gets the Appearance and Editing settings"`

---

### Task 2: Band and follow switches on the views

**Files:**
- Modify: `Source/UI/PianoRollComponent.h`, `Source/UI/PianoRollComponent.cpp`, `Source/UI/TrackListComponent.h`, `Source/UI/TrackListComponent.cpp`
- Test: `Tests/PianoRollComponent_tests.cpp`, `Tests/TrackListComponent_tests.cpp`

**Interfaces:**
- Produces: `void PianoRollComponent::setShowRangeBand (bool)` / `bool getShowRangeBand() const noexcept`; `void PianoRollComponent::setFollowPlayhead (bool)` / `bool getFollowPlayhead() const noexcept`; `void TrackListComponent::setFollowPlayhead (bool)` / `bool getFollowPlayhead() const noexcept`. Defaults true (today's behaviour).

- [ ] **Step 1: Write the failing tests.**

In `Tests/PianoRollComponent_tests.cpp`, after the existing "following a playhead at the end of a fitted view keeps fitted mode" test, add (reuses `PlayheadRollFixture`, `Access`):

```cpp
TEST_CASE ("PianoRollComponent: with follow switched off a playing playhead never scrolls the view; switching it on restores the page-flip", "[piano-roll][playhead]")
{
    PlayheadRollFixture f;
    f.roll.setPlayback (&f.controller);
    for (int i = 0; i < 15; ++i)
        Access::zoom (f.roll, 1.0f);
    auto& viewport = Access::viewport (f.roll);
    viewport.setViewPosition (0, viewport.getViewPositionY());
    f.controller.seekToTick (6000.0);
    REQUIRE (f.roll.xForTickInComponent (6000.0) >= viewport.getMaximumVisibleWidth());
    CHECK (f.roll.getFollowPlayhead());   // default: on

    f.roll.setFollowPlayhead (false);
    Access::follow (f.roll, /*playing*/ true);
    CHECK (viewport.getViewPositionX() == 0);

    f.roll.setFollowPlayhead (true);
    Access::follow (f.roll, /*playing*/ true);
    CHECK (viewport.getViewPositionX() > 0);
}
```

And, after the "an upward-folding note's ghost and range band render on-canvas" test, add:

```cpp
TEST_CASE ("PianoRollComponent: switching the band off stops painting it, and switching it on repaints exactly as before", "[piano-roll]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    PreviewNote note;
    note.prePitch = 60;
    note.postPitch = 60;
    note.startTick = 0;
    note.durationTicks = ticksPerQuarter;
    note.state = NoteState::Normal;
    PreviewNoteSource source ({ note });

    PianoRollComponent roll (PianoRollComponent::Role::Preview);
    roll.setBounds (0, 0, viewportWidth, viewportHeight);
    roll.setNoteSource (&source, ticksPerQuarter, {});
    roll.setPreviewRangeBand ({ 55, 66 });
    CHECK (roll.getShowRangeBand());   // default: on

    const int height = Access::canvas (roll).getHeight();
    const auto render = [&]
    {
        juce::Image image (juce::Image::ARGB, viewportWidth, height, true, juce::SoftwareImageType());
        juce::Graphics g (image);
        Access::paintCanvas (roll, g, { 0, 0, viewportWidth, height });
        return image;
    };
    const auto sameImage = [&] (const juce::Image& a, const juce::Image& b)
    {
        for (int y = 0; y < height; ++y)
            for (int x = 0; x < viewportWidth; ++x)
                if (a.getPixelAt (x, y) != b.getPixelAt (x, y))
                    return false;
        return true;
    };

    const auto withBand = render();
    roll.setShowRangeBand (false);
    const auto withoutBand = render();
    CHECK_FALSE (sameImage (withBand, withoutBand));   // the band and its wash are gone

    roll.setShowRangeBand (true);
    CHECK (sameImage (withBand, render()));
}
```

In `Tests/TrackListComponent_tests.cpp`, after "when not playing, seeking never scrolls the view", add:

```cpp
TEST_CASE ("TrackListComponent: with follow switched off a playing seek never scrolls the view; switching it on restores the page-flip", "[track-list][playhead]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SongDocument doc;
    auto track = doc.addTrack ("A", (int) 0xFFAABBCC, 0, 0);
    lotro::playbacktest::addNote (track, 60, 0, 96000);
    lotro::playbacktest::RecordingSink sink;
    PlaybackController controller (doc, sink);
    controller.flushRebuild();
    TrackListComponent list (doc);
    list.setPlayback (&controller);
    list.setSize (800, 300);
    list.fitTimelineToDocument();
    Access::zoomIn (list);
    CHECK (list.getFollowPlayhead());   // default: on

    controller.seekToTick (50000.0);
    list.setFollowPlayhead (false);
    list.followPlayheadForTesting (/*playing*/ true);
    CHECK (Access::scrollOffsetTicks (list) == Catch::Approx (0.0));

    list.setFollowPlayhead (true);
    list.followPlayheadForTesting (/*playing*/ true);
    CHECK (Access::scrollOffsetTicks (list) == Catch::Approx (50000.0));
}
```

- [ ] **Step 2: Run to verify they fail** — `cmake --build build` → compile errors (`getFollowPlayhead`, `setShowRangeBand` not members). Expected.
- [ ] **Step 3: Implement.**
  - `PianoRollComponent.h` public section (next to `setPreviewRangeBand`): `void setShowRangeBand (bool show); bool getShowRangeBand() const noexcept { return showRangeBand; } void setFollowPlayhead (bool on) noexcept { followEnabled = on; } bool getFollowPlayhead() const noexcept { return followEnabled; }`; private members (next to `rangeBand`): `bool showRangeBand = true; bool followEnabled = true;`.
  - `PianoRollComponent.cpp`: `void PianoRollComponent::setShowRangeBand (bool show) { if (showRangeBand == show) return; showRangeBand = show; canvas.repaint(); }`; in `drawRangeBand` change the guard to `if (! showRangeBand || role != Role::Preview || rangeBand.isEmpty() || clip.isEmpty()) return;`; in `followPlayhead` change the first guard to `if (! followEnabled || ! playing || playback == nullptr || playhead == nullptr) return;`.
  - `TrackListComponent.h` public: `void setFollowPlayhead (bool on) noexcept { followEnabled = on; } bool getFollowPlayhead() const noexcept { return followEnabled; }`, private `bool followEnabled = true;`; `TrackListComponent.cpp` `followPlayhead`: first guard becomes `if (! followEnabled || ! playing || playback == nullptr) return;`.
- [ ] **Step 4: Run** — `cmake --build build && ctest --test-dir build -R "PianoRoll|TrackList" --output-on-failure` → pass; full suite → 839 pass.
- [ ] **Step 5: Commit** (if authorised): `git add Source/UI/PianoRollComponent.* Source/UI/TrackListComponent.* Tests/PianoRollComponent_tests.cpp Tests/TrackListComponent_tests.cpp && git commit -m "feat(ui): piano roll and track list can switch the range band and playhead-follow off"`

---

### Task 3: `SongsmithMainComponent` fan-out and the default grid

**Files:**
- Modify: `Source/UI/TrackEditorWindow.h`, `Source/UI/SongsmithMainComponent.h`, `Source/UI/SongsmithMainComponent.cpp`
- Test: `Tests/SongsmithMainComponent_tests.cpp`

**Interfaces:**
- Consumes: Task 2's `PianoRollComponent::setShowRangeBand/setFollowPlayhead/getFollowPlayhead`, `TrackListComponent::setFollowPlayhead/getFollowPlayhead`.
- Produces: `void TrackEditorWindow::setFollowPlayhead (bool)` / `bool getFollowPlayhead() const`; `void SongsmithMainComponent::setShowRangeBand (bool)`, `void setFollowPlayhead (bool)`, `void setDefaultGridSize (GridSize)` (all public). Values are remembered so a track editor opened later receives them.

- [ ] **Step 1: Write the failing tests** — in `Tests/SongsmithMainComponent_tests.cpp`, add to `SongsmithMainComponentTestAccess`:

```cpp
        static PianoRollComponent& previewRoll (SongsmithMainComponent& c) { return c.previewRoll; }
        static bool activeEditorFollows (const SongsmithMainComponent& c)
        {
            return c.trackEditorWindow != nullptr && c.trackEditorWindow->getFollowPlayhead();
        }
```
and append these tests (after the "setActiveEditorGridSize resolves ticks…" test):

```cpp
TEST_CASE ("SongsmithMainComponent: a newly opened editor starts on the default grid; an open one keeps its grid; the menu still overrides", "[track-editor][view-settings]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SongDocument doc;
    auto track = doc.addTrack ("Track A", (int) 0xFFAABBCCu, 0, 0);
    const auto trackId = (juce::int64) track.getProperty (SongIDs::trackId);
    doc.getSourceMidiNode().setProperty (SongIDs::ticksPerQuarter, 480, nullptr);

    SongsmithMainComponent main (doc);
    main.setDefaultGridSize (GridSize::Eighth);
    Access::trackDoubleClicked (main, trackId);
    REQUIRE (main.isTrackEditorOpen());
    CHECK (Access::activeEditorGridTicks (main) == 240);

    main.setDefaultGridSize (GridSize::Quarter);          // changing the default...
    CHECK (Access::activeEditorGridTicks (main) == 240);  // ...leaves the open editor alone

    main.setActiveEditorGridSize (GridSize::Sixteenth);   // the menu overrides for this window
    CHECK (Access::activeEditorGridTicks (main) == 120);
}

TEST_CASE ("SongsmithMainComponent: the default grid is Off until set", "[track-editor][view-settings]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SongDocument doc;
    auto track = doc.addTrack ("Track A", (int) 0xFFAABBCCu, 0, 0);
    SongsmithMainComponent main (doc);
    Access::trackDoubleClicked (main, (juce::int64) track.getProperty (SongIDs::trackId));
    REQUIRE (main.isTrackEditorOpen());
    CHECK (Access::activeEditorGridTicks (main) == 0);
}

TEST_CASE ("SongsmithMainComponent: follow reaches the track list, the open editor and an editor opened later", "[track-editor][view-settings]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SongDocument doc;
    auto track = doc.addTrack ("Track A", (int) 0xFFAABBCCu, 0, 0);
    const auto trackId = (juce::int64) track.getProperty (SongIDs::trackId);
    SongsmithMainComponent main (doc);

    main.setFollowPlayhead (false);                       // before any editor exists
    CHECK_FALSE (Access::trackList (main).getFollowPlayhead());
    Access::trackDoubleClicked (main, trackId);
    REQUIRE (main.isTrackEditorOpen());
    CHECK_FALSE (Access::activeEditorFollows (main));     // the later editor got it

    main.setFollowPlayhead (true);                        // live, to the open editor
    CHECK (Access::activeEditorFollows (main));
    CHECK (Access::trackList (main).getFollowPlayhead());
}

TEST_CASE ("SongsmithMainComponent: the band switch reaches the preview roll", "[view-settings]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SongDocument doc;
    SongsmithMainComponent main (doc);
    CHECK (Access::previewRoll (main).getShowRangeBand());
    main.setShowRangeBand (false);
    CHECK_FALSE (Access::previewRoll (main).getShowRangeBand());
}
```

- [ ] **Step 2: Run to verify they fail** — `cmake --build build` → compile errors. Expected.
- [ ] **Step 3: Implement.**
  - `TrackEditorWindow.h` public, next to `setGridTicks`: `void setFollowPlayhead (bool on) { roll.setFollowPlayhead (on); } bool getFollowPlayhead() const { return roll.getFollowPlayhead(); }`.
  - `SongsmithMainComponent.h` public, next to `setActiveEditorGridSize`: `void setShowRangeBand (bool show); void setFollowPlayhead (bool follow); void setDefaultGridSize (GridSize size);` and private members: `bool showRangeBandSetting = true; bool followPlayheadSetting = true; GridSize defaultGridSize = GridSize::Off;`.
  - `SongsmithMainComponent.cpp`:

```cpp
void SongsmithMainComponent::setShowRangeBand (bool show)
{
    showRangeBandSetting = show;
    previewRoll.setShowRangeBand (show);
}

void SongsmithMainComponent::setFollowPlayhead (bool follow)
{
    followPlayheadSetting = follow;
    previewRoll.setFollowPlayhead (follow);
    trackList.setFollowPlayhead (follow);
    if (trackEditorWindow != nullptr)
        trackEditorWindow->setFollowPlayhead (follow);
}

// Remembered for editors opened later; an editor that is already open keeps its grid.
void SongsmithMainComponent::setDefaultGridSize (GridSize size)
{
    defaultGridSize = size;
}
```
  and in `trackDoubleClicked`, inside the `if (trackEditorWindow == nullptr)` block after `onClosed` is set: `trackEditorWindow->setFollowPlayhead (followPlayheadSetting); setActiveEditorGridSize (defaultGridSize);`.
- [ ] **Step 4: Run** — `cmake --build build && ctest --test-dir build -R SongsmithMainComponent --output-on-failure` → pass; full suite → 843 pass.
- [ ] **Step 5: Commit** (if authorised): `git add Source/UI/TrackEditorWindow.h Source/UI/SongsmithMainComponent.* Tests/SongsmithMainComponent_tests.cpp && git commit -m "feat(ui): SongsmithMainComponent applies the band, follow and default-grid settings"`

---

### Task 4: `applyViewSettings`, the two pages and the registry

**Files:**
- Create: `Source/UI/Preferences/AppearancePreferencesPage.{h,cpp}`, `Source/UI/Preferences/EditingPreferencesPage.{h,cpp}`, `Tests/ViewPreferencesPages_tests.cpp`
- Modify: `Source/UI/Preferences/PreferencesServices.h`, `Source/UI/Preferences/PreferencesDialog.cpp`, `CMakeLists.txt`, `Tests/CMakeLists.txt`, `Tests/PreferencesDialog_tests.cpp`

**Interfaces:**
- Consumes: Task 1's `AppSettings` accessors.
- Produces: `PreferencesServices::applyViewSettings` (`std::function<void ()>`, **last** member, default `{}`); `AppearancePreferencesPage (PreferencesServices&, std::function<void()> onChanged = {})` with `showBandToggleForTesting()`, `restorePlacementToggleForTesting()` (both `juce::ToggleButton&`); `EditingPreferencesPage (PreferencesServices&, std::function<void()> onChanged = {})` with `gridComboForTesting()` (`juce::ComboBox&`), `followToggleForTesting()` (`juce::ToggleButton&`). Registry order: General, Import, Playback, Appearance, Editing.

- [ ] **Step 1: Write the failing tests.** Create `Tests/ViewPreferencesPages_tests.cpp`:

```cpp
#include "UI/Preferences/AppearancePreferencesPage.h"
#include "UI/Preferences/EditingPreferencesPage.h"

#include <catch2/catch_test_macros.hpp>

using namespace lotro;

namespace
{
    // Fake app: counts applyViewSettings calls; the settings file is real so persistence is checked.
    struct FakeApp
    {
        juce::File settingsFile = juce::File::createTempFile (".settings");
        std::unique_ptr<juce::PropertiesFile> props = std::make_unique<juce::PropertiesFile> (settingsFile, juce::PropertiesFile::Options());
        AppSettings settings { *props };
        int applyCalls = 0;
        int changedCalls = 0;

        PreferencesServices services
        {
            settings,
            [] (const juce::File&) { return SoundFontLoad { SoundFontResult::loaded, {} }; },
            [] { return SoundFontLoad { SoundFontResult::loaded, {} }; },
            [] { return juce::String(); },
            [this] { ++applyCalls; }
        };

        ~FakeApp() { props.reset(); settingsFile.deleteFile(); }
    };
}

TEST_CASE ("Appearance page: the toggles reflect the settings", "[preferences][appearance-page]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    FakeApp app;
    app.settings.setShowRangeBand (false);
    AppearancePreferencesPage page (app.services);
    CHECK_FALSE (page.showBandToggleForTesting().getToggleState());
    CHECK (page.restorePlacementToggleForTesting().getToggleState());   // default on
}

TEST_CASE ("Appearance page: a click writes the setting, applies the view settings and notifies once", "[preferences][appearance-page]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    FakeApp app;
    AppearancePreferencesPage page (app.services, [&] { ++app.changedCalls; });

    page.showBandToggleForTesting().setToggleState (false, juce::sendNotification);
    CHECK_FALSE (app.settings.showRangeBand());
    CHECK (app.applyCalls == 1);
    CHECK (app.changedCalls == 1);

    page.restorePlacementToggleForTesting().setToggleState (false, juce::sendNotification);
    CHECK_FALSE (app.settings.restoreWindowPlacement());
    CHECK (app.applyCalls == 2);
    CHECK (app.changedCalls == 2);
}

TEST_CASE ("Editing page: the grid dropdown and follow toggle reflect the settings", "[preferences][editing-page]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    FakeApp app;
    app.settings.setDefaultGrid (GridSize::Sixteenth);
    app.settings.setFollowPlayhead (false);
    EditingPreferencesPage page (app.services);
    CHECK (page.gridComboForTesting().getText() == "1/16");
    CHECK_FALSE (page.followToggleForTesting().getToggleState());
}

TEST_CASE ("Editing page: choosing a grid and toggling follow write the settings, apply and notify", "[preferences][editing-page]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    FakeApp app;
    EditingPreferencesPage page (app.services, [&] { ++app.changedCalls; });

    page.gridComboForTesting().setSelectedId (3, juce::sendNotification);   // 1/8
    CHECK (app.settings.defaultGrid() == GridSize::Eighth);
    CHECK (app.applyCalls == 1);
    CHECK (app.changedCalls == 1);

    page.followToggleForTesting().setToggleState (false, juce::sendNotification);
    CHECK_FALSE (app.settings.followPlayhead());
    CHECK (app.applyCalls == 2);
    CHECK (app.changedCalls == 2);
}

TEST_CASE ("View pages: with an unwritable settings file the change still applies and the notice hook fires", "[preferences][appearance-page][editing-page]")
{
    auto blocked = juce::File::createTempFile (".settings");
    REQUIRE (blocked.createDirectory().wasOk());
    const juce::ScopedJuceInitialiser_GUI juceInit;
    const juce::ScopeGuard cleanup { [&] { blocked.deleteRecursively(); } };
    juce::PropertiesFile props (blocked, {});
    AppSettings settings (props);
    int applyCalls = 0;
    int changedCalls = 0;
    PreferencesServices services { settings,
                                   [] (const juce::File&) { return SoundFontLoad { SoundFontResult::loaded, {} }; },
                                   [] { return SoundFontLoad { SoundFontResult::loaded, {} }; },
                                   [] { return juce::String(); },
                                   [&] { ++applyCalls; } };

    {
        AppearancePreferencesPage page (services, [&] { ++changedCalls; });
        page.showBandToggleForTesting().setToggleState (false, juce::sendNotification);
        CHECK (settings.lastSaveFailed());
        CHECK_FALSE (settings.showRangeBand());   // still applied for the session
        CHECK (applyCalls == 1);
        CHECK (changedCalls == 1);
    }
    {
        EditingPreferencesPage page (services, [&] { ++changedCalls; });
        page.gridComboForTesting().setSelectedId (2, juce::sendNotification);   // 1/4
        CHECK (settings.lastSaveFailed());
        CHECK (settings.defaultGrid() == GridSize::Quarter);
        CHECK (applyCalls == 2);
        CHECK (changedCalls == 2);
    }
}
```

  In `Tests/PreferencesDialog_tests.cpp` change the page-list expectation to `CHECK (dialog.pageNames() == juce::StringArray { "General", "Import", "Playback", "Appearance", "Editing" });`.

  In `Tests/CMakeLists.txt`: add `ViewPreferencesPages_tests.cpp` under the test files (next to `PlaybackPreferencesPage_tests.cpp`, ~line 74) and `${CMAKE_SOURCE_DIR}/Source/UI/Preferences/AppearancePreferencesPage.cpp` and `.../EditingPreferencesPage.cpp` next to the other page sources (~line 118-121). In root `CMakeLists.txt` add both `.cpp` files next to `Source/UI/Preferences/PlaybackPreferencesPage.cpp` (~line 157).
- [ ] **Step 2: Run to verify they fail** — `cmake --build build` → configure/compile error (page headers missing). Expected.
- [ ] **Step 3: Implement.**
  - `PreferencesServices.h`: add as the **last** member (so existing positional initialisers still compile): `std::function<void ()> applyViewSettings = {};   // re-read the view settings and push them to the views`.
  - `AppearancePreferencesPage.h`:

```cpp
#pragma once

#include "PreferencesServices.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

namespace lotro
{
    // Preferences > Appearance. The band switch applies live; window placement applies at the
    // next launch. Uses `services` only in the constructor and in click handlers.
    class AppearancePreferencesPage : public juce::Component
    {
    public:
        explicit AppearancePreferencesPage (PreferencesServices& services, std::function<void()> onChanged = {});

        void resized() override;

        juce::ToggleButton& showBandToggleForTesting() { return showBand; }
        juce::ToggleButton& restorePlacementToggleForTesting() { return restorePlacement; }

    private:
        void notify();

        PreferencesServices&  services;
        std::function<void()> onChanged;
        juce::ToggleButton    showBand { "Show the instrument range band in the LOTRO preview" };
        juce::ToggleButton    restorePlacement { "Restore the window position and size on launch" };
        juce::Label           restoreNote;
    };
}
```
  - `AppearancePreferencesPage.cpp`:

```cpp
#include "AppearancePreferencesPage.h"
#include "../SongsmithColours.h"

namespace lotro
{
    AppearancePreferencesPage::AppearancePreferencesPage (PreferencesServices& servicesIn, std::function<void()> onChangedIn)
        : services (servicesIn), onChanged (std::move (onChangedIn))
    {
        for (auto* b : { &showBand, &restorePlacement })
        {
            b->setColour (juce::ToggleButton::textColourId, juce::Colour (SongsmithColours::text));
            addAndMakeVisible (*b);
        }
        showBand.setToggleState (services.settings.showRangeBand(), juce::dontSendNotification);
        restorePlacement.setToggleState (services.settings.restoreWindowPlacement(), juce::dontSendNotification);

        showBand.onClick = [this]
        {
            services.settings.setShowRangeBand (showBand.getToggleState());
            notify();
        };
        restorePlacement.onClick = [this]
        {
            services.settings.setRestoreWindowPlacement (restorePlacement.getToggleState());
            notify();
        };

        restoreNote.setText ("Takes effect the next time Songsmith starts.", juce::dontSendNotification);
        restoreNote.setFont (juce::FontOptions (12.0f));
        restoreNote.setColour (juce::Label::textColourId, juce::Colour (SongsmithColours::textMuted));
        addAndMakeVisible (restoreNote);
    }

    void AppearancePreferencesPage::notify()
    {
        if (services.applyViewSettings)
            services.applyViewSettings();
        if (onChanged)
            onChanged();
    }

    void AppearancePreferencesPage::resized()
    {
        auto area = getLocalBounds().reduced (12);
        showBand.setBounds (area.removeFromTop (24));
        area.removeFromTop (12);
        restorePlacement.setBounds (area.removeFromTop (24));
        restoreNote.setBounds (area.removeFromTop (20).withTrimmedLeft (24));
    }
}
```
  - `EditingPreferencesPage.h`: same shape; members `juce::Label gridLabel; juce::ComboBox grid; juce::Label gridNote; juce::ToggleButton follow { "Follow the playhead during playback" };`; accessors `gridComboForTesting()`, `followToggleForTesting()`; `void notify();`.
  - `EditingPreferencesPage.cpp`:

```cpp
#include "EditingPreferencesPage.h"
#include "../SongsmithColours.h"

namespace lotro
{
    EditingPreferencesPage::EditingPreferencesPage (PreferencesServices& servicesIn, std::function<void()> onChangedIn)
        : services (servicesIn), onChanged (std::move (onChangedIn))
    {
        gridLabel.setText ("Default grid size", juce::dontSendNotification);
        gridLabel.setColour (juce::Label::textColourId, juce::Colour (SongsmithColours::text));
        addAndMakeVisible (gridLabel);

        // Item ids are (int) GridSize + 1 (ComboBox ids start at 1): Off, 1/4, 1/8, 1/16.
        grid.addItemList ({ "Off", "1/4", "1/8", "1/16" }, 1);
        grid.setSelectedId ((int) services.settings.defaultGrid() + 1, juce::dontSendNotification);
        grid.onChange = [this]
        {
            services.settings.setDefaultGrid ((GridSize) (grid.getSelectedId() - 1));
            notify();
        };
        addAndMakeVisible (grid);

        gridNote.setText ("Applies to track editors opened afterwards. Edit > Grid size still changes an open editor.",
                          juce::dontSendNotification);
        gridNote.setFont (juce::FontOptions (12.0f));
        gridNote.setColour (juce::Label::textColourId, juce::Colour (SongsmithColours::textMuted));
        addAndMakeVisible (gridNote);

        follow.setColour (juce::ToggleButton::textColourId, juce::Colour (SongsmithColours::text));
        follow.setToggleState (services.settings.followPlayhead(), juce::dontSendNotification);
        follow.onClick = [this]
        {
            services.settings.setFollowPlayhead (follow.getToggleState());
            notify();
        };
        addAndMakeVisible (follow);
    }

    void EditingPreferencesPage::notify()
    {
        if (services.applyViewSettings)
            services.applyViewSettings();
        if (onChanged)
            onChanged();
    }

    void EditingPreferencesPage::resized()
    {
        auto area = getLocalBounds().reduced (12);
        gridLabel.setBounds (area.removeFromTop (22));
        grid.setBounds (area.removeFromTop (26).removeFromLeft (160));
        area.removeFromTop (6);
        gridNote.setBounds (area.removeFromTop (36));
        area.removeFromTop (12);
        follow.setBounds (area.removeFromTop (24));
    }
}
```
  - `PreferencesDialog.cpp`: add `#include "AppearancePreferencesPage.h"` and `#include "EditingPreferencesPage.h"`, and two registry entries after Playback, same lambda shape as Playback's: `{ "Appearance", [] (PreferencesServices& s, std::function<void()> changed) -> std::unique_ptr<juce::Component> { return std::make_unique<AppearancePreferencesPage> (s, std::move (changed)); } }, { "Editing", [] (PreferencesServices& s, std::function<void()> changed) -> std::unique_ptr<juce::Component> { return std::make_unique<EditingPreferencesPage> (s, std::move (changed)); } },`.
- [ ] **Step 4: Run** — `cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug && cmake --build build && ctest --test-dir build -R "Preferences|ViewPreferences|appearance|editing" --output-on-failure`; full suite → 848 pass. `./build-windows.sh forge_ui; echo $?` → 0, no warnings in the new files.
- [ ] **Step 5: Commit** (if authorised): `git add Source/UI/Preferences Tests/ViewPreferencesPages_tests.cpp Tests/PreferencesDialog_tests.cpp Tests/CMakeLists.txt CMakeLists.txt && git commit -m "feat(ui): Preferences Appearance and Editing pages"`

---

### Task 5: `MainWindow` wiring and the placement switch

**Files:**
- Modify: `Source/UI/MainWindow.h`, `Source/UI/MainWindow.cpp`
- Test: none headless (`MainWindow.cpp` is not in the test binary); verified by the `forge_ui` Windows build and the manual list below.

**Interfaces:**
- Consumes: Task 3's `SongsmithMainComponent::setShowRangeBand/setFollowPlayhead/setDefaultGridSize`; Task 4's `PreferencesServices::applyViewSettings`; Task 1's accessors.
- Produces: `void MainWindow::applyViewSettings()` (private), called by the Preferences hook and once at startup.

- [ ] **Step 1: Implement.**
  - `MainWindow.h`: declare `void applyViewSettings();` with the other private helpers; extend the `preferencesServices` initialiser with a fifth entry `[this] { applyViewSettings(); }`.
  - `MainWindow.cpp`: add

```cpp
// Pushes the view-affecting settings into the Songsmith view. Called once at startup and
// whenever a Preferences page changes one.
void MainWindow::applyViewSettings()
{
    auto& songsmith = body->getSongsmith();
    songsmith.setShowRangeBand (appSettings.showRangeBand());
    songsmith.setFollowPlayhead (appSettings.followPlayhead());
    songsmith.setDefaultGridSize (appSettings.defaultGrid());
}
```
  call `applyViewSettings();` immediately after `loadStartupSoundFont();` at the end of the constructor, and make the placement block honour the switch:

```cpp
    // Where it was last closed -- or centred on the primary monitor if that monitor is gone
    // (WindowPlacement::resolve), nothing was saved yet, or the user switched restoring off
    // (quit still saves, so switching it back on restores the last-closed position).
    const bool restorePlacement = appSettings.restoreWindowPlacement();
    if (! restorePlacement || ! WindowPlacement::restoreWindow (*settings, mainWindowPlacementKey, *this))
        centreWithSize (getWidth(), getHeight());
    setVisible (true);
    if (restorePlacement)
        WindowPlacement::applyMaximised (*settings, mainWindowPlacementKey, *this);
```
- [ ] **Step 2: Verify** — `cmake --build build && ctest --test-dir build 2>&1 | tail -3` → 848 pass (nothing headless changed); `./build-windows.sh forge_ui; echo $?` → 0 with no warnings in `MainWindow.cpp`. Write the manual list (below) into the task report for the user.
  - (a) Band off hides the band and red zones in the LOTRO preview at once; on restores it.
  - (b) Follow off stops page-flipping during playback in both the track list and an open editor; on restores it.
  - (c) Default grid 1/8: the next editor opened starts on 1/8; Edit ▸ Grid size still overrides; an already-open editor is unchanged.
  - (d) Placement off: next launch centres at the default size; quit saves; turning it back on restores the last-closed position at the launch after.
  - (e) All four survive a restart; every default (never opened Preferences) behaves as before.
- [ ] **Step 3: Commit** (if authorised): `git add Source/UI/MainWindow.h Source/UI/MainWindow.cpp && git commit -m "feat(ui): MainWindow applies the view settings and honours the placement switch"`

---

### Task 6: Docs and counts

**Files:**
- Modify: `docs/TESTING.md` (count + bullets), `docs/ARCHITECTURE.md` §9.16 (the new settings, `applyViewSettings`, the two pages, view setters, placement switch), `docs/UI_GUIDE.md` (Preferences ▸ Appearance and ▸ Editing: controls and when each applies), `CLAUDE.md` (docs-map row for this spec + plan; status headline mentions the Appearance and Editing pages), `docs/superpowers/specs/2026-10-09-songsmith-appearance-editing-preferences-design.md` (`Status:` → implemented)

- [ ] **Step 1:** `ctest --test-dir build -N | tail -1` → the real total; set it in `docs/TESTING.md` line 3 and add bullets in the existing style: `AppSettings_tests.cpp` view settings (defaults, round trip, grid parse incl. unrecognised → Off, failed save); `ViewPreferencesPages_tests.cpp`; the piano-roll band/follow cases, track-list follow case and the `SongsmithMainComponent_tests.cpp` view-settings cases; the dialog page list now General/Import/Playback/Appearance/Editing.
- [ ] **Step 2:** `ARCHITECTURE.md` §9.16: add `applyViewSettings` to the `PreferencesServices` description; the four keys and defaults; Appearance and Editing pages; the view setters and that `SongsmithMainComponent` remembers them for editors opened later; the default grid applies at editor open and never overrides the menu; the placement switch in the `MainWindow` constructor (quit still saves); the band switch hides only the band and out-of-range wash (not ghost or dropped-note overlays; its data and vertical fit are unchanged). Read the code first: every sentence must match it.
- [ ] **Step 3:** `UI_GUIDE.md`, `CLAUDE.md`, spec status as listed.
- [ ] **Step 4: Commit** (if authorised): `git add docs CLAUDE.md && git commit -m "docs: Appearance and Editing preferences — counts, architecture, UI guide"`

---

## Self-review

- **Spec coverage:** §1 `AppSettings` → Task 1; §2 `PreferencesServices.applyViewSettings` → Task 4; §3 view setters → Tasks 2-3, `MainWindow` apply + placement → Task 5; §4 pages and registry → Task 4; Testing → Tasks 1-4 headless, Task 5 manual, Task 6 docs. Out-of-scope items (wheel speed, reset, Apply, Core changes) appear in no task.
- **Placeholders:** none; every code step carries the code.
- **Type consistency:** `setShowRangeBand/getShowRangeBand`, `setFollowPlayhead/getFollowPlayhead`, `setDefaultGridSize (GridSize)`, `applyViewSettings`, `showBandToggleForTesting`, `restorePlacementToggleForTesting`, `gridComboForTesting`, `followToggleForTesting`, key names and `GridSize` ids (`(int) GridSize + 1`) match across Tasks 1-5. Test totals (836, 839, 843, 848) are stated as expectations; Task 6 takes the real total from `ctest -N`.
