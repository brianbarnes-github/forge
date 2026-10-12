# Songsmith Dockable Panels Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make the MIDI editor and LOTRO preview panels dockable on the main page or floatable into their own windows, with View-menu items to show/hide them and a remembered layout.

**Architecture:** A pure `LayoutState` (plain data, JSON-serialised into `AppSettings`) and a pure stack-sizing function drive a reusable `DockStack` (weighted vertical stack with draggable dividers). A `DockPanel` (header + one content component) is re-parented between a `DockStack` and a `DockFloatWindow`; a `DockHost` owns the panels and applies the state. The first-run state reproduces today's page exactly.

**Tech Stack:** C++20, JUCE 8 (existing), Catch2. No new dependencies. All new code in `Source/UI/Dock/`; nothing in `Source/Core/`.

**Spec:** `docs/superpowers/specs/2026-10-11-songsmith-dock-panels-design.md`

## Global Constraints

- First-run page is identical to today's: LOTRO preview docked and visible at the bottom, MIDI editor hidden; on first open the editor is **floating**.
- Dock/float, visibility, zone, weights and float bounds persist in `AppSettings` under the single key `dock.layout` (a JSON string), never in the `.songsmith` file.
- Two zones only (`belowTracks`, `bottom`); no drag-to-rearrange, tabs, or right-hand zone.
- One live content instance per panel; floating/docking re-parents it, never copies it.
- Never add a way to hide the LOTRO range band (it is part of the preview content; only the whole panel can be hidden).
- Conventional commits; end every commit message with `Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>` exactly. Never push without explicit approval. Do not launch the GUI.
- Custom error types, not strings; types/hints everywhere; no new dependencies without mentioning them (none are planned).
- Test run: `cmake --build build && ctest --test-dir build --output-on-failure` (run serially; window tests flake under `-j`).

## Amendment to the spec (layout detail)

The spec says the page is "one vertical stack". Precisely: the **top-level** `DockStack` holds `upper` (MIDI source header + track list), the visible `belowTracks` panels, and `lower`. `lower` is today's `LowerRegion`: the fixed transport strip and part strip on top, then a nested `DockStack` of the visible `bottom` panels, then the diagnostics list (inner splitter, unchanged). Default weights `upper 0.55 / lower 0.45` reproduce the current 55/45 splitter exactly. When `lower` has no visible bottom panel and diagnostics are hidden it collapses to its fixed chrome height and gives the rest back.

## Review Focus

- A corrupt, empty, or older `dock.layout` value → defaults, no crash (Task 1, Task 3).
- A saved float rectangle that is off-screen, zero-sized or negative → the window opens somewhere reachable (Task 4).
- Both panels in the same zone / both floating / both hidden → layout still fills the page with no gap or overlap (Task 2, Task 6).
- Window shrunk below the sum of minimum heights → no negative sizes, everything stays inside bounds (Task 2).
- New / Open (document replaced) while the editor is docked or floating → editor content cleared, panel stays in its mode, no dangling track handle (Task 7).

---

## File Structure

| File | Responsibility |
|---|---|
| `Source/UI/Dock/DockLayoutState.{h,cpp}` | `Zone`, `Mode`, `PanelDef`, `PanelState`, `LayoutState`; defaults, normalise, show/hide/float/dock/move, JSON. No JUCE in the header. |
| `Source/UI/Dock/DockStackLayout.{h,cpp}` | Pure `computeStack` and `dragDivider`. No JUCE. |
| `Source/UI/Dock/DockStack.{h,cpp}` | Component: weighted vertical stack of child components with divider bars; uses the pure layout. |
| `Source/UI/Dock/DockPanel.{h,cpp}` | Panel header (title, Float/Dock, Dock-to menu, Close) + hosted content. |
| `Source/UI/Dock/DockFloatWindow.{h,cpp}` | `DocumentWindow` that hosts a `DockPanel` while floating. |
| `Source/UI/Dock/DockHost.{h,cpp}` | Owns panels + float windows, applies/reads `LayoutState`, drives the stacks. |
| `Source/UI/AppSettings.h` | `dockLayout()` / `setDockLayout()` string accessors. |
| `Source/UI/MenuModel.h` | `ViewMidiEditorToggle`, `ViewLotroPreviewToggle`, `MenuState` fields. |
| `Source/UI/SongsmithMainComponent.{h,cpp}` | Replace the main `SplitterComponent` with the top `DockStack`; host panels; double-click shows editor. |
| `Source/UI/TrackEditorWindow.{h,cpp}` | Becomes `TrackEditorPanel` content (no longer a `DocumentWindow`). |
| `Source/UI/MainWindow.cpp` | Menu state/handlers, load/save layout. |
| Tests | `Tests/DockLayoutState_tests.cpp`, `DockStackLayout_tests.cpp`, `DockStack_tests.cpp`, `DockPanel_tests.cpp`, `DockHost_tests.cpp` + additions to `AppSettings_tests.cpp`, `MenuModel_tests.cpp`, `TrackEditorWindow_tests.cpp`, `SongsmithMainComponent` tests. |

Register every new `.cpp` in **both** `CMakeLists.txt` (near `Source/UI/SplitterComponent.cpp`, line ~158) and `Tests/CMakeLists.txt` (source list near line ~154, and the test list near line ~72–78).

---

### Task 1: Layout state (pure)

**Files:**
- Create: `Source/UI/Dock/DockLayoutState.h`, `Source/UI/Dock/DockLayoutState.cpp`
- Test: `Tests/DockLayoutState_tests.cpp`
- Modify: `CMakeLists.txt`, `Tests/CMakeLists.txt`

**Interfaces:**
- Produces (all in `namespace lotro::dock`):

```cpp
enum class Zone { belowTracks, bottom };
enum class Mode { docked, floating };

struct PanelDef {
    std::string id;           // "midiEditor" | "lotroPreview"
    std::string title;
    Zone        defaultZone;
    Mode        defaultMode;
    bool        defaultVisible;
    double      defaultWeight;   // > 0
    int         minSize;         // px, >= 40
};

struct FloatBounds { int x = 0, y = 0, width = 0, height = 0; };

struct PanelState {
    std::string id;
    Mode  mode;
    bool  visible;
    Zone  zone;
    int   order;                       // among panels of the same zone, ascending
    double weight;                     // > 0, share within its stack
    std::optional<FloatBounds> floatBounds;
};

struct LayoutState {
    double upperWeight = 0.55;
    double lowerWeight = 0.45;
    std::vector<PanelState> panels;    // one per registered def, any order
};

const std::vector<PanelDef>& panelDefs();                 // the registry: lotroPreview, midiEditor
LayoutState defaultLayout();
LayoutState normalise (LayoutState, const std::vector<PanelDef>&);   // drop unknown, add missing, clamp
PanelState*       find (LayoutState&, const std::string& id);
const PanelState* find (const LayoutState&, const std::string& id);

void show     (LayoutState&, const std::string& id);          // visible = true
void hide     (LayoutState&, const std::string& id);          // visible = false (mode kept)
void setFloating (LayoutState&, const std::string& id, std::optional<FloatBounds>);
void setDocked   (LayoutState&, const std::string& id);
void moveToZone  (LayoutState&, const std::string& id, Zone);  // appended last in that zone, order renumbered

std::string toJson (const LayoutState&);
LayoutState fromJson (const std::string&);   // never throws: corrupt/empty -> defaultLayout()
```

Registry values: `lotroPreview {"LOTRO Preview", bottom, docked, visible=true, weight 1.0, min 120}`; `midiEditor {"MIDI Editor", belowTracks, floating, visible=false, weight 0.35, min 160}`.

- [ ] **Step 1: Write the failing tests** (`Tests/DockLayoutState_tests.cpp`, tag `[dock][state]`)

```cpp
#include "UI/Dock/DockLayoutState.h"
#include <catch2/catch_test_macros.hpp>
using namespace lotro::dock;

TEST_CASE ("dock: the default layout is today's page", "[dock][state]")
{
    const auto s = defaultLayout();
    CHECK (s.upperWeight == 0.55);
    CHECK (s.lowerWeight == 0.45);
    const auto* lotro = find (s, "lotroPreview");
    REQUIRE (lotro != nullptr);
    CHECK (lotro->mode == Mode::docked);
    CHECK (lotro->visible);
    CHECK (lotro->zone == Zone::bottom);
    const auto* ed = find (s, "midiEditor");
    REQUIRE (ed != nullptr);
    CHECK_FALSE (ed->visible);
    CHECK (ed->mode == Mode::floating);   // first open is floating
    CHECK (ed->zone == Zone::belowTracks);
}

TEST_CASE ("dock: show/hide keep the mode; float/dock keep visibility", "[dock][state]")
{
    auto s = defaultLayout();
    show (s, "midiEditor");
    CHECK (find (s, "midiEditor")->visible);
    CHECK (find (s, "midiEditor")->mode == Mode::floating);
    setDocked (s, "midiEditor");
    hide (s, "midiEditor");
    CHECK (find (s, "midiEditor")->mode == Mode::docked);
    setFloating (s, "lotroPreview", FloatBounds { 10, 20, 640, 300 });
    CHECK (find (s, "lotroPreview")->visible);
    REQUIRE (find (s, "lotroPreview")->floatBounds.has_value());
    CHECK (find (s, "lotroPreview")->floatBounds->width == 640);
}

TEST_CASE ("dock: moveToZone appends last and renumbers both zones", "[dock][state]")
{
    auto s = defaultLayout();
    moveToZone (s, "midiEditor", Zone::bottom);
    CHECK (find (s, "midiEditor")->zone == Zone::bottom);
    CHECK (find (s, "midiEditor")->order > find (s, "lotroPreview")->order);
}

TEST_CASE ("dock: JSON round-trips", "[dock][state]")
{
    auto s = defaultLayout();
    show (s, "midiEditor");
    setFloating (s, "midiEditor", FloatBounds { -5, 7, 900, 500 });
    s.upperWeight = 0.3; s.lowerWeight = 0.7;
    const auto back = fromJson (toJson (s));
    CHECK (back.upperWeight == 0.3);
    CHECK (find (back, "midiEditor")->visible);
    REQUIRE (find (back, "midiEditor")->floatBounds.has_value());
    CHECK (find (back, "midiEditor")->floatBounds->x == -5);
}

TEST_CASE ("dock: corrupt, empty, wrong-typed or hostile JSON gives the defaults", "[dock][state]")
{
    for (const char* text : { "", "garbage", "{", "[]", "null", "{\"panels\": 5}",
                              "{\"upperWeight\": -1, \"lowerWeight\": 0, \"panels\": []}" })
    {
        const auto s = fromJson (text);
        CHECK (s.upperWeight > 0.0);
        CHECK (s.lowerWeight > 0.0);
        CHECK (find (s, "lotroPreview") != nullptr);
        CHECK (find (s, "midiEditor") != nullptr);
    }
}

TEST_CASE ("dock: normalise drops unknown ids, adds missing, clamps weights and bounds", "[dock][state]")
{
    LayoutState s;
    s.upperWeight = std::nan ("");
    s.panels.push_back ({ "ghost", Mode::docked, true, Zone::bottom, 0, 1.0, std::nullopt });
    s.panels.push_back ({ "midiEditor", Mode::floating, true, Zone::belowTracks, 0, -3.0,
                          FloatBounds { 0, 0, 0, 0 } });   // zero-size bounds are discarded
    const auto n = normalise (s, panelDefs());
    CHECK (find (n, "ghost") == nullptr);
    CHECK (find (n, "lotroPreview") != nullptr);
    CHECK (n.upperWeight > 0.0);
    CHECK (find (n, "midiEditor")->weight > 0.0);
    CHECK_FALSE (find (n, "midiEditor")->floatBounds.has_value());
}
```

- [ ] **Step 2: Build and run** — expected: compile failure (header missing).

Run: `cmake --build build 2>&1 | tail -5`

- [ ] **Step 3: Implement.** Header has no JUCE includes (`<optional> <string> <vector>`). In the `.cpp` use `juce::JSON::parse` / `juce::JSON::toString` and `juce::DynamicObject` to serialise (`{"upperWeight":..,"lowerWeight":..,"panels":[{"id","mode":"docked|floating","visible","zone":"belowTracks|bottom","order","weight","float":{"x","y","w","h"}?}]}`). Rules:
  - `fromJson`: any parse failure, non-object, or wrong field type for a field → that field takes its default; then `normalise`. Never throws.
  - `normalise`: drop ids not in the defs; add a default `PanelState` for each missing def; any non-finite or `<= 0` weight (panel, upper, lower) → the def's default (`0.55`/`0.45` for upper/lower); `floatBounds` with `width < 100 || height < 60` discarded; renumber `order` per zone as 0..n-1 sorted by existing `order` (stable).
  - `show/hide/setFloating/setDocked`: set `visible` / `mode` (+ `floatBounds` for `setFloating`); unknown id is a no-op. `moveToZone`: set zone, `order = max order in target zone + 1`, then renumber both zones.
- [ ] **Step 4: Run tests** — `ctest --test-dir build -R "dock:" --output-on-failure` — PASS.
- [ ] **Step 5: Commit** — `feat(ui): dock layout state with defaults, normalisation and JSON` (+ trailer).

---

### Task 2: Stack sizing (pure)

**Files:** Create `Source/UI/Dock/DockStackLayout.{h,cpp}`; Test `Tests/DockStackLayout_tests.cpp`; register in both CMake files.

**Interfaces:**
- Produces:

```cpp
namespace lotro::dock {
struct StackItem { double weight; int minSize; };     // all items passed in are visible

// Sizes (px) for each item. Dividers of `dividerSize` sit between neighbours.
// sum(result) + dividerSize * (n-1) == total when it fits; when the minimums
// do not fit every item gets its minimum (the caller clips). Never negative.
std::vector<int> computeStack (const std::vector<StackItem>&, int total, int dividerSize);

// New weights after dragging divider `index` (between items index and index+1)
// by `deltaPx`; the two neighbours trade pixels, neither goes below minSize;
// other weights unchanged. Weights are rescaled so that the sum of all weights is unchanged.
std::vector<double> dragDivider (const std::vector<StackItem>&, int total, int dividerSize,
                                 int index, int deltaPx);
}
```

- [ ] **Step 1: Failing tests** (`[dock][stack]`):

```cpp
TEST_CASE ("stack: weights split the space after dividers", "[dock][stack]")
{
    CHECK (computeStack ({ { 1, 10 }, { 1, 10 } }, 106, 6) == std::vector<int> { 50, 50 });
    const auto s = computeStack ({ { 0.55, 10 }, { 0.45, 10 } }, 700, 6);
    CHECK (s[0] + s[1] + 6 == 700);
    CHECK (s[0] > s[1]);
}
TEST_CASE ("stack: a single item takes everything; empty is empty", "[dock][stack]")
{
    CHECK (computeStack ({ { 1, 10 } }, 300, 6) == std::vector<int> { 300 });
    CHECK (computeStack ({}, 300, 6).empty());
}
TEST_CASE ("stack: minimums win over weights and the rest is redistributed", "[dock][stack]")
{
    const auto s = computeStack ({ { 0.9, 10 }, { 0.1, 100 } }, 206, 6);   // 0.1 share would be 20
    CHECK (s[1] == 100);
    CHECK (s[0] == 100);
}
TEST_CASE ("stack: when minimums cannot fit, everyone gets their minimum, never negative", "[dock][stack]")
{
    const auto s = computeStack ({ { 1, 120 }, { 1, 160 } }, 100, 6);
    CHECK (s == std::vector<int> { 120, 160 });
    for (const int v : computeStack ({ { 1, 0 }, { 1, 0 } }, 3, 6)) CHECK (v >= 0);
}
TEST_CASE ("stack: dragging a divider trades pixels and respects minimums", "[dock][stack]")
{
    const std::vector<StackItem> items { { 1, 40 }, { 1, 40 } };
    auto w = dragDivider (items, 206, 6, 0, 30);       // 100/100 -> 130/70
    auto s = computeStack ({ { w[0], 40 }, { w[1], 40 } }, 206, 6);
    CHECK (s == std::vector<int> { 130, 70 });
    w = dragDivider (items, 206, 6, 0, 500);           // clamps at the neighbour's minimum
    s = computeStack ({ { w[0], 40 }, { w[1], 40 } }, 206, 6);
    CHECK (s[1] == 40);
    CHECK (dragDivider (items, 206, 6, 5, 10) == std::vector<double> { 1, 1 });   // bad index: unchanged
}
```

- [ ] **Step 2: Run, expect compile failure.**
- [ ] **Step 3: Implement.** `computeStack`: `n = size`; if `n == 0` return `{}`; `avail = max(0, total - dividerSize*(n-1))`. Iterative: `fixed` flags all false; loop: `share_i = remaining * weight_i / sumWeightsOfUnfixed`; any unfixed item with `share_i < minSize_i` → fix it at `minSize_i`, subtract from `remaining`, repeat; once none violate assign `round(share)`, and give the rounding remainder to the last unfixed item so the sum matches `avail`. If `sum(minSize) > avail` return the minimums. Treat non-positive weights as `1e-6`. `dragDivider`: compute current sizes; if `index` out of range return weights unchanged; `delta` clamped so `size[index] + delta >= min[index]` and `size[index+1] - delta >= min[index+1]`; new sizes → weights `newSize_i / sum(sizes) * sum(oldWeights)` for **all** items (so unchanged items keep the same share).
- [ ] **Step 4: Run** `ctest -R "stack:"` — PASS.
- [ ] **Step 5: Commit** — `feat(ui): pure weighted-stack sizing and divider drag`.

---

### Task 3: AppSettings `dock.layout`

**Files:** Modify `Source/UI/AppSettings.h`; Test `Tests/AppSettings_tests.cpp`.

**Interfaces:**
- Produces: `juce::String AppSettings::dockLayout() const` (empty when never saved) and `bool AppSettings::setDockLayout (const juce::String&)` (writes + saves, returns save success; same `saveFailed` behaviour as other setters). Key constant `keyDockLayout = "dock.layout"`.

- [ ] **Step 1: Failing test** (mirror an existing `AppSettings_tests.cpp` case for fixture style; read it first):

```cpp
TEST_CASE ("AppSettings: dock.layout round-trips and is empty by default", "[app-settings][dock]")
{
    // use the same temporary PropertiesFile fixture as the neighbouring tests
    CHECK (settings.dockLayout().isEmpty());
    CHECK (settings.setDockLayout ("{\"upperWeight\":0.4}"));
    CHECK (settings.dockLayout() == "{\"upperWeight\":0.4}");
}
```
- [ ] **Step 2: Run, expect compile failure.**
- [ ] **Step 3: Implement** after `mergeScope`: `juce::String dockLayout() const { return file.getValue (keyDockLayout); }` and `bool setDockLayout (const juce::String& json) { file.setValue (keyDockLayout, json); return save(); }`.
- [ ] **Step 4: Run** `ctest -R "AppSettings"` — PASS. **Step 5: Commit** — `feat(ui): AppSettings stores the dock layout`.

---

### Task 4: DockStack, DockPanel, DockFloatWindow

**Files:** Create `Source/UI/Dock/DockStack.{h,cpp}`, `DockPanel.{h,cpp}`, `DockFloatWindow.{h,cpp}`; Test `Tests/DockStack_tests.cpp`, `Tests/DockPanel_tests.cpp`; register in CMake.

**Interfaces:**
- Consumes: `computeStack`, `dragDivider`, `Zone`, `FloatBounds` (Tasks 1–2); `WindowPlacement::resolve` (`Source/UI/WindowPlacement.h`).
- Produces:

```cpp
// DockStack.h
class DockStack : public juce::Component {
public:
    struct Item { std::string id; juce::Component* component; double weight; int minSize; };
    static constexpr int dividerSize = 6;
    void setItems (std::vector<Item>);                 // re-parents components into this stack, hides removed ones
    const std::vector<Item>& items() const;
    std::function<void (const std::vector<Item>&)> onWeightsChanged;   // after a divider drag ends
    int dividerCount() const;                          // items().size() - 1
    juce::Rectangle<int> dividerBounds (int index) const;   // for tests / hit testing
    void resized() override;
    // dragging: mouseDown on a divider bar starts, mouseDrag calls dragDivider with the delta since mouseDown, mouseUp fires onWeightsChanged
};

// DockPanel.h
class DockPanel : public juce::Component {
public:
    static constexpr int headerHeight = 20;
    DockPanel (std::string id, juce::String title, juce::Colour titleColour, juce::Component& content);  // content not owned; becomes a child
    const std::string& id() const;
    void setFloating (bool floating);                  // updates the button text only ("Float"/"Dock")
    bool isFloating() const;
    std::function<void()> onFloatToggle;               // Float/Dock button
    std::function<void()> onClose;                     // Close button
    std::function<void (lotro::dock::Zone)> onDockTo;  // "Dock to" menu items
    juce::PopupMenu buildZoneMenu() const;             // items: "Below track list", "Bottom"
    juce::Component& content();
    void resized() override;                           // header on top, content below
};

// DockFloatWindow.h
class DockFloatWindow : public juce::DocumentWindow {
public:
    DockFloatWindow (DockPanel& panel, juce::String title, std::optional<lotro::dock::FloatBounds> saved, juce::Component* centreAround);
    ~DockFloatWindow() override;                       // detaches the panel (setContentNonOwned (nullptr, false))
    lotro::dock::FloatBounds currentBounds() const;
    std::function<void()> onClosed;                    // title-bar close button
    void closeButtonPressed() override;
    static juce::Rectangle<int> resolveBounds (std::optional<lotro::dock::FloatBounds> saved,
                                               const juce::Array<juce::Rectangle<int>>& userAreas,
                                               juce::Rectangle<int> primary);   // pure, tested
};
```

- [ ] **Step 1: Failing tests.**
  - `DockStack_tests.cpp` (`[dock][stack-ui]`): two plain `juce::Component` children with weights 1/1, stack sized 200×106 → child bounds `{0,0,200,50}` and `{0,56,200,50}`, `dividerBounds (0) == {0,50,200,6}`; removing an item via `setItems` hides the component (`isVisible()` false) and gives the survivor the whole height; simulate a drag with `mouseDown`/`mouseDrag`/`mouseUp` on a divider using `juce::MouseEvent` helpers already used in `Tests/SplitterComponent_tests.cpp` (copy that file's event construction) and check `onWeightsChanged` fires once with the new weights.
  - `DockPanel_tests.cpp` (`[dock][panel]`): header is the top 20 px and content fills the rest; `setFloating (true)` flips the button text; clicking the Float/Dock button (`triggerClick`) calls `onFloatToggle`; `buildZoneMenu()` has two items; **re-parenting keeps one content instance**: put the panel in a `DockFloatWindow`, check `content().getParentComponent() == &panel`, destroy the window, check the panel still owns/parents the same content pointer; `resolveBounds`: saved `{ -5000, -5000, 800, 600 }` with one user area `{0,0,1920,1080}` → result intersects that area; `nullopt` → centred in `primary`; `{0,0,0,0}` → treated as no saved bounds.
- [ ] **Step 2: Run, expect compile failure.**
- [ ] **Step 3: Implement.** `DockStack::resized` builds `StackItem`s from `items()`, calls `computeStack (…, getHeight(), dividerSize)`, sets bounds top to bottom with a divider after every item but the last; dividers are child `Bar` components (grey fill, `setMouseCursor (UpDownResizeCursor)`) hit-tested by `mouseDown`. Drag: remember weights at mouseDown, `dragDivider (items, getHeight(), dividerSize, index, e.getDistanceFromDragStartY())`, write the weights into `items`, call `resized()`; `mouseUp` fires `onWeightsChanged`. `DockPanel` paints the header with the panel colour tokens `SongsmithColours::panelHeader` background and `titleColour` text (11 pt like `sourceHeader`), children: `juce::TextButton floatButton`, `closeButton` ("x"); right-click or a "…" button on the header opens `buildZoneMenu` and calls `onDockTo`. `DockFloatWindow` uses native title bar, `setResizable (true, false)`, `setContentNonOwned (&panel, true)`; `resolveBounds` returns `WindowPlacement::resolve (rect, userAreas, primary)` for valid saved bounds, else a 900×500 rectangle centred in `primary`. `closeButtonPressed` calls `onClosed`.
- [ ] **Step 4: Run** `ctest -R "dock"` — PASS. **Step 5: Commit** — `feat(ui): DockStack, DockPanel and floating window`.

---

### Task 5: Menu items

**Files:** Modify `Source/UI/MenuModel.h`; Test `Tests/MenuModel_tests.cpp`.

**Interfaces:** adds `MenuCommandId` values `ViewMidiEditorToggle`, `ViewLotroPreviewToggle` (insert after `ViewDiagnosticsToggle`, before `EditTempoMeter`), `MenuState::midiEditorVisible` / `lotroPreviewVisible` (bool, default `false`), `allCommandIds` size `30 → 32` with both ids appended. `MainWindow::menuItemSelected` must handle them (Task 8).

- [ ] **Step 1: Failing test** next to the existing View test (`MenuModel_tests.cpp:229–235`): the View menu lists, in order, `Export ABC panel`, `Diagnostics list`, `MIDI Editor`, `LOTRO Preview`; the last two are enabled and ticked iff the matching `MenuState` flag is set.
- [ ] **Step 2: Run, expect failure.** **Step 3: Implement** — in the View menu builder add `item (ViewMidiEditorToggle, "MIDI Editor", {}, true, s.midiEditorVisible)` and `item (ViewLotroPreviewToggle, "LOTRO Preview", {}, true, s.lotroPreviewVisible)`; update the `allCommandIds` array and its count.
- [ ] **Step 4: Run** `ctest -R "MenuModel"` — PASS (`MainWindow`'s switch gets its cases in Task 8; until then they fall into `default: return;`).
- [ ] **Step 5: Commit** — `feat(ui): View menu items for the MIDI editor and LOTRO preview panels`.

---

### Task 6: DockHost and the LOTRO preview as a docked panel

**Files:** Create `Source/UI/Dock/DockHost.{h,cpp}`; Modify `Source/UI/SongsmithMainComponent.{h,cpp}`; Test `Tests/DockHost_tests.cpp`, plus a main-component layout test.

**Interfaces:**
- Consumes: Tasks 1–4.
- Produces:

```cpp
class DockHost {
public:
    DockHost (DockStack& topStack, DockStack& bottomStack, juce::Component& upper, juce::Component& lower,
              juce::Component* centreFloatsAround);
    // Registers the content for a def id; creates the DockPanel (title/colour from the def).
    void addPanel (const lotro::dock::PanelDef&, juce::Colour titleColour, juce::Component& content);
    void apply (const lotro::dock::LayoutState&);       // re-parent, show/hide windows, set stack items
    lotro::dock::LayoutState state() const;             // current state incl. float bounds and stack weights
    bool isVisible (const std::string& id) const;
    void setVisible (const std::string& id, bool);      // show/hide, remembering mode
    std::function<void (const lotro::dock::LayoutState&)> onLayoutChanged;   // every user-driven change
    DockPanel* panel (const std::string& id);
};
```
  `apply` rules: a panel with `visible && mode == docked` goes into the stack for its zone (`belowTracks` → items between `upper` and `lower` in `topStack`; `bottom` → `bottomStack`), ordered by `order`, with `minSize`/`weight` from the state/def; `visible && floating` → a `DockFloatWindow` is created/shown; otherwise hidden and not in any stack. `topStack` always holds `upper` (id `"upper"`, weight `upperWeight`, min 120) and `lower` (id `"lower"`, weight `lowerWeight`, min = fixed chrome height 138). `DockPanel` callbacks (`onFloatToggle`, `onClose`, `onDockTo`, window `onClosed`) edit the state with the Task 1 functions, call `apply`, then `onLayoutChanged`. Weights from `DockStack::onWeightsChanged` are copied into the state and reported.

- **Main component change:** replace `SplitterComponent splitter` with `DockStack topStack`; `LowerRegion` keeps transport + part strip fixed on top and replaces its `previewRegion` child with `DockStack bottomStack`; `innerSplitter.setComponents (&bottomStack, &diagnostics)`. `PreviewRegion` loses its `previewHeader` label (the `DockPanel` header, titled `▼ LOTRO PREVIEW · how it will sound in-game` with `SongsmithColours::accentAmber`, replaces it; keep `sourceHeaderHeight` = `DockPanel::headerHeight` = 20). `SongsmithMainComponent` owns `DockHost dockHost`; the constructor calls `dockHost.addPanel (lotroDef, amber, previewRegion)` and `dockHost.apply (dock::defaultLayout())`. `LowerRegion::resized`: if the bottom stack is empty and diagnostics hidden, its own preferred height is only the chrome — implement by having `DockHost::apply` set the `lower` item's `minSize` to the chrome height and, when `bottomStack` has no items and diagnostics are hidden, give `lower` a weight of `1e-6` so it collapses to its minimum.

- [ ] **Step 1: Failing tests.**
  - `DockHost_tests.cpp` (`[dock][host]`, build a throwaway `DockStack` pair, two plain components, and dummy content): after `apply (defaultLayout())` the `topStack` items are `upper`, `lower`, and the `bottomStack` items are `lotroPreview`; `setVisible ("midiEditor", true)` with the default (floating) state creates a window (check `panel("midiEditor")->getTopLevelComponent()` is a `DockFloatWindow` and `topStack` has no editor item); after `setDocked`-equivalent (`panel->onFloatToggle()`), `topStack` items are `upper`, `midiEditor`, `lower` in that order and the editor's content has the **same pointer** as before; hiding then showing keeps the mode; `onLayoutChanged` fires for each user-driven change and its `state()` has `visible`/`mode` updated; floating the LOTRO preview empties the bottom stack and collapses `lower`.
  - Main component (`[songsmith][layout]`): the **first-run equivalence test**: construct `SongsmithMainComponent` at 1000×700 with diagnostics hidden; assert `upperRegion` height ≈ `0.55 × 700` within 2 px, the part strip and transport are at the top of `lowerRegion`, and the preview region's roll fills the rest; this must give the same numbers as the same assertions run against `git stash`-free `HEAD~` behaviour — record them from the current code first (run the assertions on the unmodified tree, copy the numbers into the test) before changing the component.
- [ ] **Step 2: Run, expect failure.**
- [ ] **Step 3: Implement** as described above. Keep `View → Diagnostics list` behaviour unchanged.
- [ ] **Step 4: Run the full suite** — all existing layout/main-component tests must still pass; fix regressions in the refactor, not in the tests. `ctest --test-dir build --output-on-failure` — PASS.
- [ ] **Step 5: Commit** — `refactor(ui): host the LOTRO preview in a dockable panel; first-run layout unchanged`.

---

### Task 7: The MIDI editor as a panel

**Files:** Modify `Source/UI/TrackEditorWindow.{h,cpp}`, `Source/UI/SongsmithMainComponent.{h,cpp}`; Test `Tests/TrackEditorWindow_tests.cpp` (+ main-component tests).

**Interfaces:**
- `TrackEditorWindow` stops being a `juce::DocumentWindow`: it becomes `class TrackEditorPanel : public juce::Component` (rename the class and files to `TrackEditorPanel`; update the CMake lists, the includes and `Tests/TrackEditorWindow_tests.cpp` → `TrackEditorPanel_tests.cpp`, and `friend struct TrackEditorPanelTestAccess`). It keeps its public API: `setTrack`, `getTrackId`, `setGridTicks`, `getGridTicks`, `setFollowPlayhead`, `getFollowPlayhead`, `quantizeSelection`, `setGhostTracks`, `trackAppearanceChanged`, `setPlayback`, `keyPressed`, `appearanceRepaintsForTesting`, and gains `juce::String title() const` ("Edit Track: <name>" or "MIDI Editor" when no track) plus `std::function<void (const juce::String&)> onTitleChanged`. `Content` (strip, ruler, roll) is its only child, filling its bounds. The `centreAroundComponent`, `setUsingNativeTitleBar`, `setResizable`, `closeButtonPressed` and `onClosed` members go away (the dock panel / float window own that). The "no track loaded" state paints "Double-click a track to edit it" centred over the roll area. It is constructed once, early, by `SongsmithMainComponent` (`std::unique_ptr<TrackEditorPanel> trackEditor`), not on first double-click.
- `SongsmithMainComponent`:
  - `trackDoubleClicked (id)` → `trackEditor->setTrack (node)`; `dockHost.setVisible ("midiEditor", true)`; ghosts refreshed as today.
  - `isTrackEditorOpen()` → `dockHost.isVisible ("midiEditor")`.
  - `documentReplaced()` → hides nothing, but calls `trackEditor->clearTrack()` (new method: resets `currentNoteSource`, `currentTrackId = -1`, `roll.setNoteSource (nullptr, …)` / `roll.setEditableTrack ({})`, title back to "MIDI Editor").
  - `setActiveEditorGridSize`, `setFollowPlayhead`, `quantizeActiveEditor`, `refreshGhostTracksOnEditor` keep working against `trackEditor` (they no longer test for a null window; they act only when `getTrackId() != -1`).
  - `trackEditor->onTitleChanged` updates the `DockPanel` title (add `DockPanel::setTitle (const juce::String&)`) and the float window's name.
  - Register with `dockHost.addPanel (editorDef, juce::Colour (SongsmithColours::text), *trackEditor)` and apply.

- [ ] **Step 1: Failing tests.** Port every case in `TrackEditorWindow_tests.cpp` to `TrackEditorPanel` (construct the panel, not a window; the title assertions use `title()`), then add: (a) a fresh panel reports `getTrackId() == -1` and title `MIDI Editor`; (b) `clearTrack()` after `setTrack` resets id and title; (c) main-component test: with a playback controller and a track, calling `trackDoubleClicked (id)` makes `isTrackEditorOpen()` true, loads the track (`trackEditor->getTrackId() == id`), and — default state — the panel is in a `DockFloatWindow` (first open is floating); calling the panel's `onFloatToggle` re-parents it into the page between `upper` and `lower`; (d) `documentReplaced()` clears the loaded track in both modes and leaves `isTrackEditorOpen()` unchanged.
- [ ] **Step 2: Run, expect failure.**
- [ ] **Step 3: Implement** as above. Space-bar play/pause stays in `keyPressed`; make sure the docked panel can take keyboard focus (`setWantsKeyboardFocus (true)` on the panel, `grabKeyboardFocus` in `setTrack`) or Space will not reach it.
- [ ] **Step 4: Run the full suite** — PASS. **Step 5: Commit** — `feat(ui): the MIDI editor is a dockable panel`.

---

### Task 8: Persistence and MainWindow wiring

**Files:** Modify `Source/UI/MainWindow.{h,cpp}`, `Source/UI/SongsmithMainComponent.{h,cpp}`; Test `Tests/MainWindow*_tests.cpp` (use the existing MainWindow test fixture; read it first) and `Tests/DockHost_tests.cpp`.

**Interfaces:**
- `SongsmithMainComponent` gains `dock::LayoutState getDockLayout() const` and `void setDockLayout (const dock::LayoutState&)` (delegating to `DockHost::state()` / `apply`), `bool isMidiEditorVisible()`, `void setMidiEditorVisible (bool)`, `bool isLotroPreviewVisible()`, `void setLotroPreviewVisible (bool)`, and `std::function<void (const dock::LayoutState&)> onDockLayoutChanged`.
- `MainWindow`: after constructing the body, `songsmith.setDockLayout (dock::fromJson (appSettings.dockLayout().toStdString()))`; `songsmith.onDockLayoutChanged = [this] (const auto& s) { appSettings.setDockLayout (juce::String (dock::toJson (s))); }` (save failures use the existing settings-save failure path); `currentMenuState()` sets `midiEditorVisible` / `lotroPreviewVisible`; `menuItemSelected` handles `ViewMidiEditorToggle` / `ViewLotroPreviewToggle` by toggling the matching panel then `menuItemsChanged()`.

- [ ] **Step 1: Failing tests.** (a) `MainWindow`/component: saving then re-creating from `toJson` of a state with the editor docked + visible and the LOTRO preview hidden restores exactly that (`getDockLayout()` equal field by field); (b) an empty/corrupt stored string yields the first-run layout (LOTRO visible and docked, editor hidden); (c) toggling each View item flips `isVisible` and fires `onDockLayoutChanged` once; (d) float bounds are written when a floating window is moved/resized (`DockHost::state()` reads `DockFloatWindow::currentBounds()`), and a saved off-screen rectangle is brought on-screen by `resolveBounds` on restore.
- [ ] **Step 2: Run, expect failure.** **Step 3: Implement.** Persist float bounds on window `moved`/`resized` (debounce not needed; JUCE calls are infrequent) via a `DockFloatWindow::onBoundsChanged` callback that the host forwards to `onLayoutChanged`.
- [ ] **Step 4: Run the full suite** — PASS.
- [ ] **Step 5: Commit** — `feat(ui): remember the dock layout and wire the View menu`.

---

### Task 9: Documentation and final checks

**Files:** Modify `docs/UI_GUIDE.md` (Layout overview, View menu, new "Dockable panels" section), `docs/ARCHITECTURE.md` (new §9.19 describing the Dock files, the amendment above, and the editor-panel refactor), `docs/TESTING.md` (test count from `ctest --test-dir build -N | tail -1`, one bullet for the dock tests), `CLAUDE.md` (status line + docs-map row for the spec/plan), spec header (`Status: implemented`).

- [ ] **Step 1:** Write the docs from the source files, not from this plan (brief docs tasks from component sources; four doc errors were found last time from briefing off a brief).
- [ ] **Step 2:** Full serial suite `ctest --test-dir build` — PASS; Windows cross-build `./build-windows.sh forge_ui` exit 0.
- [ ] **Step 3:** Commit — `docs: dockable panels`.

---

## Self-review

- **Spec coverage:** goal/decisions → Tasks 1, 8; components → Tasks 1–4, 6; layout/zones → Tasks 2, 4, 6; behaviour (View menu, editor follow, LOTRO unchanged) → Tasks 5, 6, 7; persistence/normalisation → Tasks 1, 3, 8; testing list → each task; first-run identical → Task 6 equivalence test, Task 1 defaults.
- **Placeholders:** none intended; Task 4/6/7/8 give interface and rules, with test cases written out in prose where fixture reuse (Splitter event construction, MainWindow fixture) must be copied from existing tests that the implementer reads first.
- **Type consistency:** `Zone`, `Mode`, `FloatBounds`, `PanelDef`, `LayoutState`, `StackItem`, `DockStack::Item`, `DockPanel`, `DockFloatWindow`, `DockHost` names are used identically across tasks; `TrackEditorWindow` → `TrackEditorPanel` rename is done once in Task 7.
