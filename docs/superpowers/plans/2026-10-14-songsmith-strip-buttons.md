# Transport strip command buttons Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A `+` button at the far left of the transport strip's left area opens a grouped menu of app commands (and one "apply layout" entry per saved layout); picking one adds a text button that runs it; right-click removes it.

**Architecture:** A JUCE-light data module (`StripButtons`) holds the ordered list, its JSON and the follow-the-layout rules, plus two pure functions that turn a button into a view (label/enabled/pressed) and into a `MenuCommandId` using the existing `MenuState`/`buildMenus`. A `StripButtonsBar` component renders it inside `TransportStrip`'s left area and talks only to a `StripButtonsHost` (shared state + callbacks + `ChangeBroadcaster`), so the two strips (main window and MIDI-editor panel) stay in sync. `MainWindow` owns the host, persists to `AppSettings` and dispatches through the existing `menuItemSelected`.

**Tech Stack:** C++20, JUCE (`juce_gui_basics`, `juce_core`), Catch2, CMake + Ninja.

**Spec:** `docs/superpowers/specs/2026-10-14-songsmith-strip-buttons-design.md`

## Global Constraints

- Nothing is added to `Source/Core/` (UI-side only; see the `forge-engine-ui-boundary` skill).
- No new third-party dependencies.
- Strip buttons live in `AppSettings` under key `strip.buttons`, never in the `.songsmith` file.
- `maxStripButtons = 10`; each fixed command and each layout may appear once.
- Fixed commands are persisted by **string key**, never by numeric `MenuCommandId`.
- A layout button follows its layout: rename updates it, delete removes it.
- Quit, About, Play/Pause, Stop, Go to Start, Go to End and the grid-size items are not pickable.
- Types/annotations everywhere; custom error types, not strings; conventional commits; every commit ends with `Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>`.
- Never launch the GUI from an agent; appearance/overflow feel is verified by the user.
- Test sources and new `.cpp` files must be added to BOTH `CMakeLists.txt` (~line 168) and `Tests/CMakeLists.txt` (source list ~line 181, test list ~line 80).

## Review Focus

- A `strip.buttons` setting that is corrupt, not an array, or holds entries with an unknown kind, empty key or duplicates: the strip loads what is valid and never crashes (Task 1).
- A saved command key this build does not know (settings from a newer build): the button is hidden but the key survives a load/save round-trip (Tasks 1, 3).
- A layout button whose layout was removed behind its back (hand-edited settings): shown disabled, still removable (Tasks 3, 6).
- Renaming a layout to a different case only ("layout 1" → "Layout 1") keeps its button (Task 1).
- The strip is narrowed below the width of one button, or the cap of 10 is reached: no crash, `+` still reachable or greyed (Task 6).

---

### Task 1: `StripButtons` data module

**Files:**
- Create: `Source/UI/Dock/StripButtons.h`, `Source/UI/Dock/StripButtons.cpp`
- Create: `Tests/StripButtons_tests.cpp`
- Modify: `CMakeLists.txt` (add `Source/UI/Dock/StripButtons.cpp` after `Source/UI/Dock/SavedLayouts.cpp`), `Tests/CMakeLists.txt` (add `${CMAKE_SOURCE_DIR}/Source/UI/Dock/StripButtons.cpp` after the `SavedLayouts.cpp` line; add `StripButtons_tests.cpp` after `SavedLayouts_tests.cpp`)

**Interfaces:**
- Produces (namespace `lotro::dock`):
  - `struct StripButton { enum class Kind { command, applyLayout }; Kind kind = Kind::command; std::string key; }`
  - `using StripButtons = std::vector<StripButton>;`
  - `inline constexpr std::size_t maxStripButtons = 10;`
  - `bool sameButton (const StripButton&, const StripButton&)` — same kind and key; layout keys compare case-insensitively, command keys exactly.
  - `bool contains (const StripButtons&, const StripButton&)`
  - `bool addButton (StripButtons&, StripButton)` — false (no change) if present, empty key, or already `maxStripButtons`.
  - `void removeButton (StripButtons&, std::size_t index)` — out-of-range is a no-op.
  - `void renameLayoutButton (StripButtons&, const std::string& oldName, const std::string& newName)`
  - `void removeLayoutButton (StripButtons&, const std::string& name)`
  - `std::string toJson (const StripButtons&)`
  - `StripButtons stripButtonsFromJson (const std::string&)` — never throws.

- [ ] **Step 1: Write the failing tests**

Create `Tests/StripButtons_tests.cpp`:

```cpp
#include "UI/Dock/StripButtons.h"

#include <catch2/catch_test_macros.hpp>

using namespace lotro::dock;

namespace
{
StripButton cmd (const char* key) { return { StripButton::Kind::command, key }; }
StripButton layout (const char* name) { return { StripButton::Kind::applyLayout, name }; }
}

TEST_CASE ("StripButtons: add refuses duplicates and empty keys, layout names compare case-insensitively", "[dock][strip-buttons]")
{
    StripButtons list;
    CHECK (addButton (list, cmd ("file.exportMidi")));
    CHECK_FALSE (addButton (list, cmd ("file.exportMidi")));
    CHECK_FALSE (addButton (list, cmd ("")));
    CHECK (addButton (list, layout ("Wide")));
    CHECK_FALSE (addButton (list, layout ("wide")));
    CHECK (addButton (list, cmd ("Wide")));            // a command key is not a layout name
    CHECK (list.size() == 3);
    CHECK (contains (list, layout ("WIDE")));
    CHECK_FALSE (contains (list, cmd ("file.save")));
}

TEST_CASE ("StripButtons: the strip holds at most ten buttons", "[dock][strip-buttons]")
{
    StripButtons list;
    for (std::size_t i = 0; i < maxStripButtons; ++i)
        REQUIRE (addButton (list, layout (("L" + std::to_string (i)).c_str())));
    CHECK_FALSE (addButton (list, cmd ("file.save")));
    CHECK (list.size() == 10);
}

TEST_CASE ("StripButtons: remove drops one button and ignores a bad index", "[dock][strip-buttons]")
{
    StripButtons list { cmd ("a"), cmd ("b"), cmd ("c") };
    removeButton (list, 1);
    CHECK (list.size() == 2);
    CHECK (list[1].key == "c");
    removeButton (list, 9);
    CHECK (list.size() == 2);
}

TEST_CASE ("StripButtons: renaming a layout renames its button, including a case-only change", "[dock][strip-buttons]")
{
    StripButtons list { cmd ("file.save"), layout ("layout 1"), layout ("Other") };
    renameLayoutButton (list, "layout 1", "Layout 1");
    CHECK (list[1].key == "Layout 1");
    renameLayoutButton (list, "Other", "Mixing");
    CHECK (list[2].key == "Mixing");
    renameLayoutButton (list, "file.save", "x");       // a command button is never touched
    CHECK (list[0].key == "file.save");
    renameLayoutButton (list, "nope", "x");
    CHECK (list.size() == 3);
}

TEST_CASE ("StripButtons: deleting a layout removes only its button", "[dock][strip-buttons]")
{
    StripButtons list { layout ("Wide"), cmd ("Wide"), layout ("Tall") };
    removeLayoutButton (list, "wide");
    REQUIRE (list.size() == 2);
    CHECK (list[0].kind == StripButton::Kind::command);
    CHECK (list[1].key == "Tall");
}

TEST_CASE ("StripButtons: JSON round-trips in order", "[dock][strip-buttons]")
{
    const StripButtons list { cmd ("file.exportMidi"), layout ("Wide"), cmd ("view.mixer") };
    const auto back = stripButtonsFromJson (toJson (list));
    REQUIRE (back.size() == 3);
    for (std::size_t i = 0; i < 3; ++i)
        CHECK (sameButton (back[i], list[i]));
}

TEST_CASE ("StripButtons: corrupt, wrong-shaped or partly bad JSON loads what is valid", "[dock][strip-buttons]")
{
    CHECK (stripButtonsFromJson ("").empty());
    CHECK (stripButtonsFromJson ("not json").empty());
    CHECK (stripButtonsFromJson ("{\"kind\":\"command\"}").empty());
    const auto some = stripButtonsFromJson (
        "[{\"kind\":\"command\",\"key\":\"file.save\"},"
        "{\"kind\":\"mystery\",\"key\":\"x\"},"                // unknown kind: dropped
        "{\"kind\":\"command\",\"key\":\"\"},"                 // empty key: dropped
        "{\"kind\":\"command\",\"key\":\"file.save\"},"        // duplicate: dropped
        "{\"kind\":\"command\",\"key\":\"future.thing\"},"     // unknown command key: KEPT for a newer build
        "7]");
    REQUIRE (some.size() == 2);
    CHECK (some[0].key == "file.save");
    CHECK (some[1].key == "future.thing");
}

TEST_CASE ("StripButtons: loading more than the cap keeps the first ten", "[dock][strip-buttons]")
{
    std::string json = "[";
    for (int i = 0; i < 15; ++i)
        json += std::string (i ? "," : "") + "{\"kind\":\"applyLayout\",\"key\":\"L" + std::to_string (i) + "\"}";
    json += "]";
    CHECK (stripButtonsFromJson (json).size() == maxStripButtons);
}
```

- [ ] **Step 2: Register the files and run the tests to verify they fail**

Add the two `StripButtons.cpp` lines and the test line to the CMake lists (see Files), then:

Run: `cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug && cmake --build build --target forge_tests 2>&1 | tail -20`
Expected: FAIL — `UI/Dock/StripButtons.h: No such file or directory`.

- [ ] **Step 3: Write the header**

`Source/UI/Dock/StripButtons.h`:

```cpp
#pragma once

#include <cstddef>
#include <string>
#include <vector>

// The user's chosen buttons for the transport strip's left area: pure data, no UI.
// Persisted as JSON in AppSettings (key strip.buttons). A command button stores a stable
// string key (see stripCommands in MenuModel.h); a layout button stores the layout's name.
namespace lotro::dock
{

struct StripButton
{
    enum class Kind { command, applyLayout };
    Kind kind = Kind::command;
    std::string key;
};

using StripButtons = std::vector<StripButton>;

inline constexpr std::size_t maxStripButtons = 10;

// Same kind and key. Layout names compare case-insensitively; command keys exactly.
bool sameButton (const StripButton&, const StripButton&);
bool contains (const StripButtons&, const StripButton&);

// False (nothing changed) if the button is already there, has an empty key, or the strip is full.
bool addButton (StripButtons&, StripButton);
void removeButton (StripButtons&, std::size_t index);

// The follow-the-layout rules: only applyLayout buttons are touched.
void renameLayoutButton (StripButtons&, const std::string& oldName, const std::string& newName);
void removeLayoutButton (StripButtons&, const std::string& name);

std::string toJson (const StripButtons&);
// Never throws. Empty/corrupt -> {}. Unknown kinds, empty keys and duplicates are dropped; an
// unknown command key is kept (a newer build's setting must survive a round-trip); capped at maxStripButtons.
StripButtons stripButtonsFromJson (const std::string&);

} // namespace lotro::dock
```

- [ ] **Step 4: Write the implementation**

`Source/UI/Dock/StripButtons.cpp`:

```cpp
#include "UI/Dock/StripButtons.h"

#include <juce_core/juce_core.h>

#include <algorithm>

namespace lotro::dock
{
namespace
{
bool sameName (const std::string& a, const std::string& b)
{
    return juce::String (a).equalsIgnoreCase (juce::String (b));
}

const char* kindName (StripButton::Kind k) { return k == StripButton::Kind::command ? "command" : "applyLayout"; }
}

bool sameButton (const StripButton& a, const StripButton& b)
{
    if (a.kind != b.kind)
        return false;
    return a.kind == StripButton::Kind::applyLayout ? sameName (a.key, b.key) : a.key == b.key;
}

bool contains (const StripButtons& list, const StripButton& button)
{
    return std::any_of (list.begin(), list.end(), [&] (const StripButton& b) { return sameButton (b, button); });
}

bool addButton (StripButtons& list, StripButton button)
{
    if (button.key.empty() || list.size() >= maxStripButtons || contains (list, button))
        return false;
    list.push_back (std::move (button));
    return true;
}

void removeButton (StripButtons& list, std::size_t index)
{
    if (index < list.size())
        list.erase (list.begin() + (std::ptrdiff_t) index);
}

void renameLayoutButton (StripButtons& list, const std::string& oldName, const std::string& newName)
{
    for (auto& b : list)
        if (b.kind == StripButton::Kind::applyLayout && sameName (b.key, oldName))
            b.key = newName;
}

void removeLayoutButton (StripButtons& list, const std::string& name)
{
    list.erase (std::remove_if (list.begin(), list.end(), [&] (const StripButton& b)
                { return b.kind == StripButton::Kind::applyLayout && sameName (b.key, name); }),
                list.end());
}

std::string toJson (const StripButtons& list)
{
    juce::Array<juce::var> items;
    for (const auto& b : list)
    {
        auto* o = new juce::DynamicObject();
        o->setProperty ("kind", juce::String (kindName (b.kind)));
        o->setProperty ("key", juce::String (b.key));
        items.add (juce::var (o));
    }
    return juce::JSON::toString (juce::var (items), true).toStdString();
}

StripButtons stripButtonsFromJson (const std::string& text)
{
    StripButtons out;
    const auto parsed = juce::JSON::parse (juce::String::fromUTF8 (text.data(), static_cast<int> (text.size())));
    const auto* arr = parsed.getArray();
    if (arr == nullptr)
        return out;
    for (const auto& v : *arr)
    {
        if (! v.isObject())
            continue;
        const auto kind = v.getProperty ("kind", {}).toString();
        StripButton b;
        if (kind == "command")          b.kind = StripButton::Kind::command;
        else if (kind == "applyLayout") b.kind = StripButton::Kind::applyLayout;
        else                            continue;
        b.key = v.getProperty ("key", {}).toString().trim().toStdString();
        addButton (out, std::move (b));   // drops empty keys and duplicates, stops at the cap
    }
    return out;
}

} // namespace lotro::dock
```

- [ ] **Step 5: Run the tests to verify they pass**

Run: `cmake --build build --target forge_tests 2>&1 | tail -5 && ctest --test-dir build -R "StripButtons" --output-on-failure`
Expected: 8 tests PASS.

- [ ] **Step 6: Commit**

```bash
git add Source/UI/Dock/StripButtons.h Source/UI/Dock/StripButtons.cpp Tests/StripButtons_tests.cpp CMakeLists.txt Tests/CMakeLists.txt
git commit -m "feat(ui): StripButtons data module for the transport strip's chosen buttons

Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>"
```

---

### Task 2: Pickable-command table and menu lookup (`MenuModel.h`)

**Files:**
- Modify: `Source/UI/MenuModel.h` (append before the closing `} // namespace lotro`)
- Modify: `Tests/MenuModel_tests.cpp` (append)

**Interfaces:**
- Produces (namespace `lotro`):
  - `struct StripCommand { const char* group; const char* label; const char* key; MenuCommandId id; }`
  - `inline constexpr std::array<StripCommand, 32> stripCommands`
  - `inline const StripCommand* findStripCommand (std::string_view key)` — nullptr if unknown.
  - `inline const MenuItemDesc* findMenuItem (const std::vector<MenuDesc>& menus, int id)` — searches submenus; nullptr if absent.

- [ ] **Step 1: Write the failing tests**

Append to `Tests/MenuModel_tests.cpp`:

```cpp
TEST_CASE ("MenuModel: every pickable strip command is a real command with a unique key", "[menu-model][strip-buttons]")
{
    std::set<std::string> keys;
    for (const auto& c : stripCommands)
    {
        CHECK (std::find (allCommandIds.begin(), allCommandIds.end(), c.id) != allCommandIds.end());
        CHECK (keys.insert (c.key).second);
        CHECK (std::string (c.label).size() > 0);
    }
}

TEST_CASE ("MenuModel: Quit, About and the centre transport buttons are not pickable", "[menu-model][strip-buttons]")
{
    for (const auto id : { FileQuit, HelpAbout, TransportPlayPause, TransportStop, TransportGoToStart, TransportGoToEnd })
        for (const auto& c : stripCommands)
            CHECK (c.id != id);
}

TEST_CASE ("MenuModel: findStripCommand looks a key up and rejects an unknown one", "[menu-model][strip-buttons]")
{
    REQUIRE (findStripCommand ("file.exportMidi") != nullptr);
    CHECK (findStripCommand ("file.exportMidi")->id == FileExportMidi);
    CHECK (findStripCommand ("future.thing") == nullptr);
}

TEST_CASE ("MenuModel: findMenuItem finds an item inside a submenu", "[menu-model][strip-buttons]")
{
    MenuState s; s.hasAbc = true;
    const auto menus = buildMenus (s);
    const auto* abc = findMenuItem (menus, FileExportAbc);
    REQUIRE (abc != nullptr);
    CHECK (abc->enabled);
    CHECK (findMenuItem (menus, 99999) == nullptr);
}
```

Add `#include <set>` and `#include <algorithm>` at the top of that file if missing.

- [ ] **Step 2: Run to verify failure**

Run: `cmake --build build --target forge_tests 2>&1 | grep -m3 "error"`
Expected: FAIL — `stripCommands` / `findStripCommand` / `findMenuItem` not declared.

- [ ] **Step 3: Implement**

Add `#include <string_view>` to the includes of `MenuModel.h`, then append before `} // namespace lotro`:

```cpp
// The commands the transport strip's "+" menu offers, in picker order. `key` is the stable
// string saved in AppSettings (never reuse or change one); `label` is the button text.
struct StripCommand
{
    const char*   group;
    const char*   label;
    const char*   key;
    MenuCommandId id;
};

inline constexpr std::array<StripCommand, 32> stripCommands {{
    { "File", "New",            "file.new",            FileNew },
    { "File", "Open",           "file.open",           FileOpenSong },
    { "File", "Save",           "file.save",           FileSave },
    { "File", "Save As",        "file.saveAs",         FileSaveAs },
    { "File", "Import MIDI",    "file.importMidi",     FileImportMidi },
    { "File", "Export MIDI",    "file.exportMidi",     FileExportMidi },
    { "File", "Export ABC",     "file.exportAbc",      FileExportAbc },
    { "File", "Preferences",    "file.preferences",    FilePreferences },
    { "Edit", "Undo",           "edit.undo",           EditUndo },
    { "Edit", "Redo",           "edit.redo",           EditRedo },
    { "Edit", "Split",          "edit.split",          EditSplit },
    { "Edit", "Delete",         "edit.delete",         EditDelete },
    { "Edit", "Quantize",       "edit.quantize",       EditQuantize },
    { "Edit", "Tempo/Meter",    "edit.tempoMeter",     EditTempoMeter },
    { "Edit", "Select All",     "edit.selectAll",      EditSelectAll },
    { "Edit", "Select Tracks",  "edit.selectAllTracks", EditSelectAllTracks },
    { "Edit", "Select Sections", "edit.selectAllSections", EditSelectAllSections },
    { "Song", "Default Parts",  "song.defaultParts",   SongDefaultParts },
    { "Song", "Run Converter",  "song.runConverter",   SongRunConverter },
    { "Song", "SoundFont",      "song.soundFont",      SongSoundFont },
    { "Transport", "Rewind Bar",   "transport.rewindOneBar", TransportRewindOneBar },
    { "Transport", "Clear Marker", "transport.clearMarker",  TransportClearMarker },
    { "View", "Export Panel",   "view.exportPanel",    ViewExportPanelToggle },
    { "View", "Diagnostics",    "view.diagnostics",    ViewDiagnosticsToggle },
    { "View", "Source",         "view.source",         ViewSourceToggle },
    { "View", "MIDI Editor",    "view.midiEditor",     ViewMidiEditorToggle },
    { "View", "LOTRO Preview",  "view.lotroPreview",   ViewLotroPreviewToggle },
    { "View", "Tracks",         "view.tracks",         ViewTracksToggle },
    { "View", "Parts",          "view.parts",          ViewPartsToggle },
    { "View", "Mixer",          "view.mixer",          ViewMixerToggle },
    { "View", "Save Layout",    "view.layoutSave",     ViewLayoutSave },
    { "View", "Manage Layouts", "view.layoutManage",   ViewLayoutManage },
}};

inline const StripCommand* findStripCommand (std::string_view key)
{
    for (const auto& c : stripCommands)
        if (key == c.key)
            return &c;
    return nullptr;
}

// The menu item with this id, searching submenus; nullptr if there is none.
inline const MenuItemDesc* findMenuItem (const std::vector<MenuItemDesc>& items, int id)
{
    for (const auto& i : items)
    {
        if (! i.isSubmenu && ! i.separator && i.id == id)
            return &i;
        if (i.isSubmenu)
            if (const auto* inner = findMenuItem (i.children, id))
                return inner;
    }
    return nullptr;
}

inline const MenuItemDesc* findMenuItem (const std::vector<MenuDesc>& menus, int id)
{
    for (const auto& m : menus)
        if (const auto* i = findMenuItem (m.items, id))
            return i;
    return nullptr;
}
```

- [ ] **Step 4: Run to verify they pass**

Run: `cmake --build build --target forge_tests 2>&1 | tail -3 && ctest --test-dir build -R "MenuModel" --output-on-failure`
Expected: all MenuModel tests PASS (existing ones included).

- [ ] **Step 5: Commit**

```bash
git add Source/UI/MenuModel.h Tests/MenuModel_tests.cpp
git commit -m "feat(ui): table of the commands the transport strip can host

Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>"
```

---

### Task 3: Button view and command resolution (pure)

**Files:**
- Modify: `Source/UI/Dock/StripButtons.h`, `Source/UI/Dock/StripButtons.cpp`
- Modify: `Tests/StripButtons_tests.cpp` (append)

**Interfaces:**
- Consumes: Task 1 types; Task 2 `findStripCommand`, `findMenuItem`, `stripCommands`; existing `buildMenus`, `MenuState`, `ViewLayoutApplyBase`, `maxSavedLayouts`.
- Produces (namespace `lotro::dock`):
  - `struct ButtonView { std::string label; bool enabled = true; bool pressed = false; }`
  - `std::optional<ButtonView> buttonView (const StripButton&, const MenuState&)` — nullopt = do not show (unknown command key).
  - `std::optional<int> commandIdFor (const StripButton&, const MenuState&)` — nullopt if the key is unknown or the layout no longer exists.

- [ ] **Step 1: Write the failing tests**

Append to `Tests/StripButtons_tests.cpp` (add `#include "UI/MenuModel.h"` at the top):

```cpp
TEST_CASE ("StripButtons: a command button takes its label and enabled state from the menu", "[dock][strip-buttons]")
{
    lotro::MenuState s;
    const auto abc = buttonView (cmd ("file.exportAbc"), s);
    REQUIRE (abc.has_value());
    CHECK (abc->label == "Export ABC");
    CHECK_FALSE (abc->enabled);              // no ABC yet
    s.hasAbc = true;
    CHECK (buttonView (cmd ("file.exportAbc"), s)->enabled);
    CHECK_FALSE (buttonView (cmd ("edit.undo"), s)->enabled);
    s.canUndo = true;
    CHECK (buttonView (cmd ("edit.undo"), s)->enabled);
}

TEST_CASE ("StripButtons: a panel-toggle button is pressed while its panel is visible", "[dock][strip-buttons]")
{
    lotro::MenuState s;
    s.mixerVisible = false;
    CHECK_FALSE (buttonView (cmd ("view.mixer"), s)->pressed);
    s.mixerVisible = true;
    CHECK (buttonView (cmd ("view.mixer"), s)->pressed);
    CHECK_FALSE (buttonView (cmd ("file.save"), s)->pressed);
}

TEST_CASE ("StripButtons: an unknown command key has no view and no command id", "[dock][strip-buttons]")
{
    const lotro::MenuState s;
    CHECK_FALSE (buttonView (cmd ("future.thing"), s).has_value());
    CHECK_FALSE (commandIdFor (cmd ("future.thing"), s).has_value());
}

TEST_CASE ("StripButtons: a command button resolves to its menu command id", "[dock][strip-buttons]")
{
    const lotro::MenuState s;
    CHECK (commandIdFor (cmd ("file.exportMidi"), s) == lotro::FileExportMidi);
    CHECK (commandIdFor (cmd ("view.layoutManage"), s) == lotro::ViewLayoutManage);
}

TEST_CASE ("StripButtons: a layout button resolves by name to the layout's apply id", "[dock][strip-buttons]")
{
    lotro::MenuState s;
    s.layoutNames = { "Wide", "Tall" };
    CHECK (commandIdFor (layout ("Tall"), s) == lotro::ViewLayoutApplyBase + 1);
    CHECK (commandIdFor (layout ("wide"), s) == lotro::ViewLayoutApplyBase);
    const auto v = buttonView (layout ("Tall"), s);
    REQUIRE (v.has_value());
    CHECK (v->label == "Tall");
    CHECK (v->enabled);
}

TEST_CASE ("StripButtons: a layout button whose layout is gone is shown disabled and runs nothing", "[dock][strip-buttons]")
{
    lotro::MenuState s;
    s.layoutNames = { "Wide" };
    const auto v = buttonView (layout ("Deleted"), s);
    REQUIRE (v.has_value());
    CHECK (v->label == "Deleted");
    CHECK_FALSE (v->enabled);
    CHECK_FALSE (commandIdFor (layout ("Deleted"), s).has_value());
}
```

- [ ] **Step 2: Run to verify failure**

Run: `cmake --build build --target forge_tests 2>&1 | grep -m3 "error"`
Expected: FAIL — `buttonView` / `commandIdFor` not declared.

- [ ] **Step 3: Implement**

In `StripButtons.h` add `#include "UI/MenuModel.h"`, `#include <optional>`, and before the closing namespace:

```cpp
// What a button should look like right now, derived from the same MenuState the menus use.
struct ButtonView
{
    std::string label;
    bool enabled = true;
    bool pressed = false;   // a panel toggle whose panel is visible
};

// nullopt = do not show the button (a command key this build does not know).
std::optional<ButtonView> buttonView (const StripButton&, const lotro::MenuState&);

// The MenuCommandId clicking the button should dispatch; nullopt if the key is unknown or the
// layout no longer exists.
std::optional<int> commandIdFor (const StripButton&, const lotro::MenuState&);
```

In `StripButtons.cpp` add, inside `lotro::dock` before the JSON functions:

```cpp
namespace
{
std::optional<int> layoutIndex (const StripButton& b, const lotro::MenuState& s)
{
    for (std::size_t i = 0; i < s.layoutNames.size() && (int) i < lotro::maxSavedLayouts; ++i)
        if (sameName (s.layoutNames[i], b.key))
            return (int) i;
    return std::nullopt;
}
}

std::optional<ButtonView> buttonView (const StripButton& b, const lotro::MenuState& s)
{
    if (b.kind == StripButton::Kind::applyLayout)
        return ButtonView { b.key, layoutIndex (b, s).has_value(), false };
    const auto* command = lotro::findStripCommand (b.key);
    if (command == nullptr)
        return std::nullopt;
    ButtonView view { command->label, true, false };
    if (const auto* item = lotro::findMenuItem (lotro::buildMenus (s), command->id))
    {
        view.enabled = item->enabled;
        view.pressed = item->ticked;
    }
    return view;
}

std::optional<int> commandIdFor (const StripButton& b, const lotro::MenuState& s)
{
    if (b.kind == StripButton::Kind::applyLayout)
    {
        const auto index = layoutIndex (b, s);
        return index ? std::optional<int> (lotro::ViewLayoutApplyBase + *index) : std::nullopt;
    }
    const auto* command = lotro::findStripCommand (b.key);
    return command != nullptr ? std::optional<int> ((int) command->id) : std::nullopt;
}
```

Add `#include <optional>` to the `.cpp` if not pulled in.

- [ ] **Step 4: Run to verify they pass**

Run: `cmake --build build --target forge_tests 2>&1 | tail -3 && ctest --test-dir build -R "StripButtons" --output-on-failure`
Expected: all StripButtons tests PASS.

- [ ] **Step 5: Commit**

```bash
git add Source/UI/Dock/StripButtons.h Source/UI/Dock/StripButtons.cpp Tests/StripButtons_tests.cpp
git commit -m "feat(ui): strip button view and command resolution from the menu state

Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>"
```

---

### Task 4: `AppSettings` storage

**Files:**
- Modify: `Source/UI/AppSettings.h` (next to `savedLayouts()`; add `keyStripButtons`)
- Modify: `Tests/AppSettings_tests.cpp` (append)

**Interfaces:**
- Produces: `juce::String AppSettings::stripButtons() const` (empty when never saved) and `bool AppSettings::setStripButtons (const juce::String& json)` (same save/`lastSaveFailed` behaviour as `setSavedLayouts`).

- [ ] **Step 1: Write the failing test**

Append to `Tests/AppSettings_tests.cpp`:

```cpp
TEST_CASE ("AppSettings: strip buttons default to empty and persist across a re-opened file", "[app-settings]")
{
    auto file = juce::File::createTempFile (".settings");
    const juce::ScopeGuard cleanup { [&] { file.deleteFile(); } };
    {
        juce::PropertiesFile props (file, {});
        AppSettings settings (props);
        CHECK (settings.stripButtons().isEmpty());
        CHECK (settings.setStripButtons ("[1]"));
        CHECK (settings.stripButtons() == "[1]");
        CHECK (settings.savedLayouts().isEmpty());   // independent of the layouts
    }
    juce::PropertiesFile reloaded (file, {});
    CHECK (AppSettings (reloaded).stripButtons() == "[1]");
}
```

- [ ] **Step 2: Run to verify failure**

Run: `cmake --build build --target forge_tests 2>&1 | grep -m2 "error"`
Expected: FAIL — `stripButtons` is not a member of `AppSettings`.

- [ ] **Step 3: Implement**

In `AppSettings.h`, after `setSavedLayouts`:

```cpp
    // The transport strip's chosen buttons as JSON (see Dock/StripButtons.h). Empty when none were saved.
    juce::String stripButtons() const { return file.getValue (keyStripButtons); }
    bool setStripButtons (const juce::String& json)
    {
        file.setValue (keyStripButtons, json);
        return save();
    }
```

and in the private key list: `static constexpr const char* keyStripButtons = "strip.buttons";`

- [ ] **Step 4: Run to verify it passes**

Run: `cmake --build build --target forge_tests 2>&1 | tail -3 && ctest --test-dir build -R "AppSettings" --output-on-failure`
Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add Source/UI/AppSettings.h Tests/AppSettings_tests.cpp
git commit -m "feat(ui): AppSettings stores the transport strip's buttons

Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>"
```

---

### Task 5: Manage-layouts dialog reports renames and deletions

**Files:**
- Modify: `Source/UI/Dock/ManageLayoutsDialog.h`, `Source/UI/Dock/ManageLayoutsDialog.cpp`
- Modify: `Tests/ManageLayoutsDialog_tests.cpp` (append)

**Interfaces:**
- Produces on `ManageLayoutsPanel`: `std::function<void (const std::string& oldName, const std::string& newName)> onRenamed;` and `std::function<void (const std::string& name)> onRemoved;`. Each fires after the list is edited and **before** `onChanged`, so an owner that updates strip buttons in these callbacks gets them persisted by the `onChanged` that follows.
- `showManageLayoutsDialog` gains two parameters: `std::function<void (const std::string&, const std::string&)> onRenamed` and `std::function<void (const std::string&)> onRemoved`, placed after `onChanged`, before `centreAround`.

- [ ] **Step 1: Write the failing tests**

Append to `Tests/ManageLayoutsDialog_tests.cpp`:

```cpp
TEST_CASE ("ManageLayoutsPanel: a rename reports the old and new names before onChanged", "[dock][layouts]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    auto list = three();
    ManageLayoutsPanel panel (list);
    std::vector<std::string> events;
    panel.onRenamed = [&] (const std::string& from, const std::string& to) { events.push_back ("rename " + from + ">" + to); };
    panel.onChanged = [&] { events.push_back ("changed"); };

    panel.selectRow (1);
    REQUIRE_FALSE (panel.renameSelected ("Mixing"));
    CHECK (events == std::vector<std::string> { "rename Layout 2>Mixing", "changed" });

    events.clear();
    CHECK (panel.renameSelected ("Layout 3") == LayoutNameError::duplicate);   // refused: nothing reported
    CHECK (events.empty());
}

TEST_CASE ("ManageLayoutsPanel: a delete reports the removed name before onChanged", "[dock][layouts]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    auto list = three();
    ManageLayoutsPanel panel (list);
    std::vector<std::string> events;
    panel.onRemoved = [&] (const std::string& name) { events.push_back ("removed " + name); };
    panel.onChanged = [&] { events.push_back ("changed"); };

    panel.selectRow (0);
    panel.deleteSelected();
    CHECK (events == std::vector<std::string> { "removed Layout 1", "changed" });
}
```

- [ ] **Step 2: Run to verify failure**

Run: `cmake --build build --target forge_tests 2>&1 | grep -m2 "error"`
Expected: FAIL — `onRenamed` / `onRemoved` are not members.

- [ ] **Step 3: Implement**

`ManageLayoutsDialog.h`: after `std::function<void()> onChanged;` add

```cpp
    // Fired (before onChanged) with what the edit was, so the owner can keep things that refer to a layout by name in step.
    std::function<void (const std::string& oldName, const std::string& newName)> onRenamed;
    std::function<void (const std::string& name)> onRemoved;
```

and change `showManageLayoutsDialog` to

```cpp
juce::Component::SafePointer<juce::DialogWindow> showManageLayoutsDialog (SavedLayouts& list,
                                                                          std::function<void()> onChanged,
                                                                          std::function<void (const std::string&, const std::string&)> onRenamed,
                                                                          std::function<void (const std::string&)> onRemoved,
                                                                          juce::Component* centreAround);
```

`ManageLayoutsDialog.cpp`, in `renameSelected` replace the tail with:

```cpp
    const auto oldName = list[(size_t) row].name;
    if (const auto error = renameLayout (list, (std::size_t) row, newName))
        return error;
    listBox.repaint();
    if (onRenamed) onRenamed (oldName, list[(size_t) row].name);
    if (onChanged) onChanged();
    return std::nullopt;
```

in `deleteSelected` capture the name first and report it:

```cpp
    const auto name = list[(size_t) row].name;
    removeLayout (list, (std::size_t) row);
    listBox.updateContent();
    listBox.selectRow (juce::jmin (row, rowCount() - 1));   // -1 when the list is now empty
    refreshButtons();
    listBox.repaint();
    if (onRemoved) onRemoved (name);
    if (onChanged) onChanged();
```

and in `showManageLayoutsDialog` update the signature and set `content->onRenamed = std::move (onRenamed); content->onRemoved = std::move (onRemoved);` next to `onChanged`.

Then fix the one existing call site so the build stays green: in `Source/UI/MainWindow.cpp` `manageLayouts()` temporarily pass `{}, {}`:

```cpp
    manageLayoutsWindow = dock::showManageLayoutsDialog (savedLayouts, [this] { persistSavedLayouts(); }, {}, {}, this);
```

(Task 8 replaces the `{}, {}`.)

- [ ] **Step 4: Run to verify they pass**

Run: `cmake --build build 2>&1 | tail -3 && ctest --test-dir build -R "ManageLayouts" --output-on-failure`
Expected: all ManageLayoutsPanel tests PASS and the whole project still builds.

- [ ] **Step 5: Commit**

```bash
git add Source/UI/Dock/ManageLayoutsDialog.h Source/UI/Dock/ManageLayoutsDialog.cpp Tests/ManageLayoutsDialog_tests.cpp Source/UI/MainWindow.cpp
git commit -m "feat(ui): the Manage Layouts dialog reports which layout was renamed or deleted

Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>"
```

---

### Task 6: `StripButtonsHost` and `StripButtonsBar`

**Files:**
- Create: `Source/UI/Playback/StripButtonsBar.h`, `Source/UI/Playback/StripButtonsBar.cpp`
- Create: `Tests/StripButtonsBar_tests.cpp`
- Modify: `CMakeLists.txt` (after `Source/UI/Playback/TransportControls.cpp`), `Tests/CMakeLists.txt` (source list after the `TransportControls.cpp` line ~134; test list after `TransportControls_tests.cpp` ~line 60)

**Interfaces:**
- Consumes: Task 1 `StripButtons`/`addButton`/`removeButton`/`contains`; Task 2 `stripCommands`; Task 3 `ButtonView`.
- Produces (namespace `lotro`):
  - `class StripButtonsHost : public juce::ChangeBroadcaster` — the state shared by every bar:
    - `dock::StripButtons buttons;`
    - `std::function<lotro::MenuState()> menuState;` (current state; must be set)
    - `std::function<void()> persist;` (called after the user adds/removes; may be empty)
    - `std::function<void (int commandId)> run;` (dispatch a command; may be empty)
    - `void changed() { sendSynchronousChangeMessage(); }`
  - `int visibleButtonCount (const std::vector<int>& widths, int available, int gap, int overflowWidth)` — how many leading buttons fit; if not all fit, reserves `overflowWidth` (+gap) for the `»` button.
  - `class StripButtonsBar : public juce::Component, private juce::ChangeListener, private juce::Timer`
    - `explicit StripButtonsBar (StripButtonsHost&)`
    - `void refresh()` — rebuild the visible buttons from the host.
    - test hooks: `int visibleButtonsForTesting() const`, `bool hasOverflowForTesting() const`, `juce::PopupMenu pickerMenuForTesting() const`, `void clickButtonForTesting (int index)`, `void removeButtonForTesting (int index)`, `juce::Button& plusButtonForTesting()`, `juce::TextButton& buttonForTesting (int index)`.

- [ ] **Step 1: Write the failing tests**

Create `Tests/StripButtonsBar_tests.cpp`:

```cpp
#include "UI/Playback/StripButtonsBar.h"

#include <catch2/catch_test_macros.hpp>

using namespace lotro;
using namespace lotro::dock;

namespace
{
StripButton cmd (const char* key) { return { StripButton::Kind::command, key }; }
StripButton layout (const char* name) { return { StripButton::Kind::applyLayout, name }; }

struct Rig
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    StripButtonsHost host;
    std::vector<int> ran;
    int persisted = 0;
    MenuState state;

    Rig()
    {
        state.layoutNames = { "Wide" };
        host.menuState = [this] { return state; };
        host.persist = [this] { ++persisted; };
        host.run = [this] (int id) { ran.push_back (id); };
    }
};
}

TEST_CASE ("visibleButtonCount: everything fits, or the tail moves behind the overflow button", "[strip-buttons][bar]")
{
    CHECK (visibleButtonCount ({ 50, 50, 50 }, 400, 4, 20) == 3);
    CHECK (visibleButtonCount ({ 50, 50, 50 }, 158, 4, 20) == 3);   // 50+4+50+4+50 = 158 exactly fits
    CHECK (visibleButtonCount ({ 50, 50, 50 }, 157, 4, 20) == 2);   // 50+4+50 + gap + overflow(20) = 128 <= 157
    CHECK (visibleButtonCount ({ 50, 50, 50 }, 60, 4, 20) == 0);    // not even one plus the overflow button
    CHECK (visibleButtonCount ({}, 100, 4, 20) == 0);
    CHECK (visibleButtonCount ({ 50 }, 0, 4, 20) == 0);
}

TEST_CASE ("StripButtonsBar: shows one button per host entry and hides unknown command keys", "[strip-buttons][bar]")
{
    Rig r;
    r.host.buttons = { cmd ("file.save"), cmd ("future.thing"), layout ("Wide") };
    StripButtonsBar bar (r.host);
    bar.setSize (600, 28);
    CHECK (bar.visibleButtonsForTesting() == 2);
    CHECK_FALSE (bar.hasOverflowForTesting());
}

TEST_CASE ("StripButtonsBar: clicking a command button runs its menu command id", "[strip-buttons][bar]")
{
    Rig r;
    r.state.hasAbc = true;
    r.host.buttons = { cmd ("file.exportAbc") };
    StripButtonsBar bar (r.host);
    bar.setSize (600, 28);
    bar.clickButtonForTesting (0);
    REQUIRE (r.ran.size() == 1);
    CHECK (r.ran[0] == FileExportAbc);
}

TEST_CASE ("StripButtonsBar: a layout button runs the layout's apply id", "[strip-buttons][bar]")
{
    Rig r;
    r.host.buttons = { layout ("Wide") };
    StripButtonsBar bar (r.host);
    bar.setSize (600, 28);
    bar.clickButtonForTesting (0);
    REQUIRE (r.ran.size() == 1);
    CHECK (r.ran[0] == ViewLayoutApplyBase);
}

TEST_CASE ("StripButtonsBar: a button is disabled while its command is disabled", "[strip-buttons][bar]")
{
    Rig r;
    r.host.buttons = { cmd ("edit.undo") };
    StripButtonsBar bar (r.host);
    bar.setSize (600, 28);
    CHECK_FALSE (bar.buttonForTesting (0).isEnabled());
    r.state.canUndo = true;
    bar.refresh();
    CHECK (bar.buttonForTesting (0).isEnabled());
}

TEST_CASE ("StripButtonsBar: a panel-toggle button shows as on while its panel is visible", "[strip-buttons][bar]")
{
    Rig r;
    r.host.buttons = { cmd ("view.mixer") };
    StripButtonsBar bar (r.host);
    bar.setSize (600, 28);
    CHECK_FALSE (bar.buttonForTesting (0).getToggleState());
    r.state.mixerVisible = true;
    bar.refresh();
    CHECK (bar.buttonForTesting (0).getToggleState());
}

TEST_CASE ("StripButtonsBar: the picker lists every group, greys out what is on the strip and layouts apply by name", "[strip-buttons][bar]")
{
    Rig r;
    r.host.buttons = { cmd ("file.save") };
    StripButtonsBar bar (r.host);
    bar.setSize (600, 28);
    const auto menu = bar.pickerMenuForTesting();

    juce::StringArray tops;
    juce::PopupMenu::MenuItemIterator it (menu);
    while (it.next())
        if (it.getItem().subMenu != nullptr)
            tops.add (it.getItem().text);
    CHECK (tops == juce::StringArray { "File", "Edit", "Song", "Transport", "View", "Apply Layout" });

    // "Save" is already on the strip: present but disabled.
    bool foundSave = false;
    juce::PopupMenu::MenuItemIterator all (menu, true);
    while (all.next())
        if (all.getItem().text == "Save")
        {
            foundSave = true;
            CHECK_FALSE (all.getItem().isEnabled);
        }
    CHECK (foundSave);
}

TEST_CASE ("StripButtonsBar: removing a button updates the host, persists and notifies the other bars", "[strip-buttons][bar]")
{
    Rig r;
    r.host.buttons = { cmd ("file.save"), cmd ("file.new") };
    StripButtonsBar a (r.host), b (r.host);
    a.setSize (600, 28);
    b.setSize (600, 28);
    a.removeButtonForTesting (0);
    REQUIRE (r.host.buttons.size() == 1);
    CHECK (r.host.buttons[0].key == "file.new");
    CHECK (r.persisted == 1);
    CHECK (b.visibleButtonsForTesting() == 1);   // the other bar followed
}

TEST_CASE ("StripButtonsBar: a narrow bar moves trailing buttons behind the overflow button", "[strip-buttons][bar]")
{
    Rig r;
    r.host.buttons = { cmd ("file.new"), cmd ("file.open"), cmd ("file.save"), cmd ("file.saveAs"), cmd ("file.exportMidi") };
    StripButtonsBar bar (r.host);
    bar.setSize (600, 28);
    CHECK_FALSE (bar.hasOverflowForTesting());
    bar.setSize (150, 28);
    CHECK (bar.hasOverflowForTesting());
    CHECK (bar.visibleButtonsForTesting() < 5);
    bar.setSize (10, 28);                         // narrower than anything: must not crash
    CHECK (bar.visibleButtonsForTesting() == 0);
}

TEST_CASE ("StripButtonsBar: the picker is fully disabled once ten buttons are on the strip", "[strip-buttons][bar]")
{
    Rig r;
    for (const auto* k : { "file.new", "file.open", "file.save", "file.saveAs", "file.importMidi",
                           "file.exportMidi", "file.exportAbc", "file.preferences", "edit.undo", "edit.redo" })
        r.host.buttons.push_back (cmd (k));
    StripButtonsBar bar (r.host);
    bar.setSize (600, 28);
    juce::PopupMenu::MenuItemIterator all (bar.pickerMenuForTesting(), true);
    while (all.next())
        if (all.getItem().itemID != 0)
            CHECK_FALSE (all.getItem().isEnabled);
}
```

- [ ] **Step 2: Register files; run to verify failure**

Add the CMake lines (see Files), then:
Run: `cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug && cmake --build build --target forge_tests 2>&1 | grep -m2 "error"`
Expected: FAIL — `StripButtonsBar.h: No such file or directory`.

- [ ] **Step 3: Write the header**

`Source/UI/Playback/StripButtonsBar.h`:

```cpp
#pragma once

#include "UI/Dock/StripButtons.h"
#include "UI/MenuModel.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>
#include <vector>

namespace lotro
{

// The state every strip-buttons bar shares (the main window's strip and the MIDI editor's), plus
// the three things a bar needs from the app. Owned by MainWindow, which outlives the bars.
// changed() tells every bar to rebuild.
class StripButtonsHost : public juce::ChangeBroadcaster
{
public:
    dock::StripButtons buttons;
    std::function<MenuState()> menuState;          // the live state the menus use; must be set
    std::function<void()> persist;                 // after the user adds or removes a button
    std::function<void (int commandId)> run;       // dispatch a MenuCommandId

    void changed() { sendSynchronousChangeMessage(); }
};

// How many leading buttons of the given widths fit in `available`. If they do not all fit, room
// for the overflow ("»") button is reserved and the count is what fits beside it.
int visibleButtonCount (const std::vector<int>& widths, int available, int gap, int overflowWidth);

// "+", then the user's buttons, then "»" when some do not fit. Right-click a button to remove it.
class StripButtonsBar : public juce::Component, private juce::ChangeListener, private juce::Timer
{
public:
    explicit StripButtonsBar (StripButtonsHost&);
    ~StripButtonsBar() override;

    void refresh();    // re-read every button's label / enabled / pressed state and re-lay out
    void resized() override;

    int  visibleButtonsForTesting() const { return (int) buttons.size(); }
    bool hasOverflowForTesting() const { return overflowShown; }
    juce::PopupMenu pickerMenuForTesting() const { return buildPicker(); }
    void clickButtonForTesting (int index);
    void removeButtonForTesting (int index);
    juce::Button& plusButtonForTesting() { return plusButton; }
    juce::TextButton& buttonForTesting (int index) { return *buttons[(std::size_t) index].button; }

private:
    struct Entry
    {
        dock::StripButton data;
        std::unique_ptr<juce::TextButton> button;
    };

    void mouseDown (const juce::MouseEvent&) override;   // right-click on a button: remove menu
    void changeListenerCallback (juce::ChangeBroadcaster*) override { refresh(); }
    void timerCallback() override { refresh(); }

    juce::PopupMenu buildPicker() const;
    void showPicker();
    void showOverflow();
    void showRemoveMenu (const dock::StripButton&);
    void activate (const dock::StripButton&);
    void remove (const dock::StripButton&);

    StripButtonsHost& host;
    juce::TextButton plusButton { "+" };
    juce::TextButton overflowButton { juce::String::fromUTF8 ("\xc2\xbb") };
    std::vector<Entry> buttons;                    // the ones shown, in order
    std::vector<dock::StripButton> hiddenOverflow; // the ones behind "»"
    bool overflowShown = false;
};

} // namespace lotro
```

- [ ] **Step 4: Write the implementation**

`Source/UI/Playback/StripButtonsBar.cpp`:

```cpp
#include "UI/Playback/StripButtonsBar.h"

#include "UI/SongsmithColours.h"

namespace lotro
{
namespace
{
constexpr int gap = 4;
constexpr int plusWidth = 24;
constexpr int overflowWidth = 24;
constexpr int textPadding = 18;
constexpr int pickerBase = 1;             // menu item ids: pickerBase + index into stripCommands
constexpr int layoutPickerBase = 1000;    // layoutPickerBase + layout index

juce::Font buttonFont() { return juce::Font (juce::FontOptions (13.0f)); }

int widthFor (const juce::String& label)
{
    return juce::GlyphArrangement::getStringWidthInt (buttonFont(), label) + textPadding;
}
}

int visibleButtonCount (const std::vector<int>& widths, int available, int gapIn, int overflowWidthIn)
{
    int total = 0;
    for (std::size_t i = 0; i < widths.size(); ++i)
        total += widths[i] + (i > 0 ? gapIn : 0);
    if (total <= available)
        return (int) widths.size();

    const int room = available - overflowWidthIn - gapIn;
    int used = 0, count = 0;
    for (const int w : widths)
    {
        const int next = used + (count > 0 ? gapIn : 0) + w;
        if (next > room)
            break;
        used = next;
        ++count;
    }
    return count;
}

StripButtonsBar::StripButtonsBar (StripButtonsHost& hostIn) : host (hostIn)
{
    for (auto* b : { &plusButton, &overflowButton })
    {
        b->setWantsKeyboardFocus (false);
        addAndMakeVisible (*b);
    }
    plusButton.setTooltip ("Add a command button");
    overflowButton.setTooltip ("More buttons");
    plusButton.onClick = [this] { showPicker(); };
    overflowButton.onClick = [this] { showOverflow(); };
    host.addChangeListener (this);
    startTimerHz (5);   // enabled/pressed state changes (Undo, a panel opening) have no signal of their own
    refresh();
}

StripButtonsBar::~StripButtonsBar()
{
    host.removeChangeListener (this);
}

void StripButtonsBar::refresh()
{
    const MenuState state = host.menuState ? host.menuState() : MenuState {};

    struct Shown { dock::StripButton data; dock::ButtonView view; int width; };
    std::vector<Shown> all;
    for (const auto& b : host.buttons)
        if (const auto view = dock::buttonView (b, state))
            all.push_back ({ b, *view, widthFor (juce::String (view->label)) });

    std::vector<int> widths;
    for (const auto& s : all) widths.push_back (s.width);
    const int available = juce::jmax (0, getWidth() - plusWidth - gap);
    const int shown = visibleButtonCount (widths, available, gap, overflowWidth);

    // Rebuild only when the set of shown buttons changed; otherwise just update state in place.
    bool sameSet = (int) buttons.size() == shown;
    for (int i = 0; sameSet && i < shown; ++i)
        sameSet = dock::sameButton (buttons[(std::size_t) i].data, all[(std::size_t) i].data);
    if (! sameSet)
    {
        buttons.clear();
        for (int i = 0; i < shown; ++i)
        {
            Entry e { all[(std::size_t) i].data, std::make_unique<juce::TextButton>() };
            e.button->setWantsKeyboardFocus (false);
            e.button->setClickingTogglesState (false);
            const auto data = e.data;
            e.button->onClick = [this, data] { activate (data); };
            e.button->addMouseListener (this, false);   // right-click arrives in mouseDown below
            addAndMakeVisible (*e.button);
            buttons.push_back (std::move (e));
        }
    }
    hiddenOverflow.clear();
    for (std::size_t i = (std::size_t) shown; i < all.size(); ++i)
        hiddenOverflow.push_back (all[i].data);
    overflowShown = ! hiddenOverflow.empty();

    for (int i = 0; i < shown; ++i)
    {
        auto& btn = *buttons[(std::size_t) i].button;
        const auto& v = all[(std::size_t) i].view;
        btn.setButtonText (juce::String (v.label));
        btn.setEnabled (v.enabled);
        btn.setToggleState (v.pressed, juce::dontSendNotification);
        btn.setColour (juce::TextButton::buttonOnColourId, juce::Colour (SongsmithColours::accentAmber));
    }
    overflowButton.setVisible (overflowShown);
    resized();
}

void StripButtonsBar::resized()
{
    auto area = getLocalBounds().reduced (0, 3);
    plusButton.setBounds (area.removeFromLeft (plusWidth));
    area.removeFromLeft (gap);
    for (auto& e : buttons)
    {
        e.button->setBounds (area.removeFromLeft (widthFor (e.button->getButtonText())));
        area.removeFromLeft (gap);
    }
    if (overflowShown)
        overflowButton.setBounds (area.removeFromLeft (overflowWidth));
}

juce::PopupMenu StripButtonsBar::buildPicker() const
{
    const bool full = host.buttons.size() >= dock::maxStripButtons;
    juce::PopupMenu root;
    const char* group = nullptr;
    juce::PopupMenu sub;
    auto flush = [&]
    {
        if (group != nullptr)
            root.addSubMenu (group, sub);
        sub = juce::PopupMenu();
    };
    for (std::size_t i = 0; i < stripCommands.size(); ++i)
    {
        const auto& c = stripCommands[i];
        if (group == nullptr || std::string (group) != c.group)
        {
            flush();
            group = c.group;
        }
        const bool present = dock::contains (host.buttons, { dock::StripButton::Kind::command, c.key });
        sub.addItem (pickerBase + (int) i, c.label, ! present && ! full);
    }
    flush();

    juce::PopupMenu layouts;
    const auto state = host.menuState ? host.menuState() : MenuState {};
    for (std::size_t i = 0; i < state.layoutNames.size() && (int) i < maxSavedLayouts; ++i)
    {
        const bool present = dock::contains (host.buttons, { dock::StripButton::Kind::applyLayout, state.layoutNames[i] });
        layouts.addItem (layoutPickerBase + (int) i, juce::String (state.layoutNames[i]), ! present && ! full);
    }
    root.addSubMenu ("Apply Layout", layouts, ! state.layoutNames.empty());
    return root;
}

void StripButtonsBar::showPicker()
{
    juce::Component::SafePointer<StripButtonsBar> safe (this);
    buildPicker().showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&plusButton),
        [safe] (int id)
        {
            if (safe == nullptr || id == 0)
                return;
            dock::StripButton b;
            if (id >= layoutPickerBase)
            {
                const auto names = safe->host.menuState().layoutNames;
                const auto index = (std::size_t) (id - layoutPickerBase);
                if (index >= names.size())
                    return;
                b = { dock::StripButton::Kind::applyLayout, names[index] };
            }
            else
            {
                const auto index = (std::size_t) (id - pickerBase);
                if (index >= stripCommands.size())
                    return;
                b = { dock::StripButton::Kind::command, stripCommands[index].key };
            }
            if (dock::addButton (safe->host.buttons, std::move (b)))
            {
                if (safe->host.persist) safe->host.persist();
                safe->host.changed();
            }
        });
}

void StripButtonsBar::showOverflow()
{
    juce::PopupMenu menu;
    const auto state = host.menuState ? host.menuState() : MenuState {};
    for (std::size_t i = 0; i < hiddenOverflow.size(); ++i)
        if (const auto view = dock::buttonView (hiddenOverflow[i], state))
            menu.addItem ((int) i + 1, juce::String (view->label), view->enabled, view->pressed);
    juce::Component::SafePointer<StripButtonsBar> safe (this);
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&overflowButton),
        [safe] (int id)
        {
            if (safe != nullptr && id > 0 && (std::size_t) id <= safe->hiddenOverflow.size())
                safe->activate (safe->hiddenOverflow[(std::size_t) id - 1]);
        });
}

void StripButtonsBar::showRemoveMenu (const dock::StripButton& b)
{
    juce::PopupMenu menu;
    menu.addItem (1, "Remove from strip");
    juce::Component::SafePointer<StripButtonsBar> safe (this);
    menu.showMenuAsync ({}, [safe, b] (int id)
    {
        if (safe != nullptr && id == 1)
            safe->remove (b);
    });
}

void StripButtonsBar::activate (const dock::StripButton& b)
{
    if (! host.run || ! host.menuState)
        return;
    if (const auto id = dock::commandIdFor (b, host.menuState()))
        host.run (*id);
}

void StripButtonsBar::remove (const dock::StripButton& b)
{
    for (std::size_t i = 0; i < host.buttons.size(); ++i)
        if (dock::sameButton (host.buttons[i], b))
        {
            dock::removeButton (host.buttons, i);
            if (host.persist) host.persist();
            host.changed();
            return;
        }
}

void StripButtonsBar::clickButtonForTesting (int index)
{
    activate (buttons[(std::size_t) index].data);
}

void StripButtonsBar::removeButtonForTesting (int index)
{
    remove (buttons[(std::size_t) index].data);
}

} // namespace lotro
```

Right-click wiring (declared in the header above):

```cpp
void StripButtonsBar::mouseDown (const juce::MouseEvent& e)
{
    if (! e.mods.isPopupMenu())
        return;
    for (auto& entry : buttons)
        if (e.eventComponent == entry.button.get())
        {
            showRemoveMenu (entry.data);
            return;
        }
}
```

(A disabled `TextButton` still delivers `mouseDown` to its mouse listeners, so a disabled button can be removed.)

- [ ] **Step 5: Run to verify they pass**

Run: `cmake --build build --target forge_tests 2>&1 | tail -5 && ctest --test-dir build -R "StripButtonsBar|visibleButtonCount" --output-on-failure`
Expected: all 10 tests PASS. If a `visibleButtonCount` assertion is off by the gap arithmetic, fix the *test's expected number* only after checking it against the stated rule (everything fits → all; else leading buttons that fit in `available - overflowWidth - gap`).

- [ ] **Step 6: Commit**

```bash
git add Source/UI/Playback/StripButtonsBar.h Source/UI/Playback/StripButtonsBar.cpp Tests/StripButtonsBar_tests.cpp CMakeLists.txt Tests/CMakeLists.txt
git commit -m "feat(ui): StripButtonsBar with a + picker, overflow menu and right-click remove

Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>"
```

---

### Task 7: Put the bar in both strips

**Files:**
- Modify: `Source/UI/Playback/TransportStrip.h`, `Source/UI/Playback/TransportStrip.cpp`
- Modify: `Source/UI/TrackEditorPanel.h`, `Source/UI/TrackEditorPanel.cpp`
- Modify: `Source/UI/SongsmithMainComponent.h`, `Source/UI/SongsmithMainComponent.cpp`
- Modify: `Tests/TransportStrip_tests.cpp` (append)

**Interfaces:**
- Consumes: Task 6 `StripButtonsHost`, `StripButtonsBar`.
- Produces:
  - `void TransportStrip::setButtonsHost (StripButtonsHost*)` — creates (non-null) or destroys (null) the bar inside `leftArea`, filling it.
  - `StripButtonsBar* TransportStrip::buttonsBarForTesting()` — nullptr when none.
  - `void TrackEditorPanel::setStripButtonsHost (StripButtonsHost*)` — remembers the host and applies it to the panel's strip now (if the strip exists) and when `setPlayback` later creates it.
  - `void SongsmithMainComponent::setStripButtonsHost (StripButtonsHost*)` — applies it to its own strip (if any) and to `trackEditor`.

- [ ] **Step 1: Write the failing test**

Append to `Tests/TransportStrip_tests.cpp` (add `#include "UI/Playback/StripButtonsBar.h"`):

```cpp
TEST_CASE ("TransportStrip: a buttons host puts the + bar in the left area and removing it takes it away", "[playback][strip][strip-buttons]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SongDocument doc;
    RecordingSink sink;
    PlaybackController controller (doc, sink);
    TransportStrip strip (controller, doc);
    strip.setSize (900, TransportStrip::height);
    CHECK (strip.buttonsBarForTesting() == nullptr);

    StripButtonsHost host;
    host.menuState = [] { return MenuState {}; };
    strip.setButtonsHost (&host);
    REQUIRE (strip.buttonsBarForTesting() != nullptr);
    CHECK (strip.buttonsBarForTesting()->getParentComponent() == &strip.leftAreaForTesting());
    CHECK (strip.buttonsBarForTesting()->getWidth() == strip.leftAreaForTesting().getWidth());
    CHECK (strip.buttonsBarForTesting()->plusButtonForTesting().isVisible());

    strip.dragDividerForTesting (0, 200);   // the bar follows the left area's width
    CHECK (strip.buttonsBarForTesting()->getWidth() == strip.leftAreaForTesting().getWidth());

    strip.setButtonsHost (nullptr);
    CHECK (strip.buttonsBarForTesting() == nullptr);
}
```

- [ ] **Step 2: Run to verify failure**

Run: `cmake --build build --target forge_tests 2>&1 | grep -m2 "error"`
Expected: FAIL — `setButtonsHost` is not a member of `TransportStrip`.

- [ ] **Step 3: Implement `TransportStrip`**

`TransportStrip.h`: add `#include "UI/Playback/StripButtonsBar.h"` and `<memory>`; update the class comment ("the left one hosts the user's command buttons"); declare

```cpp
    // The user's command buttons in the left area. Not owned: the host must outlive the strip.
    // nullptr removes them.
    void setButtonsHost (StripButtonsHost* host);
    StripButtonsBar* buttonsBarForTesting() { return buttonsBar.get(); }
```

and a member `std::unique_ptr<StripButtonsBar> buttonsBar;` declared **after** `controls` and **before** `splitter` is not needed — place it right after `controls` (destroyed first, before the areas it lives in).

`TransportStrip.cpp`: in the constructor remove the placeholder comment and add

```cpp
    leftArea.onBoundsChanged = [this] { if (buttonsBar != nullptr) buttonsBar->setBounds (leftArea.getLocalBounds()); };
```

and define

```cpp
void TransportStrip::setButtonsHost (StripButtonsHost* host)
{
    buttonsBar.reset();
    if (host == nullptr)
        return;
    buttonsBar = std::make_unique<StripButtonsBar> (*host);
    leftArea.addAndMakeVisible (*buttonsBar);
    buttonsBar->setBounds (leftArea.getLocalBounds());
}
```

- [ ] **Step 4: Implement the two forwarders**

`TrackEditorPanel.h`: add `void setStripButtonsHost (StripButtonsHost* host);` (forward-declare `class StripButtonsHost;` or include the bar header) and member `StripButtonsHost* stripButtonsHost = nullptr;`.
`TrackEditorPanel.cpp`:

```cpp
    void TrackEditorPanel::setStripButtonsHost (StripButtonsHost* host)
    {
        stripButtonsHost = host;
        if (content != nullptr)
            content->strip.setButtonsHost (host);
    }
```

and in `setPlayback`, right after `content = std::make_unique<Content> (...)`: `content->strip.setButtonsHost (stripButtonsHost);`.

`SongsmithMainComponent.h/.cpp`:

```cpp
void SongsmithMainComponent::setStripButtonsHost (StripButtonsHost* host)
{
    if (transportStrip != nullptr)
        transportStrip->setButtonsHost (host);
    trackEditor->setStripButtonsHost (host);
}
```

- [ ] **Step 5: Run to verify they pass**

Run: `cmake --build build 2>&1 | tail -3 && ctest --test-dir build -R "TransportStrip|TrackEditorPanel|SongsmithMainComponent" --output-on-failure`
Expected: PASS, including every pre-existing strip/panel/main-component test.

- [ ] **Step 6: Commit**

```bash
git add Source/UI/Playback/TransportStrip.h Source/UI/Playback/TransportStrip.cpp Source/UI/TrackEditorPanel.h Source/UI/TrackEditorPanel.cpp Source/UI/SongsmithMainComponent.h Source/UI/SongsmithMainComponent.cpp Tests/TransportStrip_tests.cpp
git commit -m "feat(ui): the transport strip's left area hosts the command-button bar

Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>"
```

---

### Task 8: `MainWindow` wiring

`MainWindow` is not in the test target, so the logic is already covered by Tasks 1–6; this task is wiring only, verified by the full test run, a build, and the user.

**Files:**
- Modify: `Source/UI/MainWindow.h`, `Source/UI/MainWindow.cpp`

**Interfaces:**
- Consumes: `StripButtonsHost`, `dock::stripButtonsFromJson`/`toJson`, `renameLayoutButton`, `removeLayoutButton`, `AppSettings::stripButtons/setStripButtons`, Task 5's `showManageLayoutsDialog` callbacks, `SongsmithMainComponent::setStripButtonsHost`.

- [ ] **Step 1: Declare the host**

In `MainWindow.h` add `#include "Playback/StripButtonsBar.h"` and, next to `savedLayouts`, a member **declared before `body`**'s teardown matters — the bars live inside `body`, so the host must outlive `body`. Members are destroyed in reverse order; `body` is declared *above* `appSettings`, so declare the host **above `body`** (right after `playback`):

```cpp
    StripButtonsHost                         stripButtonsHost;   // before `body`: the strips' bars refer to it
```

and declare `void persistStripButtons();`.

- [ ] **Step 2: Wire it in the constructor and persistence**

In `MainWindow.cpp`, after `savedLayouts = dock::savedLayoutsFromJson (...)`:

```cpp
    stripButtonsHost.buttons = dock::stripButtonsFromJson (appSettings.stripButtons().toStdString());
    stripButtonsHost.menuState = [this] { return currentMenuState(); };
    stripButtonsHost.persist = [this] { persistStripButtons(); };
    stripButtonsHost.run = [this] (int id) { menuItemSelected (id, 0); };
    songsmith.setStripButtonsHost (&stripButtonsHost);
```

and add

```cpp
void MainWindow::persistStripButtons()
{
    appSettings.setStripButtons (juce::String (dock::toJson (stripButtonsHost.buttons)));
}
```

In `~MainWindow()`, before the body is destroyed, detach: `body->getSongsmith().setStripButtonsHost (nullptr);` (first line after the dialog closes).

- [ ] **Step 3: Make buttons follow their layouts**

Replace the Task-5 placeholder in `manageLayouts()`:

```cpp
    manageLayoutsWindow = dock::showManageLayoutsDialog (
        savedLayouts,
        [this] { persistSavedLayouts(); persistStripButtons(); stripButtonsHost.changed(); },
        [this] (const std::string& from, const std::string& to) { dock::renameLayoutButton (stripButtonsHost.buttons, from, to); },
        [this] (const std::string& name) { dock::removeLayoutButton (stripButtonsHost.buttons, name); },
        this);
```

(The rename/remove callbacks run before `onChanged`, so the persist and refresh see the updated buttons.)

- [ ] **Step 4: Build everything and run the whole suite**

Run: `cmake --build build 2>&1 | tail -5 && ctest --test-dir build --output-on-failure 2>&1 | tail -8`
Expected: build succeeds; all tests PASS (1461 before + the new ones, 1 skipped as before).

- [ ] **Step 5: Windows cross-compile**

Run: `./build-windows.sh forge_ui > /tmp/claude-1000/-home-brian-sandbox-forge/57ec725a-d116-4e6d-8210-7cc5c7496b78/scratchpad/win.log 2>&1; echo exit=$?`
Expected: `exit=0`. On failure read the tail of the log and fix the error that is shown; do not deploy.

- [ ] **Step 6: Commit**

```bash
git add Source/UI/MainWindow.h Source/UI/MainWindow.cpp
git commit -m "feat(ui): MainWindow persists the strip buttons and keeps layout buttons in step

Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>"
```

---

### Task 9: Docs and the spec's final shape

**Files:**
- Modify: `docs/ARCHITECTURE.md` (§9.19/§9.20: add a "Strip buttons" bullet), `docs/UI_GUIDE.md` (transport strip left area), `docs/superpowers/specs/2026-10-14-songsmith-strip-buttons-design.md` (status + deviations), `CLAUDE.md` (docs-map row and status line mention)

- [ ] **Step 1: Update the architecture doc**

Add a "Strip buttons" bullet under §9.20 describing: `StripButtons` (data, `strip.buttons`, cap 10, stable command keys), `stripCommands` in `MenuModel.h`, `buttonView`/`commandIdFor` (derive state and the dispatched id from `MenuState`), `StripButtonsHost` (shared by the main strip and the MIDI editor's strip) and `StripButtonsBar` (+ picker, `»` overflow, right-click remove, 5 Hz refresh), and the `onRenamed`/`onRemoved` hooks that make layout buttons follow their layouts.

- [ ] **Step 2: Update the UI guide**

In `docs/UI_GUIDE.md`, in the transport strip section, document the `+` button, the picker groups, right-click ▸ Remove from strip, the `»` overflow, the 10-button cap, and that layout buttons follow renames/deletes.

- [ ] **Step 3: Record deviations in the spec and mark it implemented**

In the spec, set `Status: Implemented.` and add an "Implementation notes" paragraph under the title: the bar talks to a shared `StripButtonsHost` (callbacks + `ChangeBroadcaster`) instead of the four callbacks listed under "StripButtonsBar", because the MIDI editor panel has a second `TransportStrip` that must stay in sync; resolution of a button to a command id and its view live in `StripButtons` (`commandIdFor`, `buttonView`) so they are testable without a window; the pickable table has 32 entries (the spec said "about 27"); the bar refreshes on a 5 Hz timer because enabled/pressed state has no change signal.

- [ ] **Step 4: Update CLAUDE.md**

Add the spec/plan row to the docs map, and extend the status line with "and the transport strip's user-chosen command buttons".

- [ ] **Step 5: Commit**

```bash
git add docs CLAUDE.md
git commit -m "docs: strip buttons in the architecture, UI guide and docs map

Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>"
```

- [ ] **Step 6: Hand to the user for manual verification (do not launch the GUI)**

Deploy: `cp build-windows/forge_ui_artefacts/Release/song-smith.exe /mnt/c/Apps/SongSmith/` and ask the user to check: `+` menu groups and greyed-out entries; adding Export MIDI / Save Layout / a layout; clicking each; right-click remove; renaming and deleting a layout in Manage Layouts; narrowing the left area to see `»`; the buttons appear in both the main strip and the MIDI editor's strip; buttons survive a restart.
