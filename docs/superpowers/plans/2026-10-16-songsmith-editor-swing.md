# MIDI Editor Swing Grid Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Options ▸ Grid Division ▸ Swing becomes a real swung grid (REAPER's −100..+100 % model, pairs restarting at every bar line) with a strength slider on the MIDI editor's bottom bar, used everywhere the editor grid is used today.

**Architecture:** A JUCE-free value `EditorGrid` (`Source/UI/EditorGrid.{h,cpp}`) plus free functions (`snapToEditorGrid`, `stepEditorGrid`, `editorGridLineAtOrBefore`, `editorGridLineAfter`, `editorGridLinesIn`, `editorGridMinGap`) replaces the bare `int gridTicks` at every editor call site; for non-Swing grids each function reproduces today's `GridSnap.h` maths exactly. The roll owns length / division / strength and builds the grid **on demand** from the same `meterChangesOf(meterMap)` its drawing reads, and `SourceRollEditor` pulls it through a provider — there is no meter-change hook, so nothing is cached and a meter edit or undo can never leave snap targets out of line with the drawn lines.

**Tech Stack:** C++20, JUCE (UI only), Catch2, CMake/Ninja.

**Spec:** `docs/superpowers/specs/2026-10-16-songsmith-editor-swing-design.md` (approved 2026-10-16, all ten "Claude's calls" included).

## Global Constraints

- Strength scale: integer percent −100..+100, 0 = straight; `w = percent / 100.0`; odd line shift `w · s / 2`. Copy REAPER; no other scale.
- Swung lines per bar `[B, E)`: `B + j·s` (j even), `B + j·s + w·s/2` (j odd), while `B + j·s < E`; each rounded with `std::llround`; an odd line whose unrounded position is `>= E` is dropped. `E = min(B + ticksPerBar, next segment's startTick)`.
- Per-bar origin applies **only** to `GridDivision::Swing`. Straight / Triplet / Dotted keep multiples of the spacing from tick 0, bit-for-bit as `snapTickToGrid` / `stepTickByGrid` today.
- `s` = `editorGridTicks(length, division, ppq)` (Swing spaces as Straight — unchanged).
- Session-only: never saved in the Song, Preferences or `AppSettings`. No change to notes unless the user moves / resizes / inserts / quantizes.
- Nothing in `Source/Core/` (`forge-engine-ui-boundary`). No new dependencies.
- Alt frees every snap; Snap to Grid off means no snap — unchanged.
- Main window's Edit ▸ Grid Size, Preferences default grid, export: untouched (no swing).
- Commit trailer: `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`. Never push.

## Review Focus

1. **Meter edit / undo while the editor is open** — the next snap uses the new bars, matching the drawn lines. Pinned in Task 3 (`getEditorGrid` reads the live METER_MAP node).
2. **Strength kept but inert off Swing** — setting +100 % then switching to Straight gives plain 240-tick steps; switching back to Swing restores the swing. Pinned in Task 4.
3. **Grid Length set to Off (incl. from main-window Edit ▸ Grid Size) while on Swing** greys the slider; choosing a length again enables it. Pinned in Task 5.
4. **Keyboard after touching the slider** — Left/Right/Space still reach the editor (slider takes no keyboard focus). Pinned in Task 5.
5. **Double-click insert on the last, short step of a bar** (+100 %, 1/8, click at 1850) makes a 120-tick note ending on the bar line, not a 240-tick one. Pinned in Task 3.

## Plan-level decisions (beyond the spec)

- **Unsnapped double-click insert** (Snap to Grid off or Alt) keeps today's length: the grid spacing `s` (quarter note with no grid). Only a snapped insert runs "to the next swung line". For non-Swing grids the snapped rule gives `s` too, so today's tests hold.
- `snapLoopTick` keeps its `int` overload; a new `EditorGrid` overload is used by the loop call sites (non-swung: same unclamped rounding as today).

## Tick fixtures (PPQ 480, 4/4 unless stated; checked by hand)

- 1/8 → `s = 240`. **+100 %** bar 0 lines: `0, 360, 480, 840, 960, 1320, 1440, 1800`, next bar `1920`. **−100 %**: first odd line `120`, then `600`. **+67 %**: `240 + 0.67·120 = 320.4 → 320` (exactly 2:1).
- 3/4 at 1/4 (`s = 480`), +100 %: `0, 720, 960 | 1440, 2160, 2400 | 2880`.
- 5/8 at 1/4, +100 % (bar 1200): `0, 720, 960 | 1200, 1920, 2160 | 2400`.
- 3/4 then 4/4 from tick 1440, 1/4, +100 %: `0, 720, 960 | 1440, 2160, 2400, 3120 | 3360` (1440 is a downbeat, not shifted).
- 5/16 at 1/4 (bar 600): +50 % → odd line at 600 is dropped: `0 | 600 | 1200`; +25 % → `0, 540 | 600, 1140 | 1200`.
- PPQ 97, 1/8: `s = 49` (`lround(48.5)`); +100 % first odd line `49 + 24.5 = 73.5 → 74`.
- Tie: tick `180` at +100 %, 1/8 → snaps to `360` (later line, as `std::round`).
- Min gap: +100 % 1/8 → `120`; −67 % → `240·(1−0.335) = 159.6`.

---

### Task 1: `EditorGrid` — the pure swung-grid maths

**Files:**
- Create: `Source/UI/EditorGrid.h`, `Source/UI/EditorGrid.cpp`
- Create: `Tests/EditorGrid_tests.cpp`
- Modify: `CMakeLists.txt` (add `Source/UI/EditorGrid.cpp` beside `Source/UI/GridLines.cpp`, ~line 141); `Tests/CMakeLists.txt` (add `EditorGrid_tests.cpp` beside `GridLines_tests.cpp` ~line 34, and `${CMAKE_SOURCE_DIR}/Source/UI/EditorGrid.cpp` beside `GridLines.cpp` ~line 149)

**Interfaces:**
- Consumes: `GridLength`, `GridDivision`, `editorGridTicks` (`GridLength.h`); `MeterSegment`, `meterSegments`, `segmentAt` (`MeterSegments.h`); `snapTickToGrid`, `stepTickByGrid` (`GridSnap.h`).
- Produces (namespace `lotro`, no JUCE):

```cpp
constexpr int minSwingPercent = -100, maxSwingPercent = 100;
constexpr int clampSwingPercent (int percent) noexcept;

struct EditorGrid
{
    int spacingTicks = 0;              // 0 = no grid (every function returns the tick unchanged / no lines)
    bool swung = false;                // GridDivision::Swing: pairs restart at each bar line
    double swing = 0.0;                // -1..1, read only when swung
    std::vector<MeterSegment> bars;    // swung only; never empty when swung
    static EditorGrid straight (int spacingTicks);
};

EditorGrid makeEditorGrid (GridLength, GridDivision, int swingPercent, int ticksPerQuarter,
                           const std::vector<MeterChange>& meters);
double editorGridLineAtOrBefore (const EditorGrid&, double tick);   // last line <= tick; 0 for tick < 0
double editorGridLineAfter (const EditorGrid&, double tick);        // first line > tick
double snapToEditorGrid (const EditorGrid&, double tick);           // nearest line, ties to the later, >= 0
double stepEditorGrid (const EditorGrid&, double tick, int direction); // next (>0) / previous (<0) line, floor 0
std::vector<int> editorGridLinesIn (const EditorGrid&, int firstTick, int lastTick); // sorted, inclusive
double editorGridMinGap (const EditorGrid&);                        // s·(1−|w|/2) swung, s otherwise, 0 off
std::string swingPercentText (int percent);                          // "+67%", "0%", "-25%"
```

- [ ] **Step 1: Write the failing tests** (`[editor-grid][swing]`), using the fixtures above:
  - `"EditorGrid: non-Swing grids match GridSnap exactly"` — for spacings `{1, 7, 160, 240, 480}` and ticks `-100..3000` step 7: `snapToEditorGrid(straight(s), t) == snapTickToGrid(t, s)`, `stepEditorGrid(.., ±1) == stepTickByGrid(t, s, ±1)`, `editorGridLineAfter == stepTickByGrid(t, s, +1)`, `editorGridLineAtOrBefore == max(0, floor(t/s)·s)`; and `makeEditorGrid(Eighth, Triplet, 100, 480, {})` is `spacingTicks == 160 && ! swung`.
  - `"EditorGrid: off leaves every tick"` — `makeEditorGrid(Off, Swing, 50, 480, {})`: spacing 0, snap/step/lineAfter/lineAtOrBefore of 1000 return 1000, `editorGridLinesIn(.., 0, 2000)` empty; PPQ 0 the same.
  - `"EditorGrid: Swing at 0% in 4/4 equals Straight"` — `editorGridLinesIn(make(Eighth, Swing, 0, 480, {}), 0, 3840)` equals multiples of 240.
  - `"EditorGrid: +100% and -100% at 1/8"` — `linesIn(0, 1920) == {0,360,480,840,960,1320,1440,1800,1920}`; −100 % `lineAfter(0) == 120`, `lineAfter(480) == 600`.
  - `"EditorGrid: +67% gives a 2:1 pair"` — `lineAfter(0) == 320`, `lineAfter(320) == 480`.
  - `"EditorGrid: pairs restart at each bar line"` — the 3/4, 5/8 and 3/4→4/4 fixtures, each via `linesIn`.
  - `"EditorGrid: an odd line at or past the bar end is dropped"` — the 5/16 fixtures (+50 % and +25 %).
  - `"EditorGrid: rounding at odd PPQ"` — PPQ 97, 1/8, +100 %: `spacingTicks == 49`, `lineAfter(0) == 74`.
  - `"EditorGrid: snap, step and the cell edges on a swung grid"` — +100 % 1/8: `snap(500)==480`, `snap(700)==840`, `snap(180)==360`, `snap(1900)==1920`, `snap(-50)==0`; `step(400,+1)==480`, `step(400,-1)==360`, `step(360,-1)==0`, `step(1800,+1)==1920`, `step(1920,-1)==1800`, `step(0,-1)==0`; `lineAtOrBefore(359)==0`, `lineAtOrBefore(360)==360`, `lineAtOrBefore(-5)==0`, `lineAfter(360)==480`, `lineAfter(-5)==0`.
  - `"EditorGrid: min gap and strength clamp"` — `minGap` 120 at +100 %, 159.6 (Approx) at −67 %, 240 straight; `make(.., 250, ..)` gives `swing == 1.0`; `clampSwingPercent(-300) == -100`.
  - `"EditorGrid: swing readout text"` — `"+67%"`, `"0%"`, `"-25%"`, `"+100%"`.

- [ ] **Step 2: Run to verify they fail**
  Run: `cmake --build build && ctest --test-dir build -R "EditorGrid" --output-on-failure`
  Expected: build fails (`EditorGrid.h` missing).

- [ ] **Step 3: Implement `Source/UI/EditorGrid.{h,cpp}`**
  - `makeEditorGrid`: `spacingTicks = editorGridTicks(...)`; `swung = division == Swing && spacingTicks > 0`; when swung, `bars = meterSegments(meters, ppq)` and `swing = clampSwingPercent(p) / 100.0`.
  - Non-swung (or `bars` empty): delegate to `GridSnap.h` / the floor formula — this is what makes the equivalence test exact.
  - Swung: never scan from tick 0. A private helper finds the bar containing `max(tick, 0)` with `segmentAt` + `floor((t − seg.startTick) / seg.ticksPerBar)`, sets `E = min(B + ticksPerBar, next segment start)` (a meter change can cut a bar short), and lists that bar's lines per Global Constraints. `lineAtOrBefore` = last of them `<= t`; `lineAfter` = first `> t`, else `llround(E)`; `snap` = nearer of those two (tie → after); `step(+1)` = `lineAfter`, `step(−1)` = last line `< t` (go to the previous bar when `t` is on `B`), floor 0. `linesIn` walks bars from the one containing `firstTick` until `B > lastTick`.

- [ ] **Step 4: Run to verify they pass**
  Run: `cmake --build build && ctest --test-dir build -R "EditorGrid|GridSnap|MeterSegments" --output-on-failure`
  Expected: all PASS.

- [ ] **Step 5: Commit**
  `git add Source/UI/EditorGrid.* Tests/EditorGrid_tests.cpp CMakeLists.txt Tests/CMakeLists.txt && git commit -m "feat(ui): EditorGrid, the MIDI editor's grid with REAPER-style swing"`

---

### Task 2: Swung lines in `computeGridLines`

**Files:**
- Modify: `Source/UI/GridLines.h` (`EditorGridLines`), `Source/UI/GridLines.cpp` (editor-grid overload)
- Test: `Tests/GridLines_tests.cpp`

**Interfaces:**
- Consumes: `EditorGrid`, `editorGridLinesIn`, `editorGridMinGap` (Task 1).
- Produces: `struct EditorGridLines { int spacingTicks = 0; GridLevel level = GridLevel::Quarter; bool swung = false; double swing = 0.0; };` — existing two-field aggregate inits keep working.

- [ ] **Step 1: Write the failing tests** (`[grid-lines][swing]`)
  - `"GridLines: a swung editor grid draws bar lines and the swung lines only"` — PPQ 480, 4/4, `pixelsPerTick 0.2`, `EditorGridLines { 240, GridLevel::Eighth, true, 1.0 }`, range `[0, 1920]`: ticks exactly `{0,360,480,840,960,1320,1440,1800,1920}`; 0 and 1920 are `Bar`, 960 is `Eighth` (no Half/Quarter levels).
  - `"GridLines: swung lines need their smallest gap to be minGridLinePixels"` — same grid at `pixelsPerTick 0.05` (gap 120 → 6 px): only `{0, 1920}`; at 0.07 (8.4 px) the swung lines are back.
  - `"GridLines: swung pairs restart at a meter change"` — 3/4 then 4/4 at 1440, `{ 480, Quarter, true, 1.0 }`, ppt 0.2, `[0, 3360]`: `{0,720,960,1440,2160,2400,3120,3360}`.

- [ ] **Step 2: Run to verify they fail**
  Run: `cmake --build build && ctest --test-dir build -R "GridLines" --output-on-failure`
  Expected: compile error (no `swung` member) or FAIL.

- [ ] **Step 3: Implement** — in the editor-grid overload, when `editorGrid.swung && spacingTicks > 0`: skip the note-value `divisionLevels` loop, build `EditorGrid { spacingTicks, true, swing, segments }` from the segments already computed, and if `editorGridMinGap · pixelsPerTick >= minGridLinePixels` emplace each `editorGridLinesIn(grid, firstTick, lastTick)` tick at `editorGrid.level` (bar lines were added first, so they keep `Bar`). `editorGridLinesIn` is the only generator of swung lines.

- [ ] **Step 4: Run to verify they pass**
  Run: `cmake --build build && ctest --test-dir build -R "GridLines|PianoRollComponent" --output-on-failure`
  Expected: PASS.

- [ ] **Step 5: Commit** — `feat(ui): swung editor grid lines`

---

### Task 3: The roll and `SourceRollEditor` use the swung grid

**Files:**
- Modify: `Source/UI/PianoRollComponent.{h,cpp}` (`setGrid`, `setGridTicks`, new `setSwing` / `getSwing` / `getEditorGrid`, `getGridTicks`, `handleEditorMouseUp` ~512, `drawGridlines` ~880, ctor ~29)
- Modify: `Source/UI/SourceRollEditor.{h,cpp}` (`setGridTicks` / `getGridTicks` ~53, `snapsWith` ~206, move ~246, resize ~280/~295, `quantizeSelection` ~383, `createNoteAt` ~427)
- Test: `Tests/SourceRollEditor_tests.cpp`, `Tests/PianoRollComponent_tests.cpp`

**Interfaces:**
- Consumes: Task 1 functions; Task 2 `EditorGridLines`.
- Produces:
  - `SourceRollEditor::setGridSource (std::function<EditorGrid()>)`; `EditorGrid grid() const` (the source's grid, or no grid without one); `setGridTicks(int t)` kept as a shim = `setGridSource([t] { return EditorGrid::straight (t); })`; `getGridTicks()` = `grid().spacingTicks`.
  - `PianoRollComponent::setSwing (int percent)` (clamped, repaints the canvas, reports nothing); `int getSwing() const noexcept` (default 0); `EditorGrid getEditorGrid() const` — `makeEditorGrid(length, division, swing, ppq, SongDocument::meterChangesOf(meterMap))` when the length is not Off, else `EditorGrid::straight(rawGridTicks)`; `getGridTicks()` = `getEditorGrid().spacingTicks` (0 without an editor, as today).
  - The roll's ctor installs `sourceEditor->setGridSource ([this] { return getEditorGrid(); })`; `setGridTicks(ticks)` stores `rawGridTicks` and sets the length Off; `setGrid` sets `rawGridTicks = 0`. No grid is cached anywhere.

- [ ] **Step 1: Write the failing tests**
  - SourceRollEditor (`[source-roll-editor][swing]`), each with `setSnapToGrid(true)` and a source returning `makeEditorGrid(Eighth, Swing, 100, ticksPerQuarter, {})` (the fixture's `ticksPerQuarter` is 480, so the tick fixtures apply as written):
    - `"a moved note's start lands on the nearest swung line"` — note at 0 dragged to x for 700 → start 840.
    - `"resize right ends on a swung line, at least one line from the start"` — note 480..960 dragged right edge to 500 → duration 360 (end 840).
    - `"resize left starts on a swung line, at most one line before the end"` — note 480..960 left edge dragged to 950 → start 840.
    - `"a snapped double-click insert runs to the next swung line"` — click at 400 → start 360, duration 120; click at 1850 → start 1800, duration 120 (Review Focus 5).
    - `"an unsnapped insert keeps the grid spacing as its length"` — snap off, click at 400 → start 400, duration 240.
    - `"quantize moves starts to the nearest swung line and keeps the duration rule"` — note start 700 dur 250 → start 840, dur 240.
  - PianoRollComponent (`[piano-roll][swing]`):
    - `"the editor grid follows a meter edit with no setGrid call"` — doc with a source node, `setGrid(Quarter, Swing)`, `setSwing(100)`: `editorGridLineAfter(roll.getEditorGrid(), 960) == 1680`; replace the METER_MAP children with a single 3/4 at 0 → `== 1440` (Review Focus 1).
    - `"a swung 1/8 grid draws its shifted lines and not the straight odd eighths"` — copy the Triplet pixel test at line 1499: `setGrid(Eighth, Swing)`, `setSwing(100)`; `lineAt(360)`, `lineAt(480)` true; `lineAt(240)` false.
    - `"setSwing clamps and reads back"` — `setSwing(150)` → `getSwing() == 100`.

- [ ] **Step 2: Run to verify they fail**
  Run: `cmake --build build && ctest --test-dir build -R "SourceRollEditor|PianoRollComponent" --output-on-failure`
  Expected: compile errors (`setGridSource`, `setSwing` missing).

- [ ] **Step 3: Implement**
  - `snapsWith`: `grid().spacingTicks > 0`. Read `grid()` once per gesture step into a local.
  - Move: `snapToEditorGrid(g, anchor + delta) − anchor`. ResizeRight: `max(snap(start + dur), stepEditorGrid(g, start, +1))`. ResizeLeft: `min(snap(newStart), stepEditorGrid(g, end, −1))`.
  - `quantizeSelection`: start `= (int) snapToEditorGrid(g, start)`; duration rule unchanged on `g.spacingTicks`.
  - `createNoteAt`: snapped → start `editorGridLineAtOrBefore`, duration `editorGridLineAfter(start) − start`; unsnapped → start = tick, duration `spacingTicks > 0 ? spacingTicks : ticksPerQuarter`.
  - Roll marker click: `snapToEditorGrid(getEditorGrid(), tick)`. `drawGridlines`: `EditorGridLines { spacing (0 when Off), gridLevelForLength(length), gridDivision == GridDivision::Swing, getSwing() / 100.0 }`.

- [ ] **Step 4: Run to verify they pass** — `ctest --test-dir build --output-on-failure` (full suite: the `setGridTicks` shim must keep every existing test green).

- [ ] **Step 5: Commit** — `feat(ui): the MIDI editor snaps, inserts and quantizes to the swung grid`

---

### Task 4: Ruler, marker steps and loop snap follow the swung grid

**Files:**
- Modify: `Source/UI/TrackEditorPanel.{h,cpp}` (ruler `onSetMarker` ~300, `onLoopEdgeDragged` ~320, `stepMarker` ~436; new `setSwing` / `getSwing` / `getEditorGrid`)
- Modify: `Source/UI/Playback/LoopRange.{h,cpp}` (new overload)
- Modify: `Source/UI/TrackListComponent.{h,cpp}` (`loopSnapTicks` ~337 → `loopSnapGrid`, `snapLoop` ~202)
- Modify: `Source/UI/SongsmithMainComponent.{h,cpp}` (`currentLoopSnapTicks` ~320 → `currentLoopSnapGrid`, wiring ~122)
- Modify tests: `Tests/TrackEditorPanel_tests.cpp`, `Tests/LoopRange_tests.cpp`, `Tests/SongsmithMainComponent_tests.cpp:1236-1255`, `Tests/TrackListComponent_tests.cpp:3283,3359,3364`

**Interfaces:**
- Consumes: `PianoRollComponent::getEditorGrid / setSwing / getSwing` (Task 3).
- Produces:
  - `double snapLoopTick (double tick, const EditorGrid& grid, bool unsnapped) noexcept;` — unsnapped or no grid → tick; swung → `snapToEditorGrid`; otherwise the `int` overload's rounding (unclamped, as today). The `int` overload stays.
  - `TrackListComponent::loopSnapGrid` : `std::function<EditorGrid()>` (replaces `loopSnapTicks`).
  - `SongsmithMainComponent::currentLoopSnapGrid() const -> EditorGrid` — the editor's `getEditorGrid()` when loaded, else `EditorGrid::straight(gridSizeToTicks(defaultGridSize, ppq))`.
  - `TrackEditorPanel::setSwing (int)` (to the roll; Task 5 adds the slider sync), `int getSwing() const noexcept`, `EditorGrid getEditorGrid() const`.

- [ ] **Step 1: Write the failing tests** (`MarkerPanelFixture`, PPQ 480, default 4/4; `[track-editor-window][swing]`)
  - `"Move Marker by Grid steps along the swung lines"` — `setGridLength(Eighth)`, `setGridDivision(Swing)`, `setSwing(100)`, marker 0: Right → 360, Right → 480, Left → 360.
  - `"a ruler click snaps the marker to the nearest swung line"` — `rulerClick(700)` → 840.
  - `"a dragged ruler bracket snaps to the swung grid"` — as the bracket test at line 342, drag an edge to 700 → that edge 840.
  - `"strength is kept but inert off Swing"` (Review Focus 2) — `setSwing(100)`, `setGridDivision(Straight)`: marker 0 Right → 240, `getSwing() == 100`; back to Swing: from 0 Right → 360.
  - `SongsmithMainComponent_tests` loop snap: existing `list.loopSnapTicks() == 120/30/60` become `list.loopSnapGrid().spacingTicks == …`; add: editor on 1/8 Swing at +100 % → `snapLoopTick(700, list.loopSnapGrid(), false) == 840`.
  - `snapLoopTick` overload: straight(120) matches the `int` overload for a few ticks including negative; swung +100 % 1/8: 700 → 840; unsnapped → unchanged.

- [ ] **Step 2: Run to verify they fail** — `cmake --build build && ctest --test-dir build -R "TrackEditorPanel|LoopRange|SongsmithMainComponent|TrackListComponent" --output-on-failure`; Expected: compile errors.

- [ ] **Step 3: Implement** — swap the four call sites to the grid functions; update `TrackListComponent_tests.cpp:3283,3364` to `[] { return EditorGrid::straight (120); }` and 3359 to `loopSnapGrid = nullptr` (`snapLoop` treats a null provider as no grid). `canStepMarker` stays on `getGridTicks() > 0`.

- [ ] **Step 4: Run to verify they pass** — full `ctest --test-dir build --output-on-failure`.

- [ ] **Step 5: Commit** — `feat(ui): marker steps, ruler clicks and loop snap follow the swung grid`

---

### Task 5: Swing slider on the bottom bar, reset rule and docs

**Files:**
- Modify: `Source/UI/EditorBottomBar.h`; Create: `Source/UI/EditorBottomBar.cpp` (add to `CMakeLists.txt` and the `Tests/CMakeLists.txt` sources list)
- Modify: `Source/UI/TrackEditorPanel.{h,cpp}` (ctor wiring, `setSwing`, `refreshSwingControl`, calls from `setTrack`, `clearTrack`, `setGridLength`, `setGridDivision`)
- Modify: `Source/UI/SongsmithMainComponent.cpp:233` (`trackEditor->setSwing (0);` in the `! keepsGrid` branch, beside `setGridDivision (GridDivision::Straight)` — **not** in `clearTrack`)
- Modify: `Source/UI/GridLength.h:81` comment; docs below
- Test: `Tests/TrackEditorPanel_tests.cpp`, `Tests/SongsmithMainComponent_tests.cpp`

**Interfaces:**
- Consumes: `swingPercentText`, `clampSwingPercent` (Task 1); `TrackEditorPanel::setSwing` (Task 4).
- Produces (`EditorBottomBar`):
  - `std::function<void (int percent)> onSwingChanged;` fired on user change only.
  - `void setSwing (int percent);` — slider + readout, no callback. `void setSwingEnabled (bool);` — slider, label and readout greyed but visible.
  - `juce::Slider& swingSliderForTesting() noexcept;` `juce::String swingReadoutText() const;`
  - Children: `juce::Label` "Swing", `juce::Slider` (`LinearHorizontal`, `NoTextBox`, range −100..100 step 1, value 0, `setDoubleClickReturnValue (true, 0.0)`, **`setWantsKeyboardFocus (false)`** as the transport's `rateSlider`), readout `juce::Label`. `resized()`: 8 px left margin, label 44 px, slider 160 px, 4 px gap, readout 44 px, all full height.
- Panel: `refreshSwingControl()` enables iff `currentTrackId != -1 && getGridDivision() == GridDivision::Swing && getGridLength() != GridLength::Off`. `setSwing` updates roll and bar.

- [ ] **Step 1: Write the failing tests** (`[track-editor-window][bottom-bar][swing]`)
  - `"the swing slider sits at the bar's left, 160 px wide, -100..100 step 1, starting at 0"` — bounds inside `bottomBar` with `getX() == 52`, width 160; range/interval; value 0; `swingReadoutText() == "0%"`.
  - `"moving the slider sets the editor's swing and its readout"` — with a track, Eighth + Swing: `slider.setValue (67, juce::sendNotificationSync)` → `panel.getSwing() == 67`, readout `"+67%"`, `editorGridLineAfter(panel.getEditorGrid(), 0) == 320`.
  - `"the slider resets on double-click and never takes keyboard focus"` — `isDoubleClickReturnEnabled()` and `getDoubleClickReturnValue() == 0.0`; `! getWantsKeyboardFocus()` (Review Focus 4).
  - `"the slider is enabled only with a track, Swing and a grid length"` — greyed with no track; greyed on Straight; enabled on Swing; greyed after `setGridLength(Off)`; enabled again after `setGridLength(Quarter)` (Review Focus 3); greyed after `clearTrack()`.
  - `"panel setSwing moves the slider without reporting"` — `setSwing(-25)` → slider −25, readout `"-25%"`.
  - SongsmithMainComponent (`[songsmith][swing]`), next to the existing division-reset test: swing 50 kept when another track is double-clicked while the editor shows a track; reset to 0 when the editor is opened afresh after New/Open (`documentReplaced`) or while hidden.

- [ ] **Step 2: Run to verify they fail** — `cmake --build build && ctest --test-dir build -R "TrackEditorPanel|SongsmithMainComponent" --output-on-failure`; Expected: compile errors.

- [ ] **Step 3: Implement** the bar, the panel wiring (`bottomBar.onSwingChanged = [this] (int p) { roll.setSwing (p); }`), `refreshSwingControl()` calls, and the reset line.

- [ ] **Step 4: Run the full suite** — `cmake --build build && ctest --test-dir build --output-on-failure`; Expected: all pass (the known `TransportControls: Enter gives keyboard focus away` skip excepted). Record the count from `ctest --test-dir build -N | tail -1`.

- [ ] **Step 5: Docs**
  - `Source/UI/GridLength.h:81`: Swing spaces as Straight; the swing shift is `EditorGrid`'s.
  - `docs/UI_GUIDE.md:325` (Swing is a real swung grid; strength on the bottom bar) and #28 (the bottom bar's Swing slider).
  - `docs/ARCHITECTURE.md` §9.19 TrackEditorPanel bullet: replace "Swing as Straight for now" and the bottom bar's "holds no controls yet" with `EditorGrid` (on-demand build from the meter map, provider into `SourceRollEditor`, call sites, `snapLoopTick` overload, `loopSnapGrid`), the slider and the reset rule.
  - `docs/TESTING.md`: count and a line on `EditorGrid_tests.cpp` (`[editor-grid][swing]`).
  - `CLAUDE.md` status line: "Swing — spaced as Straight for now" → Swing with a strength slider on the bottom bar (REAPER scale, pairs restart per bar).

- [ ] **Step 6: Commit** — `feat(ui): Swing strength slider on the MIDI editor's bottom bar`
