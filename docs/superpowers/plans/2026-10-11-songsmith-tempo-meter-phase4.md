# Songsmith Tempo/Meter Editor Window — Graph and Meter Strip (phase 4) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add the spec's §5 graph half to the Tempo/Meter window: a tempo graph (stepped BPM line, draggable points) and a meter strip (labelled blocks), both on the **main timeline's** zoom/scroll, selection-synced with the table, every edit going through `TempoEdit`.

**Architecture:** A JUCE-free geometry module (`TempoMeterGraphModel`: BPM↔y, beat/bar snapping, point hit-test) and four thin edit helpers in `TempoMeterTable` (add/move for tempo and meter, re-finding by `(kind, tick)` like the existing edits) carry all the logic. Two paint-and-mouse components (`TempoGraphComponent`, `MeterStripComponent`) read a `const TimelineViewState&` owned by `TrackListComponent` (so the window's x-axis mirrors the main timeline — the user's choice over an independent zoom) and repaint when `TrackListComponent` fires a new `onViewChanged`. Drags change nothing in the document until mouse-up, when exactly one `TempoEdit` call (= one undo step) commits; the window supplies the event list and routes selection both ways.

**Tech Stack:** C++17, JUCE (`Component`, mouse events), Catch2. No new dependencies.

**Spec:** `docs/superpowers/specs/2026-10-10-songsmith-tempo-meter-design.md` (§5 Graph, §6 phase 4). Phase 3 plan for style reference: `docs/superpowers/plans/2026-10-11-songsmith-tempo-meter-phase3.md`.

## Global Constraints

- Everything lives in `Source/UI/`; `Source/Core/` and `forge_core` are untouched (spec requirement 4).
- Neither the model nor the components write the ValueTree: every edit goes through `TempoEdit` (`setTempo` / `moveTempo` / `setMeter` / `moveMeter`), via the `TempoMeterTable` helpers of Task 2. Each successful call is one undo transaction; a rejected one opens none.
- Notes never move (spec requirement 2). A drag previews only; the document is untouched until mouse-up.
- Events are addressed by tick, never by index; the components re-find on commit (`TempoMeterTable`'s `findCurrent`).
- BPM range `minBpm() .. 1000` rejected not clamped by `TempoEdit`; the graph itself clamps what it *proposes* to `[ceil(minBpm()), maxBpm]` so a drag can never produce a rejected value. Meters: numerator 1–32, denominator in {1,2,4,8,16,32}; a meter change snaps to a bar start; tick-0 events can be edited, not removed or moved; one tempo and one meter per tick.
- Custom error types, no bare strings as errors; add types/annotations everywhere. Prefer free functions over classes where practical.
- Use `juce::GlyphArrangement::getStringWidthInt`, never `juce::Font::getStringWidth` (MSVC C4996; see `PartSlotComponent.cpp:63`).
- `PlayheadOverlay` repaints at 30 Hz elsewhere; the new components repaint only on document change, selection change, view change and drag — never on a timer.
- Do not launch the GUI from tests or agents; headless component tests only (`juce::ScopedJuceInitialiser_GUI`).
- Commits: conventional-commit prefixes, ending with `Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>`. Never push.
- New `.cpp`/`.h` files go in **both** `CMakeLists.txt` (UI sources, next to `Source/UI/TempoMeterEditorWindow.cpp`) and `Tests/CMakeLists.txt` (source list next to the same file, plus any new `*_tests.cpp`).
- Existing `TempoMeterEditorWindow` tests construct `TempoMeterEditorWindow (doc, nullptr)` with no timeline view: with no view the window must look and lay out **exactly** as it does today (graph and strip hidden, table fills the content).
- Test fixtures: `Tests/MidiTestBytes.h` (`miditest::smf`, `TrackBody`) + `importMidiFile`, as `Tests/TempoMeterEditorWindow_tests.cpp` (`standardSong`, `importStandard`, `addThreeTempos`-style helpers).

## Review Focus

Failure modes the spec implies but a straight implementation would miss (each pinned by a test in the task named in brackets):

1. A click on a point that does not move creates **no** undo step and no edit; a drag that ends where it began likewise. [Task 5]
2. Dragging a tempo onto a tick that already holds a tempo shows the `TickOccupied` message and changes nothing (no undo step); the preview disappears. [Task 5]
3. The tick-0 tempo and meter drag vertically only (tempo) / not at all (meter) and the implicit tick-0 default (no event behind it) can still have its BPM changed by drag. [Task 5, Task 6]
4. The y-axis does not rescale under the pointer mid-drag (range frozen at mouse-down); a drag released above/below the component or left of tick 0 clamps (BPM within `[ceil(minBpm()), 1000]`, tick ≥ 0) and never proposes a value `TempoEdit` rejects. [Task 1, Task 5]
5. Undo/redo or File ▸ New while a drag is in progress, or the dragged event vanishing from `setEvents`, aborts the drag cleanly (no crash, no stale commit). [Task 5]
6. Extreme zoom (min and max `pixelsPerTick`) and two points within a pixel of each other: hit-testing stays pixel-based (6 px), the later point wins a tie, snapping stays on beats/bars. [Task 1]
7. No time base (nothing imported): graph and strip draw nothing, clicks add nothing and show the `NoTimeBase` message. [Task 4, Task 5]
8. A tempo and a meter on the same tick: selecting the tempo point selects only the tempo row. [Task 4]
9. The window outlives a main-timeline zoom/scroll/fit: the graph follows it (and never reads a destroyed `TimelineViewState`). [Task 3, Task 4]

## File Structure

| File | Responsibility |
|---|---|
| NEW `Source/UI/TempoMeterGraphModel.{h,cpp}` | JUCE-free: `BpmRange`, `bpmRangeFor`, `bpmToY`/`yToBpm`, `snapToBeat`, `snapMeterTick`, `nearestPoint` |
| MOD `Source/UI/TempoMeterTable.{h,cpp}` | `setTempoAt`, `moveTempoTo`, `setMeterAt`, `moveMeterTo` over `TempoEdit` |
| MOD `Source/UI/TrackListComponent.{h,cpp}` | `getTimelineView()`, `onViewChanged` (fired from `refreshOverlay`) |
| NEW `Source/UI/TempoGraphComponent.{h,cpp}` | Paints the stepped tempo line + points + y labels; select / add / drag; commits on mouse-up |
| NEW `Source/UI/MeterStripComponent.{h,cpp}` | Paints meter blocks; select / add / drag; commits on mouse-up |
| MOD `Source/UI/TempoMeterEditorWindow.{h,cpp}` | Hosts graph + strip above the table when a timeline view is set; `setTimelineView`, `timelineViewChanged`; selection both ways; feeds events and messages |
| MOD `Source/UI/SongsmithMainComponent.{h,cpp}` | Hands the window `&trackList.getTimelineView()`, wires `onViewChanged` |
| MOD `Source/UI/SongsmithColours.h` | Graph colours (reuse `conductorTempo` / `conductorMeter` / `eventMarker`; add only what is missing) |
| NEW `Tests/TempoMeterGraphModel_tests.cpp`; additions to `Tests/TempoMeterTable_tests.cpp`, `Tests/TrackListComponent_tests.cpp`, `Tests/TempoMeterEditorWindow_tests.cpp`; NEW `Tests/TempoGraphComponent_tests.cpp`, `Tests/MeterStripComponent_tests.cpp` | |

## Decisions made for the user (veto any)

- **The graph mirrors the main timeline** (same `pixelsPerTick` and scroll offset; user's choice). The window has no zoom/scroll of its own: zoom or scroll the main timeline and the graph follows. A tick the main view shows beyond the window's width is simply clipped.
- Graph y-axis is **linear BPM**, auto-ranged from the song's tempos (min × 0.75 … max × 1.25, at least 40 BPM tall), with gridlines/labels at round values. Dragging changes BPM in **whole BPM steps** (fractions via the table).
- Horizontal drags and click-to-add snap to the **beat** (tempo) or **bar** (meter, by `TempoEdit`). Dragging past the song end is allowed (tempo events beyond the last note are legal).
- Click on empty graph space adds a tempo at that beat with the clicked BPM; click on empty strip space adds a meter at that bar with the meter currently in force there. Click on a point/block selects it (replacing the selection); Delete (already window-level) removes the selected event(s). No Ctrl-click in the graph (the table does multi-select).
- **Adding a meter is a double-click on the strip** (a single click only selects a block), matching the earlier rule that single-click on shared rows stays inert; the spec's "click adds" is thereby tightened. Adding a tempo on the graph is a single click on empty space, as the spec says.
- Drag threshold 3 px; point hit radius 6 px.

---

### Task 1: `TempoMeterGraphModel` (pure geometry)

**Files:**
- Create: `Source/UI/TempoMeterGraphModel.h`, `Source/UI/TempoMeterGraphModel.cpp`
- Test: `Tests/TempoMeterGraphModel_tests.cpp`
- Modify: `CMakeLists.txt`, `Tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `TempoMeterEvent`, `TempoMeterKind`, `minBpm()`, `maxBpm` (`UI/TempoEdit.h`); `MeterSegment`, `meterSegments`, `segmentAt`, `nearestBarStart` (`UI/MeterSegments.h`); `MeterChange` (`Core/Song.h`).
- Produces (all in namespace `lotro`):
  ```cpp
  struct BpmRange { double lo = 60.0; double hi = 180.0; };
  // Range for the tempo events in `events` (other kinds ignored); see Decisions. Empty -> {60, 180}.
  BpmRange bpmRangeFor (const std::vector<TempoMeterEvent>& events);
  // y grows downward: bpm == hi -> plotTop, bpm == lo -> plotTop + plotHeight (rounded).
  int    bpmToY (const BpmRange&, double bpm, int plotTop, int plotHeight);
  // Inverse, rounded to a whole BPM and clamped to [ceil(minBpm()), maxBpm].
  double yToBpm (const BpmRange&, int y, int plotTop, int plotHeight);
  // Nearest beat start of the segment containing `tick`; never < 0, never past the segment end.
  int    snapToBeat (const std::vector<MeterSegment>&, int tick);
  // Nearest bar start of the meters in force WITHOUT the one at `fromTick`; tick >= 0.
  int    snapMeterTick (std::vector<MeterChange> meters, int ticksPerQuarter, int fromTick, int tick);
  // Index of the point whose (x,y) is within radiusPx (Euclidean) of (x,y); nearest wins, ties go to the
  // LATER index. nullopt when none.
  std::optional<size_t> nearestPoint (const std::vector<std::pair<int,int>>& xy, int x, int y, int radiusPx);
  ```

- [ ] **Step 1: Write the failing tests** in `Tests/TempoMeterGraphModel_tests.cpp` (tag `[tempo-graph-model]`):
  - `bpmRangeFor`: empty -> `{60, 180}`; one tempo 120 -> lo = 90, hi = 150 widened to at least 40 tall (here 60 tall: unchanged); one tempo 200 -> `{150, 250}`; tempos 100 and 101 -> height widened symmetrically to exactly 40 around 100.5×… (assert `hi - lo == 40` and `lo < 100 && hi > 101`); lo never below `1.0`; meter events ignored.
  - `bpmToY`/`yToBpm`: for `{60, 180}`, plotTop 10, height 120: `bpmToY(180) == 10`, `bpmToY(60) == 130`, `bpmToY(120) == 70`; `yToBpm(70) == 120.0`; `yToBpm(69)` rounds to a whole number (`== std::round (yToBpm (…))`); `yToBpm (-5000) == maxBpm`; `yToBpm (+5000) == std::ceil (minBpm())`; a degenerate `plotHeight <= 0` returns `lo`'s clamp without dividing by zero.
  - `snapToBeat` with `meterSegments ({{0, 4, 4}}, 480)`: 0->0, 239->0, 240->480 (rounds half up), 700->480, 1000->960; negative -> 0. With `{{0,4,4},{1920,3,4}}`: 1930 -> 1920; with `{{0,4,4},{1920,6,8}}` (ticksPerBeat 240) 2050 -> 2160; a tick whose nearest beat would pass the segment end snaps to the segment's last beat inside it.
  - `snapMeterTick`: meters `{{0,4,4},{3840,3,4}}`, ppq 480: `snapMeterTick (m, 480, 3840, 3000) == 3840` (nearest bar of 4/4 without the moved one: bars at 1920 and 3840 → 3000 is nearer 3840? assert 1920 vs 3840 by computing `|3000-1920| = 1080` vs `840` → 3840); `fromTick = 3840, tick = 2000 -> 1920`; negative tick -> 0.
  - `nearestPoint`: empty -> nullopt; point at distance 6 hit, 7 not; two points at equal distance -> later index; nearest beats later when strictly nearer.

- [ ] **Step 2: Run to verify they fail** — build errors (`TempoMeterGraphModel.h` not found). Run: `cmake --build build 2>&1 | tail -5`.

- [ ] **Step 3: Implement** the header per the Interfaces block (doc-comment each function as above) and `TempoMeterGraphModel.cpp`:
  ```cpp
  BpmRange bpmRangeFor (const std::vector<TempoMeterEvent>& events)
  {
      double lo = 1e9, hi = -1e9;
      for (const auto& e : events)
          if (e.kind == TempoMeterKind::tempo) { lo = std::min (lo, e.bpm); hi = std::max (hi, e.bpm); }
      if (lo > hi) return {};
      lo *= 0.75; hi *= 1.25;
      if (hi - lo < 40.0) { const double mid = (lo + hi) / 2.0; lo = mid - 20.0; hi = mid + 20.0; }
      if (lo < 1.0) { hi += 1.0 - lo; lo = 1.0; }
      return { lo, hi };
  }
  ```
  `bpmToY`: `plotTop + round ((hi - bpm) / (hi - lo) * plotHeight)`. `yToBpm`: guard `plotHeight <= 0`; `bpm = hi - (y - plotTop) / (double) plotHeight * (hi - lo)`; `jlimit (ceil (minBpm()), maxBpm, round (bpm))`. `snapToBeat`: `segmentAt` then `start + round ((tick - start) / beat) * beat`; clamp into `[0, segmentEnd - beat]` where segmentEnd is the next segment's `startTick` (last segment: no upper clamp). `snapMeterTick`: erase the change whose `tick == fromTick` (as `TempoEdit.cpp`'s `snapToBar` does), `llround (nearestBarStart (meterSegments (std::move (changes), ppq), max (0, tick)))`. `nearestPoint`: squared distances, `<=` radius², `<=` comparison on ties so later indices win.

- [ ] **Step 4: Run tests** — `cmake --build build && ctest --test-dir build -R "TempoMeterGraphModel" --output-on-failure` → PASS.

- [ ] **Step 5: Commit** — `feat(ui): pure geometry model for the tempo graph and meter strip`.

---

### Task 2: Edit helpers in `TempoMeterTable`

**Files:**
- Modify: `Source/UI/TempoMeterTable.h`, `Source/UI/TempoMeterTable.cpp`
- Test: `Tests/TempoMeterTable_tests.cpp`

**Interfaces:**
- Consumes: `TableEditResult`, `fromTempoEdit`, `rejected`, `findCurrent` (all already in `TempoMeterTable.cpp`'s anonymous namespace — read the file first and reuse them), `setTempo`, `moveTempo`, `setMeter`, `moveMeter`, `roundedBpm`.
- Produces:
  ```cpp
  // Creates the tempo at `tick` or edits the one there (works on the implicit tick-0 default).
  TableEditResult setTempoAt  (SongDocument&, int tick, double bpm);
  // Moves the tempo `row` to `toTick` with `bpm`. Tick-0 rows and toTick == row.tick use setTempo (the
  // implicit default has no event to move). Re-finds `row` by (kind, tick): NoSuchEvent if gone.
  // Does nothing and returns success when the tick and the (rounded) bpm are both unchanged.
  TableEditResult moveTempoTo (SongDocument&, const TempoMeterEvent& row, int toTick, double bpm);
  // Creates or edits the meter at the bar nearest `tick`.
  TableEditResult setMeterAt  (SongDocument&, int tick, int numerator, int denominator);
  // Moves the meter `row` to the bar nearest `toTick`, keeping its stored n/d (re-found, not the caller's copy).
  // Unchanged (snapped tick == row.tick) -> success without an edit. Tick-0 row: unchanged -> success.
  TableEditResult moveMeterTo (SongDocument&, const TempoMeterEvent& row, int toTick);
  ```

- [ ] **Step 1: Write the failing tests** (tag `[tempo-meter-table]`, fixture `Loaded f;` as in the existing tests, 120 BPM / 4/4):
  - `setTempoAt (f.doc, 1920, 93.75)` adds the row (`has (f.doc, tempo, 1920)`); on an existing tick edits it; one `f.doc.undo()` reverts; `setTempoAt (empty, …)` -> `NoTimeBase`; `bpm = 5000` -> `BpmOutOfRange`, document unchanged, undo history size unchanged.
  - `moveTempoTo` moves 1920 -> 2400 carrying new bpm; onto an occupied tick -> `rejectedWith (…, TickOccupied)` and `events (f.doc)` unchanged; unchanged tick+bpm returns success and `!f.doc.getUndoManager().canUndo()` after `clearUndoHistory()`; tick-0 row with a new bpm and a *different* `toTick` keeps tick 0 and changes the bpm; a row removed behind its back -> `NoSuchEvent`.
  - `setMeterAt (f.doc, 3000, 3, 4)` lands on the bar-snapped tick (assert against `nearestBarStart` of `meterSegments (f.doc.getMeterChanges(), 480)`); invalid `7/3` -> `MeterOutOfRange`.
  - `moveMeterTo` of a 3/4 meter at 3840 to 3000 snaps to the nearest bar of the meters without it and keeps `3/4`; the tick-0 meter `moveMeterTo (…, 5000)` is success with no change and no undo entry; occupied -> `TickOccupied`.

- [ ] **Step 2: Run to verify they fail** — undeclared identifiers.

- [ ] **Step 3: Implement.** `moveTempoTo`:
  ```cpp
  TableEditResult moveTempoTo (SongDocument& doc, const TempoMeterEvent& row, int toTick, double bpm)
  {
      const auto current = findCurrent (doc, row);
      if (! current)
          return rejected (TempoEditError::NoSuchEvent);
      const auto rounded = roundedBpm (bpm);
      if (! rounded)
          return rejected (TempoEditError::BpmOutOfRange);
      const bool staysPut = current->tick == 0 || toTick == current->tick;   // tick 0 never moves
      if (staysPut && *rounded == current->bpm)
          return std::nullopt;                                               // nothing to do, no undo step
      return fromTempoEdit (staysPut ? setTempo (doc, current->tick, bpm)
                                     : moveTempo (doc, current->tick, toTick, bpm));
  }
  ```
  `moveMeterTo`: re-find; `n/d` from the found event; if `current->tick == 0` -> success (a tick-0 meter cannot move; its value is untouched); else `moveMeter (doc, current->tick, toTick, n, d)` — but first compute the snapped tick with `snapMeterTick` (Task 1) and return success without editing when it equals `current->tick`. `setTempoAt`/`setMeterAt` are one-line `fromTempoEdit` wrappers.

- [ ] **Step 4: Run tests** — `ctest --test-dir build -R "TempoMeterTable" --output-on-failure` → PASS (the new tests and the existing ones).

- [ ] **Step 5: Commit** — `feat(ui): table-level add/move helpers for the graph and strip`.

---

### Task 3: `TrackListComponent` exposes its timeline view and announces changes

**Files:**
- Modify: `Source/UI/TrackListComponent.h` (public section next to `setEventMarkerTick`), `Source/UI/TrackListComponent.cpp` (`refreshOverlay`, line ~116)
- Test: `Tests/TrackListComponent_tests.cpp`

**Interfaces:**
- Produces: `const TimelineViewState& getTimelineView() const`; `std::function<void()> onViewChanged;` — fired (when set) at the end of `refreshOverlay()`, i.e. after every zoom, scroll, fit, resize and song-length change that already repaints the overlays.

- [ ] **Step 1: Write the failing tests** (tag `[track-list][view-changed]`, in the style of the existing `eventMarker` tests there — find them with `grep -n "setEventMarkerTick" Tests/TrackListComponent_tests.cpp` and copy their setup): with a loaded song, a counter in `onViewChanged`; assert it increments after (a) `fitTimelineToDocument()`, (b) a horizontal scrollbar move / the existing test hook used for scrolling (search the file for how other tests scroll, e.g. `scrollBarMoved` access), (c) a wheel zoom (`mouseWheelMove` with ctrl, as existing zoom tests do), and that `getTimelineView().getPixelsPerTick()` equals the value after the zoom. Unset callback: none of the above crashes.

- [ ] **Step 2: Run to verify they fail** — no member `onViewChanged`.

- [ ] **Step 3: Implement.** In the header, public: the getter returning `timelineView` and the `std::function<void()> onViewChanged;`. In `refreshOverlay()`, as the **first** statement after the existing `eventMarkerOverlay->repaint();` line add `if (onViewChanged) onViewChanged();` — before the early `return` for `overlay == nullptr`, so it fires in every configuration. (If a zoom/scroll path in the test list does not reach `refreshOverlay`, route it through `syncHorizontalBar()` rather than calling the callback from several places.)

- [ ] **Step 4: Run tests** — `ctest --test-dir build -R "view-changed|TrackList" --output-on-failure` → PASS, whole `TrackListComponent` suite still green.

- [ ] **Step 5: Commit** — `feat(ui): TrackListComponent exposes its timeline view and announces zoom/scroll changes`.

---

### Task 4: `TempoGraphComponent` read-only, hosted in the window, selection synced

**Files:**
- Create: `Source/UI/TempoGraphComponent.h`, `Source/UI/TempoGraphComponent.cpp`
- Modify: `Source/UI/TempoMeterEditorWindow.{h,cpp}`, `Source/UI/SongsmithMainComponent.{h,cpp}`, `CMakeLists.txt`, `Tests/CMakeLists.txt`
- Test: `Tests/TempoGraphComponent_tests.cpp` (component), additions to `Tests/TempoMeterEditorWindow_tests.cpp` (hosting/sync)

**Interfaces:**
- Consumes: Task 1 model, `TimelineViewState`, `TrackListComponent::getTimelineView/onViewChanged` (Task 3), `SongsmithColours`, `meterSegments`.
- Produces:
  ```cpp
  class TempoGraphComponent : public juce::Component
  {
  public:
      TempoGraphComponent (SongDocument&, const TimelineViewState&);
      // The listing the table shows (tempo points are the kind == tempo entries). Also drops an
      // in-flight drag whose event is gone (Task 5).
      void setEvents (std::vector<TempoMeterEvent> events);
      // Highlighted points = tempo entries among `selected`.
      void setSelected (std::vector<TempoMeterEvent> selected);
      // Fired when the user clicks a point / adds one (Task 5). The window selects that row.
      std::function<void (TempoMeterKind, int tick)> onSelect;
      // Fired after every edit attempt (Task 5) so the window can show describe().
      std::function<void (const TableEditResult&)> onEdited;

      int xForTick (int tick) const;                 // timelineView.xForTick
      int yForBpm (double bpm) const;                // bpmToY over the plot rectangle
      juce::Rectangle<int> plotArea() const;         // getLocalBounds().reduced (0, 8) minus a 0px left gutter
      void paint (juce::Graphics&) override;
  };
  ```
  Window additions: `void setTimelineView (const TimelineViewState*)` (null -> graph/strip hidden, layout identical to phase 3), `void timelineViewChanged()` (repaints graph and strip).

- [ ] **Step 1: Write the failing tests.**
  Component (`[tempo-graph]`, build the doc with `importStandard` + `setTempo (doc, 1920, 90)` + `setTempo (doc, 3840, 150)`, `TimelineViewState view; view.setPixelsPerTick (0.1);`, graph sized 600×160, `graph.setEvents (listTempoMeterEvents (doc))`):
  - `xForTick (1920) == view.xForTick (1920)`; after `view.setScrollOffsetTicks (960)` `xForTick (1920)` shrinks accordingly (mirrors the view).
  - `yForBpm`: higher BPM -> smaller y; the point for 150 is above 90's.
  - Painting into a `juce::Image` of the component size does not crash with 0 events, 1 event, 3 events, and at `pixelsPerTick` 0.001 and 2.5 (extreme zoom), and with a 0×0 size (`setSize (0,0)` then `paint` via `createComponentSnapshot` is skipped—just assert `setBounds` to zero then `repaint()` does not throw).
  - Rendered-pixel pin: render to an image; the pixel at `(xForTick (1920), yForBpm (90))` is not the background colour (point drawn) and the pixel at `(xForTick (1920) + 20, yForBpm (90))` on the step line is `conductorTempo`-coloured (compare RGB ignoring alpha).
  - No time base: `SongDocument empty; graph.setEvents ({})` paints without crash and draws no point (centre pixel equals background).
  Window (`[tempo-meter-window][graph]`): with no view the window's table bounds and `Access::rowVisible` results are unchanged vs a baseline test (assert the content has no visible graph: `Access::graphVisible (w) == false`). `setTimelineView (&view)` -> `graphVisible == true`, table still non-empty, `getHeight` of the table < before. `Access::selectRow (w, 2)` -> `Access::graphSelected (w)` is `{tempo@1920}`; selecting a *meter* row at the same tick as a tempo selects only the meter (graph's tempo highlight empty) [Review Focus 8]. `timelineViewChanged()` after `view.setPixelsPerTick (0.2)` repaints (assert via a repaint counter exposed through the test-access struct). Edit-then-undo: `setTempo (doc, 960, 95)`; pump; `Access::graphEvents (w).size()` follows the table, undo -> follows again.
  Main component: add one test next to the existing `openTempoMeterEditor` tests in `Tests/SongsmithMainComponent_tests.cpp`: after opening the editor, a `trackList` zoom (call the same method used by existing zoom tests) triggers `timelineViewChanged` (observe through `TempoMeterEditorWindowTestAccess::graphRepaintCount`), and closing the window then zooming does not crash [Review Focus 9].

- [ ] **Step 2: Run to verify they fail** — missing headers/members.

- [ ] **Step 3: Implement.**
  - `TempoGraphComponent::paint`: background `SongsmithColours::background`; light horizontal gridlines at round BPM values (choose a step from {10, 20, 25, 50, 100, 200} giving ≥ 28 px spacing) with muted labels at the left edge (`GlyphArrangement::getStringWidthInt` only if you need widths); the **stepped line**: for consecutive tempo events `a`,`b` draw a horizontal segment `(x(a.tick), y(a.bpm)) → (x(b.tick), y(a.bpm))` then a vertical to `y(b.bpm)`; the last runs to the right edge. Clip to `g.getClipBounds()` and skip segments wholly outside it (30 Hz-free, but still cheap). Points: 5 px radius circles in `conductorTempo`, selected ones filled `eventMarker`, outlined otherwise. Label each point's BPM (`formatMarkerBpm`) above it only when the previous shown label ended ≥ 4 px earlier (reuse `placeMarkers`/`ConductorMarker` from `ConductorMarkers.h` exactly as the Conductor row does for tempo kind, so overlap rules stay identical). Nothing is drawn when `events` has no tempo entries.
  - `setEvents`/`setSelected` store and `repaint()` only when the value changed.
  - Window: `Content` gains `std::unique_ptr<TempoGraphComponent> graph` (null until `setTimelineView`). `resized()`: when graph present, `graph->setBounds (area.removeFromTop (160))` + a 6 px gap above the table; otherwise unchanged. `setTimelineView` creates/destroys the graph, grows the content (`setSize (content->getWidth(), 620)`) and resize limits (`setResizeLimits (360, 420, 900, 900)` only while a view is set; phase-3 limits otherwise), wires `graph->onSelect = [this] (kind, tick) { selectRowFor (kind, tick); }` where `selectRowFor` is the body of `selectEvent` minus `toFront` and the return value (refactor `selectEvent` to call it), and `graph->onEdited = [this] (const TableEditResult& r) { showResult (r); }`. `refreshNow()` ends with `if (graph) { graph->setEvents (events); graph->setSelected (selectedEvents()); }` and `selectedRowsChanged` also calls `graph->setSelected (selectedEvents())`.
  - Test access struct additions: `graphVisible`, `graphSelected`, `graphEvents`, `graphRepaintCount` (a `int repaintCount` incremented in `TempoGraphComponent::paint` and exposed as `paintCountForTesting()`).
  - `SongsmithMainComponent::openTempoMeterEditor`: after `make_unique`, `tempoMeterWindow->setTimelineView (&trackList.getTimelineView());` and set `trackList.onViewChanged = [this] { if (tempoMeterWindow != nullptr) tempoMeterWindow->timelineViewChanged(); };` once in the constructor next to the other `trackList.on…` lines (line ~112). `tempoMeterWindow` is declared after `trackList` (header lines 212/215), so the window is destroyed first; keep that order and say so in a comment on the member.

- [ ] **Step 4: Run tests** — `cmake --build build && ctest --test-dir build -R "TempoGraph|TempoMeterEditorWindow|SongsmithMainComponent|TrackList" --output-on-failure` → PASS.

- [ ] **Step 5: Commit** — `feat(ui): tempo graph (read-only) in the Tempo/Meter window, mirroring the main timeline`.

---

### Task 5: Tempo graph editing — select, add, drag, commit on mouse-up

**Files:**
- Modify: `Source/UI/TempoGraphComponent.{h,cpp}`
- Test: `Tests/TempoGraphComponent_tests.cpp`, `Tests/TempoMeterEditorWindow_tests.cpp`

**Interfaces:**
- Consumes: Task 1 (`nearestPoint`, `snapToBeat`, `yToBpm`, `bpmRangeFor`), Task 2 (`setTempoAt`, `moveTempoTo`), `onSelect` / `onEdited`.
- Produces: mouse behaviour (no new public API besides test hooks below). State: `struct Drag { TempoMeterEvent event; juce::Point<int> down; bool moved; BpmRange range; int previewTick; double previewBpm; }` held in `std::optional<Drag> drag`.
  - `mouseDown`: hit-test with `nearestPoint` over the tempo points' `(x, y)` (radius 6). Hit -> `onSelect (tempo, tick)`; start `Drag` (freeze `range = bpmRangeFor (events)`, `moved = false`). Miss -> `tick = snapToBeat (meterSegments (doc.getMeterChanges(), ticksPerQuarter), view.tickForX (x))`, `bpm = yToBpm (bpmRangeFor (events), y, …)`; if a tempo already exists at `tick` -> `onSelect` it and stop; else `r = setTempoAt (doc, tick, bpm)`; `onEdited (r)`; on success `onSelect (tempo, tick)`.
  - `mouseDrag`: once `|dx|` or `|dy|` ≥ 3 px set `moved`; preview tick = `0` for a tick-0 event else `max (0, snapToBeat (…, view.tickForX (x)))`, preview bpm = `yToBpm (drag.range, y, …)`; `repaint()`. Painting shows the dragged point and its step line at the preview values (the document is untouched) with a small `"{bpm} BPM"` readout beside it.
  - `mouseUp`: if `drag && drag->moved` -> `onEdited (moveTempoTo (doc, drag->event, previewTick, previewBpm))`; clear `drag`; `repaint()`. Not moved -> nothing is written.
  - `setEvents`: if `drag` and its `(kind, tick)` is absent from the new listing, `drag.reset()` (undo/redo/New mid-drag).
  - The component sets `setWantsKeyboardFocus (true)` and its `keyPressed` returns `false`, so Delete/undo keys bubble to the window handler that already exists.
  - Test hooks: `std::optional<std::pair<int,double>> dragPreviewForTesting() const` (tick, bpm) and `bool isDraggingForTesting() const`.

- [ ] **Step 1: Write the failing tests** (`[tempo-graph][edit]`; mouse events synthesised like `TempoMeterEditorWindowTestAccess::mouseDown` — add a shared `makeMouse (Component&, Point, ModifierKeys, mouseDownPos)` helper in the test file; `view` ppt 0.1, graph 600×160, doc with tempos 120@0, 90@1920, 150@3840, `doc.getUndoManager().clearUndoHistory()`, `onEdited` and `onSelect` recorders):
  1. Click on the 90 point (`xForTick (1920), yForBpm (90)`): `onSelect (tempo, 1920)`, no edit, `!doc.getUndoManager().canUndo()` after mouse-up [Review 1].
  2. Drag the 90 point to `x = xForTick (2400) + 3, y = yForBpm (130)`: during drag `dragPreviewForTesting() == {2400, 130}` and `listTempoMeterEvents (doc)` is unchanged; after mouse-up the document has the tempo at 2400 = 130 and none at 1920; exactly one `doc.undo()` restores the original list.
  3. Drag onto the 150 point's tick: `onEdited` got `Rejected/TickOccupied`, list unchanged, `!canUndo()`, `isDraggingForTesting() == false` [Review 2].
  4. Tick-0 point dragged diagonally: only its BPM changes, tick stays 0; the same on the **implicit** default (fresh `importStandard` song with no explicit tempo event): BPM changes via `setTempo` [Review 3].
  5. Clamping: with tempos 120@0, 90@1920, 150@3840, drag the 150 point and release at `y = +5000`: BPM becomes `std::ceil (minBpm())` (accepted, not rejected); release at `y = -5000`: 1000. Drag the 90 point and release at `x = -500`: the tick clamps to 0, which holds a tempo, so the result is `TickOccupied`, list unchanged, no undo step [Review 4].
  6. The range is frozen: during a drag that moves the highest point far below the others, `yForBpm` of an untouched point is unchanged between mouse-down and mouse-drag (compare before/after) [Review 4].
  7. Click on empty space at `(xForTick (960) + 2, yForBpm (110))`: adds a tempo at tick 960 (snapped), `onSelect (tempo, 960)`, one undo step; clicking again on the new point only selects. Click on an empty spot that snaps onto an existing tempo's tick selects it and writes nothing.
  8. Mid-drag `doc.undo()` of an earlier unrelated step / `setEvents` without the dragged event: `isDraggingForTesting() == false`, mouse-up writes nothing, no crash [Review 5].
  9. No time base: click adds nothing, `onEdited` receives `Rejected/NoTimeBase` [Review 7].
  10. Extreme zoom (ppt 0.001 and 2.5): clicking a point still hits it (6 px); two tempos 1 tick apart at ppt 0.001 select the later one [Review 6].
  Window: select the point via `graph->onSelect`, then `w.keyPressed (KeyPress::deleteKey)` removes it (table row count drops), message empty; a rejected drag shows `describe (…)` in the window's message label.

- [ ] **Step 2: Run to verify they fail** — behaviours absent.

- [ ] **Step 3: Implement** per the Interfaces block. Convert pixel↔data only through Task 1/`TimelineViewState`; never compare floating BPM with `==` outside `moveTempoTo`.

- [ ] **Step 4: Run tests** — `ctest --test-dir build -R "TempoGraph|TempoMeterEditorWindow|TempoMeterTable" --output-on-failure` → PASS.

- [ ] **Step 5: Commit** — `feat(ui): drag, add and select tempo points on the graph (one undo step per drag)`.

---

### Task 6: `MeterStripComponent` — blocks, select, add, drag

**Files:**
- Create: `Source/UI/MeterStripComponent.h`, `Source/UI/MeterStripComponent.cpp`
- Modify: `Source/UI/TempoMeterEditorWindow.{h,cpp}`, `CMakeLists.txt`, `Tests/CMakeLists.txt`
- Test: `Tests/MeterStripComponent_tests.cpp`, additions to `Tests/TempoMeterEditorWindow_tests.cpp`

**Interfaces:**
- Consumes: `snapMeterTick` (Task 1), `setMeterAt` / `moveMeterTo` (Task 2), `TimelineViewState`.
- Produces: the same shape as the graph — `MeterStripComponent (SongDocument&, const TimelineViewState&)`, `setEvents`, `setSelected`, `onSelect`, `onEdited`, `xForTick`, test hooks `isDraggingForTesting`, `dragPreviewForTesting() -> optional<int>` (tick), `paintCountForTesting`. 28 px tall; window layout: strip directly under the graph (4 px gap) when a view is set.
- Behaviour: a block runs from each meter event's x to the next meter event's x (last: to the right edge), filled with `conductorMeter` at low alpha (alternating shade per block as the Conductor row's bottom half does), left edge a 2 px line, label `n/d` (`markerLabel`) at the left inside the block when it fits (same overlap rule as the Conductor row). `mouseDown` on a block's left edge (±6 px) or inside its first 40 px -> select it, start a drag (not for tick 0, which only selects); on a block body elsewhere -> select it; on empty space (past the last block there is none — the last block fills to the edge — so "empty" means "inside a block's body beyond the 40 px grab zone"): **clicking a block body does not add**; adding is via **double-click on the strip**: `setMeterAt (doc, snapMeterTick (meters, ppq, -1, tick), n, d)` where `n/d` is the meter in force there. `mouseDrag` previews the snapped bar (`snapMeterTick (…, fromTick, tick)`), shown as a ghost line + label; `mouseUp` commits `moveMeterTo` once when moved and the preview differs from the origin.

- [ ] **Step 1: Write the failing tests** (`[meter-strip]`; doc: 4/4@0 plus a 3/4 at bar 3 (tick 3840) via `setMeter`, view ppt 0.05, strip 600×28):
  1. Paint doesn't crash with 1 and 2 blocks, at extreme zoom and empty (no time base) — and the label pixel region differs from background.
  2. Press on the 3/4 block's left edge, drag to just past bar 5 (tick 7680 + 30) and release: meter now at 7680 (bar-snapped), 3/4, one undo step; during the drag the document is unchanged and `dragPreviewForTesting() == 7680`.
  3. Dragging onto the tick of another meter -> `TickOccupied` message via `onEdited`, no change, no undo step. Dragging the tick-0 meter: only selects; no edit [Review 3].
  4. Press-release without moving: `onSelect (meter, 3840)`, `!canUndo()` [Review 1].
  5. Double-click on the 4/4 block body at bar 2 (tick 1920+): adds `4/4` at 1920 (the meter in force), one undo step, `onSelect (meter, 1920)`; double-click that snaps onto an existing meter's tick selects only. No time base: `NoTimeBase` message [Review 7].
  6. Mid-drag `setEvents` without the dragged meter aborts the drag [Review 5].
  Window: the strip's selection follows the table (select the meter row -> `Access::stripSelected` is that meter only [Review 8]); Delete after strip selection removes it; strip hidden with no view.

- [ ] **Step 2: Run to verify they fail.**

- [ ] **Step 3: Implement** the component and its hosting (`Content` gets `std::unique_ptr<MeterStripComponent> strip`, created/destroyed with the graph in `setTimelineView`; `refreshNow`, `selectedRowsChanged` and `timelineViewChanged` feed/repaint it exactly like the graph; `onSelect`/`onEdited` wired identically).

- [ ] **Step 4: Run tests** — `ctest --test-dir build -R "MeterStrip|TempoGraph|TempoMeterEditorWindow" --output-on-failure` → PASS.

- [ ] **Step 5: Commit** — `feat(ui): meter strip with selectable, draggable meter blocks`.

---

### Task 7: Docs, full verification, Windows build

**Files:**
- Modify: `docs/ARCHITECTURE.md` (§9.18: add the graph/strip paragraph — model, helpers, `getTimelineView`/`onViewChanged`, drag-previews-then-one-commit rule, mirror-the-main-timeline decision), `docs/UI_GUIDE.md` (the Tempo/Meter window section: graph and strip gestures), `docs/songsmith-ui-map.html` (if it lists the window's regions), `docs/TESTING.md` (new count and the notable new test files), `CLAUDE.md` (status line: phase 4 graph and strip), spec header status line (phase 4 implemented, plan path).

- [ ] **Step 1: Update the docs** listed above. Grep for stale statements: `grep -rn "phase 4\|table only\|graph" docs/ARCHITECTURE.md docs/UI_GUIDE.md CLAUDE.md` and fix any that say the graph is not built.
- [ ] **Step 2: Full suite** — `cmake --build build && ctest --test-dir build -j4 --output-on-failure` → 100 % passing; report the exact count and put it in `docs/TESTING.md` (`ctest --test-dir build -N | tail -1`). If a TempoMeterEditorWindow/TrackRowComponent test fails only under `-j`, re-run it alone and with `--repeat until-fail:10` before calling it a flake.
- [ ] **Step 3: Windows cross-build** — `./build-windows.sh forge_ui`; note new warnings in the new files (there should be none). Do **not** launch the app. Deploy only if the user asks (command in `CLAUDE.md`).
- [ ] **Step 4: Commit** — `docs: tempo/meter phase 4 (graph and meter strip)`.

---

## Self-Review

- **Spec coverage (§5 graph + §6 phase 4):** stepped tempo line and draggable points (Tasks 4–5); click empty space adds, drag horizontal (beat-snapped) and vertical, Delete removes (Task 5 + existing window Delete); meter strip blocks `3/4`, click adds → *double-click* adds (decision recorded: a single click on a block must select, matching the user's earlier "single click must stay inert" feedback), drag bar-snapped, Delete removes (Task 6); drag = one undo transaction committed on mouse-up with in-progress value shown (Tasks 5–6); selection both ways with the table (Tasks 4–6); shared timeline zoom/scroll (Task 3 + user choice); both views call `TempoEdit`, never the tree (Task 2). Undo/Redo forwarding already exists in the window.
- **Placeholder scan:** none; the two places that say "find how existing tests scroll/zoom" (Task 3, Task 4) point at concrete greps because the exact test hook names live in `TrackListComponent_tests.cpp`.
- **Type consistency:** `BpmRange`, `bpmRangeFor`, `bpmToY`, `yToBpm`, `snapToBeat`, `snapMeterTick`, `nearestPoint` (Task 1) are the names used in Tasks 4–6; `setTempoAt` / `moveTempoTo` / `setMeterAt` / `moveMeterTo` (Task 2) likewise; `onSelect (TempoMeterKind, int)` / `onEdited (const TableEditResult&)` are identical on both components; `getTimelineView()` / `onViewChanged` (Task 3) feed `setTimelineView` / `timelineViewChanged` (Task 4).
- **Review Focus:** every numbered item has a named test in the task listed.
