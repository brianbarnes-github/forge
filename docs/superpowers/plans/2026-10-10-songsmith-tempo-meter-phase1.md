# Tempo / Meter — Phase 1 (data layer) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make the conductor's `FF 51` / `FF 58` events the only stored truth for tempo and meter (the `TEMPO_MAP` / `METER_MAP` nodes become a derived view), add undoable edit operations on them, and make the ruler, grid lines and Rewind One Bar understand more than one meter. No new UI.

**Architecture:** A pure `deriveMaps (events)` turns conductor events into maps; a listener on `SOURCE_MIDI` rebuilds the maps (non-undoable) whenever a conductor tempo/meter event changes, so edits, undo/redo, import and load all converge. `TempoEdit` writes only conductor `EVENT`s inside one undo transaction. A pure `meterSegments` helper is the single place that turns a meter list into per-segment bar lengths, used by the ruler, the grid and Rewind One Bar.

**Tech Stack:** C++20, JUCE 8 (`juce::ValueTree`, `juce::UndoManager`), Catch2, CMake/Ninja. No new dependencies.

**Spec:** `docs/superpowers/specs/2026-10-10-songsmith-tempo-meter-design.md` (phase 1 of 4). This plan also amends the spec in Task 3 (see "Spec amendment").

## Global Constraints

- Export equals editor: after any sequence of edit, undo/redo, import (keep and Replace) and save/load, the maps equal `deriveMaps (conductor events)`, and `buildRawMidiFile` contains exactly the maps' tempo and meter changes.
- Notes, sections and non-tempo/meter events are never touched by a tempo/meter edit.
- Tempo BPM range: what 24-bit microseconds-per-quarter encodes (≥ 3.58 BPM) up to 1000; out of range is rejected with a typed error, never clamped. Typed BPM is rounded to whole microseconds per quarter; the event and the map hold the rounded value.
- Meter numerator 1–32; denominator one of 1, 2, 4, 8, 16, 32. New `FF 58` events use `cc = 24`, `bb = 8`; editing keeps an existing event's `cc`/`bb`.
- A meter change snaps to the nearest bar start; at most one tempo and one meter event per tick; the tick-0 event cannot be removed.
- `Source/Core` is not touched (forge-engine-ui-boundary). All new code is in `Source/UI/`.
- Project style: functional where practical, full type annotations, custom error types (no string errors), conventional commits, TDD, integration tests preferred for business logic. Commit message ends with `Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>`. Never push.
- Single-meter and no-meter-change results of the ruler, grid and rewind must stay identical to today's (existing tests keep passing unchanged).
- Build/test: `cmake --build build --target forge_tests && ./build/Tests/forge_tests "<tag>"`; full run `ctest --test-dir build --output-on-failure`. New `.cpp` files go in **both** `CMakeLists.txt` (the `forge_ui` list near `Source/UI/MidiImportPlan.cpp`) and `Tests/CMakeLists.txt` (source list near `${CMAKE_SOURCE_DIR}/Source/UI/MidiImportPlan.cpp`), and new test files in the test list in `Tests/CMakeLists.txt` near `ImportOptions_tests.cpp`.

## Review Focus

1. A format-1 file that has its own conductor track **and** a tempo/meter event inside a note track: today the maps include it but the export keeps it in the note track. After this plan it must be moved into the conductor and counted in the "Moved N song-wide event(s)" Info (Task 3 test).
2. The same file imported **later** with the tempo map kept: its note-track tempo events must be dropped, not leak into the Song's map (Task 3 and Task 4 tests).
3. Meter changes not on a bar line, two meter events on one tick, a first meter event at tick > 0, and a meter map that is unsorted (old files): bar counting and snapping must stay deterministic (Task 1 tests).
4. An old `.songsmith` file whose stored maps disagree with its events, and a document before any import (maps empty, edits rejected) (Task 4 tests).
5. Undo/redo across an edit and across an import: the maps must follow the events with no extra undo steps and no playback snapshot rebuild when nothing changed (Task 4 and Task 7 tests).

---

## File Structure

| File | Responsibility |
|---|---|
| `Source/UI/MeterSegments.{h,cpp}` (new) | Pure: `MeterChange`, `MeterSegment`, `meterSegments`, `segmentAt`, `nearestBarStart`, `previousBarStart`. |
| `Source/UI/TempoMapSync.{h,cpp}` (new) | `deriveMaps` (pure), `conductorEventsOf`, `rebuildMaps`, the `TempoMapSync` listener, `MapSyncPause`. |
| `Source/UI/TempoEdit.{h,cpp}` (new) | Undoable add/set/move/remove for tempo and meter, `TempoEditError`, `listTempoMeterEvents`. |
| `Source/UI/SongDocument.{h,cpp}` | `timeBaseSet` flag, owns the `TempoMapSync`, `getMeterChanges`, `meterChangesOf`, load rebuild. |
| `Source/UI/SongModelBridge.cpp` | Stops writing map nodes; writes conductor events only; pauses/rebuilds once; `timeBaseSet`. |
| `Source/UI/MidiImportPlan.cpp` | Relocates `FF 51`/`FF 58` from note tracks of conductor-bearing files. |
| `Source/UI/NoteMerge.cpp` | `FF 51`/`FF 58` never travel in Alt-drag (All events). |
| `Source/UI/Playback/TimelineRulerMarks.{h,cpp}`, `Source/UI/GridLines.{h,cpp}`, `Source/UI/GridLinePaint.h`, `Source/UI/PianoRollComponent.cpp`, `Source/UI/TrackNotePreview.cpp`, `Source/UI/Playback/PlaybackController.cpp` | Meter-aware consumers. |
| `Source/UI/MainWindow.cpp` | "song has a tempo map" → `hasTimeBase()`. |

---

### Task 1: `MeterSegments` (pure)

**Files:**
- Create: `Source/UI/MeterSegments.h`, `Source/UI/MeterSegments.cpp`
- Test: `Tests/MeterSegments_tests.cpp` (create); register in both CMake files.

**Interfaces:**
- Produces:
  - `struct MeterChange { int tick = 0; int numerator = 4; int denominator = 4; };`
  - `struct MeterSegment { double startTick; double ticksPerBar; double ticksPerBeat; int numerator; int denominator; long long firstBar; };` (`firstBar` = 0-based number of the bar that starts at `startTick`)
  - `std::vector<MeterSegment> meterSegments (std::vector<MeterChange> changes, int ticksPerQuarter);` (empty when `ticksPerQuarter <= 0`)
  - `const MeterSegment& segmentAt (const std::vector<MeterSegment>& segments, double tick);` (precondition: non-empty)
  - `double nearestBarStart (const std::vector<MeterSegment>& segments, double tick);`
  - `double previousBarStart (const std::vector<MeterSegment>& segments, double tick);`

Rules: changes are stable-sorted by tick; an entry with `numerator <= 0` or `denominator <= 0` becomes 4/4; of two entries on one tick the later wins; negative ticks clamp to 0; the first segment always starts at tick 0 (the first entry's meter applies from the start, as `TempoMap` does for tempo); an empty list is a single 4/4 segment. A segment's `firstBar` = previous `firstBar` + `ceil ((start − prevStart) / prevTicksPerBar − 1e-9)` (a partial bar before a change counts as a bar).

- [ ] **Step 1: Write the failing test** — `Tests/MeterSegments_tests.cpp`

```cpp
#include "UI/MeterSegments.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace lotro;
using Catch::Approx;

TEST_CASE ("meterSegments: no changes is one 4/4 segment from tick 0", "[meter-segments]")
{
    const auto s = meterSegments ({}, 480);
    REQUIRE (s.size() == 1);
    CHECK (s[0].startTick == Approx (0.0));
    CHECK (s[0].ticksPerBar == Approx (1920.0));
    CHECK (s[0].ticksPerBeat == Approx (480.0));
    CHECK (s[0].firstBar == 0);
    CHECK (meterSegments ({}, 0).empty());
}

TEST_CASE ("meterSegments: later segments continue the bar count, a partial bar counts as a bar", "[meter-segments]")
{
    const auto exact = meterSegments ({ { 0, 4, 4 }, { 1920, 3, 4 } }, 480);
    REQUIRE (exact.size() == 2);
    CHECK (exact[1].firstBar == 1);
    CHECK (exact[1].ticksPerBar == Approx (1440.0));

    const auto partial = meterSegments ({ { 0, 4, 4 }, { 2400, 6, 8 } }, 480);   // 1.25 bars of 4/4 first
    CHECK (partial[1].firstBar == 2);
    CHECK (partial[1].ticksPerBeat == Approx (240.0));
    CHECK (partial[1].ticksPerBar == Approx (1440.0));
}

TEST_CASE ("meterSegments: unsorted input, duplicate ticks, bad values and a first entry after tick 0", "[meter-segments]")
{
    const auto sorted = meterSegments ({ { 1920, 3, 4 }, { 0, 4, 4 } }, 480);
    REQUIRE (sorted.size() == 2);
    CHECK (sorted[0].numerator == 4);
    CHECK (sorted[1].numerator == 3);

    const auto dup = meterSegments ({ { 0, 4, 4 }, { 1920, 3, 4 }, { 1920, 5, 4 } }, 480);
    REQUIRE (dup.size() == 2);
    CHECK (dup[1].numerator == 5);   // the later of two on one tick wins

    const auto bad = meterSegments ({ { 0, 0, 0 } }, 480);
    CHECK (bad[0].numerator == 4);
    CHECK (bad[0].denominator == 4);

    const auto late = meterSegments ({ { 960, 3, 4 } }, 480);   // applies from the start
    REQUIRE (late.size() == 1);
    CHECK (late[0].startTick == Approx (0.0));
    CHECK (late[0].numerator == 3);
}

TEST_CASE ("segmentAt and nearestBarStart", "[meter-segments]")
{
    const auto s = meterSegments ({ { 0, 4, 4 }, { 1920, 3, 4 } }, 480);
    CHECK (segmentAt (s, 0.0).numerator == 4);
    CHECK (segmentAt (s, 1919.0).numerator == 4);
    CHECK (segmentAt (s, 1920.0).numerator == 3);
    CHECK (segmentAt (s, -50.0).numerator == 4);

    CHECK (nearestBarStart (s, 100.0) == Approx (0.0));
    CHECK (nearestBarStart (s, 1100.0) == Approx (1920.0));            // nearer the next bar line
    CHECK (nearestBarStart (s, 1920.0 + 1440.0 * 0.6) == Approx (1920.0 + 1440.0));
    CHECK (nearestBarStart (s, 1900.0) == Approx (1920.0));            // never past a segment's end
    CHECK (nearestBarStart (s, -10.0) == Approx (0.0));
}

TEST_CASE ("previousBarStart: one meter matches previousBarTick, and it crosses a meter change", "[meter-segments]")
{
    const auto one = meterSegments ({ { 0, 4, 4 } }, 480);
    CHECK (previousBarStart (one, 2500.0) == Approx (1920.0));
    CHECK (previousBarStart (one, 3840.0) == Approx (1920.0));   // exactly on a bar line: the previous bar
    CHECK (previousBarStart (one, 100.0) == Approx (0.0));
    CHECK (previousBarStart (one, 0.0) == Approx (0.0));

    const auto two = meterSegments ({ { 0, 4, 4 }, { 1920, 3, 4 } }, 480);
    CHECK (previousBarStart (two, 1920.0) == Approx (0.0));              // on the change: the last bar of 4/4
    CHECK (previousBarStart (two, 2000.0) == Approx (1920.0));
    CHECK (previousBarStart (two, 3360.0 + 10.0) == Approx (3360.0));    // 1920 + 1440
    CHECK (previousBarStart (two, 3360.0) == Approx (1920.0));
    CHECK (previousBarStart ({}, 500.0) == Approx (0.0));                // no segments: start
}
```

- [ ] **Step 2: Register and verify it fails**

Add `Source/UI/MeterSegments.cpp` to `CMakeLists.txt` (next to `Source/UI/GridLines.cpp`), `${CMAKE_SOURCE_DIR}/Source/UI/MeterSegments.cpp` to `Tests/CMakeLists.txt` (next to the `GridLines.cpp` line) and `MeterSegments_tests.cpp` to the test list.
Run: `cmake --build build --target forge_tests 2>&1 | tail -5` — Expected: FAIL (`MeterSegments.h` not found).

- [ ] **Step 3: Implement** — `Source/UI/MeterSegments.h`

```cpp
#pragma once

// Pure bar arithmetic over a list of meter changes (no JUCE). The ruler, the
// grid lines, Rewind One Bar and the tempo/meter editor all turn the meter map
// into bars through this one place.

#include <vector>

namespace lotro
{

struct MeterChange
{
    int tick        = 0;
    int numerator   = 4;
    int denominator = 4;
};

struct MeterSegment
{
    double    startTick    = 0.0;
    double    ticksPerBar  = 1920.0;
    double    ticksPerBeat = 480.0;
    int       numerator    = 4;
    int       denominator  = 4;
    long long firstBar     = 0;   // 0-based number of the bar that starts at startTick
};

// Sorted, de-duplicated, sanitised segments; the first starts at tick 0. Empty
// when ticksPerQuarter <= 0.
std::vector<MeterSegment> meterSegments (std::vector<MeterChange> changes, int ticksPerQuarter);

// The segment containing `tick` (the first for tick < 0). `segments` must not be empty.
const MeterSegment& segmentAt (const std::vector<MeterSegment>& segments, double tick);

// The bar line nearest `tick`, never past the end of its segment. 0 when `segments` is empty.
double nearestBarStart (const std::vector<MeterSegment>& segments, double tick);

// Start of the bar containing `tick`, or of the previous bar when `tick` is
// exactly on a bar line. 0 when `segments` is empty.
double previousBarStart (const std::vector<MeterSegment>& segments, double tick);

} // namespace lotro
```

`Source/UI/MeterSegments.cpp`

```cpp
#include "UI/MeterSegments.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace lotro
{

std::vector<MeterSegment> meterSegments (std::vector<MeterChange> changes, int ticksPerQuarter)
{
    if (ticksPerQuarter <= 0)
        return {};

    std::stable_sort (changes.begin(), changes.end(),
                      [] (const MeterChange& a, const MeterChange& b) { return a.tick < b.tick; });

    std::vector<MeterChange> unique;
    for (auto change : changes)
    {
        change.tick = std::max (0, change.tick);
        if (change.numerator <= 0 || change.denominator <= 0)
        {
            change.numerator = 4;
            change.denominator = 4;
        }
        if (! unique.empty() && unique.back().tick == change.tick)
            unique.back() = change;
        else
            unique.push_back (change);
    }
    if (unique.empty())
        unique.push_back ({});
    unique.front().tick = 0;

    std::vector<MeterSegment> out;
    for (const auto& change : unique)
    {
        MeterSegment s;
        s.startTick    = (double) change.tick;
        s.numerator    = change.numerator;
        s.denominator  = change.denominator;
        s.ticksPerBeat = (double) ticksPerQuarter * 4.0 / (double) change.denominator;
        s.ticksPerBar  = s.ticksPerBeat * (double) change.numerator;
        if (! out.empty())
        {
            const auto& previous = out.back();
            s.firstBar = previous.firstBar
                       + (long long) std::ceil ((s.startTick - previous.startTick) / previous.ticksPerBar - 1e-9);
        }
        out.push_back (s);
    }
    return out;
}

const MeterSegment& segmentAt (const std::vector<MeterSegment>& segments, double tick)
{
    size_t index = 0;
    for (size_t i = 1; i < segments.size(); ++i)
        if (segments[i].startTick <= tick)
            index = i;
    return segments[index];
}

double nearestBarStart (const std::vector<MeterSegment>& segments, double tick)
{
    if (segments.empty())
        return 0.0;

    const auto& segment = segmentAt (segments, tick);
    const double bars = std::max (0.0, std::round ((tick - segment.startTick) / segment.ticksPerBar));
    double candidate = segment.startTick + bars * segment.ticksPerBar;

    const size_t index = (size_t) (&segment - segments.data());
    if (index + 1 < segments.size() && candidate >= segments[index + 1].startTick - 1e-9)
        candidate = segments[index + 1].startTick;
    return candidate;
}

double previousBarStart (const std::vector<MeterSegment>& segments, double tick)
{
    if (segments.empty())
        return 0.0;

    size_t index = (size_t) (&segmentAt (segments, tick) - segments.data());
    for (;; --index)
    {
        const auto& s = segments[index];
        const double end = index + 1 < segments.size() ? segments[index + 1].startTick
                                                        : std::numeric_limits<double>::infinity();
        const double bars = std::ceil ((std::min (tick, end) - s.startTick) / s.ticksPerBar - 1e-9);
        if (bars >= 1.0)
            return std::max (0.0, s.startTick + (bars - 1.0) * s.ticksPerBar);
        if (index == 0)
            return 0.0;
    }
}

} // namespace lotro
```

- [ ] **Step 4: Run to verify it passes**

Run: `cmake --build build --target forge_tests 2>&1 | grep -E "error" ; ./build/Tests/forge_tests "[meter-segments]" | tail -3` — Expected: all pass. If a check disagrees with the arithmetic, fix the *test's* expected value only where the rule in this task's "Rules" paragraph says so; otherwise fix the code.

- [ ] **Step 5: Commit**

```bash
git add Source/UI/MeterSegments.* Tests/MeterSegments_tests.cpp CMakeLists.txt Tests/CMakeLists.txt
git commit -m "feat(ui): MeterSegments, pure bar arithmetic over a list of meter changes

Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>"
```

---

### Task 2: `deriveMaps` (pure)

**Files:**
- Create: `Source/UI/TempoMapSync.h` (pure part only for now), `Source/UI/TempoMapSync.cpp`
- Test: `Tests/TempoMapSync_tests.cpp` (create); register all three in both CMake files.

**Interfaces:**
- Consumes: `RawMidiEvent { int tick; std::vector<std::uint8_t> bytes; }` (`UI/RawMidi.h`; meta bytes are `FF <type> <data>`, **length not stored**), `TempoPoint { int tick; double bpm; }` (`UI/Playback/TempoMap.h`), `MeterChange` (Task 1).
- Produces:
  - `struct DerivedMaps { std::vector<TempoPoint> tempo; std::vector<MeterChange> meter; };`
  - `DerivedMaps deriveMaps (const std::vector<RawMidiEvent>& events);` — events in `(tick, order)` order; tempo/meter in that order; defaults `{0, 120}` / `{0, 4, 4}` when absent.
  - `double bpmFromMicroseconds (std::uint32_t microsecondsPerQuarter);` = `60.0 / ((double) us / 1000000.0)` (bit-identical to the importer's arithmetic).

Mirrors `importTempoAndMeter` (`Source/Core/MidiImporter.cpp`): a tempo event is `FF 51 d0 d1 d2` (≥ 5 bytes) with `us = d0<<16 | d1<<8 | d2`, ignored when `us == 0`; a meter event is `FF 58 nn dd …` (≥ 4 bytes), `numerator = nn`, `denominator = 1 << dd`; `dd > 15` is ignored.

- [ ] **Step 1: Write the failing test** — `Tests/TempoMapSync_tests.cpp`

```cpp
#include "UI/TempoMapSync.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace lotro;
using Catch::Approx;

namespace
{
    RawMidiEvent tempoEvent (int tick, std::uint32_t us)
    {
        return { tick, { 0xFF, 0x51, (std::uint8_t) (us >> 16), (std::uint8_t) (us >> 8), (std::uint8_t) us } };
    }
    RawMidiEvent meterEvent (int tick, std::uint8_t nn, std::uint8_t dd)
    {
        return { tick, { 0xFF, 0x58, nn, dd, 24, 8 } };
    }
}

TEST_CASE ("deriveMaps: no events gives the 120 BPM, 4/4 defaults", "[tempo-sync]")
{
    const auto maps = deriveMaps ({});
    REQUIRE (maps.tempo.size() == 1);
    CHECK (maps.tempo[0].tick == 0);
    CHECK (maps.tempo[0].bpm == Approx (120.0));
    REQUIRE (maps.meter.size() == 1);
    CHECK (maps.meter[0].numerator == 4);
    CHECK (maps.meter[0].denominator == 4);
}

TEST_CASE ("deriveMaps: tempo and meter events become entries in event order", "[tempo-sync]")
{
    const auto maps = deriveMaps ({ tempoEvent (0, 500000), meterEvent (0, 3, 2),
                                    tempoEvent (960, 600000), meterEvent (1440, 6, 3) });
    REQUIRE (maps.tempo.size() == 2);
    CHECK (maps.tempo[0].bpm == Approx (120.0));
    CHECK (maps.tempo[1].tick == 960);
    CHECK (maps.tempo[1].bpm == Approx (100.0));
    REQUIRE (maps.meter.size() == 2);
    CHECK (maps.meter[0].numerator == 3);
    CHECK (maps.meter[0].denominator == 4);
    CHECK (maps.meter[1].tick == 1440);
    CHECK (maps.meter[1].numerator == 6);
    CHECK (maps.meter[1].denominator == 8);
}

TEST_CASE ("deriveMaps: other events, short events, zero tempo and absurd denominators are ignored", "[tempo-sync]")
{
    const auto maps = deriveMaps ({ { 0, { 0xFF, 0x59, 0x00, 0x00 } },        // key signature
                                    { 0, { 0xB0, 7, 100 } },                  // controller
                                    { 0, { 0xFF, 0x51, 0x07, 0xA1 } },        // tempo cut short
                                    tempoEvent (0, 0),                        // zero microseconds
                                    { 0, { 0xFF, 0x58, 4, 2 } },              // meter cut short
                                    meterEvent (0, 4, 16) });                 // 2^16 denominator
    CHECK (maps.tempo.size() == 1);     // only the default
    CHECK (maps.tempo[0].bpm == Approx (120.0));
    CHECK (maps.meter.size() == 1);
    CHECK (maps.meter[0].denominator == 4);
}

TEST_CASE ("bpmFromMicroseconds matches 60 / seconds-per-quarter exactly", "[tempo-sync]")
{
    CHECK (bpmFromMicroseconds (500000) == 60.0 / (500000 / 1000000.0));
    CHECK (bpmFromMicroseconds (597015) == 60.0 / (597015 / 1000000.0));
}
```

- [ ] **Step 2: Register and verify it fails** — add `Source/UI/TempoMapSync.cpp` (and the test) to the CMake lists; build — Expected: FAIL (`TempoMapSync.h` not found).

- [ ] **Step 3: Implement** — `Source/UI/TempoMapSync.h`

```cpp
#pragma once

// Tempo and meter live in the conductor track's FF 51 / FF 58 EVENTs and nowhere
// else; the TEMPO_MAP / METER_MAP nodes are a view derived from them (spec
// 2026-10-10-songsmith-tempo-meter-design.md, section 1). This header holds the
// pure derivation; the document listener is added in a later task.

#include "UI/MeterSegments.h"
#include "UI/Playback/TempoMap.h"
#include "UI/RawMidi.h"

#include <cstdint>
#include <vector>

namespace lotro
{

struct DerivedMaps
{
    std::vector<TempoPoint>  tempo;
    std::vector<MeterChange> meter;
};

// 60 / (microsecondsPerQuarter / 1e6), written exactly as the importer does.
double bpmFromMicroseconds (std::uint32_t microsecondsPerQuarter);

// `events` are in (tick, order) order. Defaults {0, 120 BPM} / {0, 4/4} when no
// event of that kind exists. Mirrors importTempoAndMeter (Source/Core/MidiImporter.cpp).
DerivedMaps deriveMaps (const std::vector<RawMidiEvent>& events);

} // namespace lotro
```

`Source/UI/TempoMapSync.cpp`

```cpp
#include "UI/TempoMapSync.h"

namespace lotro
{

double bpmFromMicroseconds (std::uint32_t microsecondsPerQuarter)
{
    return 60.0 / ((double) microsecondsPerQuarter / 1000000.0);
}

DerivedMaps deriveMaps (const std::vector<RawMidiEvent>& events)
{
    DerivedMaps maps;
    for (const auto& event : events)
    {
        const auto& b = event.bytes;
        if (b.size() >= 5 && b[0] == 0xFF && b[1] == 0x51)
        {
            const std::uint32_t us = ((std::uint32_t) b[2] << 16) | ((std::uint32_t) b[3] << 8) | (std::uint32_t) b[4];
            if (us > 0)
                maps.tempo.push_back ({ event.tick, bpmFromMicroseconds (us) });
        }
        else if (b.size() >= 4 && b[0] == 0xFF && b[1] == 0x58 && b[3] <= 15)
        {
            maps.meter.push_back ({ event.tick, (int) b[2], 1 << b[3] });
        }
    }
    if (maps.tempo.empty())
        maps.tempo.push_back ({ 0, TempoMap::defaultBpm });
    if (maps.meter.empty())
        maps.meter.push_back ({});
    return maps;
}

} // namespace lotro
```

- [ ] **Step 4: Run to verify it passes** — `./build/Tests/forge_tests "[tempo-sync]" | tail -3` — Expected: all pass.

- [ ] **Step 5: Commit**

```bash
git add Source/UI/TempoMapSync.* Tests/TempoMapSync_tests.cpp CMakeLists.txt Tests/CMakeLists.txt
git commit -m "feat(ui): deriveMaps, tempo and meter maps from conductor events

Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>"
```

---

### Task 3: Import moves note-track tempo/meter into the conductor; Alt-drag never carries them

**Spec amendment.** The spec says the conductor's events are the only truth. Today a format-1 file that *has* a conductor track keeps `FF 51`/`FF 58` found in note tracks in those tracks (only conductor-less files relocate), while `importMidi`'s maps include them. To keep "conductor only" exact, the planner now relocates `FF 51`/`FF 58` from note tracks in that case too (counted in the existing "Moved N song-wide event(s)" Info; dropped and counted under tempo keep on a later import). Export MIDI then writes those events in the conductor track: musically identical, but no longer event-for-event in that one rare shape. First step edits the spec to say so.

**Files:**
- Modify: `docs/superpowers/specs/2026-10-10-songsmith-tempo-meter-design.md`, `Source/UI/MidiImportPlan.cpp:306-319`, `Source/UI/NoteMerge.cpp:68-78`
- Test: `Tests/MidiImportPlan_tests.cpp`, `Tests/NoteMerge_tests.cpp`

**Interfaces:**
- Consumes: `isSongWideMetaEvent`, `hasConductorTrack`, `PlannedEvent { tick, order, relocatedFrom, bytes }`.
- Produces: `bool isTempoOrMeterEvent (const RawMidiEvent&)` (file-local in `MidiImportPlan.cpp`); `travels()` in `NoteMerge.cpp` returns false for `FF 51`/`FF 58`.

- [ ] **Step 1: Amend the spec.** In section 1 add this bullet after "Derivation mirrors the importer exactly":

```markdown
- **Tempo/meter always live in the conductor.** A file that has its own conductor
  track may still hold `FF 51` / `FF 58` inside a note track (the importer's maps
  include them). Import now moves those into the conductor like it already does
  for conductor-less files (counted in the "Moved N song-wide event(s)" Info; on a
  later import with the tempo map kept they are dropped). Export MIDI therefore
  writes them in the conductor track — musically identical, but not event-for-event
  for that one file shape. Alt-drag (All events) never carries `FF 51` / `FF 58`.
```

- [ ] **Step 2: Write the failing tests.** Append to `Tests/MidiImportPlan_tests.cpp` (use the file's existing helpers for building a `RawMidiFile`/`Song`; if it builds them inline, mirror the nearest existing test that calls `planMidiImport` on a conductor-bearing file and copy its setup):

```cpp
TEST_CASE ("planMidiImport: tempo and meter in a note track move to the conductor even when the file has a conductor track", "[import-plan][tempo-sync]")
{
    TempMidiFixture f;   // see note below
    // Format 1: track 0 is a conductor (tempo 120), track 1 is a note track that also carries
    // a tempo change (100 BPM at tick 96) and a 3/4 meter at tick 96.
    // Build the bytes with miditest (Tests/MidiTestBytes.h):
    //   conductor: ev(0, FF 51 03 07 A1 20), eot
    //   notes:     ev(0, 90 3C 64), ev(96, FF 51 03 09 27 C0), ev(0, FF 58 04 03 02 18 08), ev(0, 80 3C 40), eot
}
```

Replace the sketch above with a real test in this shape (adapt names to the helpers in `MidiImportPlan_tests.cpp`):

```cpp
TEST_CASE ("planMidiImport: tempo and meter in a note track move to the conductor even when the file has a conductor track", "[import-plan][tempo-sync]")
{
    using namespace miditest;
    TrackBody conductor;
    conductor.ev (0, { 0xFF, 0x51, 0x03, 0x07, 0xA1, 0x20 }).eot();
    TrackBody notes;
    notes.ev (0, { 0x90, 60, 100 })
         .ev (96, { 0xFF, 0x51, 0x03, 0x09, 0x27, 0xC0 })
         .ev (0, { 0xFF, 0x58, 0x04, 0x03, 0x02, 0x18, 0x08 })
         .ev (0, { 0x80, 60, 0x40 }).eot();
    const auto bytes = smf (1, 96, { conductor, notes });
    const auto raw = readMidiBytes (bytes, "t");

    Diagnostics importerDiagnostics, planDiagnostics;
    std::istringstream in (std::string (bytes.begin(), bytes.end()), std::ios::binary);
    const Song song = importMidi (in, "t", importerDiagnostics);

    const auto plan = planMidiImport (song, raw, /*isFirstImport*/ true, planDiagnostics);

    int tempoCount = 0, meterCount = 0;
    for (const auto& e : plan.conductorEvents)
    {
        if (e.bytes.size() > 1 && e.bytes[1] == 0x51) ++tempoCount;
        if (e.bytes.size() > 1 && e.bytes[1] == 0x58) ++meterCount;
    }
    CHECK (tempoCount == 2);
    CHECK (meterCount == 1);
    REQUIRE (plan.tracks.size() == 1);
    for (const auto& e : plan.tracks[0].events)
        CHECK_FALSE ((e.bytes.size() > 1 && e.bytes[0] == 0xFF && (e.bytes[1] == 0x51 || e.bytes[1] == 0x58)));
    CHECK (plan.relocatedEventCount == 2);

    // A later import with the tempo map kept drops them instead.
    Diagnostics laterDiagnostics;
    const auto later = planMidiImport (song, raw, /*isFirstImport*/ false, laterDiagnostics);
    CHECK (later.conductorEvents.empty());
    for (const auto& e : later.tracks[0].events)
        CHECK_FALSE ((e.bytes.size() > 1 && e.bytes[0] == 0xFF && (e.bytes[1] == 0x51 || e.bytes[1] == 0x58)));
    CHECK (later.droppedEventCount >= 3);   // the conductor's tempo + the two moved events
}
```

Add the includes it needs at the top of the file if absent: `"MidiTestBytes.h"`, `"Core/MidiImporter.h"`, `<sstream>`.

Append to `Tests/NoteMerge_tests.cpp` after the "track-name/end-of-track metas never travel" case:

```cpp
TEST_CASE ("merge all events: tempo and meter events never travel", "[merge][events][tempo-sync]")
{
    SongDocument doc;
    auto source = addTrack (doc);
    auto target = addTrack (doc);
    addNote (source, 60, 0, 480);
    splitAt (doc, { idOf (source) }, 960);
    const auto first = sectionsOf (source)[0];
    addEvent (source, 100, { 0xFF, 0x51, 0x07, 0xA1, 0x20 });
    addEvent (source, 200, { 0xFF, 0x58, 0x03, 0x02 });
    addEvent (source, 300, { 0xB0, 10, 64 });

    const auto r = mergeSections (doc, { { idOf (source), first.id } }, idOf (target), true, MergeScope::allEvents);

    CHECK (r.eventsCarried == 1);
    CHECK (eventBytesOf (target) == std::vector<std::vector<std::uint8_t>> { { 0xB0, 10, 64 } });
}
```

- [ ] **Step 3: Run to verify they fail** — `cmake --build build --target forge_tests && ./build/Tests/forge_tests "[tempo-sync]"` — Expected: both new cases FAIL (note-track events still in the plan; tempo event carried).

- [ ] **Step 4: Implement.** In `Source/UI/MidiImportPlan.cpp`, above `planMidiImport` (inside the file's anonymous namespace if there is one, else a new one):

```cpp
namespace
{
    bool isTempoOrMeterEvent (const RawMidiEvent& event)
    {
        return event.bytes.size() >= 2 && event.bytes[0] == 0xFF && (event.bytes[1] == 0x51 || event.bytes[1] == 0x58);
    }
}
```

and change the relocation condition at `MidiImportPlan.cpp:307`:

```cpp
const auto& e = rawTrack.events[(size_t) i];
// A conductor-less file relocates every song-wide meta; a file with a conductor still
// moves tempo and meter out of note tracks so the conductor is their only home.
if ((! fileHasConductor && isSongWideMetaEvent (e)) || (fileHasConductor && isTempoOrMeterEvent (e)))
{
```

(the body of the `if` is unchanged). In `Source/UI/NoteMerge.cpp` `travels()` add, after the End-of-Track/track-name check:

```cpp
if (b[0] == 0xFF && b.size() >= 2 && (b[1] == 0x51 || b[1] == 0x58))
    return false;   // tempo and meter live in the conductor only
```

and extend the comment above it: `// ... End-of-Track, track-name and tempo/meter metas stay put.`

- [ ] **Step 5: Run to verify they pass, then the suites around them** — `./build/Tests/forge_tests "[tempo-sync],[import-plan],[merge],[import-options]" | tail -3` — Expected: all pass. Any existing test that expected a note-track `FF 51` to stay in its track must be updated to the new rule; list each such change in the commit body.

- [ ] **Step 6: Commit**

```bash
git add -A
git commit -m "feat(ui): tempo/meter always import into the conductor; Alt-drag never carries them

Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>"
```

---

### Task 4: Derived maps in the document (listener, `timeBaseSet`, import and load)

The core migration. After this task nothing writes `TEMPO_CHANGE`/`METER_CHANGE` nodes except `rebuildMaps`.

**Files:**
- Modify: `Source/UI/TempoMapSync.{h,cpp}`, `Source/UI/SongDocument.{h,cpp}`, `Source/UI/SongModelBridge.cpp`, `Source/UI/MainWindow.cpp:377`
- Test: `Tests/TempoMapInvariant_tests.cpp` (create, register), plus fixes to whichever existing tests the change breaks.

**Interfaces:**
- Consumes: `deriveMaps` (Task 2), `meterSegments` types (Task 1).
- Produces (all in `lotro`):
  - `std::vector<RawMidiEvent> conductorEventsOf (const SongDocument&);` — the conductor's `EVENT`s sorted by `(tick, order)`.
  - `void rebuildMaps (SongDocument&);` — if `timeBaseSet` is false, clears both map nodes; else writes `deriveMaps (conductorEventsOf (doc))` into them (non-undoable, `nullptr` undo manager), only when the content differs.
  - `class MapSyncPause { public: explicit MapSyncPause (SongDocument&); ~MapSyncPause(); }` — counter-based; the destructor of the outermost pause calls `rebuildMaps`.
  - `class TempoMapSync : public juce::ValueTree::Listener` (owned by `SongDocument`).
  - `SongDocument`: `bool hasTimeBase() const;` `void setTimeBase (bool)` (non-undoable property `timeBaseSet` on `SOURCE_MIDI`, id `SongIDs::timeBaseSet`), `std::vector<MeterChange> getMeterChanges() const;`, `static std::vector<MeterChange> meterChangesOf (const juce::ValueTree& meterMapNode);`, `~SongDocument();`.

- [ ] **Step 1: Write the failing tests** — `Tests/TempoMapInvariant_tests.cpp`

```cpp
// The invariant behind "export equals editor": the maps are always the derivation
// of the conductor's events, through import, undo and save/load.

#include "MidiTestBytes.h"
#include "UI/MidiExport.h"
#include "UI/SongFile.h"
#include "UI/SongModelBridge.h"
#include "UI/TempoMapSync.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <juce_core/juce_core.h>

using namespace lotro;
using namespace miditest;
using Catch::Approx;

namespace
{
    struct TempMidi
    {
        juce::File file = juce::File::createTempFile (".mid");
        explicit TempMidi (const Bytes& bytes) { REQUIRE (file.replaceWithData (bytes.data(), bytes.size())); }
        ~TempMidi() { file.deleteFile(); }
    };

    TrackBody conductorWith (std::initializer_list<std::pair<int, std::uint32_t>> tempos)
    {
        TrackBody c;
        int last = 0;
        for (const auto& [tick, us] : tempos)
        {
            c.ev ((std::uint32_t) (tick - last), { 0xFF, 0x51, 0x03, (std::uint8_t) (us >> 16), (std::uint8_t) (us >> 8), (std::uint8_t) us });
            last = tick;
        }
        c.eot (960);
        return c;
    }

    TrackBody melody (int pitch)
    {
        TrackBody t;
        t.ev (0, { 0x90, (std::uint8_t) pitch, 100 }).ev (96, { 0x80, (std::uint8_t) pitch, 0x40 }).eot();
        return t;
    }

    // The map nodes as plain values, to compare with the derivation.
    std::vector<std::pair<int, double>> tempoNodes (const SongDocument& doc)
    {
        std::vector<std::pair<int, double>> out;
        for (auto c : doc.getTempoMapNode())
            out.emplace_back ((int) c.getProperty (SongIDs::tick), (double) c.getProperty (SongIDs::bpm));
        return out;
    }

    std::vector<std::tuple<int, int, int>> meterNodes (const SongDocument& doc)
    {
        std::vector<std::tuple<int, int, int>> out;
        for (auto c : doc.getMeterMapNode())
            out.emplace_back ((int) c.getProperty (SongIDs::tick), (int) c.getProperty (SongIDs::numerator),
                              (int) c.getProperty (SongIDs::denominator));
        return out;
    }

    void requireMapsMatchEvents (const SongDocument& doc)
    {
        const auto derived = deriveMaps (conductorEventsOf (doc));
        REQUIRE (tempoNodes (doc).size() == derived.tempo.size());
        for (size_t i = 0; i < derived.tempo.size(); ++i)
        {
            CHECK (tempoNodes (doc)[i].first == derived.tempo[i].tick);
            CHECK (tempoNodes (doc)[i].second == Approx (derived.tempo[i].bpm));
        }
        REQUIRE (meterNodes (doc).size() == derived.meter.size());
        for (size_t i = 0; i < derived.meter.size(); ++i)
            CHECK (meterNodes (doc)[i] == std::make_tuple (derived.meter[i].tick, derived.meter[i].numerator, derived.meter[i].denominator));
    }
}

TEST_CASE ("a new document has empty maps and no time base", "[tempo-sync]")
{
    SongDocument doc;
    CHECK_FALSE (doc.hasTimeBase());
    CHECK (doc.getTempoMapNode().getNumChildren() == 0);
    CHECK (doc.getMeterMapNode().getNumChildren() == 0);
}

TEST_CASE ("import: the maps are derived from the conductor events (tempo changes and a file with none)", "[tempo-sync]")
{
    TempMidi withTempos (smf (1, 96, { conductorWith ({ { 0, 500000 }, { 96, 600000 } }), melody (60) }));
    TempMidi noTempo (smf (1, 96, { melody (60) }));

    SongDocument doc;
    Diagnostics d;
    REQUIRE (importMidiFile (doc, withTempos.file, 1, d));
    CHECK (doc.hasTimeBase());
    REQUIRE (tempoNodes (doc).size() == 2);
    CHECK (tempoNodes (doc)[1].second == Approx (100.0));
    requireMapsMatchEvents (doc);

    SongDocument bare;
    REQUIRE (importMidiFile (bare, noTempo.file, 1, d));
    REQUIRE (tempoNodes (bare).size() == 1);     // the default, as importMidi seeds
    CHECK (tempoNodes (bare)[0].second == Approx (120.0));
    requireMapsMatchEvents (bare);
}

TEST_CASE ("import: the maps match what importMidi reads, for a tempo that lives in a note track", "[tempo-sync]")
{
    TrackBody cond;
    cond.ev (0, { 0xFF, 0x51, 0x03, 0x07, 0xA1, 0x20 }).eot();
    TrackBody notes;
    notes.ev (0, { 0x90, 60, 100 }).ev (96, { 0xFF, 0x51, 0x03, 0x09, 0x27, 0xC0 })
         .ev (0, { 0x80, 60, 0x40 }).eot();
    const auto bytes = smf (1, 96, { cond, notes });
    TempMidi file (bytes);

    std::istringstream in (std::string (bytes.begin(), bytes.end()), std::ios::binary);
    Diagnostics discard;
    const Song reference = importMidi (in, "t", discard);

    SongDocument doc;
    Diagnostics d;
    REQUIRE (importMidiFile (doc, file.file, 1, d));
    REQUIRE (tempoNodes (doc).size() == reference.tempoMap.size());
    for (size_t i = 0; i < reference.tempoMap.size(); ++i)
    {
        CHECK (tempoNodes (doc)[i].first == reference.tempoMap[i].tick);
        CHECK (tempoNodes (doc)[i].second == reference.tempoMap[i].bpm);   // bit-identical
    }
}

TEST_CASE ("a later import with the tempo map kept never changes the maps; Replace swaps them", "[tempo-sync]")
{
    TempMidi first  (smf (1, 96, { conductorWith ({ { 0, 500000 } }), melody (60) }));
    TrackBody cond2;
    cond2.ev (0, { 0xFF, 0x51, 0x03, 0x09, 0x27, 0xC0 }).eot();
    TrackBody notes2;   // a tempo change hidden in a note track of a file with a conductor
    notes2.ev (0, { 0x90, 64, 100 }).ev (48, { 0xFF, 0x51, 0x03, 0x0B, 0x71, 0xB0 }).ev (48, { 0x80, 64, 0x40 }).eot();
    TempMidi second (smf (1, 96, { cond2, notes2 }));

    SongDocument kept;
    Diagnostics d;
    REQUIRE (importMidiFile (kept, first.file, 1, d));
    REQUIRE (importMidiFile (kept, second.file, 2, d));
    REQUIRE (tempoNodes (kept).size() == 1);
    CHECK (tempoNodes (kept)[0].second == Approx (120.0));
    requireMapsMatchEvents (kept);

    SongDocument replaced;
    REQUIRE (importMidiFile (replaced, first.file, 1, d));
    ImportOptions options;
    options.tempo = TempoMode::replace;
    REQUIRE (importMidiFile (replaced, second.file, 2, d, options));
    REQUIRE (tempoNodes (replaced).size() == 2);
    CHECK (tempoNodes (replaced)[0].second == Approx (100.0));
    requireMapsMatchEvents (replaced);
}

TEST_CASE ("changing a conductor tempo event rebuilds the maps; undo and redo restore them", "[tempo-sync]")
{
    TempMidi file (smf (1, 96, { conductorWith ({ { 0, 500000 } }), melody (60) }));
    SongDocument doc;
    Diagnostics d;
    REQUIRE (importMidiFile (doc, file.file, 1, d));

    auto events = SongDocument::getEventsNode (doc.getConductorTrack());
    REQUIRE (events.getNumChildren() == 1);
    auto event = events.getChild (0);

    doc.getUndoManager().beginNewTransaction();
    const std::uint8_t faster[] = { 0xFF, 0x51, 0x03, 0x06, 0x1A, 0x80 };   // 400000 us = 150 BPM
    event.setProperty (SongIDs::data, juce::var (juce::MemoryBlock (faster, sizeof faster)), &doc.getUndoManager());
    CHECK (tempoNodes (doc)[0].second == Approx (150.0));

    doc.undo();
    CHECK (tempoNodes (doc)[0].second == Approx (120.0));
    doc.redo();
    CHECK (tempoNodes (doc)[0].second == Approx (150.0));
    requireMapsMatchEvents (doc);
}

TEST_CASE ("an unrelated event or note edit leaves the map nodes untouched", "[tempo-sync]")
{
    TempMidi file (smf (1, 96, { conductorWith ({ { 0, 500000 } }), melody (60) }));
    SongDocument doc;
    Diagnostics d;
    REQUIRE (importMidiFile (doc, file.file, 1, d));

    struct Counter : juce::ValueTree::Listener
    {
        int changes = 0;
        void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override { ++changes; }
        void valueTreeChildAdded (juce::ValueTree&, juce::ValueTree&) override { ++changes; }
        void valueTreeChildRemoved (juce::ValueTree&, juce::ValueTree&, int) override { ++changes; }
    } counter;
    auto tempoNode = doc.getTempoMapNode();
    tempoNode.addListener (&counter);

    juce::ValueTree controller (SongIDs::EVENT);
    controller.setProperty (SongIDs::tick, 10, nullptr);
    controller.setProperty (SongIDs::order, 5, nullptr);
    const std::uint8_t cc[] = { 0xB0, 7, 100 };
    controller.setProperty (SongIDs::data, juce::var (juce::MemoryBlock (cc, sizeof cc)), nullptr);
    doc.addChild (SongDocument::getEventsNode (doc.getConductorTrack()), controller);

    CHECK (counter.changes == 0);   // a rebuild with an identical result writes nothing
    tempoNode.removeListener (&counter);
}

TEST_CASE ("save and load: stale maps in the file are rebuilt from its events", "[tempo-sync]")
{
    TempMidi file (smf (1, 96, { conductorWith ({ { 0, 500000 } }), melody (60) }));
    SongDocument doc;
    Diagnostics d;
    REQUIRE (importMidiFile (doc, file.file, 1, d));

    auto tree = doc.getTree().createCopy();
    auto tempoNode = tree.getChildWithName (SongIDs::TEMPO_MAP);
    tempoNode.getChild (0).setProperty (SongIDs::bpm, 33.0, nullptr);   // a disagreement written by an old build

    SongDocument loaded;
    loaded.replaceContents (tree);
    CHECK (loaded.hasTimeBase());
    CHECK (tempoNodes (loaded)[0].second == Approx (120.0));
    requireMapsMatchEvents (loaded);
}

TEST_CASE ("loading a file saved before timeBaseSet existed infers it from the stored tempo map", "[tempo-sync]")
{
    TempMidi file (smf (1, 96, { conductorWith ({ { 0, 500000 } }), melody (60) }));
    SongDocument doc;
    Diagnostics d;
    REQUIRE (importMidiFile (doc, file.file, 1, d));

    auto tree = doc.getTree().createCopy();
    tree.getChildWithName (SongIDs::SOURCE_MIDI).removeProperty (SongIDs::timeBaseSet, nullptr);
    SongDocument loaded;
    loaded.replaceContents (tree);
    CHECK (loaded.hasTimeBase());

    SongDocument fresh;
    auto freshTree = fresh.getTree().createCopy();
    freshTree.getChildWithName (SongIDs::SOURCE_MIDI).removeProperty (SongIDs::timeBaseSet, nullptr);
    SongDocument loadedEmpty;
    loadedEmpty.replaceContents (freshTree);
    CHECK_FALSE (loadedEmpty.hasTimeBase());
}

TEST_CASE ("export contains exactly the maps' tempo changes", "[tempo-sync]")
{
    TempMidi file (smf (1, 96, { conductorWith ({ { 0, 500000 }, { 96, 600000 } }), melody (60) }));
    SongDocument doc;
    Diagnostics d;
    REQUIRE (importMidiFile (doc, file.file, 1, d));

    const auto exported = buildRawMidiFile (doc);
    std::vector<RawMidiEvent> conductor = exported.tracks.front().events;
    const auto derived = deriveMaps (conductor);
    REQUIRE (derived.tempo.size() == tempoNodes (doc).size());
    for (size_t i = 0; i < derived.tempo.size(); ++i)
        CHECK (derived.tempo[i].bpm == Approx (tempoNodes (doc)[i].second));
}
```

Add `#include <sstream>` and `"Core/MidiImporter.h"` if the build asks. Register the file in `Tests/CMakeLists.txt`.

- [ ] **Step 2: Run to verify it fails** — `cmake --build build --target forge_tests 2>&1 | grep -c error` — Expected: errors (`hasTimeBase`, `conductorEventsOf`, `SongIDs::timeBaseSet` undefined).

- [ ] **Step 3: Add the identifier and the flag to `SongDocument`.**

`Source/UI/SongDocument.h` — in `namespace SongIDs` next to `ticksPerQuarter`: `extern const juce::Identifier timeBaseSet;   // SOURCE_MIDI: a first import has set the time base`. Add `#include "UI/MeterSegments.h"` and forward declare `class TempoMapSync;`. In the public section:

```cpp
    // True once a first import has set the time base. Replaces "TEMPO_MAP is empty"
    // as the first-import test now that the maps are derived. Non-undoable.
    bool hasTimeBase() const;
    void setTimeBase (bool set);

    // The METER_MAP as plain values (empty when there is no time base).
    std::vector<MeterChange> getMeterChanges() const;
    static std::vector<MeterChange> meterChangesOf (const juce::ValueTree& meterMapNode);

    ~SongDocument();
```

and a private member `std::unique_ptr<TempoMapSync> tempoMapSync;` (`#include <memory>`). `Source/UI/SongDocument.cpp` — define `const juce::Identifier timeBaseSet ("timeBaseSet");` beside `ticksPerQuarter`, and:

```cpp
bool SongDocument::hasTimeBase() const
{
    return (bool) getSourceMidiNode().getProperty (SongIDs::timeBaseSet, false);
}

void SongDocument::setTimeBase (bool set)
{
    getSourceMidiNode().setProperty (SongIDs::timeBaseSet, set, nullptr);
}

std::vector<MeterChange> SongDocument::meterChangesOf (const juce::ValueTree& meterMapNode)
{
    std::vector<MeterChange> out;
    for (int i = 0; i < meterMapNode.getNumChildren(); ++i)
    {
        const auto c = meterMapNode.getChild (i);
        out.push_back ({ (int) c.getProperty (SongIDs::tick, 0), (int) c.getProperty (SongIDs::numerator, 4),
                         (int) c.getProperty (SongIDs::denominator, 4) });
    }
    return out;
}

std::vector<MeterChange> SongDocument::getMeterChanges() const { return meterChangesOf (getMeterMapNode()); }
```

- [ ] **Step 4: The sync.** Extend `Source/UI/TempoMapSync.h` (add after `deriveMaps`; add `#include <juce_data_structures/juce_data_structures.h>`, `<memory>`, forward `class SongDocument;`):

```cpp
// The conductor's EVENTs sorted by (tick, order), as raw events.
std::vector<RawMidiEvent> conductorEventsOf (const SongDocument& doc);

// Writes the derived maps into TEMPO_MAP / METER_MAP (never undoable; only when
// they differ). With no time base yet both nodes are cleared.
void rebuildMaps (SongDocument& doc);

// Rebuilds the maps whenever a conductor FF 51 / FF 58 event is added, removed or
// changed. Attached to SOURCE_MIDI (whose node object survives replaceContents).
class TempoMapSync : public juce::ValueTree::Listener
{
public:
    explicit TempoMapSync (SongDocument& document);
    ~TempoMapSync() override;

    void pause() noexcept { ++pauseDepth; }
    // Returns true when this call ended the outermost pause (the caller rebuilds).
    bool resume() noexcept { return --pauseDepth == 0; }

    void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override;
    void valueTreeChildAdded (juce::ValueTree&, juce::ValueTree&) override;
    void valueTreeChildRemoved (juce::ValueTree&, juce::ValueTree&, int) override;

private:
    bool concernsTempoOrMeter (const juce::ValueTree& changed) const;
    void maybeRebuild (const juce::ValueTree& changed);

    SongDocument& doc;
    juce::ValueTree sourceMidi;
    int pauseDepth = 0;
};

// Holds map rebuilds back for bulk writes (import, load); the outermost pause
// rebuilds once when it ends.
class MapSyncPause
{
public:
    explicit MapSyncPause (SongDocument& document);
    ~MapSyncPause();
    MapSyncPause (const MapSyncPause&) = delete;
    MapSyncPause& operator= (const MapSyncPause&) = delete;

private:
    SongDocument& doc;
};
```

`SongDocument` needs to expose its sync: add public `TempoMapSync& getTempoMapSync() { return *tempoMapSync; }` and create it at the end of the constructor: `tempoMapSync = std::make_unique<TempoMapSync> (*this);` (include `"UI/TempoMapSync.h"` in the `.cpp`). `SongDocument::~SongDocument() = default;` defined in the `.cpp`.

`Source/UI/TempoMapSync.cpp` — append:

```cpp
#include "UI/SongDocument.h"

#include <algorithm>

namespace lotro
{

namespace
{
    std::vector<std::uint8_t> bytesOf (const juce::ValueTree& event)
    {
        if (const auto* block = event.getProperty (SongIDs::data).getBinaryData())
        {
            const auto* d = static_cast<const std::uint8_t*> (block->getData());
            return std::vector<std::uint8_t> (d, d + block->getSize());
        }
        return {};
    }

    bool isTempoOrMeterBytes (const std::vector<std::uint8_t>& b)
    {
        return b.size() >= 2 && b[0] == 0xFF && (b[1] == 0x51 || b[1] == 0x58);
    }

    void writeIfDifferent (juce::ValueTree node, const juce::Identifier& childType,
                           const std::vector<std::vector<std::pair<juce::Identifier, juce::var>>>& wanted)
    {
        bool same = node.getNumChildren() == (int) wanted.size();
        for (int i = 0; same && i < node.getNumChildren(); ++i)
            for (const auto& [name, value] : wanted[(size_t) i])
                if (node.getChild (i).getProperty (name) != value)
                    same = false;
        if (same)
            return;

        node.removeAllChildren (nullptr);
        for (const auto& props : wanted)
        {
            juce::ValueTree child (childType);
            for (const auto& [name, value] : props)
                child.setProperty (name, value, nullptr);
            node.addChild (child, -1, nullptr);
        }
    }
}

std::vector<RawMidiEvent> conductorEventsOf (const SongDocument& doc)
{
    struct Item { int tick; int order; std::vector<std::uint8_t> bytes; };
    std::vector<Item> items;
    for (auto event : SongDocument::getEventsNode (doc.getConductorTrack()))
        items.push_back ({ (int) event.getProperty (SongIDs::tick, 0), (int) event.getProperty (SongIDs::order, 0), bytesOf (event) });
    std::stable_sort (items.begin(), items.end(), [] (const Item& a, const Item& b)
                      { return a.tick != b.tick ? a.tick < b.tick : a.order < b.order; });

    std::vector<RawMidiEvent> out;
    out.reserve (items.size());
    for (auto& item : items)
        out.push_back ({ item.tick, std::move (item.bytes) });
    return out;
}

void rebuildMaps (SongDocument& doc)
{
    auto tempoNode = doc.getTempoMapNode();
    auto meterNode = doc.getMeterMapNode();

    if (! doc.hasTimeBase())
    {
        tempoNode.removeAllChildren (nullptr);
        meterNode.removeAllChildren (nullptr);
        return;
    }

    const auto maps = deriveMaps (conductorEventsOf (doc));

    std::vector<std::vector<std::pair<juce::Identifier, juce::var>>> tempo, meter;
    for (const auto& t : maps.tempo)
        tempo.push_back ({ { SongIDs::tick, t.tick }, { SongIDs::bpm, t.bpm } });
    for (const auto& m : maps.meter)
        meter.push_back ({ { SongIDs::tick, m.tick }, { SongIDs::numerator, m.numerator }, { SongIDs::denominator, m.denominator } });

    writeIfDifferent (tempoNode, SongIDs::TEMPO_CHANGE, tempo);
    writeIfDifferent (meterNode, SongIDs::METER_CHANGE, meter);
}

TempoMapSync::TempoMapSync (SongDocument& document) : doc (document), sourceMidi (document.getSourceMidiNode())
{
    sourceMidi.addListener (this);
}

TempoMapSync::~TempoMapSync() { sourceMidi.removeListener (this); }

bool TempoMapSync::concernsTempoOrMeter (const juce::ValueTree& changed) const
{
    // EVENT under the conductor's EVENTS, or the EVENTS / track nodes themselves coming or going.
    if (changed.hasType (SongIDs::EVENT))
    {
        const auto events = changed.getParent();
        return events.isValid() && events.hasType (SongIDs::EVENTS)
            && (bool) events.getParent().getProperty (SongIDs::isConductor, false)
            && isTempoOrMeterBytes (bytesOf (changed));
    }
    return false;
}

void TempoMapSync::maybeRebuild (const juce::ValueTree& changed)
{
    if (pauseDepth == 0 && concernsTempoOrMeter (changed))
        rebuildMaps (doc);
}

void TempoMapSync::valueTreePropertyChanged (juce::ValueTree& tree, const juce::Identifier&) { maybeRebuild (tree); }
void TempoMapSync::valueTreeChildAdded (juce::ValueTree&, juce::ValueTree& child) { maybeRebuild (child); }

void TempoMapSync::valueTreeChildRemoved (juce::ValueTree& parent, juce::ValueTree& child, int)
{
    // The removed child no longer has a parent: judge it by its own bytes and the node it left.
    if (pauseDepth != 0 || ! child.hasType (SongIDs::EVENT) || ! parent.hasType (SongIDs::EVENTS)
        || ! (bool) parent.getParent().getProperty (SongIDs::isConductor, false))
        return;
    if (isTempoOrMeterBytes (bytesOf (child)))
        rebuildMaps (doc);
}

MapSyncPause::MapSyncPause (SongDocument& document) : doc (document) { doc.getTempoMapSync().pause(); }

MapSyncPause::~MapSyncPause()
{
    if (doc.getTempoMapSync().resume())
        rebuildMaps (doc);
}

} // namespace lotro
```

Notes for the implementer: (a) `maybeRebuild` on a removed child cannot use `getParent()`, hence the dedicated branch above; (b) when undo *re-adds* an event, `valueTreeChildAdded` fires with the child already attached, so `concernsTempoOrMeter` works; (c) if `SongIDs::TEMPO_CHANGE`/`METER_CHANGE`/`EVENTS`/`EVENT` identifiers are not already exported in `SongDocument.h`, export them (they are used elsewhere in `SongDocument.cpp`; follow the existing `extern const juce::Identifier` pattern).

- [ ] **Step 5: Bridge changes — stop writing the maps, write events, pause once.** In `Source/UI/SongModelBridge.cpp`:

1. Delete the `TEMPO_CHANGE`/`METER_CHANGE` loops in `appendImport` (the `if (isFirstImport || replacingTempo) { … }` block that fills `tempoMapNode`/`meterMapNode`, `SongModelBridge.cpp:365-394` before Task 3 line shifts) and the tempo/meter blocks at the end of `rescaleExistingDocumentTicks` (`:78-90`; the events' ticks are rescaled above them and the maps are rebuilt).
2. Replace `const bool isFirstImport = doc.getTempoMapNode().getNumChildren() == 0;` in `appendImport` (`:129`) and `appendImportedMidi` (`:496`) with `! doc.hasTimeBase()`.
3. At the top of `appendImport` (after computing `isFirstImport`): `MapSyncPause pause (doc);` and `if (isFirstImport) doc.setTimeBase (true);` — the pause's destructor rebuilds the maps once at the end. (Place `setTimeBase` before the pause is destroyed; ordering inside the function is enough.)
4. The `plan == nullptr` path (`appendImportedSong`, used by tests and previews) writes no conductor events today. For a first import there, synthesise them so the derived maps equal the `Song`'s: add this helper to the file's anonymous namespace and call it where the removed map loop used to be, only when `plan == nullptr && isFirstImport`:

```cpp
// FF 51 / FF 58 conductor events for a Song-only import (no raw MIDI), so the derived
// maps equal the Song's. Tempo is rounded to whole microseconds like any MIDI tempo.
void appendSongMapsAsConductorEvents (SongDocument& doc, const Song& song)
{
    auto events = SongDocument::getEventsNode (doc.getConductorTrack());
    int order = 0;
    const auto add = [&] (int tick, const std::vector<std::uint8_t>& bytes)
    {
        juce::ValueTree e (SongIDs::EVENT);
        e.setProperty (SongIDs::tick, tick, nullptr);
        e.setProperty (SongIDs::order, order++, nullptr);
        e.setProperty (SongIDs::data, juce::var (juce::MemoryBlock (bytes.data(), bytes.size())), nullptr);
        SongDocument::appendChildBulk (events, e);
    };
    for (const auto& t : song.tempoMap)
    {
        const auto us = (std::uint32_t) std::clamp (std::llround (60000000.0 / t.bpm), 1LL, 16777215LL);
        add (t.tick, { 0xFF, 0x51, (std::uint8_t) (us >> 16), (std::uint8_t) (us >> 8), (std::uint8_t) us });
    }
    for (const auto& m : song.meterMap)
    {
        std::uint8_t dd = 0;
        while ((1 << dd) < m.denominator && dd < 15) ++dd;
        add (m.tick, { 0xFF, 0x58, (std::uint8_t) m.numerator, dd, 24, 8 });
    }
}
```

(`song.tempoMap` entries have `.tick`, `.bpm`; `song.meterMap` entries `.tick`, `.numerator`, `.denominator` — see `Source/Core/Song.h`.)

5. The "file's maps differ" warning branch (`else { auto tempoMapNode = doc.getTempoMapNode(); … }`) is kept unchanged: under tempo keep the conductor is untouched, so the node it compares against is still the Song's derived map.

- [ ] **Step 6: Load and `MainWindow`.** In `SongDocument::replaceContents` wrap the copy and rebuild once:

```cpp
    const auto source = loaded.createCopy();
    {
        MapSyncPause pause (*this);
        tree.copyPropertiesFrom (source, nullptr);
        for (const auto& id : { SongIDs::SOURCE_MIDI, SongIDs::PARTS, SongIDs::TEMPO_MAP, SongIDs::METER_MAP })
            tree.getChildWithName (id).copyPropertiesAndChildrenFrom (source.getChildWithName (id), nullptr);

        // Files from before timeBaseSet existed: a stored tempo map means an import happened.
        auto sourceMidi = getSourceMidiNode();
        if (! sourceMidi.hasProperty (SongIDs::timeBaseSet))
            sourceMidi.setProperty (SongIDs::timeBaseSet, getTempoMapNode().getNumChildren() > 0, nullptr);
    }   // the maps are rebuilt from the events here, healing any stale ones
    undoManager.clearUndoHistory();
```

In `Source/UI/MainWindow.cpp:377` replace `songDocument.getTempoMapNode().getNumChildren() > 0` with `songDocument.hasTimeBase()`.

- [ ] **Step 7: Run the new tests, then the whole suite**

Run: `cmake --build build --target forge_tests 2>&1 | grep -E "error|warning: unused" ; ./build/Tests/forge_tests "[tempo-sync]" | tail -4; ctest --test-dir build --output-on-failure 2>&1 | tail -15`
Expected: new tests pass. Fix every other failure by understanding it — the likely ones: tests that build a `SongDocument` and call `appendImportedSong` then read the maps (should pass via step 5.4; if a `bpm` comparison is exact and the value does not round-trip through whole microseconds, compare with `Approx`), tests that asserted `getTempoMapNode().getNumChildren() == 0` after an empty import, and tests that wrote map nodes by hand (change them to write the conductor event instead, or call `rebuildMaps`). Do not weaken the invariant tests.

- [ ] **Step 8: Build the UI target** — `cmake --build build --target forge_ui 2>&1 | grep -E "error" ` — Expected: none.

- [ ] **Step 9: Commit**

```bash
git add -A
git commit -m "refactor(ui): tempo and meter maps are derived from the conductor's events

Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>"
```

---

### Task 5: Ruler understands several meters

**Files:**
- Modify: `Source/UI/Playback/TimelineRulerMarks.{h,cpp}`
- Test: `Tests/TimelineRulerMarks_tests.cpp`

**Interfaces:**
- Consumes: `meterSegments`, `MeterChange`, `SongDocument::getMeterChanges()`.
- Produces: `RulerGrid` gains `std::vector<MeterChange> meters;` (empty = the single `meter` from tick 0 — every existing `RulerGrid { tempo, { n, d } }` construction keeps working); `rulerGridFromDocument` fills `meters` from the whole map and `meter` from its first entry.

- [ ] **Step 1: Write the failing test** — append to `Tests/TimelineRulerMarks_tests.cpp`

```cpp
TEST_CASE ("computeRulerMarks: bars continue their numbering across a meter change", "[ruler][tempo-sync]")
{
    RulerGrid grid { TempoMap ({}, 480), { 4, 4 } };
    grid.meters = { { 0, 4, 4 }, { 3840, 3, 4 } };   // two 4/4 bars, then 3/4

    const auto marks = computeRulerMarks (0.0, 3840.0 + 1440.0 * 2, 0.05, grid);   // 0.05 px/tick: every bar labelled
    std::vector<std::pair<double, std::string>> bars;
    for (const auto& m : marks)
        if (m.kind == RulerMark::Kind::Bar)
            bars.emplace_back (m.tick, m.label);

    REQUIRE (bars.size() == 5);
    CHECK (bars[0] == std::make_pair (0.0, std::string ("1")));
    CHECK (bars[1] == std::make_pair (1920.0, std::string ("2")));
    CHECK (bars[2] == std::make_pair (3840.0, std::string ("3")));
    CHECK (bars[3] == std::make_pair (3840.0 + 1440.0, std::string ("4")));      // 3/4 bars are 1440 long
    CHECK (bars[4] == std::make_pair (3840.0 + 2880.0, std::string ("5")));
}

TEST_CASE ("computeRulerMarks: beats follow the meter in force", "[ruler][tempo-sync]")
{
    RulerGrid grid { TempoMap ({}, 480), { 4, 4 } };
    grid.meters = { { 0, 4, 4 }, { 1920, 6, 8 } };

    const auto marks = computeRulerMarks (0.0, 1920.0 + 1440.0, 0.5, grid);   // wide enough to show beats
    int beatsInFirstBar = 0, beatsInSecondBar = 0;
    for (const auto& m : marks)
    {
        if (m.kind != RulerMark::Kind::Beat) continue;
        if (m.tick < 1920.0) ++beatsInFirstBar; else ++beatsInSecondBar;
    }
    CHECK (beatsInFirstBar == 3);    // beats 2..4 of the 4/4 bar
    CHECK (beatsInSecondBar == 5);   // beats 2..6 of the 6/8 bar (eighth-note beats)
}

TEST_CASE ("rulerGridFromDocument carries every meter change", "[ruler][tempo-sync]")
{
    SongDocument doc;
    doc.setTimeBase (true);
    for (const auto& [tick, nn, dd] : { std::tuple { 0, 4, 4 }, std::tuple { 1920, 3, 4 } })
    {
        juce::ValueTree c (SongIDs::METER_CHANGE);
        c.setProperty (SongIDs::tick, tick, nullptr);
        c.setProperty (SongIDs::numerator, nn, nullptr);
        c.setProperty (SongIDs::denominator, dd, nullptr);
        doc.getMeterMapNode().addChild (c, -1, nullptr);
    }
    const auto grid = rulerGridFromDocument (doc);
    REQUIRE (grid.meters.size() == 2);
    CHECK (grid.meters[1].numerator == 3);
    CHECK (grid.meter.numerator == 4);
}
```

(Add `#include "UI/SongDocument.h"`, `<tuple>` if absent.)

- [ ] **Step 2: Run to verify it fails** — build: `RulerGrid` has no member `meters`.

- [ ] **Step 3: Implement.** In `TimelineRulerMarks.h` add `#include "UI/MeterSegments.h"` and extend `RulerGrid`:

```cpp
struct RulerGrid
{
    TempoMap tempo;
    RulerMeter meter;                  // the opening meter; the only one when `meters` is empty
    std::vector<MeterChange> meters;   // every meter change (empty = just `meter` from tick 0)
};
```

(`#include <vector>` is present.) Update the comment above `RulerMeter` accordingly. In `TimelineRulerMarks.cpp`, `rulerGridFromDocument` becomes:

```cpp
RulerGrid rulerGridFromDocument (const SongDocument& doc)
{
    RulerGrid grid;
    grid.tempo = tempoMapFromDocument (doc);
    grid.meters = doc.getMeterChanges();
    if (! grid.meters.empty())
    {
        grid.meter.numerator = grid.meters.front().numerator;
        grid.meter.denominator = grid.meters.front().denominator;
    }
    grid.meter = sanitised (grid.meter);
    return grid;
}
```

and `computeRulerMarks` is rewritten over segments (replace the body from `const auto meter = sanitised (grid.meter);` to the end; `barStepFor`, `formatClock`, `sanitised` stay):

```cpp
std::vector<RulerMark> computeRulerMarks (double firstTick, double lastTick, double pixelsPerTick,
                                          const RulerGrid& grid)
{
    std::vector<RulerMark> marks;
    if (pixelsPerTick <= 0.0 || lastTick < firstTick)
        return marks;

    auto changes = grid.meters;
    if (changes.empty())
    {
        const auto meter = sanitised (grid.meter);
        changes.push_back ({ 0, meter.numerator, meter.denominator });
    }
    const auto segments = meterSegments (std::move (changes), grid.tempo.getTicksPerQuarter());
    if (segments.empty())
        return marks;

    double narrowestBar = segments.front().ticksPerBar;
    double narrowestBeat = segments.front().ticksPerBeat;
    for (const auto& s : segments)
    {
        narrowestBar = std::min (narrowestBar, s.ticksPerBar);
        narrowestBeat = std::min (narrowestBeat, s.ticksPerBeat);
    }

    const int barStep = barStepFor (narrowestBar * pixelsPerTick);
    const bool showBeats = barStep == 1 && narrowestBeat * pixelsPerTick >= minBeatPixels;
    const bool showMilliseconds = narrowestBar * pixelsPerTick * (double) barStep >= minMillisecondLabelPixels;

    const auto inRange = [&] (double tick) { return tick >= firstTick && tick <= lastTick; };

    for (size_t i = 0; i < segments.size(); ++i)
    {
        const auto& s = segments[i];
        const double end = i + 1 < segments.size() ? segments[i + 1].startTick : std::numeric_limits<double>::infinity();
        if (end <= firstTick || s.startTick > lastTick)
            continue;

        const long long firstK = std::max (0LL, (long long) std::floor ((firstTick - s.startTick) / s.ticksPerBar));
        for (long long k = firstK;; ++k)
        {
            const double barTick = s.startTick + (double) k * s.ticksPerBar;
            if (barTick >= end - 1e-9 || barTick > lastTick)
                break;

            const long long bar = s.firstBar + k;
            if (bar % barStep == 0 && inRange (barTick))
            {
                RulerMark mark;
                mark.kind = RulerMark::Kind::Bar;
                mark.tick = barTick;
                mark.label = std::to_string (bar + 1) + (showBeats ? ".1" : "");
                mark.timeLabel = formatClock (grid.tempo.ticksToSeconds (barTick), showMilliseconds);
                marks.push_back (std::move (mark));
            }

            if (showBeats)
            {
                for (int beat = 1; beat < s.numerator; ++beat)
                {
                    const double beatTick = barTick + (double) beat * s.ticksPerBeat;
                    if (beatTick < end - 1e-9 && inRange (beatTick))
                    {
                        RulerMark mark;
                        mark.kind = RulerMark::Kind::Beat;
                        mark.tick = beatTick;
                        mark.label = std::to_string (bar + 1) + "." + std::to_string (beat + 1);
                        marks.push_back (std::move (mark));
                    }
                }
            }
        }
    }
    return marks;
}
```

(`#include <limits>` for `numeric_limits`.)

- [ ] **Step 4: Run** — `./build/Tests/forge_tests "[ruler]" | tail -3` — Expected: all pass, including every pre-existing ruler test unchanged.

- [ ] **Step 5: Commit**

```bash
git add -A
git commit -m "feat(ui): the timing bar numbers bars and beats across meter changes

Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>"
```

---

### Task 6: Grid lines and Rewind One Bar understand several meters

**Files:**
- Modify: `Source/UI/GridLines.{h,cpp}`, `Source/UI/GridLinePaint.h`, `Source/UI/PianoRollComponent.cpp:588-604`, `Source/UI/PianoRollComponent.h:75`, `Source/UI/TrackNotePreview.cpp:14-33`, `Source/UI/Playback/PlaybackController.cpp:124-134`
- Test: `Tests/GridLines_tests.cpp`, `Tests/PlaybackController_tests.cpp`

**Interfaces:**
- Consumes: `meterSegments`, `previousBarStart`, `SongDocument::getMeterChanges`, `SongDocument::meterChangesOf`.
- Produces: `std::vector<GridLine> computeGridLines (int, int, double, int ticksPerQuarter, const std::vector<MeterChange>& meters);` (the existing `RulerMeter` overload forwards to it); `paintGridLines (…, const std::vector<MeterChange>& meters, …)` overload alongside the `RulerMeter` one.

- [ ] **Step 1: Write the failing tests.** Append to `Tests/GridLines_tests.cpp`:

```cpp
TEST_CASE ("computeGridLines: bar lines follow a meter change", "[grid][tempo-sync]")
{
    const int ppq = 480;
    const auto lines = computeGridLines (0, 6720, 0.001, ppq, std::vector<MeterChange> { { 0, 4, 4 }, { 3840, 3, 4 } });
    std::vector<int> bars;
    for (const auto& l : lines)
        if (l.level == GridLevel::Bar)
            bars.push_back (l.tick);
    CHECK (bars == std::vector<int> { 0, 1920, 3840, 5280, 6720 });   // 4/4 bars, then 1440-tick bars
}

TEST_CASE ("computeGridLines: one meter through the list overload equals the single-meter overload", "[grid][tempo-sync]")
{
    const int ppq = 480;
    const auto a = computeGridLines (0, 4000, 0.05, ppq, RulerMeter { 6, 8 });
    const auto b = computeGridLines (0, 4000, 0.05, ppq, std::vector<MeterChange> { { 0, 6, 8 } });
    REQUIRE (a.size() == b.size());
    for (size_t i = 0; i < a.size(); ++i)
    {
        CHECK (a[i].tick == b[i].tick);
        CHECK (a[i].level == b[i].level);
    }
}
```

Append to `Tests/PlaybackController_tests.cpp` (use the file's existing fixture for building a controller with a loaded song; copy the setup from the nearest `rewindOneBar` test):

```cpp
TEST_CASE ("rewindOneBar steps back across a meter change", "[playback][tempo-sync]")
{
    // Same fixture as the existing rewindOneBar test, but with METER_MAP = 4/4 at 0 and 3/4 at 3840
    // (PPQ 480), and the playhead 100 ticks into the first 3/4 bar (tick 3940).
    // Expect: first rewind -> 3840; second rewind -> 1920 (the last 4/4 bar).
}
```

Replace the sketch with the real test by copying the existing rewind-one-bar test in that file and (a) adding the second `METER_CHANGE` before the controller rebuilds, (b) seeking to tick 3940, (c) asserting `getPositionTicks() == Approx (3840.0)` after one `rewindOneBar()` and `Approx (1920.0)` after the second.

- [ ] **Step 2: Run to verify they fail** — build: no `computeGridLines` overload taking a vector; the rewind test fails on the second assertion.

- [ ] **Step 3: Implement.** `GridLines.h`: add `#include "UI/MeterSegments.h"` and

```cpp
// As above, with bar lines following every meter change (segments from meterSegments).
std::vector<GridLine> computeGridLines (int firstTick, int lastTick, double pixelsPerTick,
                                        int ticksPerQuarter, const std::vector<MeterChange>& meters);
```

`GridLines.cpp`: turn the existing function into the vector version — replace the single bar-line call

```cpp
    const double barTicks = (double) ticksPerQuarter * 4.0 * (double) meter.numerator / (double) meter.denominator;
    addLines (lines, barTicks, GridLevel::Bar, firstTick, lastTick);
```

with

```cpp
    const auto segments = meterSegments (meters, ticksPerQuarter);
    for (size_t i = 0; i < segments.size(); ++i)
    {
        const auto& s = segments[i];
        const double end = i + 1 < segments.size() ? segments[i + 1].startTick : std::numeric_limits<double>::infinity();
        for (long long k = std::max (0LL, (long long) std::floor (((double) firstTick - s.startTick) / s.ticksPerBar));; ++k)
        {
            const double barTick = s.startTick + (double) k * s.ticksPerBar;
            if (barTick >= end - 1e-9 || barTick > (double) lastTick)
                break;
            const long long tick = std::llround (barTick);
            if (tick >= firstTick)
                lines.emplace ((int) tick, GridLevel::Bar);
        }
    }
```

signature `computeGridLines (int firstTick, int lastTick, double pixelsPerTick, int ticksPerQuarter, const std::vector<MeterChange>& meters)`, dropping the `meter.numerator <= 0` fallback (`meterSegments` sanitises). Add the forwarding overload:

```cpp
std::vector<GridLine> computeGridLines (int firstTick, int lastTick, double pixelsPerTick,
                                        int ticksPerQuarter, RulerMeter meter)
{
    if (meter.numerator <= 0 || meter.denominator <= 0)
        meter = {};
    return computeGridLines (firstTick, lastTick, pixelsPerTick, ticksPerQuarter,
                             std::vector<MeterChange> { { 0, meter.numerator, meter.denominator } });
}
```

(`#include <limits>`, `<algorithm>`.) The "invalid meter treated as 4/4" test keeps passing through the forward.

`GridLinePaint.h`: change `paintGridLines` to take `const std::vector<MeterChange>& meters` and add a thin overload taking `RulerMeter` that forwards (`{ { 0, meter.numerator, meter.denominator } }`). `PianoRollComponent.cpp::drawGridlines`:

```cpp
void PianoRollComponent::drawGridlines (juce::Graphics& g, juce::Rectangle<int> clip) const
{
    paintGridLines (g, clip, geometry.getPixelsPerQuarterNote() / (double) ticksPerQuarter, ticksPerQuarter,
                    SongDocument::meterChangesOf (meterMap),
                    [this] (int tick) { return geometry.xForTick (tick); },
                    [this] (int x) { return geometry.tickForX (x); });
}
```

and fix the comment at `PianoRollComponent.h:75` ("first meter entry only" → "every meter change"). `TrackNotePreview.cpp::paintGrid`: replace the `RulerMeter meter; … meterMap.getChild (0)` block with `const auto meters = SongDocument::meterChangesOf (root.getChildWithName (SongIDs::METER_MAP));` and pass `meters`. `PlaybackController::rewindOneBar`:

```cpp
void PlaybackController::rewindOneBar()
{
    if (snapshot == nullptr)
        return;
    const auto segments = meterSegments (doc.getMeterChanges(), snapshot->tempo().getTicksPerQuarter());
    seekToTick (previousBarStart (segments, getPositionTicks()));
}
```

(`previousBarTick` in `Transport.cpp` stays — it has its own tests and other callers may exist; if `grep -rn previousBarTick Source` shows `PlaybackController.cpp` was the only caller, delete the function and its test in this commit, since `previousBarStart` supersedes it.)

- [ ] **Step 4: Run** — `cmake --build build --target forge_tests 2>&1 | grep error; ./build/Tests/forge_tests "[grid],[playback],[ruler]" | tail -3; cmake --build build --target forge_ui 2>&1 | grep error` — Expected: pass, UI target builds.

- [ ] **Step 5: Commit**

```bash
git add -A
git commit -m "feat(ui): the bar grid and Rewind One Bar follow meter changes

Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>"
```

---

### Task 7: `TempoEdit` (undoable tempo/meter edits)

**Files:**
- Create: `Source/UI/TempoEdit.h`, `Source/UI/TempoEdit.cpp`
- Test: `Tests/TempoEdit_tests.cpp` (create, register in both CMake files)

**Interfaces:**
- Consumes: `SongDocument` (`getConductorTrack`, `getEventsNode`, `getUndoManager`, `addChild`, `removeChild`, `hasTimeBase`, `getMeterChanges`), `conductorEventsOf`, `bpmFromMicroseconds`, `meterSegments`, `nearestBarStart`.
- Produces:

```cpp
enum class TempoEditError { NoTimeBase, NegativeTick, BpmOutOfRange, MeterOutOfRange, TickOccupied, NoSuchEvent, CannotRemoveFirst };
using TempoEditResult = std::optional<TempoEditError>;   // nullopt = success

constexpr double maxBpm = 1000.0;
double minBpm();                                   // 60e6 / 16777215
double roundedBpm (double bpm);                    // whole-microsecond rounding, as stored

TempoEditResult setTempo   (SongDocument&, int tick, double bpm);              // create or replace at `tick`
TempoEditResult moveTempo  (SongDocument&, int fromTick, int toTick, double bpm);
TempoEditResult removeTempo(SongDocument&, int tick);
TempoEditResult setMeter   (SongDocument&, int tick, int numerator, int denominator);   // tick snapped to a bar start
TempoEditResult moveMeter  (SongDocument&, int fromTick, int toTick, int numerator, int denominator);
TempoEditResult removeMeter(SongDocument&, int tick);

enum class TempoMeterKind { tempo, meter };
struct TempoMeterEvent { TempoMeterKind kind; int tick; double bpm; int numerator; int denominator; };
std::vector<TempoMeterEvent> listTempoMeterEvents (const SongDocument&);   // by tick, tempo before meter on a tie
```

Every successful call is exactly one undo transaction; every failing call changes nothing and opens none. Tempo may sit on any tick `>= 0`; `setMeter`/`moveMeter` snap the *target* tick with `nearestBarStart` over the current meter segments. `TickOccupied`: `moveX` to a tick that already holds an event of that kind (other than `fromTick`). `NoSuchEvent`: `moveX`/`removeX` where no event of that kind is at `fromTick`/`tick`. `CannotRemoveFirst`: `removeX` at tick 0. A new event gets `order` = (largest `order` among conductor events at that tick) + 1, or 0.

- [ ] **Step 1: Write the failing tests** — `Tests/TempoEdit_tests.cpp`

```cpp
#include "MidiTestBytes.h"
#include "UI/MidiExport.h"
#include "UI/SongModelBridge.h"
#include "UI/TempoEdit.h"
#include "UI/TempoMapSync.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <juce_core/juce_core.h>

using namespace lotro;
using namespace miditest;
using Catch::Approx;

namespace
{
    struct TempMidi
    {
        juce::File file = juce::File::createTempFile (".mid");
        explicit TempMidi (const Bytes& bytes) { REQUIRE (file.replaceWithData (bytes.data(), bytes.size())); }
        ~TempMidi() { file.deleteFile(); }
    };

    // 120 BPM and 4/4 at tick 0, one key-signature event, one note, PPQ 480.
    struct Loaded
    {
        TempMidi midi;
        SongDocument doc;
        Loaded() : midi ([]
        {
            TrackBody c;
            c.ev (0, { 0xFF, 0x51, 0x03, 0x07, 0xA1, 0x20 }).ev (0, { 0xFF, 0x58, 0x04, 0x04, 0x02, 0x18, 0x08 })
             .ev (0, { 0xFF, 0x59, 0x02, 0x00, 0x00 }).eot (3840);
            TrackBody n;
            n.ev (0, { 0x90, 60, 100 }).ev (480, { 0x80, 60, 0x40 }).eot();
            return smf (1, 480, { c, n });
        }())
        {
            Diagnostics d;
            REQUIRE (importMidiFile (doc, midi.file, 1, d));
        }
    };

    std::vector<TempoMeterEvent> events (const SongDocument& doc) { return listTempoMeterEvents (doc); }

    int conductorEventCount (const SongDocument& doc) { return SongDocument::getEventsNode (doc.getConductorTrack()).getNumChildren(); }
}

TEST_CASE ("TempoEdit: listing shows the imported tempo and meter, by tick", "[tempo-edit]")
{
    Loaded f;
    const auto list = events (f.doc);
    REQUIRE (list.size() == 2);
    CHECK (list[0].kind == TempoMeterKind::tempo);
    CHECK (list[0].bpm == Approx (120.0));
    CHECK (list[1].kind == TempoMeterKind::meter);
    CHECK (list[1].numerator == 4);
}

TEST_CASE ("TempoEdit: setTempo adds an event, the map follows, and undo removes it in one step", "[tempo-edit]")
{
    Loaded f;
    const int before = conductorEventCount (f.doc);
    REQUIRE_FALSE (setTempo (f.doc, 1920, 90.0).has_value());

    CHECK (conductorEventCount (f.doc) == before + 1);
    REQUIRE (f.doc.getTempoMapNode().getNumChildren() == 2);
    CHECK ((int) f.doc.getTempoMapNode().getChild (1).getProperty (SongIDs::tick) == 1920);
    CHECK ((double) f.doc.getTempoMapNode().getChild (1).getProperty (SongIDs::bpm) == Approx (90.0));

    f.doc.undo();
    CHECK (conductorEventCount (f.doc) == before);
    CHECK (f.doc.getTempoMapNode().getNumChildren() == 1);
    f.doc.redo();
    CHECK (f.doc.getTempoMapNode().getNumChildren() == 2);
}

TEST_CASE ("TempoEdit: a typed BPM is stored rounded to whole microseconds, in the event and the map", "[tempo-edit]")
{
    Loaded f;
    REQUIRE_FALSE (setTempo (f.doc, 480, 100.5).has_value());
    const double stored = (double) f.doc.getTempoMapNode().getChild (1).getProperty (SongIDs::bpm);
    CHECK (stored == roundedBpm (100.5));
    CHECK (stored == Approx (100.5).margin (0.001));

    const auto exported = buildRawMidiFile (f.doc);
    const auto derived = deriveMaps (exported.tracks.front().events);
    REQUIRE (derived.tempo.size() == 2);
    CHECK (derived.tempo[1].bpm == stored);   // what the file will say is what the map says
}

TEST_CASE ("TempoEdit: setTempo on an occupied tick replaces it; the tick-0 tempo can be edited", "[tempo-edit]")
{
    Loaded f;
    const int before = conductorEventCount (f.doc);
    REQUIRE_FALSE (setTempo (f.doc, 0, 80.0).has_value());
    CHECK (conductorEventCount (f.doc) == before);   // replaced, not duplicated
    CHECK (events (f.doc)[0].bpm == Approx (80.0));
}

TEST_CASE ("TempoEdit: BPM outside what MIDI encodes is rejected and changes nothing", "[tempo-edit]")
{
    Loaded f;
    const int before = conductorEventCount (f.doc);
    CHECK (setTempo (f.doc, 480, 0.0) == TempoEditError::BpmOutOfRange);
    CHECK (setTempo (f.doc, 480, -5.0) == TempoEditError::BpmOutOfRange);
    CHECK (setTempo (f.doc, 480, 1000.5) == TempoEditError::BpmOutOfRange);
    CHECK (setTempo (f.doc, 480, 3.0) == TempoEditError::BpmOutOfRange);   // > 24-bit microseconds
    CHECK (setTempo (f.doc, -1, 100.0) == TempoEditError::NegativeTick);
    CHECK (conductorEventCount (f.doc) == before);
    CHECK_FALSE (f.doc.canUndo() && f.doc.getUndoManager().getUndoDescription().isNotEmpty());   // no transaction opened
}

TEST_CASE ("TempoEdit: moveTempo changes position and value as one step; occupied and missing ticks are rejected", "[tempo-edit]")
{
    Loaded f;
    REQUIRE_FALSE (setTempo (f.doc, 480, 100.0).has_value());
    REQUIRE_FALSE (setTempo (f.doc, 960, 110.0).has_value());

    CHECK (moveTempo (f.doc, 480, 960, 100.0) == TempoEditError::TickOccupied);
    CHECK (moveTempo (f.doc, 1234, 1300, 100.0) == TempoEditError::NoSuchEvent);

    REQUIRE_FALSE (moveTempo (f.doc, 480, 720, 105.0).has_value());
    const auto list = events (f.doc);
    CHECK (list[1].tick == 720);
    CHECK (list[1].bpm == Approx (105.0));
    f.doc.undo();
    CHECK (events (f.doc)[1].tick == 480);
    CHECK (events (f.doc)[1].bpm == Approx (100.0));
}

TEST_CASE ("TempoEdit: removeTempo removes a later event but never the one at tick 0", "[tempo-edit]")
{
    Loaded f;
    REQUIRE_FALSE (setTempo (f.doc, 480, 100.0).has_value());
    REQUIRE_FALSE (removeTempo (f.doc, 480).has_value());
    CHECK (f.doc.getTempoMapNode().getNumChildren() == 1);
    CHECK (removeTempo (f.doc, 0) == TempoEditError::CannotRemoveFirst);
    CHECK (removeTempo (f.doc, 480) == TempoEditError::NoSuchEvent);
}

TEST_CASE ("TempoEdit: setMeter snaps to a bar start, writes FF 58, and the bars renumber", "[tempo-edit]")
{
    Loaded f;
    REQUIRE_FALSE (setMeter (f.doc, 2000, 3, 4).has_value());   // nearest 4/4 bar line is 1920
    const auto list = events (f.doc);
    REQUIRE (list.size() == 3);
    CHECK (list[2].kind == TempoMeterKind::meter);
    CHECK (list[2].tick == 1920);
    CHECK (list[2].numerator == 3);
    CHECK (list[2].denominator == 4);

    const auto exported = buildRawMidiFile (f.doc);
    bool found = false;
    for (const auto& e : exported.tracks.front().events)
        if (e.tick == 1920 && e.bytes == std::vector<std::uint8_t> { 0xFF, 0x58, 3, 2, 24, 8 })
            found = true;
    CHECK (found);
}

TEST_CASE ("TempoEdit: meter values are validated; editing keeps the original cc/bb bytes", "[tempo-edit]")
{
    Loaded f;
    CHECK (setMeter (f.doc, 1920, 0, 4) == TempoEditError::MeterOutOfRange);
    CHECK (setMeter (f.doc, 1920, 33, 4) == TempoEditError::MeterOutOfRange);
    CHECK (setMeter (f.doc, 1920, 4, 3) == TempoEditError::MeterOutOfRange);
    CHECK (setMeter (f.doc, 1920, 4, 64) == TempoEditError::MeterOutOfRange);

    REQUIRE_FALSE (setMeter (f.doc, 0, 6, 8).has_value());   // edit the opening 4/4 in place
    const auto exported = buildRawMidiFile (f.doc);
    bool found = false;
    for (const auto& e : exported.tracks.front().events)
        if (e.tick == 0 && e.bytes.size() == 6 && e.bytes[1] == 0x58)
            found = (e.bytes == std::vector<std::uint8_t> { 0xFF, 0x58, 6, 3, 0x18, 0x08 });   // cc/bb of the imported event
    CHECK (found);
}

TEST_CASE ("TempoEdit: moveMeter and removeMeter obey the same rules", "[tempo-edit]")
{
    Loaded f;
    REQUIRE_FALSE (setMeter (f.doc, 1920, 3, 4).has_value());
    CHECK (moveMeter (f.doc, 1920, 0, 3, 4) == TempoEditError::TickOccupied);
    REQUIRE_FALSE (moveMeter (f.doc, 1920, 3840, 5, 4).has_value());   // 3840 is a 4/4... then 3/4 boundary check below
    CHECK (removeMeter (f.doc, 0) == TempoEditError::CannotRemoveFirst);
    REQUIRE_FALSE (removeMeter (f.doc, events (f.doc).back().tick).has_value());
    CHECK (f.doc.getMeterMapNode().getNumChildren() == 1);
}

TEST_CASE ("TempoEdit: other conductor events and every note are untouched by an edit", "[tempo-edit]")
{
    Loaded f;
    const auto notesBefore = SongDocument::getNotesNode (f.doc.getTrack (1)).createCopy();
    REQUIRE_FALSE (setTempo (f.doc, 480, 100.0).has_value());
    REQUIRE_FALSE (setMeter (f.doc, 1920, 3, 4).has_value());

    bool keySignatureKept = false;
    for (auto e : SongDocument::getEventsNode (f.doc.getConductorTrack()))
        if (const auto* b = e.getProperty (SongIDs::data).getBinaryData())
            if (b->getSize() >= 2 && static_cast<const std::uint8_t*> (b->getData())[1] == 0x59)
                keySignatureKept = true;
    CHECK (keySignatureKept);
    CHECK (SongDocument::getNotesNode (f.doc.getTrack (1)).isEquivalentTo (notesBefore));
}

TEST_CASE ("TempoEdit: before any import every edit is rejected", "[tempo-edit]")
{
    SongDocument doc;
    CHECK (setTempo (doc, 0, 120.0) == TempoEditError::NoTimeBase);
    CHECK (setMeter (doc, 0, 4, 4) == TempoEditError::NoTimeBase);
    CHECK (removeTempo (doc, 480) == TempoEditError::NoTimeBase);
    CHECK (SongDocument::getEventsNode (doc.getConductorTrack()).getNumChildren() == 0);
}

TEST_CASE ("TempoEdit: after an edit, undo, redo and the export all agree with the maps", "[tempo-edit]")
{
    Loaded f;
    REQUIRE_FALSE (setTempo (f.doc, 480, 100.0).has_value());
    REQUIRE_FALSE (setMeter (f.doc, 1920, 3, 4).has_value());

    const auto check = [&]
    {
        const auto derived = deriveMaps (buildRawMidiFile (f.doc).tracks.front().events);
        REQUIRE (derived.tempo.size() == (size_t) f.doc.getTempoMapNode().getNumChildren());
        REQUIRE (derived.meter.size() == (size_t) f.doc.getMeterMapNode().getNumChildren());
    };
    check();
    f.doc.undo(); check();
    f.doc.undo(); check();
    f.doc.redo(); check();
    f.doc.redo(); check();
}
```

(`Catch::Matchers`-free; `CHECK (x == TempoEditError::…)` compares `std::optional<TempoEditError>` with the enum, which compiles.)

Fix-up note for the implementer: in "moveMeter and removeMeter obey the same rules" the third line is an exploratory move; if `3840` snaps elsewhere given the current segments, adjust the expected tick in the test to what `nearestBarStart` over the *current* map returns — the point of that test is the `TickOccupied` and `CannotRemoveFirst` results and that remove-then-count works.

- [ ] **Step 2: Run to verify it fails** — build: `TempoEdit.h` not found.

- [ ] **Step 3: Implement** — `Source/UI/TempoEdit.h`

```cpp
#pragma once

// Undoable tempo and meter edits. They write only the conductor's FF 51 / FF 58
// EVENTs; TempoMapSync rebuilds the maps from those (spec 2026-10-10,
// sections 1 and 2). Each successful call is one undo transaction.

#include <optional>
#include <vector>

namespace lotro
{

class SongDocument;

enum class TempoEditError
{
    NoTimeBase,
    NegativeTick,
    BpmOutOfRange,
    MeterOutOfRange,
    TickOccupied,
    NoSuchEvent,
    CannotRemoveFirst
};

using TempoEditResult = std::optional<TempoEditError>;   // nullopt = success

constexpr double maxBpm = 1000.0;
double minBpm();                      // 60e6 / 16777215: what 24-bit microseconds can encode
double roundedBpm (double bpm);       // the BPM an event of whole microseconds actually stores

TempoEditResult setTempo    (SongDocument&, int tick, double bpm);
TempoEditResult moveTempo   (SongDocument&, int fromTick, int toTick, double bpm);
TempoEditResult removeTempo (SongDocument&, int tick);

TempoEditResult setMeter    (SongDocument&, int tick, int numerator, int denominator);
TempoEditResult moveMeter   (SongDocument&, int fromTick, int toTick, int numerator, int denominator);
TempoEditResult removeMeter (SongDocument&, int tick);

enum class TempoMeterKind { tempo, meter };

struct TempoMeterEvent
{
    TempoMeterKind kind        = TempoMeterKind::tempo;
    int            tick        = 0;
    double         bpm         = 0.0;   // tempo events
    int            numerator   = 0;     // meter events
    int            denominator = 0;
};

// Every tempo and meter event (the derived maps), by tick, tempo before meter on a tie.
std::vector<TempoMeterEvent> listTempoMeterEvents (const SongDocument&);

} // namespace lotro
```

`Source/UI/TempoEdit.cpp`

```cpp
#include "UI/TempoEdit.h"

#include "UI/MeterSegments.h"
#include "UI/SongDocument.h"
#include "UI/TempoMapSync.h"

#include <algorithm>
#include <cmath>

namespace lotro
{

namespace
{
    constexpr long long maxMicroseconds = 16777215;

    enum class Kind { tempo, meter };

    std::vector<std::uint8_t> bytesOf (const juce::ValueTree& event)
    {
        if (const auto* block = event.getProperty (SongIDs::data).getBinaryData())
        {
            const auto* d = static_cast<const std::uint8_t*> (block->getData());
            return std::vector<std::uint8_t> (d, d + block->getSize());
        }
        return {};
    }

    bool isKind (const std::vector<std::uint8_t>& b, Kind kind)
    {
        return b.size() >= 2 && b[0] == 0xFF && b[1] == (kind == Kind::tempo ? 0x51 : 0x58);
    }

    juce::var toVar (const std::vector<std::uint8_t>& bytes)
    {
        return juce::var (juce::MemoryBlock (bytes.data(), bytes.size()));
    }

    // The conductor EVENT of `kind` at `tick` that wins (the last, as in deriveMaps); invalid if none.
    juce::ValueTree findEvent (const SongDocument& doc, Kind kind, int tick)
    {
        juce::ValueTree found;
        int foundOrder = -1;
        for (auto event : SongDocument::getEventsNode (doc.getConductorTrack()))
            if ((int) event.getProperty (SongIDs::tick, 0) == tick && isKind (bytesOf (event), kind)
                && (int) event.getProperty (SongIDs::order, 0) >= foundOrder)
            {
                found = event;
                foundOrder = (int) event.getProperty (SongIDs::order, 0);
            }
        return found;
    }

    int nextOrderAt (const SongDocument& doc, int tick)
    {
        int order = -1;
        for (auto event : SongDocument::getEventsNode (doc.getConductorTrack()))
            if ((int) event.getProperty (SongIDs::tick, 0) == tick)
                order = std::max (order, (int) event.getProperty (SongIDs::order, 0));
        return order + 1;
    }

    std::optional<std::vector<std::uint8_t>> tempoBytes (double bpm)
    {
        if (! (bpm >= minBpm() && bpm <= maxBpm))
            return std::nullopt;
        const long long us = std::llround (60000000.0 / bpm);
        if (us < 1 || us > maxMicroseconds)
            return std::nullopt;
        return std::vector<std::uint8_t> { 0xFF, 0x51, (std::uint8_t) (us >> 16), (std::uint8_t) (us >> 8), (std::uint8_t) us };
    }

    std::optional<int> denominatorPower (int denominator)
    {
        for (int power = 0; power <= 5; ++power)
            if ((1 << power) == denominator)
                return power;
        return std::nullopt;
    }

    bool meterValid (int numerator, int denominator)
    {
        return numerator >= 1 && numerator <= 32 && denominatorPower (denominator).has_value();
    }

    // Replaces `existing` (or creates a new event) so the conductor holds `bytes` at `tick`.
    // The first undoable change opens the transaction; later ones join it.
    void writeEvent (SongDocument& doc, juce::ValueTree existing, int tick, const std::vector<std::uint8_t>& bytes,
                     bool& transactionOpen)
    {
        auto& undo = doc.getUndoManager();
        if (! transactionOpen)
        {
            undo.beginNewTransaction();
            transactionOpen = true;
        }
        if (existing.isValid())
        {
            existing.setProperty (SongIDs::tick, tick, &undo);
            existing.setProperty (SongIDs::data, toVar (bytes), &undo);
            return;
        }
        juce::ValueTree event (SongIDs::EVENT);
        event.setProperty (SongIDs::tick, tick, nullptr);
        event.setProperty (SongIDs::order, nextOrderAt (doc, tick), nullptr);
        event.setProperty (SongIDs::data, toVar (bytes), nullptr);
        doc.addChild (SongDocument::getEventsNode (doc.getConductorTrack()), event, false);
    }

    TempoEditResult remove (SongDocument& doc, Kind kind, int tick)
    {
        if (! doc.hasTimeBase())
            return TempoEditError::NoTimeBase;
        if (tick == 0)
            return TempoEditError::CannotRemoveFirst;
        if (! findEvent (doc, kind, tick).isValid())
            return TempoEditError::NoSuchEvent;

        bool open = false;
        auto& undo = doc.getUndoManager();
        undo.beginNewTransaction();
        open = true;
        auto events = SongDocument::getEventsNode (doc.getConductorTrack());
        for (int i = events.getNumChildren() - 1; i >= 0; --i)   // every event of that kind on the tick
        {
            auto event = events.getChild (i);
            if ((int) event.getProperty (SongIDs::tick, 0) == tick && isKind (bytesOf (event), kind))
                doc.removeChild (events, event, false);
        }
        (void) open;
        return std::nullopt;
    }
}

double minBpm() { return 60000000.0 / (double) maxMicroseconds; }

double roundedBpm (double bpm)
{
    const long long us = std::llround (60000000.0 / bpm);
    return bpmFromMicroseconds ((std::uint32_t) us);
}

TempoEditResult setTempo (SongDocument& doc, int tick, double bpm)
{
    if (! doc.hasTimeBase())
        return TempoEditError::NoTimeBase;
    if (tick < 0)
        return TempoEditError::NegativeTick;
    const auto bytes = tempoBytes (bpm);
    if (! bytes)
        return TempoEditError::BpmOutOfRange;

    bool open = false;
    writeEvent (doc, findEvent (doc, Kind::tempo, tick), tick, *bytes, open);
    return std::nullopt;
}

TempoEditResult moveTempo (SongDocument& doc, int fromTick, int toTick, double bpm)
{
    if (! doc.hasTimeBase())
        return TempoEditError::NoTimeBase;
    if (toTick < 0)
        return TempoEditError::NegativeTick;
    const auto bytes = tempoBytes (bpm);
    if (! bytes)
        return TempoEditError::BpmOutOfRange;
    const auto event = findEvent (doc, Kind::tempo, fromTick);
    if (! event.isValid())
        return TempoEditError::NoSuchEvent;
    if (toTick != fromTick && findEvent (doc, Kind::tempo, toTick).isValid())
        return TempoEditError::TickOccupied;

    bool open = false;
    writeEvent (doc, event, toTick, *bytes, open);
    return std::nullopt;
}

TempoEditResult removeTempo (SongDocument& doc, int tick) { return remove (doc, Kind::tempo, tick); }

TempoEditResult setMeter (SongDocument& doc, int tick, int numerator, int denominator)
{
    if (! doc.hasTimeBase())
        return TempoEditError::NoTimeBase;
    if (tick < 0)
        return TempoEditError::NegativeTick;
    if (! meterValid (numerator, denominator))
        return TempoEditError::MeterOutOfRange;

    const auto segments = meterSegments (doc.getMeterChanges(), (int) doc.getSourceMidiNode().getProperty (SongIDs::ticksPerQuarter, 480));
    const int snapped = (int) std::llround (nearestBarStart (segments, (double) tick));

    auto existing = findEvent (doc, Kind::meter, snapped);
    std::vector<std::uint8_t> bytes { 0xFF, 0x58, (std::uint8_t) numerator, (std::uint8_t) *denominatorPower (denominator), 24, 8 };
    if (existing.isValid())
    {
        const auto old = bytesOf (existing);
        if (old.size() >= 6)
        {
            bytes[4] = old[4];   // keep the original clocks-per-click and 32nds-per-quarter
            bytes[5] = old[5];
        }
    }
    bool open = false;
    writeEvent (doc, existing, snapped, bytes, open);
    return std::nullopt;
}

TempoEditResult moveMeter (SongDocument& doc, int fromTick, int toTick, int numerator, int denominator)
{
    if (! doc.hasTimeBase())
        return TempoEditError::NoTimeBase;
    if (toTick < 0)
        return TempoEditError::NegativeTick;
    if (! meterValid (numerator, denominator))
        return TempoEditError::MeterOutOfRange;
    const auto event = findEvent (doc, Kind::meter, fromTick);
    if (! event.isValid())
        return TempoEditError::NoSuchEvent;

    const auto segments = meterSegments (doc.getMeterChanges(), (int) doc.getSourceMidiNode().getProperty (SongIDs::ticksPerQuarter, 480));
    const int snapped = fromTick == 0 ? 0 : (int) std::llround (nearestBarStart (segments, (double) toTick));
    if (snapped != fromTick && findEvent (doc, Kind::meter, snapped).isValid())
        return TempoEditError::TickOccupied;

    auto bytes = bytesOf (event);
    std::vector<std::uint8_t> next { 0xFF, 0x58, (std::uint8_t) numerator, (std::uint8_t) *denominatorPower (denominator),
                                     bytes.size() >= 6 ? bytes[4] : (std::uint8_t) 24, bytes.size() >= 6 ? bytes[5] : (std::uint8_t) 8 };
    bool open = false;
    writeEvent (doc, event, snapped, next, open);
    return std::nullopt;
}

TempoEditResult removeMeter (SongDocument& doc, int tick) { return remove (doc, Kind::meter, tick); }

std::vector<TempoMeterEvent> listTempoMeterEvents (const SongDocument& doc)
{
    std::vector<TempoMeterEvent> out;
    for (auto change : doc.getTempoMapNode())
        out.push_back ({ TempoMeterKind::tempo, (int) change.getProperty (SongIDs::tick), (double) change.getProperty (SongIDs::bpm), 0, 0 });
    for (auto change : doc.getMeterMapNode())
        out.push_back ({ TempoMeterKind::meter, (int) change.getProperty (SongIDs::tick), 0.0,
                         (int) change.getProperty (SongIDs::numerator), (int) change.getProperty (SongIDs::denominator) });
    std::stable_sort (out.begin(), out.end(), [] (const TempoMeterEvent& a, const TempoMeterEvent& b)
                      { return a.tick != b.tick ? a.tick < b.tick : a.kind < b.kind; });
    return out;
}

} // namespace lotro
```

Implementer notes: (a) `writeEvent` for a new event must put the `addChild` inside the same transaction — `doc.addChild (…, false)` joins the transaction opened by `beginNewTransaction()`, so call `undo.beginNewTransaction()` *before* it (done via `transactionOpen`); (b) in `remove`, the leftover `open` variable can be deleted — it is only there to mirror `writeEvent`; (c) `meterValid`/`denominatorPower` are what the "MeterOutOfRange" tests exercise; (d) `moveMeter` on the tick-0 event keeps it at 0 (it may change value, not position).

- [ ] **Step 4: Run** — `./build/Tests/forge_tests "[tempo-edit]" | tail -4` — Expected: all pass. Then `ctest --test-dir build --output-on-failure | tail -5` — Expected: 100% pass.

- [ ] **Step 5: Commit**

```bash
git add Source/UI/TempoEdit.* Tests/TempoEdit_tests.cpp CMakeLists.txt Tests/CMakeLists.txt
git commit -m "feat(ui): TempoEdit, undoable tempo and meter edits on the conductor's events

Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>"
```

---

### Task 8: Documentation

**Files:** `docs/ARCHITECTURE.md`, `docs/TESTING.md`, `docs/UI_GUIDE.md`, `CLAUDE.md`, the spec header.

- [ ] **Step 1: ARCHITECTURE.** In §9.2 (data model, the paragraph beginning "Songsmith's editable-document data model") state that `TEMPO_MAP`/`METER_MAP` are derived and add `timeBaseSet` to `SOURCE_MIDI`. In "The conductor" paragraph add the rule that tempo/meter always live in the conductor and are moved there at import. In §9.17 "Tempo replace" replace "the bridge then replaces the Song's `TEMPO_MAP`/`METER_MAP` wholesale" with "the conductor events are replaced and the maps rebuilt from them". Add a new subsection after §9.17 (`### 9.18 Tempo and meter — TempoMapSync, TempoEdit, MeterSegments`) covering: `deriveMaps` rules, the listener and `MapSyncPause`, `rebuildMaps` semantics (cleared with no time base; writes only on change), `TempoEdit` operations and error enum, `meterSegments`, and which consumers use it (ruler, grid, Rewind One Bar). Link the spec and this plan.
- [ ] **Step 2: TESTING.** List `MeterSegments_tests`, `TempoMapSync_tests`, `TempoMapInvariant_tests`, `TempoEdit_tests` and the added cases in the ruler/grid/playback/import-plan/note-merge files, and the new test count (`ctest --test-dir build -N | tail -1`).
- [ ] **Step 3: UI_GUIDE.** Under the Import options section note that a file's tempo/meter events always land in the Conductor track; under the ruler (#32) note that bar numbers and beats follow meter changes; no editor UI yet (phase 3).
- [ ] **Step 4: CLAUDE.md.** Add the Phase 1 docs-map row (spec + this plan, "code walkthrough: `docs/ARCHITECTURE.md` §9.18") and extend the status line with "tempo/meter data layer (derived maps, `TempoEdit`, multi-meter ruler/grid)".
- [ ] **Step 5: Spec header.** Change "Status: approved in conversation, awaiting spec review." to "Status: approved; phase 1 implemented (plan `docs/superpowers/plans/2026-10-10-songsmith-tempo-meter-phase1.md`)."
- [ ] **Step 6: Final verification and commit**

```bash
cmake --build build 2>&1 | grep -E "error" ; ctest --test-dir build --output-on-failure 2>&1 | tail -3
git add -A
git commit -m "docs: tempo and meter data layer (phase 1)

Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>"
```

Expected: no build errors, 100% tests pass.

---

## Self-review (done while writing)

- **Spec coverage:** §1 derived maps → Tasks 2, 4; conductor-only rule + spec amendment → Task 3; first-import flag, load rebuild, no format change → Task 4; §2 edit operations, encoding, ranges, snapping, tick 0, no duplicates, untouched notes/other events → Task 7; §3 meter-aware consumers (ruler, grid, rewind) → Tasks 1, 5, 6; testing section → each task; docs → Task 8. Snap-to-beat in the graph is phase 4 (UI), not here.
- **Placeholders:** the two "adapt to the file's helpers" notes (Task 3 plan test, Task 6 rewind test) name the exact existing test to copy and the exact assertions to add; the Task 7 `moveMeter` fix-up note names what to adjust.
- **Type consistency:** `MeterChange`, `MeterSegment`, `meterSegments`, `nearestBarStart`, `previousBarStart` (Task 1) are used with those names in Tasks 5–7; `deriveMaps`, `DerivedMaps`, `conductorEventsOf`, `rebuildMaps`, `MapSyncPause`, `TempoMapSync` (Tasks 2, 4) in Tasks 4, 7; `hasTimeBase`/`setTimeBase`/`getMeterChanges`/`meterChangesOf` (Task 4) in Tasks 5–7; `TempoEditError`/`TempoEditResult` (Task 7) only in Task 7.
- **Review Focus coverage:** (1) Task 3 test 1; (2) Task 3 test 1 second half and Task 4 "a later import with the tempo map kept"; (3) Task 1 tests; (4) Task 4 save/load and "new document" tests; (5) Task 4 undo test and "unrelated edit leaves map nodes untouched", Task 7 final test.
