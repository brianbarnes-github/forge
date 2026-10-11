# Tempo / Meter — Phase 2 (Conductor row display) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** The Conductor row draws a marker for every tempo event (`♩=120`) and every meter event (`3/4`) from the derived maps, aligned with the ruler and the other tracks, with a faint background change at each tempo event. Display only: nothing becomes clickable or editable.

**Architecture:** A pure, JUCE-free `ConductorMarkers` module turns `TempoMeterEvent`s into labelled markers and decides which labels fit at the current zoom. `TrackNotePreview` (the canvas side of every row) paints those markers when its track is the conductor, reading the maps from the document root exactly as it already reads `METER_MAP` for the grid. Edits already make `TrackListComponent` rebuild its rows (the conductor's `EVENTS` change under `SOURCE_MIDI`), so the markers refresh on edit, undo/redo, import and load with no new listener.

**Tech Stack:** C++20, JUCE 8, Catch2, CMake/Ninja. No new dependencies.

**Spec:** `docs/superpowers/specs/2026-10-10-songsmith-tempo-meter-design.md` §4 (phase 2 of 4). The double-click / Edit-menu / right-click entry points in §4 open the editor window, which is phase 3; this plan adds none of them. Phase 1 plan: `docs/superpowers/plans/2026-10-10-songsmith-tempo-meter-phase1.md`.

## Global Constraints

- Markers are read from the **derived maps** (`TEMPO_MAP` / `METER_MAP`) via `listTempoMeterEvents`; nothing in this phase writes the tree, and `Source/Core` is not touched (forge-engine-ui-boundary). All new code is in `Source/UI/`.
- "The row stays unmuteable and non-editable as a track": no new gesture, callback or menu on the Conductor row. A single click behaves exactly as today (selects the row).
- Markers use `TimelineViewState::xForTick`, the same mapping as notes, sections and the grid, so they align with the ruler at any zoom/scroll.
- Non-conductor rows must paint pixel-identically to today.
- Tempo label `♩=<bpm>` (UTF-8 `\xe2\x99\xa9`); meter label `<n>/<d>`. BPM shown with up to 2 decimals, trailing zeros trimmed (`120`, `93.75`).
- Project style: functional where practical, full type annotations, conventional commits, TDD, integration tests preferred. Commit messages end with `Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>`. Never push. Do not launch the GUI.
- Build/test: `cmake --build build --target forge_tests && ./build/Tests/forge_tests "<tag>"`; full run `ctest --test-dir build --output-on-failure`. New `.cpp` files go in **both** `CMakeLists.txt` (UI source list, near `Source/UI/TrackNotePreview.cpp`) and `Tests/CMakeLists.txt` (source list near `${CMAKE_SOURCE_DIR}/Source/UI/TrackNotePreview.cpp`, and the test list near `TrackNotePreview_tests.cpp`).

## Review Focus

1. A song with no tempo/meter events (implicit defaults) and a document before any import: the implicit `♩=120` / `4/4` at tick 0 are shown (they are what plays); an empty document shows nothing and does not crash (Task 2 tests).
2. Dense events (many tempo changes inside a few pixels) and extreme zoom-out: labels must be thinned, never overlap, and the marker lines must all still draw (Task 1 `placeMarkers` tests, Task 2 test).
3. A marker whose tick is left of the scrolled view but whose label would extend into it, and markers far right of the visible area: no crash, no stray paint outside the row (Task 2 test).
4. Two tempo events and a meter event on the same tick: tempo label on the top half, meter label on the bottom half, neither hides the other (Task 1 and Task 2 tests).
5. After `setTempo` / `setMeter` / undo / redo on the document, the live `TrackListComponent` conductor row shows the new markers (Task 3 test) while every other row's output is unchanged.

---

## File Structure

| File | Responsibility |
|---|---|
| `Source/UI/TempoEdit.{h,cpp}` (modify) | New `listTempoMeterEvents (const juce::ValueTree& songRoot)` overload so a component holding only a track node can list the events. |
| `Source/UI/ConductorMarkers.{h,cpp}` (new) | Pure: `formatBpm`, `markerLabel`, `conductorMarkers`, `placeMarkers`. No JUCE. |
| `Source/UI/SongsmithColours.h` (modify) | `conductorTempo`, `conductorMeter` colours. |
| `Source/UI/TrackNotePreview.{h,cpp}` (modify) | `paintConductorMarkers`, called for conductor tracks. |
| `Tests/ConductorMarkers_tests.cpp` (new) | Pure-module tests. |
| `Tests/TrackNotePreview_tests.cpp`, `Tests/TrackListComponent_tests.cpp` (modify) | Pixel and integration tests. |
| `docs/ARCHITECTURE.md`, `docs/UI_GUIDE.md`, `docs/TESTING.md`, `CLAUDE.md` (modify) | Delivery docs (Task 3). |

---

### Task 1: Pure marker module and tree-based listing

**Files:**
- Modify: `Source/UI/TempoEdit.h`, `Source/UI/TempoEdit.cpp:276-287`
- Create: `Source/UI/ConductorMarkers.h`, `Source/UI/ConductorMarkers.cpp`
- Create: `Tests/ConductorMarkers_tests.cpp`
- Modify: `CMakeLists.txt`, `Tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `TempoMeterEvent`, `TempoMeterKind` (`TempoEdit.h`).
- Produces (all in namespace `lotro`):
  ```cpp
  struct ConductorMarker { TempoMeterKind kind; int tick; std::string label; };
  struct PlacedMarker    { ConductorMarker marker; int x; bool labelShown; };
  std::string formatBpm (double bpm);                         // "120", "93.75"
  std::string markerLabel (const TempoMeterEvent&);           // "\xe2\x99\xa9=120" or "3/4"
  std::vector<ConductorMarker> conductorMarkers (const std::vector<TempoMeterEvent>&);  // same order as input
  // x = xForTick(tick). A label is shown when it starts at least `labelGap` px after the
  // previous *shown* label of the same kind ends (label starts at x + labelInset).
  std::vector<PlacedMarker> placeMarkers (const std::vector<ConductorMarker>&,
                                          const std::function<int (int)>& xForTick,
                                          const std::function<int (const std::string&)>& textWidth,
                                          int labelInset = 3, int labelGap = 4);
  // TempoEdit.h
  std::vector<TempoMeterEvent> listTempoMeterEvents (const juce::ValueTree& songRoot);
  ```

- [ ] **Step 1: Write the failing tests** in `Tests/ConductorMarkers_tests.cpp` (tag `[conductor-markers]`):
  ```cpp
  #include "UI/ConductorMarkers.h"
  #include "UI/SongDocument.h"
  #include "UI/SongModelBridge.h"
  #include "UI/TempoEdit.h"
  #include <catch2/catch_test_macros.hpp>
  using namespace lotro;

  namespace
  {
      TempoMeterEvent tempo (int tick, double bpm) { return { TempoMeterKind::tempo, tick, bpm, 0, 0 }; }
      TempoMeterEvent meter (int tick, int n, int d) { return { TempoMeterKind::meter, tick, 0.0, n, d }; }
      const auto xOf  = [] (int tick) { return tick / 10; };          // 0.1 px per tick
      const auto wide = [] (const std::string& s) { return (int) s.size() * 6; };
  }

  TEST_CASE ("ConductorMarkers: BPM is shown with up to two decimals, trailing zeros trimmed", "[conductor-markers]")
  {
      CHECK (formatBpm (120.0) == "120");
      CHECK (formatBpm (93.75) == "93.75");
      CHECK (formatBpm (100.5) == "100.5");
      CHECK (formatBpm (60e6 / 500001.0) == "120");      // rounds, never "119.999998"
  }

  TEST_CASE ("ConductorMarkers: labels", "[conductor-markers]")
  {
      CHECK (markerLabel (tempo (0, 120.0)) == "\xe2\x99\xa9=120");
      CHECK (markerLabel (meter (0, 6, 8)) == "6/8");
  }

  TEST_CASE ("ConductorMarkers: one marker per event, order and ticks kept", "[conductor-markers]")
  {
      const auto m = conductorMarkers ({ tempo (0, 120), meter (0, 4, 4), tempo (960, 90) });
      REQUIRE (m.size() == 3);
      CHECK (m[1].kind == TempoMeterKind::meter);
      CHECK (m[2].tick == 960);
      CHECK (conductorMarkers ({}).empty());
  }

  TEST_CASE ("ConductorMarkers: placeMarkers thins labels per kind but keeps every marker", "[conductor-markers]")
  {
      // Marker x = 0, 20, 40, 60. A label is 5 chars * 6 = 30 px wide and starts 3 px after its marker.
      // Label 0 ends at 33, so the next may start at 33 + 4 = 37: marker 1 (23) hides, marker 2 (43) shows
      // and ends at 73, so marker 3 (63) hides.
      const auto m = conductorMarkers ({ tempo (0, 100), tempo (200, 110), tempo (400, 120), tempo (600, 130) });
      const auto p = placeMarkers (m, xOf, wide);
      REQUIRE (p.size() == 4);
      CHECK (p[0].labelShown);
      CHECK_FALSE (p[1].labelShown);
      CHECK (p[2].labelShown);
      CHECK_FALSE (p[3].labelShown);
      CHECK (p[3].x == 60);
  }
  Add: same-tick tempo and meter both shown (thinning is per kind); an empty input gives an empty result; and an integration test that `listTempoMeterEvents (doc.getTree())` equals `listTempoMeterEvents (doc)` for a freshly imported single-note MIDI (copy the `Loaded` helper from `Tests/TempoEdit_tests.cpp:52-61`; put it in this file's anonymous namespace).

- [ ] **Step 2: Run to verify failure.** Add the test file to `Tests/CMakeLists.txt` (test list) and `ConductorMarkers.cpp` to both source lists, then `cmake --build build --target forge_tests`. Expected: compile failure, `ConductorMarkers.h` not found.

- [ ] **Step 3: Implement.**
  - `TempoEdit.h`: `#include <juce_data_structures/juce_data_structures.h>`; declare the `const juce::ValueTree& songRoot` overload.
  - `TempoEdit.cpp`: move the body of `listTempoMeterEvents (const SongDocument&)` into the root overload, reading `songRoot.getChildWithName (SongIDs::TEMPO_MAP)` / `METER_MAP`; the `SongDocument` overload becomes `return listTempoMeterEvents (doc.getTree());`.
  - `ConductorMarkers.cpp`: `formatBpm` = `snprintf("%.2f")`, strip trailing `0`s then a trailing `.`; `markerLabel` per kind; `conductorMarkers` maps 1:1; `placeMarkers` keeps `std::array<int,2> shownRight{INT_MIN/2, INT_MIN/2}` indexed by kind and for each marker sets `labelShown = x + labelInset >= shownRight[kind] + labelGap`, updating `shownRight[kind] = x + labelInset + textWidth(label)` when shown.

- [ ] **Step 4: Run** `./build/Tests/forge_tests "[conductor-markers],[tempo-edit]"`. Expected: PASS.

- [ ] **Step 5: Commit.**
  ```bash
  git add Source/UI/ConductorMarkers.h Source/UI/ConductorMarkers.cpp Source/UI/TempoEdit.h Source/UI/TempoEdit.cpp Tests/ConductorMarkers_tests.cpp CMakeLists.txt Tests/CMakeLists.txt
  git commit -m "feat(ui): conductor marker labels and label thinning, plus a tree-based event listing"
  ```

---

### Task 2: Paint the markers on the Conductor row

**Files:**
- Modify: `Source/UI/SongsmithColours.h`, `Source/UI/TrackNotePreview.h`, `Source/UI/TrackNotePreview.cpp`
- Test: `Tests/TrackNotePreview_tests.cpp`

**Interfaces:**
- Consumes: `listTempoMeterEvents (const juce::ValueTree&)`, `conductorMarkers`, `placeMarkers`, `TimelineViewState::xForTick`.
- Produces: `TrackNotePreview::paintConductorMarkers (juce::Graphics&) const` (private); `SongsmithColours::conductorTempo = 0xFFE0B080`, `conductorMeter = 0xFF7FA8D0` (opaque, so tests can compare pixels exactly).

Rendering contract (what the tests pin):
- Only when `track` has `isConductor == true`; called after `paintSections` and before the ghost toggle.
- Each marker draws a 1 px full-height vertical line at `x = viewState.xForTick (tick)` in its kind's colour.
- Labels: font 10, tempo label in the top half of the row at `x + 3`, meter label in the bottom half; only when `labelShown`; text colour = the kind's colour.
- Tint: for tempo events sorted by tick, every odd-indexed tempo segment (from its x to the next tempo's x, the last to the right edge) is filled with `juce::Colours::white.withAlpha (0.04f)` over the background, so each tempo change flips the background shade.
- Read the root with `track.getRoot()`; an invalid or detached track (as in the existing "empty track" test) paints nothing extra. Skip drawing for `x` outside `[-1, getWidth()]` (labels of left-off-screen markers may still extend in: compute `placeMarkers` over all markers, draw a label when its rectangle intersects the clip).

- [ ] **Step 1: Write failing tests** (tag `[track-note-preview][conductor]`) using the existing `previewWidth`/`previewHeight` and the render-to-`juce::Image` pattern at `Tests/TrackNotePreview_tests.cpp:33-40`. Build a real document with the `Loaded` helper pattern from `Tests/TempoEdit_tests.cpp` (copy into this file's anonymous namespace), take `doc.getConductorTrack()` as the preview's track, `pixelsPerTick = 0.1`, scroll 0:
  1. Imported 120 BPM + 4/4 song: pixel at `(xForTick(0), 1)` equals `conductorTempo`; the same song's non-conductor track preview has no pixel equal to `conductorTempo` at that x (rows unchanged).
  2. `setTempo (doc, 960, 90)`: pixel at `(xForTick(960), previewHeight - 1)` equals `conductorTempo` (the line), and a pixel at `(xForTick(960) + 40, 3)` differs from `SongsmithColours::background` (tint of the odd segment) while `(xForTick(480), 3)` still equals the plain background. Use pixels away from label glyphs (right of the label, e.g. `+ 60`), width permitting; widen the preview in this test to 600 if needed.
  3. `setMeter (doc, 1920, 3, 4)`: a `conductorMeter` pixel at `(xForTick(1920), previewHeight - 1)`.
  4. Scrolled so tick 960 is left of the view (`setScrollOffsetTicks (2000)`): no assertion failure, no crash, and no `conductorTempo` line pixel in `x < 0` is addressable; the view still renders (smoke) and the row background right of any label is the odd-tint shade.
  5. Dense: 40 `setTempo` events 10 ticks apart at 0.1 px/tick; the render completes, and every event's line pixel is `conductorTempo` (all markers drawn though most labels are hidden).
  6. Empty `SongDocument` (nothing imported), conductor track from `addTrack`-style fixture with `isConductor` set (as in `Tests/TrackRowComponent_tests.cpp:36-47`): renders without crash and the centre pixel is the plain background.
  7. Tempo and meter on the same tick (import a MIDI whose conductor has `FF 51` and `FF 58` at tick 0 — the standard song does): scan the top half of the render for a `conductorTempo` pixel right of x = 1 (label text) and the bottom half for a `conductorMeter` pixel right of x = 1, proving neither label hides the other.

- [ ] **Step 2: Run to verify failure:** `./build/Tests/forge_tests "[conductor]"`. Expected: the new assertions fail (no markers painted).

- [ ] **Step 3: Implement** `paintConductorMarkers` per the rendering contract. Text width callback: `juce::GlyphArrangement`-free simple form `juce::Font (juce::FontOptions (10.0f)).getStringWidth (juce::String::fromUTF8 (s.c_str()))`. Add the two colours. Declare the method in `TrackNotePreview.h`.

- [ ] **Step 4: Run** `./build/Tests/forge_tests "[track-note-preview],[track-row],[conductor]"`. Expected: PASS, including every pre-existing preview and row test.

- [ ] **Step 5: Commit.**
  ```bash
  git add Source/UI/SongsmithColours.h Source/UI/TrackNotePreview.h Source/UI/TrackNotePreview.cpp Tests/TrackNotePreview_tests.cpp
  git commit -m "feat(ui): the Conductor row draws tempo and meter markers"
  ```

---

### Task 3: Live refresh through the track list, docs, delivery check

**Files:**
- Test: `Tests/TrackListComponent_tests.cpp`
- Modify: `docs/ARCHITECTURE.md` (§9.10 row/preview, §9.18), `docs/UI_GUIDE.md` (Conductor row), `docs/TESTING.md` (count and new files), `CLAUDE.md` (status line, docs-map row for this plan), spec header in `docs/superpowers/specs/2026-10-10-songsmith-tempo-meter-design.md` (phase 2 implemented).

- [ ] **Step 1: Write the failing integration test** in `Tests/TrackListComponent_tests.cpp` (tag `[track-list][conductor]`): import a MIDI via the `Loaded`-style helper, construct `TrackListComponent list (doc)` with a size, grab the conductor row's `notePreviewForTesting()` (row index 0), then `setTempo (doc, 960, 90)`, pump `juce::MessageManager::getInstance()->runDispatchLoopUntil (100)` (rows are rebuilt asynchronously; refetch row 0 afterwards), render its preview to an `Image` and require the `conductorTempo` line at `xForTick (960)`. Then `doc.undo()`, pump, re-render: the line is gone. Then `doc.redo()`: back. Also assert a non-conductor row's snapshot is byte-identical before and after the edit (`image.getPixelAt` over a full scan compared to the earlier image).
- [ ] **Step 2: Run to verify.** If it already passes (rebuild covers it) that is the expected result: keep the test as the regression pin and say so in the commit message. If it fails because the maps are rewritten after the rebuild paints, fix by making `TrackListComponent::valueTreePropertyChanged`/`valueTreeChildAdded` repaint `content` after the async rebuild (the maps are written synchronously by `TempoMapSync` on the same tree change, so this is not expected).
- [ ] **Step 3: Docs.** Update the files listed above: the Conductor row now shows tempo (amber, top half) and meter (blue, bottom half) markers with thinned labels and an alternating tempo tint; no new gestures; the editor entry points arrive in phase 3. `docs/TESTING.md` gets the new total from the full run.
- [ ] **Step 4: Full suite.** `cmake --build build && ctest --test-dir build --output-on-failure`. Expected: all pass (1024 + new). Record the count.
- [ ] **Step 5: Commit.**
  ```bash
  git add Tests/TrackListComponent_tests.cpp docs CLAUDE.md
  git commit -m "docs: Conductor row tempo/meter markers (phase 2)"
  ```

## Delivery notes (not tasks)

- Real-MSVC CI is still the release gate; phases 1 and 2 have only Linux evidence. A manual check for the user after deploy (`./build-windows.sh forge_ui` + cp, command in CLAUDE.md): open a multi-tempo MIDI, confirm the Conductor row markers align with the ruler and move with zoom/scroll; Edit ▸ Undo of a Replace import does not apply (history cleared).
- No way to *create* tempo/meter events from the UI until phase 3; before then the markers show imported and loaded events only.
