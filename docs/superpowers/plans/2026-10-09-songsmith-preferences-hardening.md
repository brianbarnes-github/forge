# Songsmith Preferences hardening Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make the Preferences dialog and its neighbours safe to build more pages on: close the dialog before `MainWindow` dies, surface failed settings writes, enforce a minimum dialog size, validate SECTION nodes at load, stop the import dialog lingering during a long import, and clear two warnings and one wrong comment.

**Architecture:** All UI-layer (`Source/UI/`, `Tests/`). `AppSettings` setters return `bool` and remember the last save result; pages get an optional `onChanged` callback so `PreferencesDialog` can show a notice. `MainWindow` keeps a `SafePointer` to the Preferences window and closes it in its destructor. `SongDocument::validateLoaded` gains SECTION rules (overlap is explicitly allowed). Small independent fixes elsewhere.

**Tech Stack:** C++20, JUCE, Catch2, CMake/Ninja. No new dependencies.

**Spec:** `docs/superpowers/specs/2026-10-09-songsmith-preferences-hardening-design.md`

## Global Constraints

- Nothing in `Source/Core/` changes (`forge-engine-ui-boundary`); no new dependencies.
- TDD: failing test first wherever the item is headless-testable. Always add types; custom error types, not strings (the existing `SongFileError` is reused).
- Conventional commits; trailer `Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>`. **Commit only when the user has authorised it** (repo convention: commit/push/deploy each only on explicit ask) — confirm once at the start of execution whether per-task commits are authorised. Never push.
- Never launch the GUI from agents (`song-smith` is user-only). Window-level behaviour (Tasks 3 step 6, 4) is checked by the user by hand.
- Build/test from the repo root: `cmake --build build && ctest --test-dir build --output-on-failure`. Baseline: 807/807.
- `Tests/CMakeLists.txt` lists test files explicitly; this plan adds tests only to existing files, so no CMake change.
- Warning checks use `./build-windows.sh forge_ui` (clang-cl), where both warnings were seen.

## Review Focus

1. Song file whose track has an **empty** `SECTIONS` node, or **no** `nextSectionId` on the root (a file that predates sections) → must still load (Task 2 tests).
2. Song file with **overlapping** sections → must still load (the editor produces these) (Task 2 test).
3. Settings file that cannot be written → toggles still work for the session and a notice shows; a later successful write clears it (Task 3 tests).
4. Quit while Preferences is open → no crash, dialog closes (manual, Task 4).
5. Long MIDI import after OK → dialog not painted during the import (manual, Task 4).

---

### Task 1: Warnings and the misleading comment

**Files:**
- Modify: `Source/UI/SectionEdit.cpp:263` (`consider` lambda in `hitTestSection`)
- Modify: `Source/UI/SongsmithMainComponent.cpp:93-` (constructor initialiser order)
- Modify: `Source/UI/MidiImportPlan.cpp:180`
- Test (guard, existing): `Tests/SectionEdit_tests.cpp` hit-test tests

**Interfaces:** none produced or consumed.

- [ ] **Step 1: Record the baseline**

Run: `cmake --build build && ctest --test-dir build --output-on-failure 2>&1 | tail -3`
Expected: `100% tests passed` (807 tests).

- [ ] **Step 2: Fix `-Wfloat-equal`** — in `Source/UI/SectionEdit.cpp` replace

```cpp
        if (distance < bestDistance || (distance == bestDistance && contains && ! bestContains))
```
with
```cpp
        // Equal distance without `==` on doubles: not less (handled first) and not greater.
        if (distance < bestDistance || (! (distance > bestDistance) && contains && ! bestContains))
```

- [ ] **Step 3: Fix `-Wreorder-ctor`** — members are declared `... diagnostics, transportStrip, upperRegion, previewRegion, lowerRegion, splitter`, but the constructor lists `transportStrip` after `previewRegion`. Move the `transportStrip` initialiser to sit before `upperRegion` (it depends only on `playbackIn`, so nothing changes at run time):

```cpp
      trackList (document),
      partStrip (document),
      transportStrip (playbackIn != nullptr ? std::make_unique<TransportStrip> (*playbackIn) : nullptr),
      upperRegion (sourceHeader, trackList),
      previewRegion (previewHeader, previewAssignedPanel, previewRoll),
      lowerRegion (transportStrip.get(), partStrip, previewRegion, diagnostics),
      splitter (SplitterComponent::Orientation::topBottom)
```

- [ ] **Step 4: Fix the comment** — in `Source/UI/MidiImportPlan.cpp` replace
`// Tie-break relies on each source track's notes already being tick-sorted (importMidi guarantees it).` with
`// Tie-break relies on each source track's notes already being tick-sorted (the order JUCE's MidiMessageSequence yields them in; importMidi does not sort).`
First confirm by reading `importMidi` in `Source/UI/MidiImporter.cpp` that it does not sort; if it does sort, keep the original comment and say so in the report.

- [ ] **Step 5: Verify**

Run: `cmake --build build && ctest --test-dir build --output-on-failure 2>&1 | tail -3` → 807 pass (hit-test tie-break tests guard step 2).
Run: `./build-windows.sh forge_ui 2>&1 | grep -E "Wfloat-equal|Wreorder-ctor"` → no output.

- [ ] **Step 6: Commit** (if authorised)

```bash
git add Source/UI/SectionEdit.cpp Source/UI/SongsmithMainComponent.cpp Source/UI/MidiImportPlan.cpp
git commit -m "fix(ui): clear -Wfloat-equal and -Wreorder-ctor, correct sort comment"
```

---

### Task 2: `validateLoaded` checks SECTION nodes

**Files:**
- Modify: `Source/UI/SongDocument.cpp:273-` (`validateLoaded`, inside the per-track loop)
- Test: `Tests/SongDocument_tests.cpp` (after the "rejects structurally invalid trees" case, which is where `arrange`/`isInvalid` live)

**Interfaces:**
- Consumes: `SongIDs::SECTIONS`, `SongIDs::SECTION`, `SongIDs::sectionId`, `SongIDs::startTick`, `SongIDs::endTick`, `SongIDs::nextSectionId`; existing `bad (...)` lambda.
- Produces: no new API; `validateLoaded` returns `InvalidStructure` for the new cases.

- [ ] **Step 1: Write the failing tests** — append to `Tests/SongDocument_tests.cpp`:

```cpp
namespace
{
    juce::ValueTree makeSection (juce::int64 id, int start, int end)
    {
        juce::ValueTree s (SongIDs::SECTION);
        s.setProperty (SongIDs::sectionId, id, nullptr);
        s.setProperty (SongIDs::startTick, start, nullptr);
        s.setProperty (SongIDs::endTick, end, nullptr);
        return s;
    }

    // A copy of `good` whose first non-conductor track carries these sections.
    // `nextSectionId` < 0 leaves the root property absent (a file that predates sections).
    juce::ValueTree withSections (const juce::ValueTree& good, std::vector<juce::ValueTree> sections, int nextSectionId = 10)
    {
        auto t = good.createCopy();
        auto track = t.getChildWithName (SongIDs::SOURCE_MIDI).getChild (1);
        juce::ValueTree node (SongIDs::SECTIONS);
        for (auto& s : sections)
            node.addChild (s, -1, nullptr);
        track.addChild (node, -1, nullptr);
        if (nextSectionId >= 0)
            t.setProperty (juce::Identifier ("nextSectionId"), nextSectionId, nullptr);
        return t;
    }
}

TEST_CASE ("SongDocument: validateLoaded accepts well-formed sections, overlap, an empty node and no counter", "[songdocument]")
{
    SongDocument doc;
    arrange (doc);
    const auto good = doc.getTree();

    CHECK_FALSE (SongDocument::validateLoaded (withSections (good, { makeSection (1, 0, 480), makeSection (2, 480, 960) })).has_value());
    CHECK_FALSE (SongDocument::validateLoaded (withSections (good, { makeSection (1, 0, 960), makeSection (2, 480, 1440) })).has_value());   // overlap is allowed
    CHECK_FALSE (SongDocument::validateLoaded (withSections (good, {})).has_value());                                                       // empty SECTIONS
    CHECK_FALSE (SongDocument::validateLoaded (withSections (good, { makeSection (1, 0, 480) }, -1)).has_value());                          // no counter: mintSectionId repairs it
}

TEST_CASE ("SongDocument: validateLoaded rejects damaged SECTION nodes", "[songdocument]")
{
    SongDocument doc;
    arrange (doc);
    const auto good = doc.getTree();

    SECTION ("a SECTIONS child that is not a SECTION")
    {
        CHECK (isInvalid (withSections (good, { juce::ValueTree ("SURPRISE") })));
    }
    SECTION ("a section id below 1, or missing")
    {
        CHECK (isInvalid (withSections (good, { makeSection (0, 0, 480) })));
        auto noId = makeSection (1, 0, 480);
        noId.removeProperty (SongIDs::sectionId, nullptr);
        CHECK (isInvalid (withSections (good, { noId })));
    }
    SECTION ("a section id at or above nextSectionId")
    {
        CHECK (isInvalid (withSections (good, { makeSection (5, 0, 480) }, 5)));
    }
    SECTION ("two sections share an id")
    {
        CHECK (isInvalid (withSections (good, { makeSection (1, 0, 480), makeSection (1, 480, 960) })));
    }
    SECTION ("a negative start, an empty range, a reversed range, a missing tick")
    {
        CHECK (isInvalid (withSections (good, { makeSection (1, -1, 480) })));
        CHECK (isInvalid (withSections (good, { makeSection (1, 480, 480) })));
        CHECK (isInvalid (withSections (good, { makeSection (1, 960, 480) })));
        auto noEnd = makeSection (1, 0, 480);
        noEnd.removeProperty (SongIDs::endTick, nullptr);
        CHECK (isInvalid (withSections (good, { noEnd })));
    }
    SECTION ("ids must be unique across tracks, not just within one")
    {
        SongDocument two;
        arrange (two);
        auto second = two.addTrack ("Harmony", 0xFF445566, 2, two.mintImportBatch());
        auto t = withSections (two.getTree(), { makeSection (1, 0, 480) });
        auto track2 = t.getChildWithName (SongIDs::SOURCE_MIDI).getChild (2);
        juce::ValueTree node (SongIDs::SECTIONS);
        node.addChild (makeSection (1, 0, 480), -1, nullptr);
        track2.addChild (node, -1, nullptr);
        CHECK (isInvalid (t));
        (void) second;
    }
}
```

- [ ] **Step 2: Run to verify the new reject cases fail**

Run: `cmake --build build && ctest --test-dir build -R "validateLoaded" --output-on-failure`
Expected: "accepts well-formed…" passes (nothing checked yet); "rejects damaged SECTION nodes" FAILS.

- [ ] **Step 3: Implement** — in `validateLoaded`, declare `std::vector<juce::int64> sectionIds;` next to `trackIds`, and inside the per-track loop, after the `importBatch` check, add:

```cpp
        const auto sections = track.getChildWithName (SongIDs::SECTIONS);
        for (int s = 0; s < sections.getNumChildren(); ++s)
        {
            const auto section = sections.getChild (s);
            if (! section.hasType (SongIDs::SECTION))
                return bad ("a section entry is not a section.");

            const auto sid = (juce::int64) section.getProperty (SongIDs::sectionId, -1);
            if (sid < 1 || (t.hasProperty (SongIDs::nextSectionId)
                            && sid >= (juce::int64) t.getProperty (SongIDs::nextSectionId)))
                return bad ("a section id is out of range.");
            if (std::find (sectionIds.begin(), sectionIds.end(), sid) != sectionIds.end())
                return bad ("two sections share an id.");
            sectionIds.push_back (sid);

            const auto start = (int) section.getProperty (SongIDs::startTick, -1);
            const auto end   = (int) section.getProperty (SongIDs::endTick, -1);
            if (start < 0 || end <= start)
                return bad ("a section has an invalid tick range.");
        }
```
(Overlap is deliberately not checked.) If `SongIDs::nextSectionId` is not visible at this point of `SongDocument.cpp`, use the same declaration `mintSectionId` uses at line ~261.

- [ ] **Step 4: Run to verify they pass**

Run: `cmake --build build && ctest --test-dir build --output-on-failure 2>&1 | tail -3` → all pass.

- [ ] **Step 5: Sanity — a real sectioned Song still loads.** The existing SectionEdit and `.songsmith` round-trip tests already build documents through `mintSectionId`; they must stay green (included in step 4).

- [ ] **Step 6: Commit** (if authorised)

```bash
git add Source/UI/SongDocument.cpp Tests/SongDocument_tests.cpp
git commit -m "fix(ui): validateLoaded rejects damaged SECTION nodes"
```

---

### Task 3: Settings write failures are visible; dialog minimum size

**Files:**
- Modify: `Source/UI/AppSettings.h` (setters return `bool`, `lastSaveFailed()`)
- Modify: `Source/UI/Preferences/PreferencesDialog.{h,cpp}` (registry signature, notice, min-size constants)
- Modify: `Source/UI/Preferences/GeneralPreferencesPage.{h,cpp}`, `Source/UI/Preferences/ImportPreferencesPage.{h,cpp}` (optional `onChanged`)
- Test: `Tests/AppSettings_tests.cpp`, `Tests/PreferencesDialog_tests.cpp`

**Interfaces:**
- Produces: `bool AppSettings::setAskToSaveUnsavedChanges (bool)`, `bool setAskBeforeReplacingFile (bool)`, `bool setImportTrackOptions (ImportTrackOptions)` — true = persisted; `bool AppSettings::lastSaveFailed() const noexcept`.
- Produces: `PreferencesPage::make` becomes `std::function<std::unique_ptr<juce::Component> (AppSettings&, std::function<void()> onChanged)>`; page constructors `GeneralPreferencesPage (AppSettings&, std::function<void()> onChanged = {})` and likewise `ImportPreferencesPage`.
- Produces: `constexpr int preferencesMinWidth = 480, preferencesMinHeight = 300;` in `PreferencesDialog.h` (namespace `lotro`); `bool PreferencesDialog::saveNoticeVisibleForTesting() const`.

- [ ] **Step 1: Write failing AppSettings tests** — append to `Tests/AppSettings_tests.cpp`:

```cpp
TEST_CASE ("AppSettings: a failed save is reported, keeps the value for the session and clears on the next good save", "[app-settings]")
{
    // A directory where the settings file should be: the PropertiesFile cannot replace it.
    auto blocked = juce::File::createTempFile (".settings");
    REQUIRE (blocked.createDirectory().wasOk());
    const juce::ScopedJuceInitialiser_GUI juceInit;   // PropertiesFile may touch the message manager
    const juce::ScopeGuard cleanup { [&] { blocked.deleteRecursively(); } };

    juce::PropertiesFile props (blocked, {});
    AppSettings settings (props);
    CHECK_FALSE (settings.lastSaveFailed());

    CHECK_FALSE (settings.setAskToSaveUnsavedChanges (false));
    CHECK (settings.lastSaveFailed());
    CHECK_FALSE (settings.askToSaveUnsavedChanges());   // still applied for this session

    blocked.deleteRecursively();                         // the path is writable again
    CHECK (settings.setAskBeforeReplacingFile (false));
    CHECK_FALSE (settings.lastSaveFailed());
}

TEST_CASE ("AppSettings: a normal save reports success", "[app-settings]")
{
    auto file = juce::File::createTempFile (".settings");
    const juce::ScopeGuard cleanup { [&] { file.deleteFile(); } };
    juce::PropertiesFile props (file, {});
    AppSettings settings (props);
    CHECK (settings.setAskToSaveUnsavedChanges (false));
    CHECK (settings.setImportTrackOptions (ImportTrackOptions::expandedAlways));
    CHECK_FALSE (settings.lastSaveFailed());
}
```
(If constructing a `PropertiesFile` on a directory path asserts in a Debug JUCE build, switch the fixture to a path under a directory that is removed after construction and re-created as a file — the goal is only "`saveIfNeeded()` returns false". Say so in the report.)

- [ ] **Step 2: Run to verify they fail** — `cmake --build build` fails to compile (`lastSaveFailed` undefined). Expected.

- [ ] **Step 3: Implement `AppSettings`** — in `AppSettings.h`:

```cpp
    bool setAskToSaveUnsavedChanges (bool on) { return write (keyUnsavedChanges, on); }
    bool setAskBeforeReplacingFile (bool on)  { return write (keyReplaceFile, on); }
    bool setImportTrackOptions (ImportTrackOptions value)
    {
        file.setValue (keyImportTrackOptions, value == ImportTrackOptions::expandedAlways ? "expanded" : "ask");
        return save();
    }

    // True when the most recent setter could not write the settings file. The new
    // value still applies for this session (PropertiesFile holds it in memory).
    bool lastSaveFailed() const noexcept { return saveFailed; }
```
and in the private section:
```cpp
    bool write (const char* key, bool on)
    {
        file.setValue (key, on ? "1" : "0");
        return save();
    }
    bool save()
    {
        saveFailed = ! file.saveIfNeeded();
        return ! saveFailed;
    }

    juce::PropertiesFile& file;
    bool saveFailed = false;
```
Update the header comment: "Setters write and save at once; they return whether the save succeeded." Existing callers ignore the return value and still compile.

- [ ] **Step 4: Run AppSettings tests** — `cmake --build build && ctest --test-dir build -R "AppSettings" --output-on-failure` → pass.

- [ ] **Step 5: Write failing dialog tests** — append to `Tests/PreferencesDialog_tests.cpp`:

```cpp
TEST_CASE ("PreferencesDialog: no notice while saves succeed; the notice appears when a toggle cannot be saved", "[preferences]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    Fixture f;
    PreferencesDialog dialog (f.settings);
    auto* general = dynamic_cast<GeneralPreferencesPage*> (dialog.currentPage());
    REQUIRE (general != nullptr);
    CHECK_FALSE (dialog.saveNoticeVisibleForTesting());

    general->unsavedChangesToggleForTesting().setToggleState (false, juce::sendNotification);
    CHECK_FALSE (dialog.saveNoticeVisibleForTesting());   // saved fine
}

TEST_CASE ("PreferencesDialog: a failing settings file shows the notice from either page", "[preferences]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    auto blocked = juce::File::createTempFile (".settings");
    REQUIRE (blocked.createDirectory().wasOk());
    const juce::ScopeGuard cleanup { [&] { blocked.deleteRecursively(); } };
    juce::PropertiesFile props (blocked, {});
    AppSettings settings (props);
    PreferencesDialog dialog (settings);

    auto* general = dynamic_cast<GeneralPreferencesPage*> (dialog.currentPage());
    REQUIRE (general != nullptr);
    general->replaceFileToggleForTesting().setToggleState (false, juce::sendNotification);
    CHECK (dialog.saveNoticeVisibleForTesting());

    CHECK_FALSE (settings.askBeforeReplacingFile());      // still applied for the session
}

TEST_CASE ("PreferencesDialog: the Import page also reports a failed save", "[preferences]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    auto blocked = juce::File::createTempFile (".settings");
    REQUIRE (blocked.createDirectory().wasOk());
    const juce::ScopeGuard cleanup { [&] { blocked.deleteRecursively(); } };
    juce::PropertiesFile props (blocked, {});
    AppSettings settings (props);
    PreferencesDialog dialog (settings);
    dialog.selectPage (1);
    auto* page = dynamic_cast<ImportPreferencesPage*> (dialog.currentPage());
    REQUIRE (page != nullptr);
    page->trackOptionsForTesting().setSelectedId (2, juce::sendNotificationSync);
    CHECK (dialog.saveNoticeVisibleForTesting());
}

TEST_CASE ("PreferencesDialog: opens at least as large as its minimum size", "[preferences]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    Fixture f;
    PreferencesDialog dialog (f.settings);
    CHECK (dialog.getWidth() >= preferencesMinWidth);
    CHECK (dialog.getHeight() >= preferencesMinHeight);
}
```

- [ ] **Step 6: Run to verify they fail** — compile error (`saveNoticeVisibleForTesting`, `preferencesMinWidth` undefined). Expected.

- [ ] **Step 7: Implement the pages** — `GeneralPreferencesPage.h`: constructor `explicit GeneralPreferencesPage (AppSettings& settings, std::function<void()> onChanged = {});` (add `#include <functional>`). `.cpp`: take `onChanged` by value and change the two handlers to

```cpp
        unsavedChanges.onClick = [this, &settings, onChanged]
        {
            settings.setAskToSaveUnsavedChanges (unsavedChanges.getToggleState());
            if (onChanged) onChanged();
        };
        replaceFile.onClick = [this, &settings, onChanged]
        {
            settings.setAskBeforeReplacingFile (replaceFile.getToggleState());
            if (onChanged) onChanged();
        };
```
`ImportPreferencesPage`: same constructor change; `trackOptions.onChange = [this, &settings, onChanged] { settings.setImportTrackOptions (...same expression...); if (onChanged) onChanged(); };`.

- [ ] **Step 8: Implement the dialog** — `PreferencesDialog.h`: add near the top of namespace `lotro`:

```cpp
    // The Preferences window cannot be resized below this (set on the window by showPreferencesDialog).
    constexpr int preferencesMinWidth  = 480;
    constexpr int preferencesMinHeight = 300;
```
change `PreferencesPage::make` to `std::function<std::unique_ptr<juce::Component> (AppSettings&, std::function<void()>)>`; add to the class `bool saveNoticeVisibleForTesting() const { return saveNotice.isVisible(); }`, private `void refreshSaveNotice();` and `juce::Label saveNotice;`. Document the page rule above the struct:

```cpp
    // A page touches AppSettings only in its constructor and in click handlers,
    // never in its destructor: the window can outlive its owner's settings during shutdown.
```
`PreferencesDialog.cpp`: registry entries become
`[] (AppSettings& s, std::function<void()> changed) -> std::unique_ptr<juce::Component> { return std::make_unique<GeneralPreferencesPage> (s, std::move (changed)); }` (same for Import); in `selectPage` call `pages[(size_t) index].make (settings, [this] { refreshSaveNotice(); })`; in the constructor configure the label before `setSize`:

```cpp
        saveNotice.setText ("Settings could not be saved; changes last until you quit.", juce::dontSendNotification);
        saveNotice.setFont (juce::FontOptions (12.0f));
        saveNotice.setColour (juce::Label::textColourId, juce::Colour (SongsmithColours::textMuted));
        addChildComponent (saveNotice);   // hidden until a save fails
```
add
```cpp
    void PreferencesDialog::refreshSaveNotice()
    {
        saveNotice.setVisible (settings.lastSaveFailed());
    }
```
and in `resized()` lay the notice in the bottom row:

```cpp
        auto bottom = area.removeFromBottom (44).reduced (8);
        closeButton.setBounds (bottom.removeFromRight (80));
        saveNotice.setBounds (bottom.withTrimmedRight (8));
```
(replacing the old `closeButton.setBounds (area.removeFromBottom (44)...)` line).

- [ ] **Step 9: Run everything** — `cmake --build build && ctest --test-dir build --output-on-failure 2>&1 | tail -3` → all pass (807 + new).

- [ ] **Step 10: Commit** (if authorised)

```bash
git add Source/UI/AppSettings.h Source/UI/Preferences Tests/AppSettings_tests.cpp Tests/PreferencesDialog_tests.cpp
git commit -m "fix(ui): Preferences reports a failed settings save and has a minimum size constant"
```

---

### Task 4: Window-level fixes (not headless-testable)

**Files:**
- Modify: `Source/UI/Preferences/PreferencesDialog.{h,cpp}` (`showPreferencesDialog` returns the window, sets resize limits)
- Modify: `Source/UI/MainWindow.{h,cpp}` (member, assign at the menu call, close in destructor)
- Modify: `Source/UI/ImportOptionsDialog.cpp:101`

**Interfaces:**
- Consumes: `preferencesMinWidth`, `preferencesMinHeight` (Task 3).
- Produces: `juce::Component::SafePointer<juce::DialogWindow> showPreferencesDialog (AppSettings&, juce::Component* centreAround)`.

- [ ] **Step 1: `showPreferencesDialog` returns the window and limits its size** — header: change the return type (add `#include <juce_gui_basics/juce_gui_basics.h>` is already present) and update the comment ("The returned pointer goes null when the window closes; the owner closes it if still alive when it is destroyed."). Implementation:

```cpp
    juce::Component::SafePointer<juce::DialogWindow> showPreferencesDialog (AppSettings& settings, juce::Component* centreAround)
    {
        ...unchanged up to launchAsync...
        auto* window = options.launchAsync();   // enters modal state
        if (window == nullptr)
            return {};
        window->setResizeLimits (preferencesMinWidth, preferencesMinHeight, 1600, 1200);
        // The window owns the content that owns this callback, so it cannot outlive `window`.
        dialog->onCloseRequested = [window] { window->exitModalState (0); };
        return window;
    }
```
(`dialog->onCloseRequested` assignment must stay after the null check.)

- [ ] **Step 2: `MainWindow` owns the pointer** — `MainWindow.h`, after `appSettings`:

```cpp
    // The open Preferences window, if any. Closed in ~MainWindow: the dialog holds a
    // reference to `appSettings`, which dies with this window.
    juce::Component::SafePointer<juce::DialogWindow> preferencesWindow;
```
`MainWindow.cpp:305` → `case FilePreferences: preferencesWindow = showPreferencesDialog (appSettings, this); return;`. At the top of `~MainWindow()`, before `synthGc.stopTimer()`:

```cpp
    // A modal Preferences window references appSettings; close it while that is still alive.
    if (auto* prefs = preferencesWindow.getComponent())
        prefs->exitModalState (0);
```

- [ ] **Step 3: Import dialog hides before the import runs** — `ImportOptionsDialog.cpp`:

```cpp
        component->onAccepted = [window, accepted = std::move (onAccepted)] (const ImportOptions& chosen)
        {
            window->setVisible (false);   // a long import must not run under a still-painted dialog
            window->exitModalState (1);
            if (accepted) accepted (chosen);
        };
```

- [ ] **Step 4: Build and run the suite** — `cmake --build build && ctest --test-dir build --output-on-failure 2>&1 | tail -3` → all pass. `./build-windows.sh forge_ui 2>&1 | tail -5` → builds (MainWindow.cpp is outside the test binary, so this is the compile check).

- [ ] **Step 5: Manual checks for the user (record in the report; do not launch the GUI)**
1. Open File ▸ Preferences…, try to drag the window below ~480 × 300 → it stops.
2. With Preferences open, quit from the OS (or Alt+F4 the main window path available while modal) → no crash, both windows go.
3. Import ▸ MIDI… a large file, press OK → the dialog disappears immediately, then the import runs.

- [ ] **Step 6: Commit** (if authorised)

```bash
git add Source/UI/Preferences Source/UI/MainWindow.h Source/UI/MainWindow.cpp Source/UI/ImportOptionsDialog.cpp
git commit -m "fix(ui): close Preferences with the main window, enforce its minimum size, hide the import dialog before importing"
```

---

### Task 5: Docs and counts

**Files:**
- Modify: `docs/TESTING.md:3` (test count), `docs/ARCHITECTURE.md` §9.16 (setter return values / failure notice / `showPreferencesDialog` ownership; `validateLoaded` section rules where it is described, §9.13), `docs/superpowers/specs/2026-10-09-songsmith-preferences-hardening-design.md` (Status → implemented)
- Modify: `CLAUDE.md` docs map (add the hardening spec + plan row)

- [ ] **Step 1:** Run `ctest --test-dir build 2>&1 | tail -3`, take the new total, and set it in `docs/TESTING.md` line 3.
- [ ] **Step 2:** Edit §9.16 of `docs/ARCHITECTURE.md`: setters return whether the save succeeded and `lastSaveFailed()` drives the dialog's notice; pages take an optional `onChanged`; `showPreferencesDialog` returns the window and `MainWindow` closes it in its destructor; the window has a 480 × 300 minimum. Edit the `validateLoaded` description (§9.13) to list the SECTION rules and that overlap is allowed.
- [ ] **Step 3:** Set the spec's `Status:` line to `implemented` and add the plan/spec row to the `CLAUDE.md` docs map in the existing style.
- [ ] **Step 4: Commit** (if authorised)

```bash
git add docs CLAUDE.md
git commit -m "docs: Preferences hardening — counts, architecture notes, docs map"
```

---

## Self-review

- **Spec coverage:** §1 lifetime → Task 4 steps 1-2 (+ page rule comment, Task 3 step 8); §2 write failure → Task 3; §3 min size → Task 3 constant/test + Task 4 limits; §4 validateLoaded → Task 2; §5 import dialog → Task 4 step 3; §6 warnings/comment → Task 1. Docs → Task 5.
- **Placeholders:** none; the two "if X differs" notes (PropertiesFile on a directory path, `nextSectionId` visibility) name the exact fallback.
- **Type consistency:** `onChanged` is `std::function<void()>` everywhere; `make` takes `(AppSettings&, std::function<void()>)`; `lastSaveFailed()` / `saveNoticeVisibleForTesting()` / `preferencesMinWidth|Height` names match across tasks.
