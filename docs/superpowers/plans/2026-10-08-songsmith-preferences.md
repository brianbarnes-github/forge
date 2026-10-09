# Songsmith Preferences Dialog Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** File ▸ Preferences… opens a fully modal dialog (page tree, splitter, page canvas) whose General page switches the unsaved-changes and replace-file prompts on and off, persisted across restarts.

**Architecture:** A header-only typed `AppSettings` wraps the `juce::PropertiesFile` `MainWindow` already owns. `PreferencesDialog` is a content component (TreeView + the project's `SplitterComponent` + a page host) launched with `juce::DialogWindow::LaunchOptions::launchAsync` like `AboutBox`; pages are registry data. `MainWindow` reads the settings at the moment of use in `guarded()` and in the two overwrite checks.

**Tech Stack:** C++/JUCE (`juce_gui_basics`, `juce_data_structures`), Catch2. No new dependencies.

**Spec:** `docs/superpowers/specs/2026-10-08-songsmith-preferences-design.md`

## Global Constraints

- UI-only: changes live in `Source/UI/` and `Tests/`; nothing in `Source/Core/` (skill `forge-engine-ui-boundary`).
- No new dependencies (JUCE modules `juce_gui_basics` and `juce_data_structures` are already linked).
- Setting keys and defaults exactly: `confirm.unsavedChanges` = true, `confirm.replaceFile` = true.
- "Off" for the unsaved-changes prompt means discard silently (no auto-save).
- Menu: File = New, Open…, Save, Save As…, Import ▸, Export ▸, **separator, Preferences…, separator**, Quit; Preferences has no shortcut and is always enabled.
- Splitter initial fraction 0.25 (`SplitterComponent` clamps to [0.15, 0.85]).
- Types/hints on everything, no `any`; functional helpers over classes where practical; commits are conventional with the trailer `Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>`; never push without explicit approval.
- Tests: `cmake --build build && ctest --test-dir build -R "<name>" --output-on-failure`. Do not launch the GUI from agents.

## Review Focus

- A corrupt or hand-edited settings file (non-boolean text under `confirm.*`) must fall back to the defaults, not crash or disable prompts — pinned in Task 1.
- Toggling a checkbox must persist before the dialog closes (crash safety) — Task 1 tests persistence across a re-opened file; Task 2 tests the toggle writes through.
- A second `PreferencesDialog` built over the same `AppSettings` shows the saved state, not defaults — Task 2.
- With the unsaved-changes prompt off, a *clean* Song still proceeds and a dirty one proceeds without a prompt; with it on, clean proceeds and dirty prompts — Task 1 truth table.
- Escape / the title-bar close / Close button must all dismiss the dialog without leaving the main window blocked — manual check in Task 4 (needs real modality).

---

### Task 1: AppSettings and the prompt-decision functions

**Files:**
- Create: `Source/UI/AppSettings.h`
- Create: `Tests/AppSettings_tests.cpp`
- Modify: `Tests/CMakeLists.txt` (add `AppSettings_tests.cpp` after `AboutBox_tests.cpp`, ~line 71)

**Interfaces:**
- Produces:
  - `class lotro::AppSettings { explicit AppSettings (juce::PropertiesFile&); bool askToSaveUnsavedChanges() const; void setAskToSaveUnsavedChanges (bool); bool askBeforeReplacingFile() const; void setAskBeforeReplacingFile (bool); }`
  - `bool lotro::shouldPromptForUnsavedChanges (const AppSettings&, bool isDirty)`
  - `bool lotro::shouldConfirmReplace (const AppSettings&)`

- [ ] **Step 1: Write the failing test** — `Tests/AppSettings_tests.cpp`

```cpp
#include "UI/AppSettings.h"

#include <catch2/catch_test_macros.hpp>

using namespace lotro;

TEST_CASE ("AppSettings: both confirmations default to on", "[app-settings]")
{
    auto file = juce::File::createTempFile (".settings");
    const juce::ScopeGuard cleanup { [&] { file.deleteFile(); } };
    juce::PropertiesFile props (file, {});
    const AppSettings settings (props);
    CHECK (settings.askToSaveUnsavedChanges());
    CHECK (settings.askBeforeReplacingFile());
}

TEST_CASE ("AppSettings: setters round-trip and persist across a re-opened file", "[app-settings]")
{
    auto file = juce::File::createTempFile (".settings");
    const juce::ScopeGuard cleanup { [&] { file.deleteFile(); } };
    {
        juce::PropertiesFile props (file, {});
        AppSettings settings (props);
        settings.setAskToSaveUnsavedChanges (false);
        CHECK_FALSE (settings.askToSaveUnsavedChanges());
        CHECK (settings.askBeforeReplacingFile());   // independent
        // No explicit saveIfNeeded() here: the setter must have written to disk.
    }
    juce::PropertiesFile reloaded (file, {});
    const AppSettings settings (reloaded);
    CHECK_FALSE (settings.askToSaveUnsavedChanges());
    CHECK (settings.askBeforeReplacingFile());

    juce::PropertiesFile again (file, {});
    AppSettings writer (again);
    writer.setAskBeforeReplacingFile (false);
    juce::PropertiesFile third (file, {});
    CHECK_FALSE (AppSettings (third).askBeforeReplacingFile());
}

TEST_CASE ("AppSettings: a non-boolean stored value falls back to the default", "[app-settings]")
{
    auto file = juce::File::createTempFile (".settings");
    const juce::ScopeGuard cleanup { [&] { file.deleteFile(); } };
    juce::PropertiesFile props (file, {});
    props.setValue ("confirm.unsavedChanges", "banana");
    props.setValue ("confirm.replaceFile", "");
    const AppSettings settings (props);
    CHECK (settings.askToSaveUnsavedChanges());
    CHECK (settings.askBeforeReplacingFile());
}

TEST_CASE ("shouldPromptForUnsavedChanges / shouldConfirmReplace follow the settings", "[app-settings]")
{
    auto file = juce::File::createTempFile (".settings");
    const juce::ScopeGuard cleanup { [&] { file.deleteFile(); } };
    juce::PropertiesFile props (file, {});
    AppSettings settings (props);

    CHECK (shouldPromptForUnsavedChanges (settings, true));
    CHECK_FALSE (shouldPromptForUnsavedChanges (settings, false));   // clean: nothing to ask
    CHECK (shouldConfirmReplace (settings));

    settings.setAskToSaveUnsavedChanges (false);
    settings.setAskBeforeReplacingFile (false);
    CHECK_FALSE (shouldPromptForUnsavedChanges (settings, true));    // off: discard silently
    CHECK_FALSE (shouldPromptForUnsavedChanges (settings, false));
    CHECK_FALSE (shouldConfirmReplace (settings));
}
```

- [ ] **Step 2: Register and run to verify it fails**

Add `AppSettings_tests.cpp` to `Tests/CMakeLists.txt`, then:
Run: `cmake --build build 2>&1 | grep -E "error" | head`
Expected: FAIL — `UI/AppSettings.h: No such file`.

- [ ] **Step 3: Write minimal implementation** — `Source/UI/AppSettings.h`

```cpp
#pragma once

#include <juce_data_structures/juce_data_structures.h>

// Typed view over the per-user settings file (MainWindow's PropertiesFile).
// Key names and defaults live here and nowhere else. Setters write and save at
// once, so a toggle survives a crash. Reads are live: callers ask at the moment
// of use, so a change takes effect without a restart.
namespace lotro
{

class AppSettings
{
public:
    explicit AppSettings (juce::PropertiesFile& fileIn) : file (fileIn) {}

    bool askToSaveUnsavedChanges() const { return read (keyUnsavedChanges); }
    void setAskToSaveUnsavedChanges (bool on) { write (keyUnsavedChanges, on); }

    bool askBeforeReplacingFile() const { return read (keyReplaceFile); }
    void setAskBeforeReplacingFile (bool on) { write (keyReplaceFile, on); }

private:
    static constexpr const char* keyUnsavedChanges = "confirm.unsavedChanges";
    static constexpr const char* keyReplaceFile    = "confirm.replaceFile";

    // Anything that is not an explicit "0" / "1" (corrupt or hand-edited file)
    // reads as the default, which is on.
    bool read (const char* key) const
    {
        const auto text = file.getValue (key);
        return text == "0" ? false : true;
    }
    void write (const char* key, bool on)
    {
        file.setValue (key, on ? "1" : "0");
        file.saveIfNeeded();
    }

    juce::PropertiesFile& file;
};

// New / Open / drops / Quit: ask only when the Song is dirty and the user wants asking.
// When this is false for a dirty Song, the caller proceeds as "Don't Save".
inline bool shouldPromptForUnsavedChanges (const AppSettings& settings, bool isDirty)
{
    return isDirty && settings.askToSaveUnsavedChanges();
}

inline bool shouldConfirmReplace (const AppSettings& settings)
{
    return settings.askBeforeReplacingFile();
}

} // namespace lotro
```

- [ ] **Step 4: Run tests to verify they pass**

Run: `cmake --build build 2>&1 | grep -E " error" ; ctest --test-dir build -R "AppSettings|shouldPrompt" --output-on-failure`
Expected: 4 tests PASS.

- [ ] **Step 5: Commit**

```bash
git add Source/UI/AppSettings.h Tests/AppSettings_tests.cpp Tests/CMakeLists.txt
git commit -m "feat(ui): AppSettings with the two confirmation toggles

Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>"
```

---

### Task 2: PreferencesDialog with the General page

**Files:**
- Create: `Source/UI/Preferences/GeneralPreferencesPage.h`, `Source/UI/Preferences/GeneralPreferencesPage.cpp`
- Create: `Source/UI/Preferences/PreferencesDialog.h`, `Source/UI/Preferences/PreferencesDialog.cpp`
- Create: `Tests/PreferencesDialog_tests.cpp`
- Modify: `CMakeLists.txt` (add the two `.cpp` files to `forge_ui` next to `Source/UI/AboutBox.cpp`, ~line 153)
- Modify: `Tests/CMakeLists.txt` (add `PreferencesDialog_tests.cpp` to the test list; add both `.cpp` files to the UI source list next to `AboutBox.cpp`, ~line 111)

**Interfaces:**
- Consumes: `AppSettings` (Task 1); `SplitterComponent` (`SplitterComponent (Orientation::leftRight)`, `setComponents (first, second)`, `setFraction (float)`); `SongsmithColours::{background, text, textMuted}`.
- Produces:
  - `class GeneralPreferencesPage : public juce::Component { explicit GeneralPreferencesPage (AppSettings&); juce::ToggleButton& unsavedChangesToggleForTesting(); juce::ToggleButton& replaceFileToggleForTesting(); }`
  - `struct PreferencesPage { juce::String name; std::function<std::unique_ptr<juce::Component> (AppSettings&)> make; }` and `const std::vector<PreferencesPage>& preferencePages()`
  - `class PreferencesDialog : public juce::Component { explicit PreferencesDialog (AppSettings&); juce::StringArray pageNames() const; int getSelectedPageIndex() const; void selectPage (int); juce::Component* currentPage(); juce::TextButton& closeButtonForTesting(); std::function<void()> onCloseRequested; }`
  - `void showPreferencesDialog (AppSettings&, juce::Component* centreAround)`

- [ ] **Step 1: Write the failing tests** — `Tests/PreferencesDialog_tests.cpp`

```cpp
#include "UI/Preferences/PreferencesDialog.h"
#include "UI/Preferences/GeneralPreferencesPage.h"

#include <catch2/catch_test_macros.hpp>

using namespace lotro;

namespace
{
    struct Fixture
    {
        juce::File file = juce::File::createTempFile (".settings");
        std::unique_ptr<juce::PropertiesFile> props = std::make_unique<juce::PropertiesFile> (file, juce::PropertiesFile::Options());
        AppSettings settings { *props };
        ~Fixture() { props.reset(); file.deleteFile(); }
    };
}

TEST_CASE ("PreferencesDialog: the tree lists the registered pages, General first and selected", "[preferences]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    Fixture f;
    PreferencesDialog dialog (f.settings);

    REQUIRE (preferencePages().size() >= 1);
    CHECK (preferencePages().front().name == "General");
    CHECK (dialog.pageNames() == juce::StringArray { "General" });
    CHECK (dialog.getSelectedPageIndex() == 0);
    CHECK (dynamic_cast<GeneralPreferencesPage*> (dialog.currentPage()) != nullptr);
}

TEST_CASE ("PreferencesDialog: tree, splitter and page share the content, tree on the left", "[preferences]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    Fixture f;
    PreferencesDialog dialog (f.settings);
    dialog.setSize (640, 400);

    auto* page = dialog.currentPage();
    REQUIRE (page != nullptr);
    CHECK (dialog.getLocalBounds().contains (page->getScreenBounds().translated (-dialog.getScreenX(), -dialog.getScreenY())));
    CHECK (dialog.treeLeftEdgeForTesting() < page->getScreenX() - dialog.getScreenX());
    CHECK (dialog.treeWidthForTesting() < dialog.getWidth() / 2);   // fraction 0.25
}

TEST_CASE ("General page: toggles show the saved state and write straight through", "[preferences]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    Fixture f;
    {
        GeneralPreferencesPage page (f.settings);
        CHECK (page.unsavedChangesToggleForTesting().getToggleState());
        CHECK (page.replaceFileToggleForTesting().getToggleState());

        page.unsavedChangesToggleForTesting().setToggleState (false, juce::sendNotification);
        CHECK_FALSE (f.settings.askToSaveUnsavedChanges());
        CHECK (f.settings.askBeforeReplacingFile());

        page.replaceFileToggleForTesting().setToggleState (false, juce::sendNotification);
        CHECK_FALSE (f.settings.askBeforeReplacingFile());
    }
    // A fresh page over the same settings shows what was saved, not the defaults.
    GeneralPreferencesPage again (f.settings);
    CHECK_FALSE (again.unsavedChangesToggleForTesting().getToggleState());
    CHECK_FALSE (again.replaceFileToggleForTesting().getToggleState());
}

TEST_CASE ("PreferencesDialog: the Close button asks to close", "[preferences]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    Fixture f;
    PreferencesDialog dialog (f.settings);
    bool closed = false;
    dialog.onCloseRequested = [&] { closed = true; };
    dialog.closeButtonForTesting().triggerClick();
    CHECK (closed);
}
```

(`treeLeftEdgeForTesting()` / `treeWidthForTesting()` return the tree's x and width in the dialog's coordinates — add them to the Produces list of `PreferencesDialog`.)

- [ ] **Step 2: Register and run to verify it fails**

Add the test file and sources to both CMake lists, then:
Run: `cmake --build build 2>&1 | grep -E "error" | head`
Expected: FAIL — `UI/Preferences/PreferencesDialog.h: No such file`.

- [ ] **Step 3: Write minimal implementation**

`Source/UI/Preferences/GeneralPreferencesPage.h`:

```cpp
#pragma once

#include "../AppSettings.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace lotro
{
    // Preferences > General. Each toggle is applied (and saved) on click.
    class GeneralPreferencesPage : public juce::Component
    {
    public:
        explicit GeneralPreferencesPage (AppSettings& settings);

        void resized() override;

        juce::ToggleButton& unsavedChangesToggleForTesting() { return unsavedChanges; }
        juce::ToggleButton& replaceFileToggleForTesting() { return replaceFile; }

    private:
        juce::ToggleButton unsavedChanges { "Ask about unsaved changes" };
        juce::Label        unsavedChangesNote;
        juce::ToggleButton replaceFile { "Ask before replacing an existing file" };
    };
}
```

`Source/UI/Preferences/GeneralPreferencesPage.cpp`:

```cpp
#include "GeneralPreferencesPage.h"
#include "../SongsmithColours.h"

namespace lotro
{
    GeneralPreferencesPage::GeneralPreferencesPage (AppSettings& settings)
    {
        for (auto* b : { &unsavedChanges, &replaceFile })
        {
            b->setColour (juce::ToggleButton::textColourId, juce::Colour (SongsmithColours::text));
            addAndMakeVisible (*b);
        }
        unsavedChanges.setToggleState (settings.askToSaveUnsavedChanges(), juce::dontSendNotification);
        replaceFile.setToggleState (settings.askBeforeReplacingFile(), juce::dontSendNotification);
        unsavedChanges.onClick = [this, &settings] { settings.setAskToSaveUnsavedChanges (unsavedChanges.getToggleState()); };
        replaceFile.onClick    = [this, &settings] { settings.setAskBeforeReplacingFile (replaceFile.getToggleState()); };

        unsavedChangesNote.setText ("When off, New, Open and Quit discard unsaved edits without asking.",
                                    juce::dontSendNotification);
        unsavedChangesNote.setFont (juce::FontOptions (12.0f));
        unsavedChangesNote.setColour (juce::Label::textColourId, juce::Colour (SongsmithColours::textMuted));
        addAndMakeVisible (unsavedChangesNote);
    }

    void GeneralPreferencesPage::resized()
    {
        auto area = getLocalBounds().reduced (12);
        unsavedChanges.setBounds (area.removeFromTop (24));
        unsavedChangesNote.setBounds (area.removeFromTop (20).withTrimmedLeft (24));
        area.removeFromTop (12);
        replaceFile.setBounds (area.removeFromTop (24));
    }
}
```

`Source/UI/Preferences/PreferencesDialog.h`:

```cpp
#pragma once

#include "../AppSettings.h"
#include "../SplitterComponent.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>
#include <vector>

namespace lotro
{
    // One entry per tree item. Adding a page = one entry in preferencePages()
    // plus its component.
    struct PreferencesPage
    {
        juce::String name;
        std::function<std::unique_ptr<juce::Component> (AppSettings&)> make;
    };

    const std::vector<PreferencesPage>& preferencePages();

    // File > Preferences... content: page tree | splitter | selected page.
    class PreferencesDialog : public juce::Component
    {
    public:
        explicit PreferencesDialog (AppSettings& settings);
        ~PreferencesDialog() override;

        void paint (juce::Graphics& g) override;
        void resized() override;

        juce::StringArray pageNames() const;
        int getSelectedPageIndex() const noexcept { return selectedIndex; }
        void selectPage (int index);
        juce::Component* currentPage() noexcept { return page.get(); }

        // Fired by the Close button; showPreferencesDialog() closes the window on it.
        std::function<void()> onCloseRequested;

        juce::TextButton& closeButtonForTesting() { return closeButton; }
        int treeLeftEdgeForTesting() const { return tree.getX(); }
        int treeWidthForTesting() const { return tree.getWidth(); }

    private:
        class RootItem;
        class PageItem;

        AppSettings& settings;
        juce::TreeView tree;
        std::unique_ptr<RootItem> root;
        juce::Component pageHost;
        juce::Label pageTitle;
        std::unique_ptr<juce::Component> page;
        SplitterComponent splitter { SplitterComponent::Orientation::leftRight };
        juce::TextButton closeButton { "Close" };
        int selectedIndex = -1;
    };

    // Opens the dialog fully modal (the main window and its menus are blocked
    // until it closes). `settings` must outlive it; MainWindow owns both.
    void showPreferencesDialog (AppSettings& settings, juce::Component* centreAround);
}
```

`Source/UI/Preferences/PreferencesDialog.cpp`:

```cpp
#include "PreferencesDialog.h"
#include "GeneralPreferencesPage.h"
#include "../SongsmithColours.h"

namespace lotro
{
    const std::vector<PreferencesPage>& preferencePages()
    {
        static const std::vector<PreferencesPage> pages
        {
            { "General", [] (AppSettings& s) -> std::unique_ptr<juce::Component> { return std::make_unique<GeneralPreferencesPage> (s); } },
        };
        return pages;
    }

    class PreferencesDialog::PageItem : public juce::TreeViewItem
    {
    public:
        PageItem (PreferencesDialog& ownerIn, int indexIn) : owner (ownerIn), index (indexIn) {}

        bool mightContainSubItems() override { return false; }
        void paintItem (juce::Graphics& g, int width, int height) override
        {
            if (isSelected())
                g.fillAll (juce::Colour (SongsmithColours::selectedRow));
            g.setColour (juce::Colour (SongsmithColours::text));
            g.setFont (juce::FontOptions (14.0f));
            g.drawText (preferencePages()[(size_t) index].name, 8, 0, width - 8, height, juce::Justification::centredLeft);
        }
        void itemSelectionChanged (bool nowSelected) override
        {
            if (nowSelected)
                owner.selectPage (index);
        }

    private:
        PreferencesDialog& owner;
        int index;
    };

    class PreferencesDialog::RootItem : public juce::TreeViewItem
    {
    public:
        explicit RootItem (PreferencesDialog& owner)
        {
            for (int i = 0; i < (int) preferencePages().size(); ++i)
                addSubItem (new PageItem (owner, i));
        }
        bool mightContainSubItems() override { return true; }
    };

    PreferencesDialog::PreferencesDialog (AppSettings& settingsIn) : settings (settingsIn)
    {
        root = std::make_unique<RootItem> (*this);
        tree.setRootItem (root.get());
        tree.setRootItemVisible (false);
        tree.setDefaultOpenness (true);
        tree.setColour (juce::TreeView::backgroundColourId, juce::Colour (SongsmithColours::background));
        addAndMakeVisible (tree);

        pageTitle.setFont (juce::FontOptions (18.0f));
        pageTitle.setColour (juce::Label::textColourId, juce::Colour (SongsmithColours::text));
        pageHost.addAndMakeVisible (pageTitle);
        addAndMakeVisible (pageHost);

        splitter.setComponents (&tree, &pageHost);
        splitter.setFraction (0.25f);
        addAndMakeVisible (splitter);

        closeButton.onClick = [this] { if (onCloseRequested) onCloseRequested(); };
        addAndMakeVisible (closeButton);

        setSize (640, 400);
        selectPage (0);
        if (auto* item = root->getSubItem (0))
            item->setSelected (true, false, juce::dontSendNotification);
    }

    PreferencesDialog::~PreferencesDialog()
    {
        tree.setRootItem (nullptr);   // before root is destroyed
    }

    juce::StringArray PreferencesDialog::pageNames() const
    {
        juce::StringArray names;
        for (const auto& p : preferencePages())
            names.add (p.name);
        return names;
    }

    void PreferencesDialog::selectPage (int index)
    {
        const auto& pages = preferencePages();
        if (index < 0 || index >= (int) pages.size() || index == selectedIndex)
            return;
        selectedIndex = index;
        page = pages[(size_t) index].make (settings);
        pageHost.addAndMakeVisible (*page);
        pageTitle.setText (pages[(size_t) index].name, juce::dontSendNotification);
        resized();
    }

    void PreferencesDialog::paint (juce::Graphics& g)
    {
        g.fillAll (juce::Colour (SongsmithColours::background));
    }

    void PreferencesDialog::resized()
    {
        auto area = getLocalBounds();
        closeButton.setBounds (area.removeFromBottom (44).reduced (8).removeFromRight (80));
        splitter.setBounds (area);   // lays out tree (left) and pageHost (right)

        auto host = pageHost.getLocalBounds();
        pageTitle.setBounds (host.removeFromTop (36).reduced (12, 4));
        if (page != nullptr)
            page->setBounds (host);
    }

    void showPreferencesDialog (AppSettings& settings, juce::Component* centreAround)
    {
        auto content = std::make_unique<PreferencesDialog> (settings);
        juce::DialogWindow::LaunchOptions options;
        auto* dialog = content.get();
        options.content.setOwned (content.release());
        options.dialogTitle = "Preferences";
        options.dialogBackgroundColour = juce::Colour (SongsmithColours::background);
        options.componentToCentreAround = centreAround;
        options.useNativeTitleBar = true;
        options.resizable = true;
        auto* window = options.launchAsync();   // enters modal state
        dialog->onCloseRequested = [window] { if (window != nullptr) window->exitModalState (0); };
    }
}
```

Notes for the implementer:
- `SongsmithColours::selectedRow` exists (used by `TrackRowComponent`); confirm with `grep selectedRow Source/UI/SongsmithColours.h`.
- `SplitterComponent::resized()` positions its two components itself, so only the splitter gets bounds; `pageHost` and `tree` are siblings and children of this component, and the splitter is added last so it sits on top.
- If `launchAsync()` returns a window that is deleted by JUCE on close, `window` in the lambda must not be used after that; closing only ever happens through this lambda or the window's own close button, both of which go through `exitModalState`.

- [ ] **Step 4: Run tests to verify they pass**

Run: `cmake --build build 2>&1 | grep -E " error" ; ctest --test-dir build -R "PreferencesDialog|General page" --output-on-failure`
Expected: 4 tests PASS. If the layout test fails on screen coordinates (component not on screen), compare `page->getBounds()` translated through `pageHost.getPosition()` instead of `getScreenBounds()` — the assertion's intent is "tree left of page, tree narrower than half".

- [ ] **Step 5: Commit**

```bash
git add Source/UI/Preferences Tests/PreferencesDialog_tests.cpp CMakeLists.txt Tests/CMakeLists.txt
git commit -m "feat(ui): Preferences dialog with a page tree, splitter and General page

Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>"
```

---

### Task 3: Menu entry and applying the settings in MainWindow

**Files:**
- Modify: `Source/UI/MenuModel.h` (enum, `allCommandIds`, File menu)
- Modify: `Tests/MenuModel_tests.cpp` (File layout test, new Preferences test)
- Modify: `Source/UI/MainWindow.h` (member `AppSettings appSettings`, nothing else public)
- Modify: `Source/UI/MainWindow.cpp` (include, dispatch, `guarded()`, two overwrite sites, `chooseAndConfirm` signature)

**Interfaces:**
- Consumes: `AppSettings`, `shouldPromptForUnsavedChanges`, `shouldConfirmReplace` (Task 1); `showPreferencesDialog (AppSettings&, juce::Component*)` (Task 2).
- Produces: `MenuCommandId::FilePreferences`; `allCommandIds` size 29.

- [ ] **Step 1: Write the failing tests** — edit `Tests/MenuModel_tests.cpp`

Replace the File layout expectation (line ~60):

```cpp
    CHECK (labels (file) == V { "New", "Open...", "-", "Save", "Save As...", "-", "Import>", "Export>", "-", "Preferences...", "-", "Quit" });
```

Add after the File layout test:

```cpp
TEST_CASE ("MenuModel: Preferences sits between Export and Quit, always enabled, no shortcut", "[menu-model]")
{
    for (const bool dirty : { false, true })
    {
        MenuState s; s.isDirty = dirty;
        const auto file = menuNamed (buildMenus (s), "File");
        const auto prefs = itemWith (file, FilePreferences);
        CHECK (prefs.label == "Preferences...");
        CHECK (prefs.shortcut.empty());
        CHECK (prefs.enabled);
    }
}
```

- [ ] **Step 2: Run to verify it fails**

Run: `cmake --build build 2>&1 | grep -E "error" | head`
Expected: FAIL — `FilePreferences` undeclared.

- [ ] **Step 3: Write minimal implementation**

`Source/UI/MenuModel.h`:
- Add `FilePreferences,` after `FileExportMidi,` in the enum (before `FileQuit`). It is not at the end of the enum, so every later id shifts by one; that is fine (ids are only compared, never persisted).
- `inline constexpr std::array<MenuCommandId, 29> allCommandIds { ..., FileExportMidi, FilePreferences, FileQuit, ... }`.
- File menu: after the Export submenu, change

```cpp
        separator(),
        item (FileQuit,     "Quit",       "Ctrl+Q") } });
```
to
```cpp
        separator(),
        item (FilePreferences, "Preferences..."),
        separator(),
        item (FileQuit,     "Quit",       "Ctrl+Q") } });
```

`Source/UI/MainWindow.h`: add `#include "AppSettings.h"` with the other includes and, directly after `std::unique_ptr<juce::PropertiesFile> settings;`:

```cpp
    // Typed view over `settings`; declared after it so it is built from a live file.
    AppSettings                             appSettings { *settings };
```

`Source/UI/MainWindow.cpp`:
- `#include "AppSettings.h"` and `#include "Preferences/PreferencesDialog.h"` with the other includes.
- In `menuItemSelected`, next to `case HelpAbout:` add `case FilePreferences: showPreferencesDialog (appSettings, this);                         return;`
- `guarded()`:

```cpp
void MainWindow::guarded (std::function<void()> action)
{
    // With the prompt switched off a dirty Song counts as clean here, so the action
    // proceeds and the unsaved edits are discarded without asking.
    confirmDiscardChanges (shouldPromptForUnsavedChanges (appSettings, session.isDirty()), guardHooks(), std::move (action));
}
```
- `chooseAndConfirm`: add a `bool confirmReplace` parameter after `owner` (`MainWindow* owner, bool confirmReplace, std::unique_ptr<juce::FileChooser>& holder, ...`), capture it in the lambda (`[safe = ..., confirmReplace, accepted, defaultExt, write]`) and change `if (! file.existsAsFile()) { write (file); return; }` to `if (! confirmReplace || ! file.existsAsFile()) { write (file); return; }`. Update its callers (grep `chooseAndConfirm (`) to pass `shouldConfirmReplace (appSettings)`.
- Song Save As (~line 510): change `if (file.existsAsFile() && file != session.getFile())` to `if (shouldConfirmReplace (appSettings) && file.existsAsFile() && file != session.getFile())`.

- [ ] **Step 4: Run tests to verify they pass**

Run: `cmake --build build 2>&1 | grep -E " error" ; ctest --test-dir build 2>&1 | tail -4`
Expected: all tests pass (the `allCommandIds` coverage test in `MenuModel_tests.cpp` now includes `FilePreferences`, which is in the File menu).
Then build the UI target to prove `MainWindow.cpp` compiles (it is not in the test binary): `cmake --build build --target forge_ui 2>&1 | grep -E " error|Linking"`.

- [ ] **Step 5: Commit**

```bash
git add Source/UI/MenuModel.h Source/UI/MainWindow.h Source/UI/MainWindow.cpp Tests/MenuModel_tests.cpp
git commit -m "feat(ui): File > Preferences... opens the dialog; the two prompts follow the settings

Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>"
```

---

### Task 4: Docs, test count and manual verification list

**Files:**
- Modify: `docs/UI_GUIDE.md` (File menu listing near line 380; add a "Preferences" subsection)
- Modify: `docs/ARCHITECTURE.md` (new §9.16, `AppSettings` + dialog walkthrough; also mention `chooseAndConfirm`'s new parameter)
- Modify: `docs/TESTING.md` line 3 (new count from `ctest --test-dir build -N | tail -1`) and a line for `AppSettings_tests`, `PreferencesDialog_tests`
- Modify: `CLAUDE.md` (the File-menu sentence: add "Preferences…" between Export and Quit; status line mention)
- Modify: `docs/superpowers/specs/2026-10-08-songsmith-preferences-design.md` (Status: implemented)

- [ ] **Step 1: Make the doc edits** (content: menu position; the dialog's tree/splitter/canvas structure; the two settings with their keys and defaults; "off = discard silently"; how to add a page; the not-headless-testable parts).
- [ ] **Step 2: Run the full suite and record the count**

Run: `ctest --test-dir build 2>&1 | tail -3`
Expected: 100% passed; put the number in `docs/TESTING.md`.
- [ ] **Step 3: Commit**

```bash
git add docs CLAUDE.md
git commit -m "docs: Preferences dialog, AppSettings and test count

Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>"
```
- [ ] **Step 4: Hand the manual checks to the user (lead only; no GUI launch by agents)** after the Windows deploy (`./build-windows.sh forge_ui && cp … /mnt/c/Apps/SongSmith/ …` per `CLAUDE.md`):
  1. File menu shows Preferences… alone between Export and Quit.
  2. While Preferences is open, the main window, its menus and shortcuts do nothing; Close, Escape and the title-bar X each dismiss it and the main window works again.
  3. Tree shows General (selected); dragging the splitter resizes tree vs. page.
  4. Untick "Ask about unsaved changes": make an edit, then New (and Quit) — no prompt, edits discarded. Re-tick: the prompt returns.
  5. Untick "Ask before replacing…": Save As / Export over an existing file writes without the Replace box. Re-tick: the box returns.
  6. Quit and relaunch: both toggles keep their state.

---

## Self-review

- **Spec coverage:** menu (Task 3), `AppSettings` + keys/defaults + decision functions (Task 1), applying at the moment of use incl. both overwrite sites (Task 3), dialog tree/splitter/page host/Close/registry/General page + note (Task 2), tests list (Tasks 1–3), docs and manual checks incl. modality (Task 4). Out-of-scope items are not planned.
- **Placeholder scan:** none; every code step has code. Task 4 Step 1 lists doc content rather than prose because the docs are descriptive; its contents are fully determined by Tasks 1–3.
- **Type consistency:** `AppSettings`, `shouldPromptForUnsavedChanges`, `shouldConfirmReplace`, `PreferencesDialog`, `PreferencesPage`, `preferencePages()`, `GeneralPreferencesPage`, `showPreferencesDialog`, `FilePreferences` are spelled identically across tasks.
