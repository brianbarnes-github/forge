# Songsmith Preferences ▸ Playback page + SongSmith.sf2 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a Preferences ▸ Playback page (active SoundFont, Browse…, Clear back to the bundled default) and rename the bundled SoundFont to `SongSmith.sf2`.

**Architecture:** `AppSettings` gets typed `soundFontPath` accessors. A header-only `PreferencesServices` struct (settings + callbacks `MainWindow` provides) replaces `AppSettings&` as what the dialog and pages receive, so a page can load a SoundFont and get the result synchronously. `MainWindow` owns the load / clear / label logic; Song ▸ SoundFont… uses the same load path. The page itself is a small component over the services.

**Tech Stack:** C++20, JUCE, Catch2, CMake/Ninja. No new dependencies.

**Spec:** `docs/superpowers/specs/2026-10-09-songsmith-playback-preferences-design.md`

## Global Constraints

- Nothing in `Source/Core/` changes (`forge-engine-ui-boundary`); no new dependencies.
- The SoundFont is GPL-2 TimGM6mb data, git-ignored under `resources/soundfonts/`; it is **never committed**. `SongSmith.sf2` is the same file renamed; md5 `1f1ad87ae6f87033d9a591eca567d919`. The bundled lookup has **no fallback to the old name**.
- The `soundFontPath` settings key and any saved value are unchanged.
- TDD wherever headless-testable. Types on everything; no `any`-like escapes; custom result type (`SoundFontLoad`) rather than strings for outcomes.
- A page uses `PreferencesServices` / `AppSettings` only in its constructor and in click / chooser handlers, never in its destructor.
- Conventional commits; trailer `Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>`. **Commit only when the user has authorised it** (confirm once at the start of execution). Never push without explicit approval. Never launch the GUI (`song-smith` is user-only); never touch `/mnt/c/Apps/SongSmith/` (deploy is the user's step).
- Build/test from the repo root: `cmake --build build && ctest --test-dir build --output-on-failure`. Baseline: 822/822. Windows compile check: `./build-windows.sh forge_ui` (exit code 0, no warnings in touched files).
- `Tests/CMakeLists.txt` lists sources and test files explicitly: a new `.cpp` needs an entry (Task 4).

## Review Focus

1. Clear while a saved path exists but `<exe>/resources/SongSmith.sf2` is missing: path is still cleared, status says the bundled file was not found, the current SoundFont stays (Task 4 fake-services test; manual).
2. Browse to a corrupt or non-SoundFont `.sf2`: failure shown inline, nothing saved, previous SoundFont keeps playing (Task 4 test; manual).
3. Saved path that no longer exists at startup: falls back to the bundled file (Task 3 manual list).
4. Settings file unwritable when picking a SoundFont: the load still works and the save-failure notice shows (Task 4 test).
5. Only the old `TimGM6mb.sf2` present beside the exe (no `SongSmith.sf2`): starts with no SoundFont and Play shows the existing "No SoundFont" message, no crash (Task 3 manual list).

---

### Task 1: Rename the bundled SoundFont to `SongSmith.sf2`

**Files:**
- Rename (untracked, git-ignored): `resources/soundfonts/TimGM6mb.sf2` → `resources/soundfonts/SongSmith.sf2`
- Modify: `CMakeLists.txt:189`, `Source/UI/MainWindow.cpp` (the `bundled` line in `loadStartupSoundFont`, ~598), `Tests/SynthVoice_tests.cpp:17`
- Modify (docs): `CLAUDE.md` (deploy command line ~87, licensing guardrail ~100), `docs/BUILD.md:130`, `docs/UI_GUIDE.md:301`, `docs/TESTING.md:129`, `docs/ARCHITECTURE.md:759`
- Leave as written (historical): everything under `docs/superpowers/specs/2026-10-03-*` and `docs/superpowers/plans/2026-10-03-*`

**Interfaces:** none produced or consumed.

- [ ] **Step 1: Baseline** — `cmake --build build && ctest --test-dir build --output-on-failure 2>&1 | tail -3` → 822 pass. Run `ls -l resources/soundfonts/` and `md5sum resources/soundfonts/TimGM6mb.sf2` (expect `1f1ad87ae6f87033d9a591eca567d919`).
- [ ] **Step 2: Rename the file** — `mv resources/soundfonts/TimGM6mb.sf2 resources/soundfonts/SongSmith.sf2`; `md5sum` it again, same value. `git status --short` must show nothing for it (ignored).
- [ ] **Step 3: Update references.** `CMakeLists.txt`: `set(FORGE_SOUNDFONT "${CMAKE_SOURCE_DIR}/resources/soundfonts/SongSmith.sf2")`. `MainWindow.cpp`: `.getChildFile ("SongSmith.sf2")` in the `bundled` expression. `SynthVoice_tests.cpp`: `.getChildFile ("resources/soundfonts/SongSmith.sf2")`.
- [ ] **Step 4: Update the current docs.**
  - `CLAUDE.md` deploy command: `cp resources/soundfonts/SongSmith.sf2 /mnt/c/Apps/SongSmith/resources/`; licensing guardrail: "`SongSmith.sf2` is the GPL-2 TimGM6mb bank renamed (md5 `1f1ad87ae6f87033d9a591eca567d919`) and is never committed or shipped by CI …" (keep the rest of the sentence).
  - `docs/BUILD.md:130`: the copy/md5 command writes `resources/soundfonts/SongSmith.sf2` (source file on the other machine keeps its name `TimGM6mb.sf2`): `cp /mnt/c/Apps/NewPlayer/resources/TimGM6mb.sf2 resources/soundfonts/SongSmith.sf2 && md5sum resources/soundfonts/SongSmith.sf2`; add one sentence: "`SongSmith.sf2` is the TimGM6mb bank (GPL-2) under the app's own name."
  - `docs/UI_GUIDE.md:301`, `docs/TESTING.md:129`, `docs/ARCHITECTURE.md:759`: replace the name; read each sentence first so the surrounding text still makes sense (the startup lookup is "the stored path, then `SongSmith.sf2` next to the exe").
- [ ] **Step 5: Verify** — `grep -rn "TimGM6mb" --include="*" . --exclude-dir=build --exclude-dir=build-windows --exclude-dir=.git --exclude-dir=JUCE --exclude-dir=transport --exclude-dir=.superpowers -I` lists only the historical 2026-10-03 specs/plans, the BUILD.md "source file keeps its name" command, and the CLAUDE.md/BUILD.md provenance sentences. `cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug` (re-configure) then `cmake --build build && ctest --test-dir build --output-on-failure 2>&1 | tail -3` → 822 pass, and `ctest --test-dir build -R SynthVoice --output-on-failure` shows the SoundFont-dependent tests ran (not skipped) because the file exists under its new name. `./build-windows.sh forge_ui; echo $?` → 0; `ls build-windows/forge_ui_artefacts/Release/resources/` shows `SongSmith.sf2`.
- [ ] **Step 6: Commit** (if authorised): `git add CMakeLists.txt Source/UI/MainWindow.cpp Tests/SynthVoice_tests.cpp CLAUDE.md docs && git commit -m "refactor: rename the bundled SoundFont to SongSmith.sf2"`

---

### Task 2: `AppSettings` SoundFont accessors

**Files:**
- Modify: `Source/UI/AppSettings.h`
- Test: `Tests/AppSettings_tests.cpp`

**Interfaces:**
- Produces: `juce::String AppSettings::soundFontPath() const`; `bool AppSettings::setSoundFontPath (const juce::File&)`; `bool AppSettings::clearSoundFontPath()` — setters return the save result and update `lastSaveFailed()` via the existing `save()`.

- [ ] **Step 1: Write the failing tests** — append to `Tests/AppSettings_tests.cpp`:

```cpp
TEST_CASE ("AppSettings: the SoundFont path defaults to empty, round-trips, persists and clears", "[app-settings]")
{
    auto file = juce::File::createTempFile (".settings");
    const juce::ScopeGuard cleanup { [&] { file.deleteFile(); } };
    const auto chosen = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("Chosen.sf2");
    {
        juce::PropertiesFile props (file, {});
        AppSettings settings (props);
        CHECK (settings.soundFontPath().isEmpty());
        CHECK (settings.setSoundFontPath (chosen));
        CHECK (settings.soundFontPath() == chosen.getFullPathName());
    }
    juce::PropertiesFile reopened (file, {});
    AppSettings settings (reopened);
    CHECK (settings.soundFontPath() == chosen.getFullPathName());   // persisted under the existing key
    CHECK (reopened.getValue ("soundFontPath") == chosen.getFullPathName());

    CHECK (settings.clearSoundFontPath());
    CHECK (settings.soundFontPath().isEmpty());
    juce::PropertiesFile third (file, {});
    CHECK (AppSettings (third).soundFontPath().isEmpty());
}

TEST_CASE ("AppSettings: SoundFont setters report a failed save", "[app-settings]")
{
    auto blocked = juce::File::createTempFile (".settings");
    REQUIRE (blocked.createDirectory().wasOk());
    const juce::ScopedJuceInitialiser_GUI juceInit;
    const juce::ScopeGuard cleanup { [&] { blocked.deleteRecursively(); } };
    juce::PropertiesFile props (blocked, {});
    AppSettings settings (props);

    CHECK_FALSE (settings.setSoundFontPath (juce::File ("/tmp/x.sf2")));
    CHECK (settings.lastSaveFailed());
    CHECK (settings.soundFontPath().isNotEmpty());   // still applied for the session

    CHECK_FALSE (settings.clearSoundFontPath());
    CHECK (settings.lastSaveFailed());
    CHECK (settings.soundFontPath().isEmpty());
}
```

- [ ] **Step 2: Run to verify they fail** — `cmake --build build` → compile error (`soundFontPath` undefined). Expected.
- [ ] **Step 3: Implement** — in `AppSettings.h`, public section (after `setImportTrackOptions`):

```cpp
    // The SoundFont the user chose (full path); empty when never chosen or cleared,
    // in which case the bundled default is used. Key unchanged since before Preferences.
    juce::String soundFontPath() const { return file.getValue (keySoundFontPath); }
    bool setSoundFontPath (const juce::File& soundFont)
    {
        file.setValue (keySoundFontPath, soundFont.getFullPathName());
        return save();
    }
    bool clearSoundFontPath()
    {
        file.removeValue (keySoundFontPath);
        return save();
    }
```
and `static constexpr const char* keySoundFontPath = "soundFontPath";` with the other keys. Add `#include <juce_core/juce_core.h>` only if the header does not already get `juce::File` (it includes `juce_data_structures`, which does).
- [ ] **Step 4: Run** — `cmake --build build && ctest --test-dir build -R AppSettings --output-on-failure` → pass; full suite `ctest --test-dir build 2>&1 | tail -3` → 824 pass.
- [ ] **Step 5: Commit** (if authorised): `git add Source/UI/AppSettings.h Tests/AppSettings_tests.cpp && git commit -m "feat(ui): AppSettings gets typed SoundFont path accessors"`

---

### Task 3: `PreferencesServices` and the `MainWindow` SoundFont logic

**Files:**
- Create: `Source/UI/Preferences/PreferencesServices.h`
- Modify: `Source/UI/Preferences/PreferencesDialog.{h,cpp}` (take `PreferencesServices&`; registry signature)
- Modify: `Source/UI/MainWindow.{h,cpp}` (services member, `activeSoundFont`, load / bundled / label helpers, startup, Song ▸ SoundFont…, Preferences call)
- Test: `Tests/PreferencesDialog_tests.cpp` (adapt existing tests; no behaviour change)

**Interfaces:**
- Produces:
```cpp
enum class SoundFontResult { loaded, failed, bundledUnavailable };
struct SoundFontLoad { SoundFontResult result; juce::String detail; };   // detail = error text when failed
struct PreferencesServices
{
    AppSettings& settings;
    std::function<SoundFontLoad (const juce::File&)> loadSoundFont;   // load, then remember the choice
    std::function<SoundFontLoad ()>                  useBundledSoundFont;   // clear the choice, load the bundled file
    std::function<juce::String ()>                   activeSoundFontLabel;
};
```
- `PreferencesPage::make` becomes `std::function<std::unique_ptr<juce::Component> (PreferencesServices&, std::function<void()>)>`; `PreferencesDialog (PreferencesServices&)`; `showPreferencesDialog (PreferencesServices&, juce::Component*)` (same `SafePointer<DialogWindow>` return).
- Consumed by Task 4: `PreferencesServices`, `SoundFontLoad`, `SoundFontResult`.

- [ ] **Step 1: Adapt the tests first (RED = compile failure).** In `Tests/PreferencesDialog_tests.cpp` add to the anonymous namespace, above `Fixture`:

```cpp
    PreferencesServices makeServices (AppSettings& settings)
    {
        return { settings,
                 [] (const juce::File&) { return SoundFontLoad { SoundFontResult::loaded, {} }; },
                 [] { return SoundFontLoad { SoundFontResult::loaded, {} }; },
                 [] { return juce::String(); } };
    }
```
give `Fixture` a member `PreferencesServices services = makeServices (settings);` (declared after `settings`), and change every `PreferencesDialog dialog (f.settings)` to `PreferencesDialog dialog (f.services)`. For the tests that build their own `settings` (the blocked-directory ones), add `auto services = makeServices (settings);` and pass `services`. Page-level tests (`GeneralPreferencesPage page (f.settings)`, `ImportPreferencesPage ...`) stay unchanged — those constructors still take `AppSettings&`. Add `#include "UI/Preferences/PreferencesServices.h"` is covered by `PreferencesDialog.h` including it.
- [ ] **Step 2: Run to verify RED** — `cmake --build build` fails (`PreferencesServices` undefined). Expected.
- [ ] **Step 3: Create `Source/UI/Preferences/PreferencesServices.h`** with the struct above (`#pragma once`, includes `../AppSettings.h`, `<juce_core/juce_core.h>`, `<functional>`; comment: "What a Preferences page may ask of the app. MainWindow fills it in; pages call it only from their constructor and click/chooser handlers, never from a destructor. A failed load keeps the previous SoundFont active and writes nothing.").
- [ ] **Step 4: Refactor the dialog.** `PreferencesDialog.h`: `#include "PreferencesServices.h"`; `PreferencesPage::make` signature as above; constructor `explicit PreferencesDialog (PreferencesServices& services);`; member `PreferencesServices& services;` replaces `AppSettings& settings;`; `showPreferencesDialog (PreferencesServices& services, juce::Component* centreAround)` (update its comment: "`services` and what it references must outlive it; MainWindow owns both"). `PreferencesDialog.cpp`: constructor `PreferencesDialog::PreferencesDialog (PreferencesServices& servicesIn) : services (servicesIn)`; `refreshSaveNotice` → `services.settings.lastSaveFailed()`; `selectPage` → `pages[..].make (services, [this] { refreshSaveNotice(); })`; registry lambdas:

```cpp
{ "General", [] (PreferencesServices& s, std::function<void()> changed) -> std::unique_ptr<juce::Component> { return std::make_unique<GeneralPreferencesPage> (s.settings, std::move (changed)); } },
{ "Import",  [] (PreferencesServices& s, std::function<void()> changed) -> std::unique_ptr<juce::Component> { return std::make_unique<ImportPreferencesPage> (s.settings, std::move (changed)); } },
```
and `showPreferencesDialog` builds `std::make_unique<PreferencesDialog> (services)`.
- [ ] **Step 5: `MainWindow.h`** — after `appSettings` add:

```cpp
    // The SoundFont currently loaded in `synth` (empty = none). Set on every successful load.
    juce::File                              activeSoundFont;
    // What the Preferences pages may ask of this window. Declared after appSettings (it refers to it).
    PreferencesServices                     preferencesServices
    {
        appSettings,
        [this] (const juce::File& file) { return loadSoundFontAndRemember (file); },
        [this] { return useBundledSoundFont(); },
        [this] { return activeSoundFontLabel(); }
    };
```
(`#include "Preferences/PreferencesServices.h"`) and private declarations `juce::File bundledSoundFont() const; SoundFontLoad loadSoundFontAndRemember (const juce::File& file); SoundFontLoad useBundledSoundFont(); juce::String activeSoundFontLabel() const;`.
- [ ] **Step 6: `MainWindow.cpp`.** Replace `loadStartupSoundFont` / `chooseSoundFont` and add the helpers (keep `ensurePlaybackReady` as is):

```cpp
juce::File MainWindow::bundledSoundFont() const
{
    return juce::File::getSpecialLocation (juce::File::currentExecutableFile)
               .getSiblingFile ("resources").getChildFile ("SongSmith.sf2");
}

void MainWindow::loadStartupSoundFont()
{
    const juce::String configuredPath = appSettings.soundFontPath();
    const juce::File configured = configuredPath.isNotEmpty() ? juce::File (configuredPath) : juce::File();
    for (const auto& candidate : { configured, bundledSoundFont() })
    {
        if (candidate == juce::File() || ! candidate.existsAsFile())
            continue;
        try { synth.loadSoundFont (candidate); activeSoundFont = candidate; return; }
        catch (const PlaybackError&) { /* fall through to the next candidate */ }
    }
    // No SoundFont: the app still starts; Play explains (ensurePlaybackReady).
}

SoundFontLoad MainWindow::loadSoundFontAndRemember (const juce::File& file)
{
    // Message thread. A failed load leaves the previous SoundFont active (SynthVoice restores
    // its state) and nothing is written.
    try { synth.loadSoundFont (file); }
    catch (const PlaybackError& e) { return { SoundFontResult::failed, e.what() }; }
    catch (const std::exception& e) { return { SoundFontResult::failed, e.what() }; }
    activeSoundFont = file;
    appSettings.setSoundFontPath (file);
    return { SoundFontResult::loaded, {} };
}

SoundFontLoad MainWindow::useBundledSoundFont()
{
    appSettings.clearSoundFontPath();   // "forget my choice" happens even if the bundled file is unusable
    const auto bundled = bundledSoundFont();
    if (! bundled.existsAsFile())
        return { SoundFontResult::bundledUnavailable, {} };   // no unload exists: the current SoundFont stays
    try { synth.loadSoundFont (bundled); }
    catch (const PlaybackError& e) { return { SoundFontResult::failed, e.what() }; }
    catch (const std::exception& e) { return { SoundFontResult::failed, e.what() }; }
    activeSoundFont = bundled;
    return { SoundFontResult::loaded, {} };
}

juce::String MainWindow::activeSoundFontLabel() const
{
    if (activeSoundFont == juce::File())        return "No SoundFont loaded";
    if (activeSoundFont == bundledSoundFont())  return "Using bundled SongSmith.sf2";
    return "Using " + activeSoundFont.getFullPathName();
}

void MainWindow::chooseSoundFont()
{
    fileChooser = std::make_unique<juce::FileChooser> ("Choose a SoundFont", juce::File(), "*.sf2");
    fileChooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [safe = juce::Component::SafePointer<MainWindow> (this)] (const juce::FileChooser& fc)
        {
            if (safe == nullptr) return;
            const auto file = fc.getResult();
            if (file == juce::File()) return;
            const auto loaded = safe->loadSoundFontAndRemember (file);
            if (loaded.result == SoundFontResult::failed)
                showError ("SoundFont", loaded.detail);
        });
}
```
and in the menu switch: `case FilePreferences: preferencesWindow = showPreferencesDialog (preferencesServices, this); return;`. `~MainWindow` already closes `preferencesWindow` first; the services lambdas capture `this` and are only reachable through that window.
- [ ] **Step 7: Verify** — `cmake --build build && ctest --test-dir build --output-on-failure 2>&1 | tail -3` → 824 pass (adapted tests green); `./build-windows.sh forge_ui; echo $?` → 0 with no warnings in the files touched; `grep -n "soundFontPath" Source/UI/MainWindow.cpp` shows only `appSettings.` calls (no `settings->getValue/setValue`).
- [ ] **Step 8: Manual checks for the user (record in the report; do not launch the GUI):** (a) Song ▸ SoundFont… still loads a `.sf2` and a bad file shows the "SoundFont" message box while the old sound keeps playing; (b) launch with only `SongSmith.sf2` beside the exe → playback works; (c) launch with a saved path that no longer exists → falls back to the bundled file; (d) launch with neither → Play shows "No SoundFont", no crash.
- [ ] **Step 9: Commit** (if authorised): `git add Source/UI/Preferences Source/UI/MainWindow.h Source/UI/MainWindow.cpp Tests/PreferencesDialog_tests.cpp && git commit -m "refactor(ui): Preferences pages reach the app through PreferencesServices; MainWindow owns SoundFont load/clear"`

---

### Task 4: The Playback page

**Files:**
- Create: `Source/UI/Preferences/PlaybackPreferencesPage.{h,cpp}`, `Tests/PlaybackPreferencesPage_tests.cpp`
- Modify: `Source/UI/Preferences/PreferencesDialog.cpp` (third registry entry), `Tests/CMakeLists.txt` (add `PlaybackPreferencesPage_tests.cpp` beside `PreferencesDialog_tests.cpp` ~line 73 and `${CMAKE_SOURCE_DIR}/Source/UI/Preferences/PlaybackPreferencesPage.cpp` beside the other pages ~line 117-119), `Tests/PreferencesDialog_tests.cpp` (page list)

**Interfaces:**
- Consumes: `PreferencesServices`, `SoundFontLoad`, `SoundFontResult` (Task 3); `AppSettings::soundFontPath()` (Task 2).
- Produces: `class PlaybackPreferencesPage : public juce::Component` with `PlaybackPreferencesPage (PreferencesServices&, std::function<void()> onChanged = {})`, `void resized() override`, and testing accessors `juce::String labelTextForTesting() const`, `juce::String statusTextForTesting() const`, `juce::TextButton& browseButtonForTesting()`, `juce::TextButton& clearButtonForTesting()`, `void loadChosenFileForTesting (const juce::File&)`.

- [ ] **Step 1: Write the failing tests** — create `Tests/PlaybackPreferencesPage_tests.cpp` (add it to `Tests/CMakeLists.txt` in this step too, otherwise it will not build):

```cpp
#include "UI/Preferences/PlaybackPreferencesPage.h"

#include <catch2/catch_test_macros.hpp>

using namespace lotro;

namespace
{
    // Fake app: records calls, keeps the settings and label consistent the way MainWindow does.
    struct FakeApp
    {
        juce::File settingsFile = juce::File::createTempFile (".settings");
        std::unique_ptr<juce::PropertiesFile> props = std::make_unique<juce::PropertiesFile> (settingsFile, juce::PropertiesFile::Options());
        AppSettings settings { *props };
        SoundFontLoad nextLoad    { SoundFontResult::loaded, {} };
        SoundFontLoad nextBundled { SoundFontResult::loaded, {} };
        juce::String  label { "Using bundled SongSmith.sf2" };
        juce::File    lastLoaded;
        int           bundledCalls = 0;

        PreferencesServices services
        {
            settings,
            [this] (const juce::File& file)
            {
                lastLoaded = file;
                if (nextLoad.result == SoundFontResult::loaded)
                {
                    label = "Using " + file.getFullPathName();
                    settings.setSoundFontPath (file);
                }
                return nextLoad;
            },
            [this]
            {
                ++bundledCalls;
                settings.clearSoundFontPath();
                if (nextBundled.result == SoundFontResult::loaded)
                    label = "Using bundled SongSmith.sf2";
                return nextBundled;
            },
            [this] { return label; }
        };

        ~FakeApp() { props.reset(); settingsFile.deleteFile(); }
    };

    const juce::File chosen = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("Chosen.sf2");
}

TEST_CASE ("Playback page: shows the active SoundFont label; Clear is off while no choice is saved", "[preferences][playback-page]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    FakeApp app;
    PlaybackPreferencesPage page (app.services);
    CHECK (page.labelTextForTesting() == "Using bundled SongSmith.sf2");
    CHECK_FALSE (page.clearButtonForTesting().isEnabled());
}

TEST_CASE ("Playback page: a chosen file that loads updates the label, enables Clear and notifies", "[preferences][playback-page]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    FakeApp app;
    int changed = 0;
    PlaybackPreferencesPage page (app.services, [&] { ++changed; });

    page.loadChosenFileForTesting (chosen);
    CHECK (app.lastLoaded == chosen);
    CHECK (page.labelTextForTesting() == "Using " + chosen.getFullPathName());
    CHECK (page.statusTextForTesting() == "Loaded.");
    CHECK (page.clearButtonForTesting().isEnabled());
    CHECK (changed == 1);
}

TEST_CASE ("Playback page: a file that fails to load shows the error and changes nothing", "[preferences][playback-page]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    FakeApp app;
    app.nextLoad = { SoundFontResult::failed, "not a SoundFont" };
    PlaybackPreferencesPage page (app.services);

    page.loadChosenFileForTesting (chosen);
    CHECK (page.statusTextForTesting() == "Could not load: not a SoundFont. Still using the previous SoundFont.");
    CHECK (page.labelTextForTesting() == "Using bundled SongSmith.sf2");   // unchanged
    CHECK (app.settings.soundFontPath().isEmpty());                         // nothing saved
    CHECK_FALSE (page.clearButtonForTesting().isEnabled());
}

TEST_CASE ("Playback page: Clear goes back to the bundled SoundFont", "[preferences][playback-page]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    FakeApp app;
    PlaybackPreferencesPage page (app.services);
    page.loadChosenFileForTesting (chosen);
    REQUIRE (page.clearButtonForTesting().isEnabled());

    int changed = 0;
    PlaybackPreferencesPage again (app.services, [&] { ++changed; });   // a reopened page sees the saved choice
    REQUIRE (again.clearButtonForTesting().isEnabled());
    again.clearButtonForTesting().onClick();

    CHECK (app.bundledCalls == 1);
    CHECK (again.statusTextForTesting() == "Using the bundled SoundFont.");
    CHECK (again.labelTextForTesting() == "Using bundled SongSmith.sf2");
    CHECK_FALSE (again.clearButtonForTesting().isEnabled());
    CHECK (app.settings.soundFontPath().isEmpty());
    CHECK (changed == 1);
}

TEST_CASE ("Playback page: Clear reports a missing or unloadable bundled SoundFont but still forgets the choice", "[preferences][playback-page]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    FakeApp app;
    app.settings.setSoundFontPath (chosen);
    PlaybackPreferencesPage page (app.services);
    REQUIRE (page.clearButtonForTesting().isEnabled());

    app.nextBundled = { SoundFontResult::bundledUnavailable, {} };
    page.clearButtonForTesting().onClick();
    CHECK (page.statusTextForTesting() == "Your choice was cleared, but the bundled SongSmith.sf2 was not found. The current SoundFont stays until you quit.");
    CHECK (app.settings.soundFontPath().isEmpty());
    CHECK (page.labelTextForTesting() == "Using bundled SongSmith.sf2");   // the fake label did not change: the current font stays

    app.settings.setSoundFontPath (chosen);
    PlaybackPreferencesPage second (app.services);
    app.nextBundled = { SoundFontResult::failed, "corrupt" };
    second.clearButtonForTesting().onClick();
    CHECK (second.statusTextForTesting() == "Your choice was cleared, but the bundled SoundFont could not be loaded: corrupt");
}

TEST_CASE ("Playback page: a SoundFont chosen while the settings file is unwritable still loads and the failed save is visible", "[preferences][playback-page]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    auto blocked = juce::File::createTempFile (".settings");
    REQUIRE (blocked.createDirectory().wasOk());
    const juce::ScopeGuard cleanup { [&] { blocked.deleteRecursively(); } };
    juce::PropertiesFile props (blocked, {});
    AppSettings settings (props);
    juce::String label = "Using bundled SongSmith.sf2";
    PreferencesServices services
    {
        settings,
        [&] (const juce::File& file) { label = "Using " + file.getFullPathName(); settings.setSoundFontPath (file); return SoundFontLoad { SoundFontResult::loaded, {} }; },
        [&] { return SoundFontLoad { SoundFontResult::loaded, {} }; },
        [&] { return label; }
    };
    int changed = 0;
    PlaybackPreferencesPage page (services, [&] { ++changed; });

    page.loadChosenFileForTesting (chosen);
    CHECK (page.labelTextForTesting() == "Using " + chosen.getFullPathName());   // the load worked
    CHECK (settings.lastSaveFailed());                                            // the dialog's notice reads this after onChanged
    CHECK (changed == 1);
}
```
Also in `Tests/PreferencesDialog_tests.cpp` change the page-list expectation to `juce::StringArray { "General", "Import", "Playback" }` and add `#include "UI/Preferences/PlaybackPreferencesPage.h"`; add a case: `dialog.selectPage (2)` makes `dynamic_cast<PlaybackPreferencesPage*> (dialog.currentPage()) != nullptr`.
- [ ] **Step 2: Run to verify RED** — `cmake --build build` fails (`PlaybackPreferencesPage.h` not found). Expected.
- [ ] **Step 3: Implement the header** `Source/UI/Preferences/PlaybackPreferencesPage.h`:

```cpp
#pragma once

#include "PreferencesServices.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>

namespace lotro
{
    // Preferences > Playback: the active SoundFont, Browse... to load another, Clear to
    // forget the choice and return to the bundled default. Everything applies live.
    // Uses `services` only in the constructor and in click / chooser handlers.
    class PlaybackPreferencesPage : public juce::Component
    {
    public:
        explicit PlaybackPreferencesPage (PreferencesServices& services, std::function<void()> onChanged = {});

        void resized() override;

        juce::String labelTextForTesting() const { return activeLabel.getText(); }
        juce::String statusTextForTesting() const { return status.getText(); }
        juce::TextButton& browseButtonForTesting() { return browse; }
        juce::TextButton& clearButtonForTesting() { return clear; }
        // What the Browse chooser's callback runs once a file is picked.
        void loadChosenFileForTesting (const juce::File& file) { loadChosen (file); }

    private:
        void browseForFile();
        void loadChosen (const juce::File& file);
        void clearChoice();
        void refresh();

        PreferencesServices&              services;
        std::function<void()>             onChanged;
        juce::Label                       heading, activeLabel, status;
        juce::TextButton                  browse { "Browse..." };
        juce::TextButton                  clear  { "Clear" };
        std::unique_ptr<juce::FileChooser> chooser;
    };
}
```
- [ ] **Step 4: Implement the source** `Source/UI/Preferences/PlaybackPreferencesPage.cpp`:

```cpp
#include "PlaybackPreferencesPage.h"
#include "../SongsmithColours.h"

namespace lotro
{
    PlaybackPreferencesPage::PlaybackPreferencesPage (PreferencesServices& servicesIn, std::function<void()> onChangedIn)
        : services (servicesIn), onChanged (std::move (onChangedIn))
    {
        heading.setText ("SoundFont", juce::dontSendNotification);
        heading.setColour (juce::Label::textColourId, juce::Colour (SongsmithColours::textMuted));
        heading.setFont (juce::FontOptions (13.0f));
        activeLabel.setColour (juce::Label::textColourId, juce::Colour (SongsmithColours::text));
        status.setFont (juce::FontOptions (12.0f));
        status.setColour (juce::Label::textColourId, juce::Colour (SongsmithColours::textMuted));
        status.setJustificationType (juce::Justification::topLeft);
        status.setMinimumHorizontalScale (1.0f);   // wrap, do not shrink
        for (auto* c : { static_cast<juce::Component*> (&heading), static_cast<juce::Component*> (&activeLabel),
                         static_cast<juce::Component*> (&status), static_cast<juce::Component*> (&browse),
                         static_cast<juce::Component*> (&clear) })
            addAndMakeVisible (*c);

        browse.onClick = [this] { browseForFile(); };
        clear.onClick  = [this] { clearChoice(); };
        refresh();
    }

    void PlaybackPreferencesPage::resized()
    {
        auto area = getLocalBounds().reduced (12);
        heading.setBounds (area.removeFromTop (20));
        activeLabel.setBounds (area.removeFromTop (24));
        area.removeFromTop (8);
        auto row = area.removeFromTop (28);
        browse.setBounds (row.removeFromLeft (100));
        row.removeFromLeft (8);
        clear.setBounds (row.removeFromLeft (100));
        area.removeFromTop (8);
        status.setBounds (area.removeFromTop (64));
    }

    void PlaybackPreferencesPage::refresh()
    {
        activeLabel.setText (services.activeSoundFontLabel(), juce::dontSendNotification);
        clear.setEnabled (services.settings.soundFontPath().isNotEmpty());
    }

    void PlaybackPreferencesPage::browseForFile()
    {
        chooser = std::make_unique<juce::FileChooser> ("Choose a SoundFont", juce::File(), "*.sf2");
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
            [safe = juce::Component::SafePointer<PlaybackPreferencesPage> (this)] (const juce::FileChooser& fc)
            {
                if (safe == nullptr) return;
                const auto file = fc.getResult();
                if (file != juce::File())
                    safe->loadChosen (file);
            });
    }

    void PlaybackPreferencesPage::loadChosen (const juce::File& file)
    {
        const auto loaded = services.loadSoundFont (file);
        if (loaded.result == SoundFontResult::loaded)
            status.setText ("Loaded.", juce::dontSendNotification);
        else
            status.setText ("Could not load: " + loaded.detail + ". Still using the previous SoundFont.",
                            juce::dontSendNotification);
        refresh();
        if (onChanged) onChanged();
    }

    void PlaybackPreferencesPage::clearChoice()
    {
        const auto outcome = services.useBundledSoundFont();
        switch (outcome.result)
        {
            case SoundFontResult::loaded:
                status.setText ("Using the bundled SoundFont.", juce::dontSendNotification);
                break;
            case SoundFontResult::bundledUnavailable:
                status.setText ("Your choice was cleared, but the bundled SongSmith.sf2 was not found. "
                                "The current SoundFont stays until you quit.", juce::dontSendNotification);
                break;
            case SoundFontResult::failed:
                status.setText ("Your choice was cleared, but the bundled SoundFont could not be loaded: " + outcome.detail,
                                juce::dontSendNotification);
                break;
        }
        refresh();
        if (onChanged) onChanged();
    }
}
```
- [ ] **Step 5: Register the page** — `PreferencesDialog.cpp`: `#include "PlaybackPreferencesPage.h"` and a third entry after Import:
`{ "Playback", [] (PreferencesServices& s, std::function<void()> changed) -> std::unique_ptr<juce::Component> { return std::make_unique<PlaybackPreferencesPage> (s, std::move (changed)); } },`
- [ ] **Step 6: Run** — `cmake --build build && ctest --test-dir build --output-on-failure 2>&1 | tail -3` → all pass (824 + 7 new = 831; use the real total). Build output must show no new warnings in the touched files. `./build-windows.sh forge_ui; echo $?` → 0.
- [ ] **Step 7: Manual checks for the user (record; do not launch the GUI):** (a) Preferences ▸ Playback shows "Using bundled SongSmith.sf2" (or the chosen path); (b) Browse… picks a `.sf2`, label and sound change live, even while a Song is playing; (c) a non-SoundFont file shows "Could not load: …" and the old sound continues; (d) Clear returns to the bundled sound and disables itself; with `SongSmith.sf2` temporarily absent it shows the "was not found" message; (e) restart keeps the choice (and after Clear, starts on the bundled one).
- [ ] **Step 8: Commit** (if authorised): `git add Source/UI/Preferences Tests/PlaybackPreferencesPage_tests.cpp Tests/PreferencesDialog_tests.cpp Tests/CMakeLists.txt && git commit -m "feat(ui): Preferences Playback page — SoundFont Browse and Clear"`

---

### Task 5: Docs and counts

**Files:**
- Modify: `docs/TESTING.md` (count + a bullet for the new tests), `docs/ARCHITECTURE.md` §9.16 (Playback page, `PreferencesServices`, `AppSettings` SoundFont accessors, the `MainWindow` helpers and `activeSoundFont`), `docs/UI_GUIDE.md` (Preferences ▸ Playback page: controls and what Clear does; keep the Song ▸ SoundFont… text consistent), `CLAUDE.md` (docs-map row for this spec + plan; status headline mentions the Playback page), `docs/superpowers/specs/2026-10-09-songsmith-playback-preferences-design.md` (`Status:` → implemented)

- [ ] **Step 1:** `ctest --test-dir build -N | tail -1` → the real total; set it in `docs/TESTING.md` line 3 and add the new-test descriptions in the existing style (AppSettings SoundFont accessors; `PlaybackPreferencesPage_tests.cpp`; dialog page list General/Import/Playback).
- [ ] **Step 2:** Edit `ARCHITECTURE.md` §9.16: pages now receive `PreferencesServices` (`AppSettings&` plus `loadSoundFont`, `useBundledSoundFont`, `activeSoundFontLabel`); the Playback page and its three outcome states; Clear always forgets the saved path first and, because `SynthVoice` has no unload, keeps the current SoundFont when the bundled file is missing; `MainWindow::activeSoundFont`; the startup lookup is "saved path, then `SongSmith.sf2` beside the exe".
- [ ] **Step 3:** `UI_GUIDE.md`, `CLAUDE.md`, spec status as listed.
- [ ] **Step 4: Commit** (if authorised): `git add docs CLAUDE.md && git commit -m "docs: Playback Preferences page — counts, architecture, UI guide"`

---

## Self-review

- **Spec coverage:** §1 rename → Task 1; §2 `AppSettings` → Task 2; §3 `PreferencesServices` → Task 3; §4 `MainWindow` behaviour (load, bundled, label, startup, Song ▸ SoundFont…) → Task 3; §5 Playback page (layout, Browse, Clear statuses, `onChanged`, Clear enabled only with a saved path, `loadChosenFileForTesting`) → Task 4; testing and docs → Tasks 2–5.
- **Placeholders:** none; every code step carries the code; the manual-check lists name exact scenarios.
- **Type consistency:** `SoundFontLoad { SoundFontResult result; juce::String detail; }`, `PreferencesServices` member names (`settings`, `loadSoundFont`, `useBundledSoundFont`, `activeSoundFontLabel`), `AppSettings::soundFontPath/setSoundFontPath/clearSoundFontPath`, and the page's testing accessors match across Tasks 2–4. Test totals are stated as "the real total" where the arithmetic depends on the implementer's count.
