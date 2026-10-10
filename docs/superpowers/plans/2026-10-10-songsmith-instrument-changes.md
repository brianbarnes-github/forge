# Songsmith Instrument Changes Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Show a track's instrument (Program Change) changes on its row, let the user split at them or force one instrument via a right-click menu on the instrument band, and add a Notes-only / All-events preference for Alt-drag Move/Copy to another track.

**Architecture:** A pure-logic `ProgramChanges` module reads the track's `EVENTS` blobs; `InstrumentEdit` (plus a multi-tick `splitAtTicks` in `SectionEdit`) holds the two menu actions as undoable document edits; `TrackRowComponent` paints the band segments and shows the menu, with `TrackListComponent` wiring row callbacks to the document; `mergeSections` gains a `MergeScope` argument fed from a new `AppSettings` value. All in `Source/UI/`; `Source/Core/` untouched.

**Tech Stack:** C++20, JUCE 8 (`juce::ValueTree`, `juce::PopupMenu`), Catch2, CMake/Ninja.

**Spec:** `docs/superpowers/specs/2026-10-10-songsmith-instrument-changes-design.md`

## Global Constraints

- Core/UI boundary: nothing under `Source/Core/` changes.
- Guiding principle: the MIDI is the source of truth. No inferred instrument: a section never takes "the instrument in effect at its start".
- One undo transaction per document call; a call that would change nothing opens none and writes nothing (`juce-valuetree-conventions`).
- No tree mutation mid-gesture. Menu actions run after the menu closes.
- Conventional commits; end commit messages with `Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>`. Never push without approval.
- Types/type hints everywhere; no new dependencies.
- New `.cpp` files are added to both `CMakeLists.txt` (forge_ui sources) and `Tests/CMakeLists.txt` (the `forge_tests` source list, near `${CMAKE_SOURCE_DIR}/Source/UI/NoteMerge.cpp`), and new test files to the test list near `NoteMerge_tests.cpp`.
- Run Catch tags directly: `./build/Tests/forge_tests "[tag]"` (ctest `-R` does not match tags).
- Never launch the GUI from agents.
- Deviations from the spec text, to fold into the spec in Task 6: `ProgramChange` carries the `EVENT` node (`juce::ValueTree event`) instead of `eventIndex`; the enum is `MergeScope` (not `MoveCopyScope`) in `Source/UI/MergeScope.h`.

## Review Focus

- A program change at tick 0 only, or the same program repeated: **Auto split** is disabled / splits nothing and opens no undo step.
- A program change at or after the last section's end: no empty section is created.
- **Set track instrument** on a track with no program change inserts one at tick 0 on `defaultChannel`; picking the program already in `sourceProgram` on such a track changes nothing.
- A track with program changes on two channels: the first change per channel is kept (set to P), later ones are deleted.
- All-events merge: an event exactly at the section's `endTick` is not carried (half-open), one at `startTick` is; End-of-Track and track-name metas never travel.
- All-events Move then undo restores the source's events and the target's events in one step.
- A section that holds events but whose notes are all dropped (copy) still counts as changed.

---

### Task 1: `ProgramChanges` — parse program changes and build instrument segments

**Files:**
- Create: `Source/UI/ProgramChanges.h`, `Source/UI/ProgramChanges.cpp`
- Create: `Tests/ProgramChanges_tests.cpp`
- Modify: `CMakeLists.txt` (add `Source/UI/ProgramChanges.cpp` after `Source/UI/NoteMerge.cpp`), `Tests/CMakeLists.txt` (add `ProgramChanges_tests.cpp` after `NoteMerge_tests.cpp`; add `${CMAKE_SOURCE_DIR}/Source/UI/ProgramChanges.cpp` after the `NoteMerge.cpp` source line)

**Interfaces:**
- Produces:
  - `struct ProgramChange { int tick; int channel; int program; juce::ValueTree event; };` (`channel` 1..16)
  - `std::vector<ProgramChange> programChangesOf (const juce::ValueTree& track);`
  - `struct InstrumentSegment { int startTick; int endTick; int program; };`
  - `std::vector<InstrumentSegment> instrumentSegmentsOf (const juce::ValueTree& track);`
  - `struct BandSegment { int x0; int x1; int program; };`
  - `std::vector<BandSegment> bandSegments (const std::vector<InstrumentSegment>&, const TimelineViewState&);`

- [ ] **Step 1: Write the failing tests** — create `Tests/ProgramChanges_tests.cpp`:

```cpp
#include "PlaybackTestSupport.h"
#include "UI/ProgramChanges.h"
#include "UI/TimelineViewState.h"

#include <catch2/catch_test_macros.hpp>

using namespace lotro;
using namespace lotro::playbacktest;

TEST_CASE ("program changes: parsed in tick order with channel and program", "[instrument]")
{
    SongDocument doc;
    auto t = addTrack (doc, "T", 2);
    addEvent (t, 960, { 0xC1, 40 });
    addEvent (t, 0, { 0xC1, 73 });
    addEvent (t, 10, { 0xB1, 7, 100 });   // a controller is not a program change

    const auto pcs = programChangesOf (t);

    REQUIRE (pcs.size() == 2);
    CHECK (pcs[0].tick == 0);
    CHECK (pcs[0].program == 73);
    CHECK (pcs[0].channel == 2);
    CHECK (pcs[1].tick == 960);
    CHECK (pcs[1].program == 40);
}

TEST_CASE ("program changes: the conductor has none", "[instrument]")
{
    SongDocument doc;
    auto t = addTrack (doc);
    t.setProperty (SongIDs::isConductor, true, nullptr);
    addEvent (t, 0, { 0xC0, 5 });
    CHECK (programChangesOf (t).empty());
    CHECK (instrumentSegmentsOf (t).empty());
}

TEST_CASE ("instrument segments: one per distinct instrument, to the next change", "[instrument]")
{
    SongDocument doc;
    auto t = addTrack (doc);
    t.setProperty (SongIDs::sourceProgram, 73, nullptr);
    t.setProperty (SongIDs::endTick, 1920, nullptr);
    addEvent (t, 0, { 0xC0, 73 });
    addEvent (t, 480, { 0xC0, 73 });   // same program again: merged
    addEvent (t, 960, { 0xC0, 40 });

    const auto s = instrumentSegmentsOf (t);

    REQUIRE (s.size() == 2);
    CHECK (s[0].startTick == 0);
    CHECK (s[0].endTick == 960);
    CHECK (s[0].program == 73);
    CHECK (s[1].startTick == 960);
    CHECK (s[1].endTick == 1920);
    CHECK (s[1].program == 40);
}

TEST_CASE ("instrument segments: the span before a late first change uses sourceProgram", "[instrument]")
{
    SongDocument doc;
    auto t = addTrack (doc);
    t.setProperty (SongIDs::sourceProgram, 73, nullptr);
    t.setProperty (SongIDs::endTick, 1000, nullptr);
    addEvent (t, 480, { 0xC0, 40 });

    const auto s = instrumentSegmentsOf (t);

    REQUIRE (s.size() == 2);
    CHECK (s[0].startTick == 0);
    CHECK (s[0].endTick == 480);
    CHECK (s[0].program == 73);
    CHECK (s[1].program == 40);
}

TEST_CASE ("instrument segments: a track with no program change is one sourceProgram segment", "[instrument]")
{
    SongDocument doc;
    auto t = addTrack (doc);
    t.setProperty (SongIDs::sourceProgram, 5, nullptr);
    addNote (t, 60, 0, 480);

    const auto s = instrumentSegmentsOf (t);

    REQUIRE (s.size() == 1);
    CHECK (s[0].program == 5);
    CHECK (s[0].endTick >= 480);
}

TEST_CASE ("instrument segments: two changes on one tick keep the later one", "[instrument]")
{
    SongDocument doc;
    auto t = addTrack (doc);
    t.setProperty (SongIDs::sourceProgram, 1, nullptr);
    t.setProperty (SongIDs::endTick, 1000, nullptr);
    addEvent (t, 0, { 0xC0, 1 });
    addEvent (t, 0, { 0xC0, 9 });

    const auto s = instrumentSegmentsOf (t);

    REQUIRE (s.size() == 1);
    CHECK (s[0].program == 9);
}

TEST_CASE ("band segments: ticks map to pixels through the shared view state", "[instrument]")
{
    TimelineViewState view;
    view.setPixelsPerTick (0.1);
    const std::vector<InstrumentSegment> segs { { 0, 960, 73 }, { 960, 1920, 40 } };

    const auto b = bandSegments (segs, view);

    REQUIRE (b.size() == 2);
    CHECK (b[0].x0 == 0);
    CHECK (b[0].x1 == 96);
    CHECK (b[1].x0 == 96);
    CHECK (b[1].x1 == 192);
    CHECK (b[1].program == 40);
}
```

- [ ] **Step 2: Create the header and a stub-free source, add to CMake, run to see it fail first.** Add the CMake lines and the test file only (no `ProgramChanges.h` yet) and build:

Run: `cmake -B build && cmake --build build --target forge_tests 2>&1 | tail -5`
Expected: FAIL — `UI/ProgramChanges.h: No such file or directory`.

- [ ] **Step 3: Write the implementation.**

`Source/UI/ProgramChanges.h`:

```cpp
#pragma once

#include "UI/SongDocument.h"
#include "UI/TimelineViewState.h"

#include <juce_data_structures/juce_data_structures.h>

#include <vector>

// A track's instrument (Program Change) changes (spec:
// docs/superpowers/specs/2026-10-10-songsmith-instrument-changes-design.md).
// Pure document logic: no UI painting, no Source/Core. Never mutates.
namespace lotro
{

struct ProgramChange
{
    int tick = 0;
    int channel = 1;   // 1..16
    int program = 0;   // 0..127
    juce::ValueTree event;   // the EVENT node
};

// Every `Cn pp` event of the track, by tick then raw order. The conductor has none.
std::vector<ProgramChange> programChangesOf (const juce::ValueTree& track);

struct InstrumentSegment
{
    int startTick = 0;
    int endTick = 0;   // half-open
    int program = 0;
};

// One segment per distinct instrument. The span before a first change that is
// after tick 0 (and a track with no change at all) uses `sourceProgram`.
// Consecutive changes to the same program merge; two changes on one tick keep the
// later. The last segment ends at the track's endTick / last note end (at least
// one tick past its start). Empty for the conductor.
std::vector<InstrumentSegment> instrumentSegmentsOf (const juce::ValueTree& track);

struct BandSegment
{
    int x0 = 0;   // preview-local pixels
    int x1 = 0;
    int program = 0;
};

// The segments in pixels for the current zoom/scroll; each at least 1 px wide.
std::vector<BandSegment> bandSegments (const std::vector<InstrumentSegment>& segments, const TimelineViewState& view);

} // namespace lotro
```

`Source/UI/ProgramChanges.cpp`:

```cpp
#include "UI/ProgramChanges.h"

#include <algorithm>
#include <cstdint>

namespace lotro
{

std::vector<ProgramChange> programChangesOf (const juce::ValueTree& track)
{
    std::vector<ProgramChange> out;
    if (! track.isValid() || (bool) track.getProperty (SongIDs::isConductor, false))
        return out;

    const auto events = SongDocument::getEventsNode (track);
    for (int i = 0; i < events.getNumChildren(); ++i)
    {
        const auto event = events.getChild (i);
        const auto* block = event.getProperty (SongIDs::data).getBinaryData();
        if (block == nullptr || block->getSize() != 2)
            continue;
        const auto* d = static_cast<const std::uint8_t*> (block->getData());
        if ((d[0] & 0xF0) != 0xC0)
            continue;
        out.push_back ({ (int) event.getProperty (SongIDs::tick), (d[0] & 0x0F) + 1, d[1] & 0x7F, event });
    }
    std::stable_sort (out.begin(), out.end(), [] (const ProgramChange& a, const ProgramChange& b)
    {
        if (a.tick != b.tick)
            return a.tick < b.tick;
        return (int) a.event.getProperty (SongIDs::order, 0) < (int) b.event.getProperty (SongIDs::order, 0);
    });
    return out;
}

std::vector<InstrumentSegment> instrumentSegmentsOf (const juce::ValueTree& track)
{
    if (! track.isValid() || (bool) track.getProperty (SongIDs::isConductor, false))
        return {};

    const auto changes = programChangesOf (track);
    std::vector<InstrumentSegment> out;
    const auto push = [&out] (int start, int program)
    {
        if (! out.empty() && out.back().startTick == start)
            out.pop_back();
        if (! out.empty() && out.back().program == program)
            return;
        out.push_back ({ start, 0, program });
    };

    if (changes.empty() || changes.front().tick > 0)
        push (0, (int) track.getProperty (SongIDs::sourceProgram, 0));
    for (const auto& c : changes)
        push (c.tick, c.program);

    int endTick = (int) track.getProperty (SongIDs::endTick, 0);
    const auto notes = SongDocument::getNotesNode (track);
    for (int i = 0; i < notes.getNumChildren(); ++i)
    {
        const auto n = notes.getChild (i);
        endTick = std::max (endTick, (int) n.getProperty (SongIDs::startTick) + (int) n.getProperty (SongIDs::durationTicks));
    }

    for (size_t i = 0; i < out.size(); ++i)
        out[i].endTick = i + 1 < out.size() ? out[i + 1].startTick : std::max (endTick, out[i].startTick + 1);
    return out;
}

std::vector<BandSegment> bandSegments (const std::vector<InstrumentSegment>& segments, const TimelineViewState& view)
{
    std::vector<BandSegment> out;
    for (const auto& s : segments)
    {
        const int x0 = view.xForTick (s.startTick);
        out.push_back ({ x0, std::max (x0 + 1, view.xForTick (s.endTick)), s.program });
    }
    return out;
}

} // namespace lotro
```

- [ ] **Step 4: Run to verify they pass**

Run: `cmake --build build --target forge_tests 2>&1 | tail -3 && ./build/Tests/forge_tests "[instrument]" | tail -3`
Expected: `All tests passed`.

- [ ] **Step 5: Commit**

```bash
git add Source/UI/ProgramChanges.* Tests/ProgramChanges_tests.cpp CMakeLists.txt Tests/CMakeLists.txt
git commit -m "feat(ui): read a track's program changes and build instrument segments

Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>"
```

---

### Task 2: Menu actions — `splitAtTicks`, auto split, set track instrument

**Files:**
- Modify: `Source/UI/SectionEdit.h`, `Source/UI/SectionEdit.cpp` (add `splitAtTicks`; extract the per-track split body)
- Create: `Source/UI/InstrumentEdit.h`, `Source/UI/InstrumentEdit.cpp`
- Create: `Tests/InstrumentEdit_tests.cpp`
- Modify: `CMakeLists.txt`, `Tests/CMakeLists.txt` (same three places as Task 1, after the ProgramChanges lines)

**Interfaces:**
- Consumes: `programChangesOf`, `instrumentSegmentsOf`, `ProgramChange` (Task 1); `SectionEdit` internals.
- Produces:
  - `void splitAtTicks (SongDocument& doc, juce::int64 trackId, std::vector<int> ticks);` — every tick strictly inside a section of the track splits it, all in one undo transaction; others ignored.
  - `bool canAutoSplit (const juce::ValueTree& track);` — `instrumentSegmentsOf (track).size() >= 2`
  - `void autoSplitOnInstrumentChange (SongDocument& doc, juce::int64 trackId);`
  - `void setTrackInstrument (SongDocument& doc, juce::int64 trackId, int program);`

- [ ] **Step 1: Write the failing tests** — create `Tests/InstrumentEdit_tests.cpp`:

```cpp
#include "PlaybackTestSupport.h"
#include "UI/InstrumentEdit.h"
#include "UI/ProgramChanges.h"
#include "UI/SectionEdit.h"

#include <catch2/catch_test_macros.hpp>

using namespace lotro;
using namespace lotro::playbacktest;

namespace
{
    juce::int64 idOf (const juce::ValueTree& t) { return (juce::int64) t.getProperty (SongIDs::trackId); }

    // Piano (73) for the first bar, then 40: notes in both halves.
    juce::ValueTree twoInstrumentTrack (SongDocument& doc)
    {
        auto t = addTrack (doc);
        t.setProperty (SongIDs::sourceProgram, 73, nullptr);
        t.setProperty (SongIDs::endTick, 1920, nullptr);
        addEvent (t, 0, { 0xC0, 73 });
        addEvent (t, 960, { 0xC0, 40 });
        addNote (t, 60, 0, 480);
        addNote (t, 62, 960, 480);
        return t;
    }
}

TEST_CASE ("splitAtTicks: several ticks are one undo step", "[instrument][split]")
{
    SongDocument doc;
    auto t = addTrack (doc);
    addNote (t, 60, 0, 1800);

    splitAtTicks (doc, idOf (t), { 480, 960 });

    CHECK (sectionsOf (t).size() == 3);
    doc.undo();
    CHECK (sectionsOf (t).size() == 1);
    CHECK_FALSE (doc.canUndo());
}

TEST_CASE ("splitAtTicks: ticks outside the sections change nothing and open no transaction", "[instrument][split]")
{
    SongDocument doc;
    auto t = addTrack (doc);
    addNote (t, 60, 0, 480);

    splitAtTicks (doc, idOf (t), { 0, 480, 5000 });

    CHECK_FALSE (doc.canUndo());
}

TEST_CASE ("auto split: cuts the track at each instrument change, in one undo step", "[instrument][split]")
{
    SongDocument doc;
    auto t = twoInstrumentTrack (doc);
    REQUIRE (canAutoSplit (t));

    autoSplitOnInstrumentChange (doc, idOf (t));

    const auto s = sectionsOf (t);
    REQUIRE (s.size() == 2);
    CHECK (s[0].endTick == 960);
    CHECK (s[1].startTick == 960);
    doc.undo();
    CHECK (sectionsOf (t).size() == 1);
    CHECK_FALSE (doc.canUndo());
}

TEST_CASE ("auto split: one instrument is disabled and changes nothing", "[instrument][split]")
{
    SongDocument doc;
    auto t = addTrack (doc);
    t.setProperty (SongIDs::sourceProgram, 73, nullptr);
    addEvent (t, 0, { 0xC0, 73 });
    addNote (t, 60, 0, 480);
    CHECK_FALSE (canAutoSplit (t));

    autoSplitOnInstrumentChange (doc, idOf (t));

    CHECK_FALSE (doc.canUndo());
}

TEST_CASE ("auto split: a change after the last section creates no empty section", "[instrument][split]")
{
    SongDocument doc;
    auto t = addTrack (doc);
    t.setProperty (SongIDs::sourceProgram, 73, nullptr);
    addEvent (t, 0, { 0xC0, 73 });
    addEvent (t, 5000, { 0xC0, 40 });
    addNote (t, 60, 0, 480);

    autoSplitOnInstrumentChange (doc, idOf (t));

    CHECK (sectionsOf (t).size() == 1);
    CHECK_FALSE (doc.canUndo());
}

TEST_CASE ("set instrument: rewrites the first change, deletes the later ones, updates sourceProgram", "[instrument][set]")
{
    SongDocument doc;
    auto t = twoInstrumentTrack (doc);

    setTrackInstrument (doc, idOf (t), 24);

    const auto pcs = programChangesOf (t);
    REQUIRE (pcs.size() == 1);
    CHECK (pcs[0].tick == 0);
    CHECK (pcs[0].program == 24);
    CHECK ((int) t.getProperty (SongIDs::sourceProgram) == 24);
    doc.undo();
    CHECK (programChangesOf (t).size() == 2);
    CHECK ((int) t.getProperty (SongIDs::sourceProgram) == 73);
    CHECK_FALSE (doc.canUndo());
}

TEST_CASE ("set instrument: a track without a change gets one at tick 0 on its default channel", "[instrument][set]")
{
    SongDocument doc;
    auto t = addTrack (doc, "T", 3);
    t.setProperty (SongIDs::defaultChannel, 3, nullptr);
    t.setProperty (SongIDs::sourceProgram, 0, nullptr);
    addNote (t, 60, 0, 480);

    setTrackInstrument (doc, idOf (t), 40);

    const auto pcs = programChangesOf (t);
    REQUIRE (pcs.size() == 1);
    CHECK (pcs[0].tick == 0);
    CHECK (pcs[0].channel == 3);
    CHECK (pcs[0].program == 40);
}

TEST_CASE ("set instrument: picking what the track already plays changes nothing", "[instrument][set]")
{
    SongDocument doc;
    auto t = addTrack (doc);
    t.setProperty (SongIDs::sourceProgram, 5, nullptr);
    addNote (t, 60, 0, 480);

    setTrackInstrument (doc, idOf (t), 5);   // no change event, already program 5

    CHECK (programChangesOf (t).empty());
    CHECK_FALSE (doc.canUndo());
}

TEST_CASE ("set instrument: keeps the first change per channel", "[instrument][set]")
{
    SongDocument doc;
    auto t = addTrack (doc);
    t.setProperty (SongIDs::sourceProgram, 1, nullptr);
    addEvent (t, 0, { 0xC0, 1 });
    addEvent (t, 0, { 0xC1, 2 });
    addEvent (t, 480, { 0xC0, 3 });

    setTrackInstrument (doc, idOf (t), 9);

    const auto pcs = programChangesOf (t);
    REQUIRE (pcs.size() == 2);
    CHECK (pcs[0].program == 9);
    CHECK (pcs[1].program == 9);
}

TEST_CASE ("set instrument: the conductor and unknown tracks are ignored", "[instrument][set]")
{
    SongDocument doc;
    auto c = addTrack (doc);
    c.setProperty (SongIDs::isConductor, true, nullptr);

    setTrackInstrument (doc, idOf (c), 9);
    setTrackInstrument (doc, 9999, 9);

    CHECK_FALSE (doc.canUndo());
}
```

- [ ] **Step 2: Run to verify they fail**

Run: `cmake -B build && cmake --build build --target forge_tests 2>&1 | tail -5`
Expected: FAIL — `UI/InstrumentEdit.h: No such file or directory`.

- [ ] **Step 3: Implement `splitAtTicks`.**

In `Source/UI/SectionEdit.cpp`, replace the whole `splitAt` function (from `void splitAt (SongDocument& doc, ...` to its closing brace, the line before `void moveSections`) with these three functions. The loop body is the existing one, moved verbatim into `splitTrackAt`:

```cpp
namespace
{
    // Splits every stored section of `track` that strictly contains `tick`. The caller
    // has begun the transaction and materialised the track.
    void splitTrackAt (SongDocument& doc, juce::ValueTree track, int tick)
    {
        auto sectionsNode = track.getChildWithName (SongIDs::SECTIONS);
        auto notesNode = SongDocument::getNotesNode (track);

        for (const auto& s : storedSections (track))
        {
            if (! (s.startTick < tick && tick < s.endTick))
                continue;

            const auto members = membersOf (track, s.id);
            const auto newId = doc.mintSectionId();

            juce::ValueTree right (SongIDs::SECTION);
            right.setProperty (SongIDs::sectionId, newId, nullptr);
            right.setProperty (SongIDs::startTick, tick, nullptr);
            right.setProperty (SongIDs::endTick, s.endTick, nullptr);
            doc.setProperty (findSectionNode (track, s.id), SongIDs::endTick, tick, false);
            doc.addChild (sectionsNode, right, false);

            for (auto note : members)
            {
                const int start = (int) note.getProperty (SongIDs::startTick);
                const int end = start + (int) note.getProperty (SongIDs::durationTicks);
                if (start >= tick)
                {
                    doc.setProperty (note, SongIDs::sectionId, newId, false);
                }
                else if (end > tick)
                {
                    auto tail = note.createCopy();
                    tail.setProperty (SongIDs::startTick, tick, nullptr);
                    tail.setProperty (SongIDs::durationTicks, end - tick, nullptr);
                    tail.setProperty (SongIDs::sectionId, newId, nullptr);
                    for (const auto& p : { SongIDs::onOrder, SongIDs::offOrder })
                        tail.removeProperty (p, nullptr);
                    tail.setProperty (SongIDs::offSynthesized, false, nullptr);
                    doc.addChild (notesNode, tail, false);

                    doc.setProperty (note, SongIDs::durationTicks, tick - start, false);
                    markNoteTimingEdited (doc, note);
                }
            }
        }
    }
}

void splitAt (SongDocument& doc, const std::vector<juce::int64>& trackIds, int tick)
{
    std::vector<juce::ValueTree> tracks;
    for (const auto id : trackIds)
    {
        auto track = doc.findTrackById (id);
        if (track.isValid() && ! (bool) track.getProperty (SongIDs::isConductor, false)
            && containsStrictly (sectionsOf (track), tick))
            tracks.push_back (track);
    }
    if (tracks.empty())
        return;

    doc.getUndoManager().beginNewTransaction();   // materialising is part of the split's one undo step
    for (auto track : tracks)
        materialise (doc, track);
    for (auto track : tracks)
        splitTrackAt (doc, track, tick);
}

void splitAtTicks (SongDocument& doc, juce::int64 trackId, std::vector<int> ticks)
{
    auto track = doc.findTrackById (trackId);
    if (! track.isValid() || (bool) track.getProperty (SongIDs::isConductor, false))
        return;

    // A tick strictly inside a section stays so until it is split itself, so filtering
    // against the sections as they are now is enough.
    const auto sections = sectionsOf (track);
    std::sort (ticks.begin(), ticks.end());
    ticks.erase (std::unique (ticks.begin(), ticks.end()), ticks.end());
    ticks.erase (std::remove_if (ticks.begin(), ticks.end(),
                                 [&] (int t) { return ! containsStrictly (sections, t); }),
                 ticks.end());
    if (ticks.empty())
        return;

    doc.getUndoManager().beginNewTransaction();
    materialise (doc, track);
    for (const int tick : ticks)
        splitTrackAt (doc, track, tick);
}
```

In `Source/UI/SectionEdit.h`, after the `splitAt` declaration add:

```cpp
// As splitAt for one track and several ticks: every tick strictly inside a section
// splits it, in one undo transaction. Ticks that split nothing are ignored; if none
// does, nothing is touched and no transaction opens.
void splitAtTicks (SongDocument& doc, juce::int64 trackId, std::vector<int> ticks);
```

- [ ] **Step 4: Implement `InstrumentEdit`.**

`Source/UI/InstrumentEdit.h`:

```cpp
#pragma once

#include "UI/SongDocument.h"

// The two instrument-band menu actions (spec:
// docs/superpowers/specs/2026-10-10-songsmith-instrument-changes-design.md).
// Pure document logic: no UI, no Source/Core. Each call is one undo transaction;
// a call that would change nothing touches nothing and opens none.
namespace lotro
{

// True when the track has at least two instrument segments.
bool canAutoSplit (const juce::ValueTree& track);

// Splits the track at every instrument change after tick 0.
void autoSplitOnInstrumentChange (SongDocument& doc, juce::int64 trackId);

// Makes the whole track one instrument: the first program change on each channel is
// set to `program`, the later ones are deleted, a track with none gets one at tick 0
// on its defaultChannel, and sourceProgram follows. The conductor and unknown
// tracks are ignored.
void setTrackInstrument (SongDocument& doc, juce::int64 trackId, int program);

} // namespace lotro
```

`Source/UI/InstrumentEdit.cpp`:

```cpp
#include "UI/InstrumentEdit.h"

#include "UI/ProgramChanges.h"
#include "UI/SectionEdit.h"

#include <algorithm>
#include <cstdint>
#include <set>

namespace lotro
{

namespace
{
    juce::var programBytes (int channel, int program)
    {
        const std::uint8_t bytes[2] { (std::uint8_t) (0xC0 | ((channel - 1) & 0x0F)), (std::uint8_t) program };
        return juce::var (juce::MemoryBlock (bytes, 2));
    }

    int nextEventOrder (const juce::ValueTree& track)
    {
        int next = 0;
        const auto events = SongDocument::getEventsNode (track);
        for (int i = 0; i < events.getNumChildren(); ++i)
            next = std::max (next, (int) events.getChild (i).getProperty (SongIDs::order, 0) + 1);
        return next;
    }
}

bool canAutoSplit (const juce::ValueTree& track)
{
    return instrumentSegmentsOf (track).size() >= 2;
}

void autoSplitOnInstrumentChange (SongDocument& doc, juce::int64 trackId)
{
    const auto segments = instrumentSegmentsOf (doc.findTrackById (trackId));
    std::vector<int> ticks;
    for (size_t i = 1; i < segments.size(); ++i)
        ticks.push_back (segments[i].startTick);
    splitAtTicks (doc, trackId, ticks);
}

void setTrackInstrument (SongDocument& doc, juce::int64 trackId, int program)
{
    auto track = doc.findTrackById (trackId);
    if (! track.isValid() || (bool) track.getProperty (SongIDs::isConductor, false))
        return;
    program = std::clamp (program, 0, 127);

    const auto changes = programChangesOf (track);
    std::set<int> seenChannels;
    std::vector<ProgramChange> keep, drop;
    for (const auto& c : changes)
        (seenChannels.insert (c.channel).second ? keep : drop).push_back (c);

    const bool sourceDiffers = (int) track.getProperty (SongIDs::sourceProgram, 0) != program;
    const bool keptDiffers = std::any_of (keep.begin(), keep.end(), [program] (const ProgramChange& c) { return c.program != program; });
    if (! sourceDiffers && ! keptDiffers && drop.empty())
        return;

    doc.getUndoManager().beginNewTransaction();
    auto events = SongDocument::getEventsNode (track);
    for (const auto& c : keep)
        if (c.program != program)
            doc.setProperty (c.event, SongIDs::data, programBytes (c.channel, program), false);
    for (const auto& c : drop)
        doc.removeChild (events, c.event, false);

    if (changes.empty())
    {
        juce::ValueTree event (SongIDs::EVENT);
        event.setProperty (SongIDs::tick, 0, nullptr);
        event.setProperty (SongIDs::order, nextEventOrder (track), nullptr);
        event.setProperty (SongIDs::data, programBytes ((int) track.getProperty (SongIDs::defaultChannel, 1), program), nullptr);
        doc.addChild (events, event, false);
    }
    if (sourceDiffers)
        doc.setProperty (track, SongIDs::sourceProgram, program, false);
}

} // namespace lotro
```

Add `Source/UI/InstrumentEdit.cpp` to `CMakeLists.txt` and `${CMAKE_SOURCE_DIR}/Source/UI/InstrumentEdit.cpp` to `Tests/CMakeLists.txt`, and `InstrumentEdit_tests.cpp` to the test list.

- [ ] **Step 5: Run the new tests and the existing split/section tests**

Run: `cmake --build build --target forge_tests 2>&1 | tail -3 && ./build/Tests/forge_tests "[instrument]" | tail -3 && ctest --test-dir build --output-on-failure | tail -3`
Expected: all pass (the `splitAt` refactor changes no behaviour; the existing section tests cover it).

If any existing section test fails, the extraction altered `splitAt`; compare against `git diff Source/UI/SectionEdit.cpp`.

- [ ] **Step 6: Commit**

```bash
git add Source/UI/SectionEdit.* Source/UI/InstrumentEdit.* Tests/InstrumentEdit_tests.cpp CMakeLists.txt Tests/CMakeLists.txt
git commit -m "feat(ui): auto-split at instrument changes and set a track's instrument

Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>"
```

---

### Task 3: Band segments, family names and the right-click menu

**Files:**
- Modify: `Source/UI/GmProgramNames.h` (add `gmFamilyName`)
- Modify: `Source/UI/TrackRowComponent.h`, `Source/UI/TrackRowComponent.cpp`
- Modify: `Source/UI/TrackListComponent.cpp` (wire the callbacks, next to `row->onTrackDoubleClicked`)
- Test: `Tests/TrackRowComponent_tests.cpp`, `Tests/TrackListComponent_tests.cpp`

**Interfaces:**
- Consumes: `instrumentSegmentsOf`, `bandSegments` (Task 1); `canAutoSplit`, `autoSplitOnInstrumentChange`, `setTrackInstrument` (Task 2); `gmFamilyFor`, `familyBaseColour` (`SongsmithColours.h`).
- Produces on `TrackRowComponent`:
  - `juce::PopupMenu buildInstrumentMenu() const;` — item id `1` = Auto split; submenu "Set track instrument to" whose leaves have id `1000 + program`; the first segment's program is ticked.
  - `void instrumentMenuChosen (int itemId);`
  - `std::function<void (juce::int64)> onAutoSplitRequested;`
  - `std::function<void (juce::int64, int)> onSetInstrumentRequested;`
  - `static bool inInstrumentBand (juce::Point<int> p);`
- Produces: `inline const char* gmFamilyName (int family)` (0..15) in `GmProgramNames.h`.

- [ ] **Step 1: Write the failing tests.**

Append to `Tests/TrackRowComponent_tests.cpp` (it already includes `SongDocument.h`, `TrackRowComponent.h`, `TimelineViewState.h`; add `#include "PlaybackTestSupport.h"` at the top):

```cpp
namespace
{
    // First menu item (searching submenus) with this id, or nullptr.
    const juce::PopupMenu::Item* findItem (const juce::PopupMenu& menu, int id, std::vector<juce::PopupMenu::Item>& keep)
    {
        juce::PopupMenu::MenuItemIterator it (menu);
        while (it.next())
        {
            keep.push_back (it.getItem());
            if (it.getItem().itemID == id)
                return &keep.back();
            if (it.getItem().subMenu != nullptr)
                if (const auto* sub = findItem (*it.getItem().subMenu, id, keep))
                    return sub;
        }
        return nullptr;
    }
}

TEST_CASE ("TrackRowComponent: the band menu offers auto split only when the track changes instrument", "[track-row][instrument]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SongDocument doc;
    auto multi = playbacktest::addTrack (doc, "Multi");
    multi.setProperty (SongIDs::sourceProgram, 73, nullptr);
    multi.setProperty (SongIDs::endTick, 1920, nullptr);
    playbacktest::addEvent (multi, 0, { 0xC0, 73 });
    playbacktest::addEvent (multi, 960, { 0xC0, 40 });
    playbacktest::addNote (multi, 60, 0, 480);
    auto single = playbacktest::addTrack (doc, "Single");
    single.setProperty (SongIDs::sourceProgram, 5, nullptr);
    playbacktest::addNote (single, 60, 0, 480);

    TimelineViewState view;
    TrackRowComponent multiRow (multi, 1, view);
    TrackRowComponent singleRow (single, 2, view);
    std::vector<juce::PopupMenu::Item> keep;

    const auto multiMenu = multiRow.buildInstrumentMenu();
    const auto* split = findItem (multiMenu, 1, keep);
    REQUIRE (split != nullptr);
    CHECK (split->isEnabled);

    const auto singleMenu = singleRow.buildInstrumentMenu();
    const auto* disabled = findItem (singleMenu, 1, keep);
    REQUIRE (disabled != nullptr);
    CHECK_FALSE (disabled->isEnabled);
}

TEST_CASE ("TrackRowComponent: the set-instrument list ticks the track's first instrument", "[track-row][instrument]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SongDocument doc;
    auto t = playbacktest::addTrack (doc);
    t.setProperty (SongIDs::sourceProgram, 73, nullptr);
    playbacktest::addEvent (t, 0, { 0xC0, 73 });
    playbacktest::addNote (t, 60, 0, 480);
    TimelineViewState view;
    TrackRowComponent row (t, 1, view);
    std::vector<juce::PopupMenu::Item> keep;
    const auto menu = row.buildInstrumentMenu();

    const auto* flute = findItem (menu, 1000 + 73, keep);
    const auto* other = findItem (menu, 1000 + 40, keep);

    REQUIRE (flute != nullptr);
    REQUIRE (other != nullptr);
    CHECK (flute->isTicked);
    CHECK_FALSE (other->isTicked);
}

TEST_CASE ("TrackRowComponent: choosing a band menu item fires the matching callback", "[track-row][instrument]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SongDocument doc;
    auto t = playbacktest::addTrack (doc);
    playbacktest::addNote (t, 60, 0, 480);
    const auto id = (juce::int64) t.getProperty (SongIDs::trackId);
    TimelineViewState view;
    TrackRowComponent row (t, 1, view);
    juce::int64 splitId = -1, setId = -1;
    int setProgram = -1;
    row.onAutoSplitRequested = [&] (juce::int64 i) { splitId = i; };
    row.onSetInstrumentRequested = [&] (juce::int64 i, int p) { setId = i; setProgram = p; };

    row.instrumentMenuChosen (1);
    row.instrumentMenuChosen (1000 + 40);
    row.instrumentMenuChosen (0);   // dismissed: nothing

    CHECK (splitId == id);
    CHECK (setId == id);
    CHECK (setProgram == 40);
}

TEST_CASE ("TrackRowComponent: the instrument band is the top strip of the canvas side", "[track-row][instrument]")
{
    CHECK (TrackRowComponent::inInstrumentBand ({ TrackRowComponent::trackInfoWidth + 5, 3 }));
    CHECK_FALSE (TrackRowComponent::inInstrumentBand ({ TrackRowComponent::trackInfoWidth - 5, 3 }));
    CHECK_FALSE (TrackRowComponent::inInstrumentBand ({ TrackRowComponent::trackInfoWidth + 5, TrackRowComponent::instrumentBandHeight }));
}
```

Append to `Tests/TrackListComponent_tests.cpp` (the helper `TrackListComponentTestAccess::rowFor` exists):

```cpp
TEST_CASE ("TrackListComponent: the band menu callbacks edit the document", "[track-list][instrument]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SongDocument doc;
    auto t = playbacktest::addTrack (doc);
    t.setProperty (SongIDs::sourceProgram, 73, nullptr);
    t.setProperty (SongIDs::endTick, 1920, nullptr);
    playbacktest::addEvent (t, 0, { 0xC0, 73 });
    playbacktest::addEvent (t, 960, { 0xC0, 40 });
    playbacktest::addNote (t, 60, 0, 480);
    playbacktest::addNote (t, 62, 960, 480);
    const auto id = (juce::int64) t.getProperty (SongIDs::trackId);
    TrackListComponent list (doc);
    list.setBounds (0, 0, 600, 300);
    TrackListComponentTestAccess::rebuild (list);
    auto* row = TrackListComponentTestAccess::rowFor (list, id);
    REQUIRE (row != nullptr);

    row->instrumentMenuChosen (1);
    CHECK (sectionsOf (doc.findTrackById (id)).size() == 2);

    row = TrackListComponentTestAccess::rowFor (list, id);   // rows are rebuilt on a document change
    REQUIRE (row != nullptr);
    row->instrumentMenuChosen (1000 + 24);
    CHECK ((int) doc.findTrackById (id).getProperty (SongIDs::sourceProgram) == 24);
}
```

(Add `#include "UI/SectionEdit.h"` to that test file if it is not already included.)

- [ ] **Step 2: Run to verify they fail**

Run: `cmake --build build --target forge_tests 2>&1 | grep -E "error" | head -5`
Expected: FAIL — `buildInstrumentMenu` / `instrumentMenuChosen` / `inInstrumentBand` not declared.

- [ ] **Step 3: Add `gmFamilyName`.** In `Source/UI/GmProgramNames.h`, after `gmProgramName`, inside the namespace:

```cpp
    // The General MIDI family (program / 8) a program belongs to; 0..15.
    inline const char* gmFamilyName (int family)
    {
        static const char* const names[16] =
        {
            "Piano", "Chromatic Percussion", "Organ", "Guitar", "Bass", "Strings", "Ensemble", "Brass",
            "Reed", "Pipe", "Synth Lead", "Synth Pad", "Synth Effects", "Ethnic", "Percussive", "Sound Effects"
        };
        return (family >= 0 && family < 16) ? names[family] : "";
    }
```

- [ ] **Step 4: Implement the row changes.**

In `Source/UI/TrackRowComponent.h` add to the public section (after `instrumentLabel()`):

```cpp
    // True for a point in the instrument band (the top strip of the canvas side).
    static bool inInstrumentBand (juce::Point<int> p) noexcept
    {
        return p.x >= trackInfoWidth && p.y >= 0 && p.y < instrumentBandHeight;
    }

    // The band's right-click menu: item 1 "Auto split on instrument change" (enabled
    // when the track has two or more instrument segments) and a "Set track instrument
    // to" submenu, families then programs, whose leaf ids are 1000 + program and
    // whose first-segment program is ticked.
    juce::PopupMenu buildInstrumentMenu() const;
    void instrumentMenuChosen (int itemId);

    std::function<void (juce::int64)> onAutoSplitRequested;
    std::function<void (juce::int64, int)> onSetInstrumentRequested;
```

`trackInfoWidth` is declared later in the class; since these are inline member functions of the class body, the later `static constexpr` is visible. Add `#include <juce_gui_basics/juce_gui_basics.h>` is already present.

In `Source/UI/TrackRowComponent.cpp`, add includes `#include "InstrumentEdit.h"`, `#include "ProgramChanges.h"` (use the include style the file already uses for `GmProgramNames.h`), then:

Replace the band painting block (the `// Instrument band:` block) with:

```cpp
    // Instrument band: canvas side only, over the top of the notes. One coloured,
    // labelled segment per instrument; drum and conductor rows keep a single label.
    {
        const auto band = bounds.withTrimmedLeft (juce::jmin (trackInfoWidth, bounds.getWidth()))
                                .removeFromTop (instrumentBandHeight);
        g.setColour (juce::Colour (instrumentBand));
        g.fillRect (band);
        g.setFont (juce::Font (juce::FontOptions (10.0f)));

        const bool plainLabel = isConductorTrack() || (int) track.getProperty (SongIDs::sourceMidiChannel) == 10;
        const auto segments = instrumentSegmentsOf (track);
        if (plainLabel || segments.size() < 2)
        {
            g.setColour (juce::Colour (textMuted));
            g.drawText (instrumentLabel(), band.withTrimmedLeft (6).withTrimmedRight (4), juce::Justification::centredLeft);
        }
        else
        {
            for (const auto& s : bandSegments (segments, viewState))
            {
                const auto cell = juce::Rectangle<int> (band.getX() + s.x0, band.getY(), s.x1 - s.x0, band.getHeight())
                                      .getIntersection (band);
                if (cell.isEmpty())
                    continue;
                const auto family = gmFamilyFor (s.program, 1);
                g.setColour (juce::Colour (familyBaseColour[(int) family]).withAlpha (0.55f));
                g.fillRect (cell);
                g.setColour (juce::Colour (columnDivider));
                g.fillRect (cell.withWidth (1));
                g.setColour (juce::Colour (text));
                g.drawText (gmProgramName (s.program), cell.withTrimmedLeft (4).withTrimmedRight (2), juce::Justification::centredLeft);
            }
        }
    }
```

This block needs the row to know `viewState` and `isConductorTrack()`: check the constructor — `TrackRowComponent (juce::ValueTree, int, const TimelineViewState&)` passes `viewState` to the preview. If the row does not keep its own reference, add a member `const TimelineViewState& viewState;` initialised in the constructor, and add `bool isConductorTrack() const { return (bool) track.getProperty (SongIDs::isConductor, false); }` to the private section. (`familyBaseColour` is the table in `SongsmithColours.h`; if its element type is `juce::uint32`, the `juce::Colour (...)` call above is correct, otherwise adapt the cast.)

Add the new member functions:

```cpp
juce::PopupMenu TrackRowComponent::buildInstrumentMenu() const
{
    const auto segments = instrumentSegmentsOf (track);
    const int current = segments.empty() ? -1 : segments.front().program;

    juce::PopupMenu menu;
    menu.addItem (1, "Auto split on instrument change", canAutoSplit (track));

    juce::PopupMenu all;
    for (int family = 0; family < 16; ++family)
    {
        juce::PopupMenu programs;
        for (int p = family * 8; p < family * 8 + 8; ++p)
            programs.addItem (1000 + p, gmProgramName (p), true, p == current);
        all.addSubMenu (gmFamilyName (family), programs);
    }
    menu.addSubMenu ("Set track instrument to", all);
    return menu;
}

void TrackRowComponent::instrumentMenuChosen (int itemId)
{
    if (itemId == 1 && onAutoSplitRequested)
        onAutoSplitRequested (getTrackId());
    else if (itemId >= 1000 && itemId < 1128 && onSetInstrumentRequested)
        onSetInstrumentRequested (getTrackId(), itemId - 1000);
}
```

In `mouseDown`, before the existing `onTrackSelected` call:

```cpp
    if (e.mods.isPopupMenu() && inInstrumentBand (e.getPosition()) && ! isConductorTrack())
    {
        juce::Component::SafePointer<TrackRowComponent> safe (this);
        buildInstrumentMenu().showMenuAsync (juce::PopupMenu::Options(), [safe] (int id)
        {
            if (safe != nullptr)
                safe->instrumentMenuChosen (id);
        });
        return;
    }
```

(`e.getPosition()` is row-local, matching `inInstrumentBand`'s coordinates because the band is at the row's top.)

- [ ] **Step 5: Wire the list.** In `Source/UI/TrackListComponent.cpp`, next to `row->onTrackDoubleClicked = ...` add (and `#include "InstrumentEdit.h"`):

```cpp
        row->onAutoSplitRequested = [this] (juce::int64 id) { autoSplitOnInstrumentChange (doc, id); };
        row->onSetInstrumentRequested = [this] (juce::int64 id, int program) { setTrackInstrument (doc, id, program); };
```

- [ ] **Step 6: Run the tests**

Run: `cmake --build build --target forge_tests 2>&1 | tail -3 && ./build/Tests/forge_tests "[instrument]" | tail -3 && ctest --test-dir build --output-on-failure 2>&1 | tail -4`
Expected: all pass; total is 876 + the new test cases.

- [ ] **Step 7: Commit**

```bash
git add Source/UI Tests
git commit -m "feat(ui): instrument segments in the band and a right-click menu to split or set the instrument

Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>"
```

---

### Task 4: `MergeScope` setting and Preferences ▸ Editing radio

**Files:**
- Create: `Source/UI/MergeScope.h`
- Modify: `Source/UI/AppSettings.h`, `Source/UI/Preferences/EditingPreferencesPage.h`, `Source/UI/Preferences/EditingPreferencesPage.cpp`
- Modify: `Source/UI/TrackListComponent.h`, `Source/UI/SongsmithMainComponent.h`, `Source/UI/SongsmithMainComponent.cpp`, `Source/UI/MainWindow.cpp`
- Test: `Tests/AppSettings_tests.cpp`, `Tests/ViewPreferencesPages_tests.cpp`, `Tests/SongsmithMainComponent_tests.cpp`

**Interfaces:**
- Produces: `enum class MergeScope { notesOnly, allEvents };` in `Source/UI/MergeScope.h`; `AppSettings::mergeScope() const` / `bool setMergeScope (MergeScope)`, key `editing.mergeScope` (`"notes"` / `"all"`, anything else reads `notesOnly`); `TrackListComponent::setMergeScope (MergeScope)` / `getMergeScope()`; `SongsmithMainComponent::setMergeScope (MergeScope)`; `EditingPreferencesPage::notesOnlyRadioForTesting()` / `allEventsRadioForTesting()` (`juce::ToggleButton&`).

- [ ] **Step 1: Write the failing tests.**

Append to `Tests/AppSettings_tests.cpp`:

```cpp
TEST_CASE ("AppSettings: the merge scope defaults to notes only, round-trips and ignores junk", "[app-settings]")
{
    auto file = juce::File::createTempFile (".settings");
    const juce::ScopeGuard cleanup { [&] { file.deleteFile(); } };
    {
        juce::PropertiesFile props (file, {});
        AppSettings settings (props);
        CHECK (settings.mergeScope() == MergeScope::notesOnly);
        CHECK (settings.setMergeScope (MergeScope::allEvents));
    }
    juce::PropertiesFile reopened (file, {});
    AppSettings settings (reopened);
    CHECK (settings.mergeScope() == MergeScope::allEvents);
    CHECK (reopened.getValue ("editing.mergeScope") == "all");

    reopened.setValue ("editing.mergeScope", "banana");
    CHECK (settings.mergeScope() == MergeScope::notesOnly);
}
```

Append to `Tests/ViewPreferencesPages_tests.cpp`:

```cpp
TEST_CASE ("Editing page: the merge scope radios reflect and change the setting", "[preferences][editing-page]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    FakeApp app;
    EditingPreferencesPage page (app.services, [&] { ++app.changedCalls; });
    CHECK (page.notesOnlyRadioForTesting().getToggleState());
    CHECK_FALSE (page.allEventsRadioForTesting().getToggleState());

    page.allEventsRadioForTesting().triggerClick();

    CHECK (app.settings.mergeScope() == MergeScope::allEvents);
    CHECK (app.applyCalls == 1);
    CHECK (app.changedCalls == 1);

    EditingPreferencesPage reopened (app.services);
    CHECK (reopened.allEventsRadioForTesting().getToggleState());
}
```

Append to `Tests/SongsmithMainComponent_tests.cpp` (follow the file's existing fixture for building a `SongsmithMainComponent`; the two lines that matter are):

```cpp
    songsmith.setMergeScope (MergeScope::allEvents);
    CHECK (songsmith.getTrackListForTesting().getMergeScope() == MergeScope::allEvents);
```

(If `SongsmithMainComponent` has no accessor for its `TrackListComponent`, mirror how the existing `setFollowPlayhead` test reads `trackList.getFollowPlayhead()` and use that same route.)

- [ ] **Step 2: Run to verify they fail**

Run: `cmake --build build --target forge_tests 2>&1 | grep error | head -3`
Expected: FAIL — `MergeScope` undeclared.

- [ ] **Step 3: Implement.**

`Source/UI/MergeScope.h`:

```cpp
#pragma once

namespace lotro
{
    // What an Alt-drag Move/Copy to another track carries (Preferences > Editing).
    enum class MergeScope { notesOnly, allEvents };
}
```

`Source/UI/AppSettings.h`: add `#include "MergeScope.h"` beside `GridSize.h`; in the public section after `setDefaultGrid`:

```cpp
    // What moving or copying sections to another track carries. Absent or unrecognised reads notesOnly.
    MergeScope mergeScope() const
    {
        return file.getValue (keyMergeScope) == "all" ? MergeScope::allEvents : MergeScope::notesOnly;
    }
    bool setMergeScope (MergeScope scope)
    {
        file.setValue (keyMergeScope, scope == MergeScope::allEvents ? "all" : "notes");
        return save();
    }
```

and in the private key list: `static constexpr const char* keyMergeScope = "editing.mergeScope";`

`Source/UI/Preferences/EditingPreferencesPage.h`: add members

```cpp
        juce::ToggleButton& notesOnlyRadioForTesting() { return notesOnly; }
        juce::ToggleButton& allEventsRadioForTesting() { return allEvents; }
    ...
        juce::Label        scopeLabel;
        juce::ToggleButton notesOnly { "Notes only" };
        juce::ToggleButton allEvents { "All events (controllers, program changes, pitch bend)" };
```

`EditingPreferencesPage.cpp` constructor, before the closing brace:

```cpp
        scopeLabel.setText ("When moving or copying sections to another track (Alt-drag), carry", juce::dontSendNotification);
        scopeLabel.setColour (juce::Label::textColourId, juce::Colour (SongsmithColours::text));
        addAndMakeVisible (scopeLabel);

        constexpr int radioGroup = 4201;
        for (auto* b : { &notesOnly, &allEvents })
        {
            b->setRadioGroupId (radioGroup);
            b->setColour (juce::ToggleButton::textColourId, juce::Colour (SongsmithColours::text));
            addAndMakeVisible (*b);
        }
        const bool all = services.settings.mergeScope() == MergeScope::allEvents;
        notesOnly.setToggleState (! all, juce::dontSendNotification);
        allEvents.setToggleState (all, juce::dontSendNotification);
        const auto changeScope = [this]
        {
            services.settings.setMergeScope (allEvents.getToggleState() ? MergeScope::allEvents : MergeScope::notesOnly);
            notify();
        };
        notesOnly.onClick = changeScope;
        allEvents.onClick = changeScope;
```

and in `resized()` append:

```cpp
        area.removeFromTop (16);
        scopeLabel.setBounds (area.removeFromTop (22));
        notesOnly.setBounds (area.removeFromTop (24));
        allEvents.setBounds (area.removeFromTop (24));
```

`Source/UI/TrackListComponent.h` (public, beside `setFollowPlayhead`): 

```cpp
    // What an Alt-drag Move/Copy to another track carries.
    void setMergeScope (MergeScope scope) noexcept { mergeScope = scope; }
    MergeScope getMergeScope() const noexcept { return mergeScope; }
```

with member `MergeScope mergeScope = MergeScope::notesOnly;` in the private section and `#include "MergeScope.h"`. `SongsmithMainComponent`: declare `void setMergeScope (MergeScope scope);` beside `setFollowPlayhead`, and define it in the `.cpp`: `void SongsmithMainComponent::setMergeScope (MergeScope scope) { trackList.setMergeScope (scope); }`. In `MainWindow::applyViewSettings` add `songsmith.setMergeScope (appSettings.mergeScope());`.

- [ ] **Step 4: Run the tests**

Run: `cmake --build build --target forge_tests 2>&1 | tail -3 && ./build/Tests/forge_tests "[app-settings],[editing-page],[preferences]" | tail -3`
Expected: pass.

- [ ] **Step 5: Commit**

```bash
git add Source/UI Tests
git commit -m "feat(ui): Preferences > Editing: what moving or copying sections to another track carries

Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>"
```

---

### Task 5: `mergeSections` carries events in All-events mode

**Files:**
- Modify: `Source/UI/NoteMerge.h`, `Source/UI/NoteMerge.cpp`, `Source/UI/TrackListComponent.cpp` (pass the scope)
- Test: `Tests/NoteMerge_tests.cpp`, `Tests/TrackListComponent_tests.cpp`

**Interfaces:**
- Consumes: `MergeScope` (Task 4).
- Produces: `MergeResult::eventsCarried` (int); `MergeResult mergeSections (SongDocument&, const std::vector<SectionRef>&, juce::int64 targetTrackId, bool copy, MergeScope scope = MergeScope::notesOnly);`

- [ ] **Step 1: Write the failing tests.** Append to `Tests/NoteMerge_tests.cpp` (it already has `addNote`, `addEvent`, `idOf`, `wholeTrack`, `splitAt` available; add `#include "UI/MergeScope.h"` and `#include "UI/ProgramChanges.h"`):

```cpp
namespace
{
    std::vector<std::vector<std::uint8_t>> eventBytesOf (const juce::ValueTree& track)
    {
        std::vector<std::vector<std::uint8_t>> out;
        const auto events = SongDocument::getEventsNode (track);
        for (int i = 0; i < events.getNumChildren(); ++i)
        {
            const auto* b = events.getChild (i).getProperty (SongIDs::data).getBinaryData();
            const auto* d = static_cast<const std::uint8_t*> (b->getData());
            out.emplace_back (d, d + b->getSize());
        }
        return out;
    }
}

TEST_CASE ("merge all events: events inside the section travel, others stay", "[merge][events]")
{
    SongDocument doc;
    auto source = addTrack (doc, "S", 1);
    auto target = addTrack (doc, "T", 3);
    target.setProperty (SongIDs::defaultChannel, 3, nullptr);
    addNote (source, 60, 0, 480);
    addNote (source, 62, 960, 480);
    addEvent (source, 0, { 0xB0, 7, 100 });      // in the first section
    addEvent (source, 960, { 0xC0, 40 });        // at the second section's start
    addEvent (source, 1200, { 0xE0, 0, 64 });    // in the second section
    splitAt (doc, { idOf (source) }, 960);
    const auto second = sectionsOf (source)[1];

    const auto r = mergeSections (doc, { { idOf (source), second.id } }, idOf (target), false, MergeScope::allEvents);

    CHECK (r.changed);
    CHECK (r.eventsCarried == 2);
    // Channel rewritten to the target's (3 -> nibble 2).
    CHECK (eventBytesOf (target) == std::vector<std::vector<std::uint8_t>> { { 0xC2, 40 }, { 0xE2, 0, 64 } });
    CHECK (eventBytesOf (source) == std::vector<std::vector<std::uint8_t>> { { 0xB0, 7, 100 } });
}

TEST_CASE ("merge all events: nothing is synthesized for an instrument set before the section", "[merge][events]")
{
    SongDocument doc;
    auto source = addTrack (doc, "S", 1);
    auto target = addTrack (doc, "T", 1);
    addEvent (source, 0, { 0xC0, 73 });          // before the section
    addNote (source, 60, 0, 480);
    addNote (source, 62, 960, 480);
    splitAt (doc, { idOf (source) }, 960);
    const auto second = sectionsOf (source)[1];

    mergeSections (doc, { { idOf (source), second.id } }, idOf (target), true, MergeScope::allEvents);

    CHECK (programChangesOf (target).empty());
}

TEST_CASE ("merge all events: the section end is exclusive and track-name/end-of-track metas never travel", "[merge][events]")
{
    SongDocument doc;
    auto source = addTrack (doc);
    auto target = addTrack (doc);
    addNote (source, 60, 0, 480);
    addNote (source, 62, 960, 480);
    splitAt (doc, { idOf (source) }, 960);
    const auto first = sectionsOf (source)[0];   // [0, 960)
    addEvent (source, 959, { 0xB0, 10, 64 });    // last tick inside
    addEvent (source, 960, { 0xB0, 11, 64 });    // at the end: excluded
    addEvent (source, 0, { 0xFF, 0x03, 0x01, 'x' });   // track name
    addEvent (source, 500, { 0xFF, 0x2F, 0x00 });      // end of track

    const auto r = mergeSections (doc, { { idOf (source), first.id } }, idOf (target), true, MergeScope::allEvents);

    CHECK (r.eventsCarried == 1);
    CHECK (eventBytesOf (target) == std::vector<std::vector<std::uint8_t>> { { 0xB0, 10, 64 } });
}

TEST_CASE ("merge all events: a copy keeps the source's events and a move undoes in one step", "[merge][events]")
{
    SongDocument doc;
    auto source = addTrack (doc);
    auto target = addTrack (doc);
    addNote (source, 60, 0, 480);
    addEvent (source, 100, { 0xB0, 7, 90 });

    mergeSections (doc, { wholeTrack (source) }, idOf (target), true, MergeScope::allEvents);
    CHECK (eventBytesOf (source).size() == 1);
    CHECK (eventBytesOf (target).size() == 1);
    doc.undo();
    CHECK (eventBytesOf (target).empty());

    mergeSections (doc, { wholeTrack (source) }, idOf (target), false, MergeScope::allEvents);
    CHECK (eventBytesOf (source).empty());
    CHECK (eventBytesOf (target).size() == 1);
    doc.undo();
    CHECK (eventBytesOf (source).size() == 1);
    CHECK (eventBytesOf (target).empty());
}

TEST_CASE ("merge all events: events alone make the merge a change even when every note is dropped", "[merge][events]")
{
    SongDocument doc;
    auto source = addTrack (doc);
    auto target = addTrack (doc);
    addNote (source, 60, 0, 480);
    addNote (target, 60, 0, 480);                // the carried note lies inside this one
    addEvent (source, 100, { 0xB0, 7, 90 });

    const auto r = mergeSections (doc, { wholeTrack (source) }, idOf (target), true, MergeScope::allEvents);

    CHECK (r.dropped == 1);
    CHECK (r.changed);
    CHECK (eventBytesOf (target).size() == 1);
}

TEST_CASE ("merge notes only: events never travel", "[merge][events]")
{
    SongDocument doc;
    auto source = addTrack (doc);
    auto target = addTrack (doc);
    addNote (source, 60, 0, 480);
    addEvent (source, 100, { 0xB0, 7, 90 });

    const auto r = mergeSections (doc, { wholeTrack (source) }, idOf (target), false);

    CHECK (r.eventsCarried == 0);
    CHECK (eventBytesOf (target).empty());
    CHECK (eventBytesOf (source).size() == 1);
}
```

Append to `Tests/TrackListComponent_tests.cpp`:

```cpp
TEST_CASE ("TrackListComponent: an Alt-drag carries events when the merge scope is All events", "[track-list][merge][events]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    MergeFixture f;
    playbacktest::addEvent (f.source, 100, { 0xB0, 7, 90 });
    f.list->setMergeScope (MergeScope::allEvents);
    using A = TrackListComponentTestAccess;

    A::mergePress (*f.list, f.sourceId, { 0, SectionZone::Body }, altMods);
    A::mergeDrag (*f.list, screenOf (*f.list, f.targetId), altMods);
    A::mergeRelease (*f.list, screenOf (*f.list, f.targetId), altMods);

    CHECK (SongDocument::getEventsNode (f.target).getNumChildren() == 1);
    CHECK (SongDocument::getEventsNode (f.source).getNumChildren() == 0);
}
```

- [ ] **Step 2: Run to verify they fail**

Run: `cmake --build build --target forge_tests 2>&1 | grep error | head -3`
Expected: FAIL — `mergeSections` takes 4 arguments / `eventsCarried` not a member.

- [ ] **Step 3: Implement.**

`Source/UI/NoteMerge.h`: add `#include "UI/MergeScope.h"`; add `int eventsCarried = 0;   // non-note events that travelled (allEvents only)` to `MergeResult`; change the declaration to

```cpp
MergeResult mergeSections (SongDocument& doc, const std::vector<SectionRef>& refs,
                           juce::int64 targetTrackId, bool copy,
                           MergeScope scope = MergeScope::notesOnly);
```

and extend its comment: "With MergeScope::allEvents the non-note events whose tick lies in a referenced section's [start, end) travel too, at the same ticks, channel messages re-addressed to the target's channel; End-of-Track and track-name metas never do."

`Source/UI/NoteMerge.cpp`: in the anonymous namespace add (after `carriedNotes`):

```cpp
    struct CarriedEvent
    {
        juce::ValueTree event;
        juce::ValueTree sourceTrack;
    };

    std::vector<std::uint8_t> bytesOf (const juce::ValueTree& event)
    {
        if (const auto* block = event.getProperty (SongIDs::data).getBinaryData())
        {
            const auto* d = static_cast<const std::uint8_t*> (block->getData());
            return std::vector<std::uint8_t> (d, d + block->getSize());
        }
        return {};
    }

    // Notes (and the importer's stray note pairs), End-of-Track and track-name metas stay put.
    bool travels (const std::vector<std::uint8_t>& b)
    {
        if (b.empty())
            return false;
        const int kind = b[0] & 0xF0;
        if (kind == 0x80 || kind == 0x90)
            return false;
        if (b[0] == 0xFF && b.size() >= 2 && (b[1] == 0x2F || b[1] == 0x03))
            return false;
        return true;
    }

    // The travelling events inside every ref'd section's [start, end), once each,
    // with the same ref rules as carriedNotes.
    std::vector<CarriedEvent> carriedEvents (const SongDocument& doc, const std::vector<SectionRef>& refs,
                                             juce::int64 targetTrackId)
    {
        std::vector<CarriedEvent> out;
        std::vector<SectionRef> seen;
        for (const auto& r : refs)
        {
            const auto track = doc.findTrackById (r.trackId);
            if (! isMergeTrack (track) || r.trackId == targetTrackId)
                continue;
            const auto sections = sectionsOf (track);
            if (sections.empty())
                continue;
            const auto id = r.sectionId == 0 ? sections.front().id : r.sectionId;
            const auto found = std::find_if (sections.begin(), sections.end(), [id] (const SectionRange& s) { return s.id == id; });
            if (found == sections.end())
                continue;
            const SectionRef key { r.trackId, id };
            if (std::find (seen.begin(), seen.end(), key) != seen.end())
                continue;
            seen.push_back (key);

            const auto events = SongDocument::getEventsNode (track);
            for (int i = 0; i < events.getNumChildren(); ++i)
            {
                const auto event = events.getChild (i);
                const int tick = (int) event.getProperty (SongIDs::tick);
                if (tick < found->startTick || tick >= found->endTick || ! travels (bytesOf (event)))
                    continue;
                if (std::any_of (out.begin(), out.end(), [&] (const CarriedEvent& c) { return c.event == event; }))
                    continue;   // two overlapping sections hold it
                out.push_back ({ event, track });
            }
        }
        return out;
    }

    int nextEventOrder (const juce::ValueTree& track)
    {
        int next = 0;
        const auto events = SongDocument::getEventsNode (track);
        for (int i = 0; i < events.getNumChildren(); ++i)
            next = std::max (next, (int) events.getChild (i).getProperty (SongIDs::order, 0) + 1);
        return next;
    }

    // A detached copy of `src` for `target`: a channel message is re-addressed to the
    // target's channel (the notes inserted by the merge take it too).
    juce::ValueTree makeEvent (const juce::ValueTree& src, const juce::ValueTree& target, int order)
    {
        auto e = src.createCopy();
        e.removeProperty (SongIDs::relocatedFrom, nullptr);
        e.setProperty (SongIDs::order, order, nullptr);
        auto bytes = bytesOf (src);
        if ((bytes[0] & 0xF0) >= 0x80 && (bytes[0] & 0xF0) <= 0xE0)
        {
            const int channel = (int) target.getProperty (SongIDs::defaultChannel, 1);
            bytes[0] = (std::uint8_t) ((bytes[0] & 0xF0) | ((channel - 1) & 0x0F));
        }
        e.setProperty (SongIDs::data, juce::var (juce::MemoryBlock (bytes.data(), bytes.size())), nullptr);
        return e;
    }
```

Add `#include <cstdint>` at the top if missing. Change `mergeSections`:

- signature gains `MergeScope scope`;
- replace `auto carried = carriedNotes (...); if (carried.empty()) return result;` with:

```cpp
    auto carried = carriedNotes (doc, refs, targetTrackId);
    const auto events = scope == MergeScope::allEvents ? carriedEvents (doc, refs, targetTrackId)
                                                       : std::vector<CarriedEvent> {};
    if (carried.empty() && events.empty())
        return result;
```

- replace the `result.changed = ...` line with:

```cpp
    result.eventsCarried = (int) events.size();
    result.changed = result.inserted > 0 || result.extended > 0 || (! copy && ! carried.empty()) || ! events.empty();
```

- at the end, just before `return result;` (after the `if (! copy) for (...) removeChild` note removal), add:

```cpp
    if (! events.empty())
    {
        auto targetEvents = SongDocument::getEventsNode (target);
        int order = nextEventOrder (target);
        for (const auto& c : events)
            doc.addChild (targetEvents, makeEvent (c.event, target, order++), false);
        if (! copy)
            for (const auto& c : events)
                doc.removeChild (SongDocument::getEventsNode (c.sourceTrack), c.event, false);
    }
```

`Source/UI/TrackListComponent.cpp`, in `mergeReleased`: `mergeSections (doc, g.refs, target, copy, mergeScope)`.

- [ ] **Step 4: Run all merge and list tests, then the full suite**

Run: `cmake --build build --target forge_tests 2>&1 | tail -3 && ./build/Tests/forge_tests "[merge]" | tail -3 && ctest --test-dir build --output-on-failure 2>&1 | tail -4`
Expected: all pass.

- [ ] **Step 5: Commit**

```bash
git add Source/UI Tests
git commit -m "feat(ui): Alt-drag Move/Copy carries a section's events when the scope is All events

Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>"
```

---

### Task 6: Docs, test count and Windows build check

**Files:**
- Modify: `docs/ARCHITECTURE.md` (§9.15 sections/merge: add the program-change reader, `InstrumentEdit`, `splitAtTicks`, the band segments and menu, and `MergeScope`; §9.16 Preferences: the Editing radio), `docs/UI_GUIDE.md` (instrument band: segments and right-click menu; Preferences ▸ Editing), `docs/TESTING.md` (test count + notable files), `CLAUDE.md` (docs-map row for this spec/plan; status line), the spec (Status: implemented; signature deviations from the Global Constraints)

- [ ] **Step 1:** Run `ctest --test-dir build -N | tail -1` for the new total and write it into `docs/TESTING.md` line 3, adding one line each for `ProgramChanges_tests.cpp`, `InstrumentEdit_tests.cpp` and the `[merge][events]` cases.
- [ ] **Step 2:** Update the architecture / UI guide / CLAUDE.md docs-map row as listed above, in the style of the neighbouring entries.
- [ ] **Step 3:** In the spec, set `Status: implemented`, change the `ProgramChange` struct to the `juce::ValueTree event` form and rename `MoveCopyScope`→`MergeScope` / `Source/UI/MergeScope.h`, and add an Implementation notes section listing any deviation found while building.
- [ ] **Step 4:** Cross-build check: `./build-windows.sh forge_tests` (the dev-loop accelerant; `windows-2022` CI remains the release gate). Expected: builds.
- [ ] **Step 5: Commit**

```bash
git add docs CLAUDE.md
git commit -m "docs: instrument changes architecture, UI guide, test counts

Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>"
```

- [ ] **Step 6:** Deploy for manual testing (command in `CLAUDE.md` ▸ Build / test commands) only after the user asks.
