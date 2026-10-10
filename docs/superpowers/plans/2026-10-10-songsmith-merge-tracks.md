# Songsmith: merge a track's notes into another track — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Alt-drag a section's notes from one track's strip onto another track's row to merge them (move, or copy with Ctrl/Cmd held), never leaving a same-pitch note inside another and joining same-pitch notes that overlap.

**Architecture:** A pure document module `NoteMerge` (`Source/UI/NoteMerge.{h,cpp}`, beside `SectionEdit`) does the merge in one undo transaction. `TrackNotePreview` recognises an Alt press and reports press/drag/release (with screen position and modifiers). `TrackListComponent` owns a `MergeGesture` that, like `SectionGesture`, only updates a transient preview in `SectionViewState` while dragging and commits once on release.

**Tech Stack:** C++20, JUCE 8 (`ValueTree`, `UndoManager`), Catch2, CMake/Ninja. No new dependencies.

**Spec:** `docs/superpowers/specs/2026-10-10-songsmith-merge-tracks-design.md`

## Global Constraints

- `Source/Core/` is not touched (editing logic lives in `Source/UI/`).
- One undo transaction per merge; a merge that changes nothing opens no transaction and writes nothing.
- No tree mutation mid-drag; the gesture commits once on mouse-up.
- Same-pitch rule only: different pitches coexist. Touching notes (end == start) join.
- Pre-existing overlaps in the target are never rewritten, except notes an incoming note actually collides with.
- Inserted notes: no `onOrder`/`offOrder`/`sectionId` carried over, `sourceTrackIndex`/`sourceEventIndex` = -1, `channel` = target `defaultChannel` (default 1), `isDrum` = `(channel == 10)`.
- Copy modifier is Ctrl (Cmd on macOS), read live; Alt starts the gesture. Shift is not used.
- Conventional commits, each ending with `Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>`. Never push without explicit approval.
- Do not launch the GUI from agents. New `.cpp` files must be added to BOTH `CMakeLists.txt` and `Tests/CMakeLists.txt`.
- Single test case: `./build/Tests/forge_tests "[tag]"` (ctest `-R` does not match Catch tags).

## Review Focus

Inputs the spec implies but a happy-path test would miss (each is pinned by a test in the owning task):
1. A virtual-section source (track with notes and no stored sections, ref `sectionId == 0`) — Task 2.
2. A target with stored sections and an incoming note that lies outside every section — it must still be inserted and attributed to the nearest section — Task 2.
3. Two carried notes from the same source that collide with each other (stacked duplicate) — Task 3.
4. An incoming note that bridges two existing target notes — they collapse into one — Task 3.
5. A copy where every note is contained, and a release over the source row, the conductor or empty space — nothing changes, `canUndo()` stays false — Tasks 3 and 5.

Known limits (intentional, noted for the reviewer): stray note-on/off pairs kept in `EVENTS` are not carried; the source's sections are left as they are after a move.

---

### Task 1: `notesInSection` read helper

**Files:**
- Modify: `Source/UI/SectionEdit.h`, `Source/UI/SectionEdit.cpp`
- Test: `Tests/SectionEdit_tests.cpp`

**Interfaces:**
- Produces: `std::vector<juce::ValueTree> notesInSection (const juce::ValueTree& track, juce::int64 sectionId);` — the notes whose `sectionIdOfNote` is `sectionId` (a virtual section is id 0). Read-only.

- [ ] **Step 1: Write the failing test** (append to `Tests/SectionEdit_tests.cpp`)

```cpp
TEST_CASE ("sections: notesInSection returns the notes of one section", "[sections]")
{
    SongDocument doc;
    auto t = addTrack (doc);
    addNote (t, 60, 0, 480);
    addNote (t, 62, 480, 480);
    const auto id = (juce::int64) t.getProperty (SongIDs::trackId);
    splitAt (doc, { id }, 480);

    const auto sections = sectionsOf (t);
    REQUIRE (sections.size() == 2);
    const auto first = notesInSection (t, sections[0].id);
    const auto second = notesInSection (t, sections[1].id);
    REQUIRE (first.size() == 1);
    REQUIRE (second.size() == 1);
    CHECK ((int) first[0].getProperty (SongIDs::pitch) == 60);
    CHECK ((int) second[0].getProperty (SongIDs::pitch) == 62);
}

TEST_CASE ("sections: notesInSection on a virtual section (id 0) returns every note", "[sections]")
{
    SongDocument doc;
    auto t = addTrack (doc);
    addNote (t, 60, 0, 480);
    addNote (t, 62, 480, 480);
    CHECK (notesInSection (t, 0).size() == 2);
}
```

- [ ] **Step 2: Run to verify it fails**

Run: `cmake --build build --target forge_tests 2>&1 | tail -5`
Expected: compile error, `notesInSection` not declared.

- [ ] **Step 3: Implement.** In `SectionEdit.h`, after `sectionIdOfNote`:

```cpp
// The track's NOTE nodes belonging to `sectionId` (see sectionIdOfNote). Read-only.
std::vector<juce::ValueTree> notesInSection (const juce::ValueTree& track, juce::int64 sectionId);
```

In `SectionEdit.cpp`, directly after the anonymous namespace that defines `membersOf` closes (outside it, inside `namespace lotro`):

```cpp
std::vector<juce::ValueTree> notesInSection (const juce::ValueTree& track, juce::int64 sectionId)
{
    return membersOf (track, sectionId);
}
```

If `membersOf` sits in a namespace that closes later in the file than its first use, place the new function after the namespace that contains `membersOf`.

- [ ] **Step 4: Run to verify it passes**

Run: `cmake --build build --target forge_tests 2>&1 | tail -2 && ./build/Tests/forge_tests "[sections]" 2>&1 | tail -3`
Expected: all `[sections]` cases pass.

- [ ] **Step 5: Commit**

```bash
git add Source/UI/SectionEdit.h Source/UI/SectionEdit.cpp Tests/SectionEdit_tests.cpp
git commit -m "refactor(ui): expose notesInSection from SectionEdit

Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>"
```

---

### Task 2: `NoteMerge` — validity, insert, move/copy, undo, provenance

**Files:**
- Create: `Source/UI/NoteMerge.h`, `Source/UI/NoteMerge.cpp`, `Tests/NoteMerge_tests.cpp`
- Modify: `CMakeLists.txt` (add `Source/UI/NoteMerge.cpp` after `Source/UI/SectionEdit.cpp`), `Tests/CMakeLists.txt` (add `NoteMerge_tests.cpp` after `SectionEdit_tests.cpp`, and `${CMAKE_SOURCE_DIR}/Source/UI/NoteMerge.cpp` after the `SectionEdit.cpp` source line)

**Interfaces:**
- Consumes: `notesInSection`, `sectionsOf`, `sectionIdOfNote`, `markNoteTimingEdited` (`SectionEdit.h`); `SongDocument::{findTrackById,getNotesNode,addChild,removeChild,getUndoManager}`.
- Produces:
  - `struct MergeResult { int inserted = 0; int dropped = 0; int extended = 0; bool changed = false; };` — each carried note counts in exactly one of inserted/dropped/extended.
  - `bool canMergeInto (const SongDocument& doc, const std::vector<SectionRef>& refs, juce::int64 targetTrackId);` — target is a non-conductor `MIDI_TRACK` and at least one ref names a different non-conductor track.
  - `MergeResult mergeSections (SongDocument& doc, const std::vector<SectionRef>& refs, juce::int64 targetTrackId, bool copy);`

This task implements insertion only (every carried note is inserted as a new note); Task 3 replaces the insertion loop with collision resolution.

- [ ] **Step 1: Register the files** (so the tests build). In `CMakeLists.txt` add `Source/UI/NoteMerge.cpp` on the line after `Source/UI/SectionEdit.cpp`. In `Tests/CMakeLists.txt` add `NoteMerge_tests.cpp` after `SectionEdit_tests.cpp` and `${CMAKE_SOURCE_DIR}/Source/UI/NoteMerge.cpp` after the `SectionEdit.cpp` source entry. Create `Source/UI/NoteMerge.h` with the declarations below and an empty-bodied `NoteMerge.cpp` stub (`#include "UI/NoteMerge.h"` and the function bodies returning `false` / `{}`) so everything compiles.

`Source/UI/NoteMerge.h`:

```cpp
#pragma once

#include "UI/SectionEdit.h"
#include "UI/SongDocument.h"

#include <vector>

// Merging a track's notes into another track (spec:
// docs/superpowers/specs/2026-10-10-songsmith-merge-tracks-design.md).
// Pure document logic: no UI, no Source/Core.
namespace lotro
{

struct MergeResult
{
    int inserted = 0;   // carried notes that became new notes in the target
    int dropped = 0;    // carried notes that lay inside an existing same-pitch note
    int extended = 0;   // carried notes joined with existing same-pitch material
    bool changed = false;
};

// True when `targetTrackId` is a non-conductor MIDI track and at least one ref
// names a different non-conductor track.
bool canMergeInto (const SongDocument& doc, const std::vector<SectionRef>& refs, juce::int64 targetTrackId);

// Merges the notes of the referenced sections into the target track at their own
// ticks. copy=false removes them from their source tracks. One undo transaction;
// a call that would change nothing opens none and writes nothing. Refs on the
// target itself, the conductor and unknown tracks/sections are ignored.
MergeResult mergeSections (SongDocument& doc, const std::vector<SectionRef>& refs,
                           juce::int64 targetTrackId, bool copy);

} // namespace lotro
```

- [ ] **Step 2: Write the failing tests** — `Tests/NoteMerge_tests.cpp`:

```cpp
#include "PlaybackTestSupport.h"
#include "UI/NoteMerge.h"
#include "UI/SectionEdit.h"
#include "UI/SongDocument.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>

using namespace lotro;
using namespace lotro::playbacktest;

namespace
{
    using Note = std::array<int, 3>;   // pitch, start, duration

    std::vector<Note> notesOf (const juce::ValueTree& track)
    {
        std::vector<Note> out;
        const auto notes = SongDocument::getNotesNode (track);
        for (int i = 0; i < notes.getNumChildren(); ++i)
        {
            const auto n = notes.getChild (i);
            out.push_back ({ (int) n.getProperty (SongIDs::pitch), (int) n.getProperty (SongIDs::startTick),
                             (int) n.getProperty (SongIDs::durationTicks) });
        }
        std::sort (out.begin(), out.end());
        return out;
    }

    SectionRef wholeTrack (const juce::ValueTree& t)
    {
        return { (juce::int64) t.getProperty (SongIDs::trackId), 0 };   // 0 = the virtual section
    }

    juce::int64 idOf (const juce::ValueTree& t) { return (juce::int64) t.getProperty (SongIDs::trackId); }

    juce::ValueTree addStoredSection (SongDocument& doc, juce::ValueTree track, int start, int end)
    {
        auto sections = track.getChildWithName (SongIDs::SECTIONS);
        if (! sections.isValid())
        {
            sections = juce::ValueTree (SongIDs::SECTIONS);
            track.addChild (sections, -1, nullptr);
        }
        juce::ValueTree s (SongIDs::SECTION);
        s.setProperty (SongIDs::sectionId, doc.mintSectionId(), nullptr);
        s.setProperty (SongIDs::startTick, start, nullptr);
        s.setProperty (SongIDs::endTick, end, nullptr);
        sections.addChild (s, -1, nullptr);
        return s;
    }
}

TEST_CASE ("merge: a move puts the notes in the target and removes them from the source, in one undo step", "[merge]")
{
    SongDocument doc;
    auto source = addTrack (doc, "S");
    auto target = addTrack (doc, "T");
    addNote (source, 60, 0, 480);
    addNote (source, 62, 480, 480);

    const auto r = mergeSections (doc, { wholeTrack (source) }, idOf (target), false);

    CHECK (r.changed);
    CHECK (r.inserted == 2);
    CHECK (notesOf (target) == std::vector<Note> { { 60, 0, 480 }, { 62, 480, 480 } });
    CHECK (notesOf (source).empty());

    doc.undo();
    CHECK (notesOf (source) == std::vector<Note> { { 60, 0, 480 }, { 62, 480, 480 } });
    CHECK (notesOf (target).empty());
    CHECK_FALSE (doc.canUndo());   // the whole merge was one transaction
}

TEST_CASE ("merge: a copy leaves the source alone", "[merge]")
{
    SongDocument doc;
    auto source = addTrack (doc, "S");
    auto target = addTrack (doc, "T");
    addNote (source, 60, 0, 480);

    const auto r = mergeSections (doc, { wholeTrack (source) }, idOf (target), true);

    CHECK (r.changed);
    CHECK (notesOf (source) == std::vector<Note> { { 60, 0, 480 } });
    CHECK (notesOf (target) == std::vector<Note> { { 60, 0, 480 } });
}

TEST_CASE ("merge: only the referenced section's notes are carried", "[merge]")
{
    SongDocument doc;
    auto source = addTrack (doc, "S");
    auto target = addTrack (doc, "T");
    addNote (source, 60, 0, 480);
    addNote (source, 62, 480, 480);
    splitAt (doc, { idOf (source) }, 480);
    const auto second = sectionsOf (source)[1];

    mergeSections (doc, { { idOf (source), second.id } }, idOf (target), false);

    CHECK (notesOf (target) == std::vector<Note> { { 62, 480, 480 } });
    CHECK (notesOf (source) == std::vector<Note> { { 60, 0, 480 } });
}

TEST_CASE ("merge: invalid requests change nothing and open no transaction", "[merge]")
{
    SongDocument doc;
    auto source = addTrack (doc, "S");
    auto target = addTrack (doc, "T");
    addNote (source, 60, 0, 480);

    CHECK_FALSE (mergeSections (doc, { wholeTrack (source) }, idOf (source), false).changed);   // onto itself
    CHECK_FALSE (mergeSections (doc, { wholeTrack (source) }, idOf (doc.getConductorTrack()), false).changed);
    CHECK_FALSE (mergeSections (doc, { wholeTrack (source) }, 9999, false).changed);            // unknown target
    CHECK_FALSE (mergeSections (doc, { { 9999, 0 } }, idOf (target), false).changed);           // unknown source
    CHECK_FALSE (mergeSections (doc, { { idOf (source), 12345 } }, idOf (target), false).changed);   // unknown section
    CHECK_FALSE (mergeSections (doc, {}, idOf (target), false).changed);
    CHECK_FALSE (doc.canUndo());
    CHECK (notesOf (source).size() == 1);
}

TEST_CASE ("merge: canMergeInto needs a non-conductor target and a different source", "[merge]")
{
    SongDocument doc;
    auto a = addTrack (doc, "A");
    auto b = addTrack (doc, "B");
    addNote (a, 60, 0, 480);

    CHECK (canMergeInto (doc, { wholeTrack (a) }, idOf (b)));
    CHECK_FALSE (canMergeInto (doc, { wholeTrack (a) }, idOf (a)));
    CHECK_FALSE (canMergeInto (doc, { wholeTrack (a) }, idOf (doc.getConductorTrack())));
    CHECK_FALSE (canMergeInto (doc, { wholeTrack (a) }, 9999));
    CHECK (canMergeInto (doc, { wholeTrack (a), wholeTrack (b) }, idOf (b)));   // a still differs from the target
    CHECK_FALSE (canMergeInto (doc, {}, idOf (b)));
}

TEST_CASE ("merge: inserted notes are new material on the target's channel", "[merge][provenance]")
{
    SongDocument doc;
    auto source = addTrack (doc, "S");
    auto target = addTrack (doc, "T");
    target.setProperty (SongIDs::defaultChannel, 10, nullptr);

    juce::ValueTree note (SongIDs::NOTE);
    note.setProperty (SongIDs::pitch, 60, nullptr);
    note.setProperty (SongIDs::startTick, 0, nullptr);
    note.setProperty (SongIDs::durationTicks, 480, nullptr);
    note.setProperty (SongIDs::velocity, 77, nullptr);
    note.setProperty (SongIDs::channel, 1, nullptr);
    note.setProperty (SongIDs::onOrder, 3, nullptr);
    note.setProperty (SongIDs::offOrder, 4, nullptr);
    note.setProperty (SongIDs::sourceTrackIndex, 2, nullptr);
    note.setProperty (SongIDs::sourceEventIndex, 5, nullptr);
    SongDocument::appendChildBulk (SongDocument::getNotesNode (source), note);

    mergeSections (doc, { wholeTrack (source) }, idOf (target), true);

    const auto merged = SongDocument::getNotesNode (target).getChild (0);
    CHECK_FALSE (merged.hasProperty (SongIDs::onOrder));
    CHECK_FALSE (merged.hasProperty (SongIDs::offOrder));
    CHECK ((int) merged.getProperty (SongIDs::sourceTrackIndex) == -1);
    CHECK ((int) merged.getProperty (SongIDs::sourceEventIndex) == -1);
    CHECK ((int) merged.getProperty (SongIDs::channel) == 10);
    CHECK ((bool) merged.getProperty (SongIDs::isDrum));
    CHECK ((int) merged.getProperty (SongIDs::velocity) == 77);   // everything else is kept
}

TEST_CASE ("merge: an inserted note is tagged with the target section that holds it, or the nearest", "[merge][sections]")
{
    SongDocument doc;
    auto source = addTrack (doc, "S");
    auto target = addTrack (doc, "T");
    addNote (target, 72, 0, 100);
    const auto s1 = addStoredSection (doc, target, 0, 1000);
    const auto s2 = addStoredSection (doc, target, 2000, 3000);
    addNote (source, 60, 100, 100);    // inside s1
    addNote (source, 62, 5000, 100);   // beyond every section: nearest is s2

    mergeSections (doc, { wholeTrack (source) }, idOf (target), true);

    const auto notes = SongDocument::getNotesNode (target);
    REQUIRE (notes.getNumChildren() == 3);
    for (int i = 0; i < notes.getNumChildren(); ++i)
    {
        const auto n = notes.getChild (i);
        if ((int) n.getProperty (SongIDs::pitch) == 60)
            CHECK ((juce::int64) n.getProperty (SongIDs::sectionId) == (juce::int64) s1.getProperty (SongIDs::sectionId));
        if ((int) n.getProperty (SongIDs::pitch) == 62)
            CHECK ((juce::int64) n.getProperty (SongIDs::sectionId) == (juce::int64) s2.getProperty (SongIDs::sectionId));
    }
}
```

- [ ] **Step 3: Run to verify they fail**

Run: `cmake --build build --target forge_tests 2>&1 | tail -3 && ./build/Tests/forge_tests "[merge]" 2>&1 | tail -8`
Expected: FAIL (stubs do nothing).

- [ ] **Step 4: Implement** `Source/UI/NoteMerge.cpp`:

```cpp
#include "UI/NoteMerge.h"

#include <algorithm>

namespace lotro
{

namespace
{
    bool isMergeTrack (const juce::ValueTree& track)
    {
        return track.hasType (SongIDs::MIDI_TRACK) && ! (bool) track.getProperty (SongIDs::isConductor, false);
    }

    struct Carried
    {
        juce::ValueTree note;
        juce::ValueTree sourceTrack;
    };

    // The notes of every ref'd section once each (id 0 reads as the track's first
    // section), skipping refs on the target, the conductor and unknown tracks/sections.
    std::vector<Carried> carriedNotes (const SongDocument& doc, const std::vector<SectionRef>& refs,
                                       juce::int64 targetTrackId)
    {
        std::vector<Carried> out;
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
            if (std::none_of (sections.begin(), sections.end(), [id] (const SectionRange& s) { return s.id == id; }))
                continue;
            const SectionRef key { r.trackId, id };
            if (std::find (seen.begin(), seen.end(), key) != seen.end())
                continue;
            seen.push_back (key);
            for (auto note : notesInSection (track, id))
                out.push_back ({ note, track });
        }
        return out;
    }

    int startOf (const juce::ValueTree& n) { return (int) n.getProperty (SongIDs::startTick); }
    int endOf (const juce::ValueTree& n) { return startOf (n) + (int) n.getProperty (SongIDs::durationTicks); }

    // A detached copy of `src` as new material for `target`.
    juce::ValueTree makeNote (const juce::ValueTree& src, int start, int end, const juce::ValueTree& target)
    {
        auto n = src.createCopy();
        for (const auto* id : { &SongIDs::onOrder, &SongIDs::offOrder, &SongIDs::sectionId })
            n.removeProperty (*id, nullptr);
        const int channel = (int) target.getProperty (SongIDs::defaultChannel, 1);
        n.setProperty (SongIDs::startTick, start, nullptr);
        n.setProperty (SongIDs::durationTicks, end - start, nullptr);
        n.setProperty (SongIDs::channel, channel, nullptr);
        n.setProperty (SongIDs::isDrum, channel == 10, nullptr);
        n.setProperty (SongIDs::sourceTrackIndex, -1, nullptr);
        n.setProperty (SongIDs::sourceEventIndex, -1, nullptr);
        n.setProperty (SongIDs::offSynthesized, false, nullptr);

        const auto stored = target.getChildWithName (SongIDs::SECTIONS);
        if (stored.getNumChildren() > 0)
        {
            const auto id = sectionIdOfNote (n, sectionsOf (target));
            if (id != 0)
                n.setProperty (SongIDs::sectionId, id, nullptr);
        }
        return n;
    }
}

bool canMergeInto (const SongDocument& doc, const std::vector<SectionRef>& refs, juce::int64 targetTrackId)
{
    if (! isMergeTrack (doc.findTrackById (targetTrackId)))
        return false;
    return std::any_of (refs.begin(), refs.end(), [&] (const SectionRef& r)
    {
        return r.trackId != targetTrackId && isMergeTrack (doc.findTrackById (r.trackId));
    });
}

MergeResult mergeSections (SongDocument& doc, const std::vector<SectionRef>& refs,
                           juce::int64 targetTrackId, bool copy)
{
    MergeResult result;
    const auto target = doc.findTrackById (targetTrackId);
    if (! isMergeTrack (target))
        return result;

    auto carried = carriedNotes (doc, refs, targetTrackId);
    if (carried.empty())
        return result;
    std::stable_sort (carried.begin(), carried.end(),
                      [] (const Carried& a, const Carried& b) { return startOf (a.note) < startOf (b.note); });

    // Plan on plain data first, so a call that changes nothing never opens a transaction.
    result.inserted = (int) carried.size();
    result.changed = true;

    doc.getUndoManager().beginNewTransaction();
    auto targetNotes = SongDocument::getNotesNode (target);
    for (const auto& c : carried)
        doc.addChild (targetNotes, makeNote (c.note, startOf (c.note), endOf (c.note), target), false);
    if (! copy)
        for (const auto& c : carried)
            doc.removeChild (SongDocument::getNotesNode (c.sourceTrack), c.note, false);
    return result;
}

} // namespace lotro
```

- [ ] **Step 5: Run to verify they pass**

Run: `cmake --build build --target forge_tests 2>&1 | tail -2 && ./build/Tests/forge_tests "[merge]" 2>&1 | tail -3`
Expected: all `[merge]` cases pass.

- [ ] **Step 6: Commit**

```bash
git add CMakeLists.txt Tests/CMakeLists.txt Source/UI/NoteMerge.h Source/UI/NoteMerge.cpp Tests/NoteMerge_tests.cpp
git commit -m "feat(ui): NoteMerge moves or copies section notes into another track

Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>"
```

---

### Task 3: `NoteMerge` — same-pitch collision resolution

**Files:**
- Modify: `Source/UI/NoteMerge.cpp`
- Test: `Tests/NoteMerge_tests.cpp`

**Interfaces:**
- Consumes: Task 2's `mergeSections`, `makeNote`, `carriedNotes`, `startOf`, `endOf`.
- Produces: the same signatures; behaviour now follows the spec's overlap rules and fills `MergeResult::{inserted,dropped,extended}`.

- [ ] **Step 1: Write the failing tests** (append to `Tests/NoteMerge_tests.cpp`)

```cpp
TEST_CASE ("merge: a note inside an existing same-pitch note is dropped", "[merge][overlap]")
{
    SongDocument doc;
    auto source = addTrack (doc, "S");
    auto target = addTrack (doc, "T");
    addNote (target, 60, 0, 960);
    addNote (source, 60, 240, 240);

    const auto r = mergeSections (doc, { wholeTrack (source) }, idOf (target), true);

    CHECK (r.dropped == 1);
    CHECK_FALSE (r.changed);        // a copy that adds nothing
    CHECK_FALSE (doc.canUndo());    // and opens no transaction
    CHECK (notesOf (target) == std::vector<Note> { { 60, 0, 960 } });
}

TEST_CASE ("merge: a move of a contained note still removes it from the source", "[merge][overlap]")
{
    SongDocument doc;
    auto source = addTrack (doc, "S");
    auto target = addTrack (doc, "T");
    addNote (target, 60, 0, 960);
    addNote (source, 60, 240, 240);

    const auto r = mergeSections (doc, { wholeTrack (source) }, idOf (target), false);

    CHECK (r.changed);
    CHECK (notesOf (target) == std::vector<Note> { { 60, 0, 960 } });
    CHECK (notesOf (source).empty());
}

TEST_CASE ("merge: an overlapping same-pitch note extends the existing one", "[merge][overlap]")
{
    SongDocument doc;
    auto source = addTrack (doc, "S");
    auto target = addTrack (doc, "T");
    addNote (target, 60, 0, 480);
    SongDocument::getNotesNode (target).getChild (0).setProperty (SongIDs::onOrder, 5, nullptr);
    addNote (source, 60, 240, 480);

    const auto r = mergeSections (doc, { wholeTrack (source) }, idOf (target), true);

    CHECK (r.extended == 1);
    CHECK (notesOf (target) == std::vector<Note> { { 60, 0, 720 } });
    CHECK_FALSE (SongDocument::getNotesNode (target).getChild (0).hasProperty (SongIDs::onOrder));   // timing edited
}

TEST_CASE ("merge: touching same-pitch notes join", "[merge][overlap]")
{
    SongDocument doc;
    auto source = addTrack (doc, "S");
    auto target = addTrack (doc, "T");
    addNote (target, 60, 0, 480);
    addNote (source, 60, 480, 480);

    mergeSections (doc, { wholeTrack (source) }, idOf (target), true);

    CHECK (notesOf (target) == std::vector<Note> { { 60, 0, 960 } });
}

TEST_CASE ("merge: a note bridging two existing notes collapses them into one", "[merge][overlap]")
{
    SongDocument doc;
    auto source = addTrack (doc, "S");
    auto target = addTrack (doc, "T");
    addNote (target, 60, 0, 480);
    addNote (target, 60, 960, 480);
    addNote (source, 60, 240, 960);

    mergeSections (doc, { wholeTrack (source) }, idOf (target), true);

    CHECK (notesOf (target) == std::vector<Note> { { 60, 0, 1440 } });
    doc.undo();
    CHECK (notesOf (target) == std::vector<Note> { { 60, 0, 480 }, { 60, 960, 480 } });   // one undo step
}

TEST_CASE ("merge: notes of different pitch coexist as a chord", "[merge][overlap]")
{
    SongDocument doc;
    auto source = addTrack (doc, "S");
    auto target = addTrack (doc, "T");
    addNote (target, 60, 0, 480);
    addNote (source, 64, 0, 480);

    mergeSections (doc, { wholeTrack (source) }, idOf (target), true);

    CHECK (notesOf (target) == std::vector<Note> { { 60, 0, 480 }, { 64, 0, 480 } });
}

TEST_CASE ("merge: the earliest-starting note's properties win", "[merge][overlap]")
{
    SongDocument doc;
    auto source = addTrack (doc, "S");
    auto target = addTrack (doc, "T");
    addNote (target, 60, 240, 480, 90);
    addNote (source, 60, 0, 300, 50);   // starts earlier than the target's note

    mergeSections (doc, { wholeTrack (source) }, idOf (target), true);

    REQUIRE (notesOf (target) == std::vector<Note> { { 60, 0, 720 } });
    CHECK ((int) SongDocument::getNotesNode (target).getChild (0).getProperty (SongIDs::velocity) == 50);

    SongDocument doc2;
    auto source2 = addTrack (doc2, "S");
    auto target2 = addTrack (doc2, "T");
    addNote (target2, 60, 0, 480, 90);
    addNote (source2, 60, 240, 480, 50);   // starts later: the existing note keeps its velocity

    mergeSections (doc2, { wholeTrack (source2) }, idOf (target2), true);

    REQUIRE (notesOf (target2) == std::vector<Note> { { 60, 0, 720 } });
    CHECK ((int) SongDocument::getNotesNode (target2).getChild (0).getProperty (SongIDs::velocity) == 90);
}

TEST_CASE ("merge: overlaps that already exist in the target are left alone", "[merge][overlap]")
{
    SongDocument doc;
    auto source = addTrack (doc, "S");
    auto target = addTrack (doc, "T");
    addNote (target, 60, 0, 960);
    addNote (target, 60, 480, 960);   // already overlaps the first
    addNote (source, 62, 0, 480);

    mergeSections (doc, { wholeTrack (source) }, idOf (target), true);

    CHECK (notesOf (target) == std::vector<Note> { { 60, 0, 960 }, { 60, 480, 960 }, { 62, 0, 480 } });
}

TEST_CASE ("merge: carried notes that collide with each other are joined too", "[merge][overlap]")
{
    SongDocument doc;
    auto source = addTrack (doc, "S");
    auto target = addTrack (doc, "T");
    addNote (source, 60, 0, 480);
    addNote (source, 60, 240, 480);   // a stacked duplicate in the same source

    const auto r = mergeSections (doc, { wholeTrack (source) }, idOf (target), false);

    CHECK (notesOf (target) == std::vector<Note> { { 60, 0, 720 } });
    CHECK (r.inserted == 1);
    CHECK (r.extended == 1);
    CHECK (notesOf (source).empty());
}
```

- [ ] **Step 2: Run to verify they fail**

Run: `cmake --build build --target forge_tests 2>&1 | tail -2 && ./build/Tests/forge_tests "[overlap]" 2>&1 | tail -10`
Expected: FAIL (Task 2 inserts everything unconditionally).

- [ ] **Step 3: Implement.** In `NoteMerge.cpp` add `#include <map>` and an item model in the anonymous namespace:

```cpp
    // One span of a given pitch in the target while planning: an existing NOTE
    // (node valid) or a note to be created (node invalid).
    struct Item
    {
        juce::ValueTree node;
        juce::ValueTree source;   // the carried note a new item is built from
        int start = 0;
        int end = 0;
        bool removed = false;
        bool grown = false;       // an existing item whose span changed
    };
```

Replace the body of `mergeSections` from the `// Plan on plain data first` comment to the final `return result;` with:

```cpp
    // Plan on plain data first, so a call that changes nothing never opens a transaction.
    auto targetNotes = SongDocument::getNotesNode (target);
    std::map<int, std::vector<Item>> byPitch;
    for (int i = 0; i < targetNotes.getNumChildren(); ++i)
    {
        const auto n = targetNotes.getChild (i);
        byPitch[(int) n.getProperty (SongIDs::pitch)].push_back ({ n, {}, startOf (n), endOf (n) });
    }

    for (const auto& c : carried)
    {
        const int s = startOf (c.note), e = endOf (c.note);
        auto& items = byPitch[(int) c.note.getProperty (SongIDs::pitch)];

        std::vector<size_t> hits;   // live spans this note overlaps or touches
        for (size_t i = 0; i < items.size(); ++i)
            if (! items[i].removed && items[i].start <= e && s <= items[i].end)
                hits.push_back (i);

        if (hits.empty())
        {
            items.push_back ({ {}, c.note, s, e });
            ++result.inserted;
            continue;
        }

        if (std::any_of (hits.begin(), hits.end(), [&] (size_t i) { return items[i].start <= s && e <= items[i].end; }))
        {
            ++result.dropped;   // already inside one
            continue;
        }

        int unionStart = s, unionEnd = e;
        size_t earliest = hits.front();
        for (const auto i : hits)
        {
            unionStart = std::min (unionStart, items[i].start);
            unionEnd = std::max (unionEnd, items[i].end);
            if (items[i].start < items[earliest].start)
                earliest = i;
        }

        if (s < items[earliest].start)
        {
            // The carried note starts first: its properties win, so it replaces what it joined.
            for (const auto i : hits)
                items[i].removed = true;
            items.push_back ({ {}, c.note, unionStart, unionEnd });
        }
        else
        {
            // The earliest existing span keeps its properties and absorbs the rest.
            for (const auto i : hits)
                if (i != earliest)
                    items[i].removed = true;
            items[earliest].start = unionStart;
            items[earliest].end = unionEnd;
            items[earliest].grown = true;
        }
        ++result.extended;
    }

    result.changed = result.inserted > 0 || result.extended > 0 || (! copy && ! carried.empty());
    if (! result.changed)
        return result;

    doc.getUndoManager().beginNewTransaction();
    for (auto& [pitch, items] : byPitch)
    {
        for (auto& item : items)
        {
            if (item.removed)
            {
                if (item.node.isValid())
                    doc.removeChild (targetNotes, item.node, false);
            }
            else if (! item.node.isValid())
            {
                doc.addChild (targetNotes, makeNote (item.source, item.start, item.end, target), false);
            }
            else if (item.grown)
            {
                doc.setProperty (item.node, SongIDs::startTick, item.start, false);
                doc.setProperty (item.node, SongIDs::durationTicks, item.end - item.start, false);
                markNoteTimingEdited (doc, item.node);
            }
        }
    }
    if (! copy)
        for (const auto& c : carried)
            doc.removeChild (SongDocument::getNotesNode (c.sourceTrack), c.note, false);
    return result;
}
```

(Delete the now-unused `result.inserted = (int) carried.size(); result.changed = true;` lines from Task 2. `markNoteTimingEdited` is declared in `SectionEdit.h`.)

- [ ] **Step 4: Run to verify they pass**

Run: `cmake --build build --target forge_tests 2>&1 | tail -2 && ./build/Tests/forge_tests "[merge]" 2>&1 | tail -3`
Expected: all `[merge]` cases pass, including Task 2's.

- [ ] **Step 5: Commit**

```bash
git add Source/UI/NoteMerge.cpp Tests/NoteMerge_tests.cpp
git commit -m "feat(ui): merge drops contained notes and joins overlapping same-pitch notes

Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>"
```

---

### Task 4: Alt press on the strip reports merge press/drag/release

**Files:**
- Modify: `Source/UI/SectionViewState.h`, `Source/UI/TrackNotePreview.h`, `Source/UI/TrackNotePreview.cpp`, `Source/UI/TrackRowComponent.h`, `Source/UI/TrackRowComponent.cpp`
- Test: `Tests/TrackNotePreview_tests.cpp`

**Interfaces:**
- Produces in `SectionViewState.h`:
  ```cpp
  struct MergeDragPreview
  {
      juce::int64 targetTrackId = -1;     // the row under the pointer; -1 when none
      bool valid = false;                 // that row can take the notes
      bool copy = false;                  // Ctrl/Cmd is down
      juce::Point<int> pointerScreen;     // for the Move/Copy label
      std::vector<SectionRange> ghosts;   // the carried sections, at their own ticks
  };
  // SectionViewState gains: std::optional<MergeDragPreview> merge;
  ```
- Produces on `TrackNotePreview` (and forwarded, with the row's track id prepended where noted, by `TrackRowComponent`):
  ```cpp
  std::function<void (const SectionHit&, const juce::ModifierKeys&)> onMergePressed;          // row: (juce::int64 trackId, const SectionHit&, const juce::ModifierKeys&)
  std::function<bool (juce::Point<int> screenPos, const juce::ModifierKeys&)> onMergeDragged; // true when the row under the pointer can take the notes
  std::function<void (juce::Point<int> screenPos, const juce::ModifierKeys&)> onMergeReleased;
  ```
- Behaviour: a left press with `mods.isAltDown()` on a strip with a section view fires `onMergePressed` (any hit, including `SectionZone::None`) INSTEAD of `onSectionPressed`; a press without Alt is unchanged. Dragging fires `onMergeDragged` only after the pointer is `mergeDragThresholdPixels` (3) from the press; the cursor shows NotAllowed/Copying/DraggingHand from its return value. Release always fires `onMergeReleased` and restores the cursor.

- [ ] **Step 1: Write the failing tests** (append to `Tests/TrackNotePreview_tests.cpp`; reuse its includes and `previewWidth`/`previewHeight`)

```cpp
namespace
{
    juce::MouseEvent previewEvent (juce::Component& c, juce::Point<int> p, juce::ModifierKeys mods)
    {
        const auto pf = p.toFloat();
        return juce::MouseEvent (juce::Desktop::getInstance().getMainMouseSource(), pf, mods,
                                 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, &c, &c,
                                 juce::Time::getCurrentTime(), pf, juce::Time::getCurrentTime(), 1, false);
    }
}

TEST_CASE ("TrackNotePreview: an Alt press starts a merge instead of a section gesture", "[track-note-preview][merge]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SongDocument doc;
    auto track = playbacktest::addTrack (doc);
    playbacktest::addNote (track, 60, 0, 960);
    TimelineViewState viewState;
    viewState.setPixelsPerTick (0.1);
    SectionViewState sectionView;
    TrackNotePreview preview (track, viewState);
    preview.setSectionView (&sectionView, (juce::int64) track.getProperty (SongIDs::trackId));
    preview.setBounds (0, 0, previewWidth, previewHeight);

    int sectionPresses = 0, mergePresses = 0, mergeDrags = 0, mergeReleases = 0;
    preview.onSectionPressed = [&] (const SectionHit&, int, const juce::ModifierKeys&) { ++sectionPresses; };
    preview.onMergePressed = [&] (const SectionHit& h, const juce::ModifierKeys&) { ++mergePresses; CHECK (h.zone == SectionZone::Body); };
    preview.onMergeDragged = [&] (juce::Point<int>, const juce::ModifierKeys&) { ++mergeDrags; return true; };
    preview.onMergeReleased = [&] (juce::Point<int>, const juce::ModifierKeys&) { ++mergeReleases; };

    const auto alt = juce::ModifierKeys (juce::ModifierKeys::leftButtonModifier | juce::ModifierKeys::altModifier);
    const juce::Point<int> p (200, previewHeight / 2);
    preview.mouseDown (previewEvent (preview, p, alt));
    CHECK (mergePresses == 1);
    CHECK (sectionPresses == 0);

    preview.mouseDrag (previewEvent (preview, p + juce::Point<int> (0, 1), alt));   // under the threshold
    CHECK (mergeDrags == 0);
    preview.mouseDrag (previewEvent (preview, p + juce::Point<int> (0, 10), alt));
    CHECK (mergeDrags == 1);

    preview.mouseUp (previewEvent (preview, p + juce::Point<int> (0, 10), alt));
    CHECK (mergeReleases == 1);
}

TEST_CASE ("TrackNotePreview: a press without Alt still starts a section gesture", "[track-note-preview][merge]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SongDocument doc;
    auto track = playbacktest::addTrack (doc);
    playbacktest::addNote (track, 60, 0, 960);
    TimelineViewState viewState;
    viewState.setPixelsPerTick (0.1);
    SectionViewState sectionView;
    TrackNotePreview preview (track, viewState);
    preview.setSectionView (&sectionView, (juce::int64) track.getProperty (SongIDs::trackId));
    preview.setBounds (0, 0, previewWidth, previewHeight);

    int sectionPresses = 0, mergePresses = 0;
    preview.onSectionPressed = [&] (const SectionHit&, int, const juce::ModifierKeys&) { ++sectionPresses; };
    preview.onMergePressed = [&] (const SectionHit&, const juce::ModifierKeys&) { ++mergePresses; };

    preview.mouseDown (previewEvent (preview, { 200, previewHeight / 2 }, juce::ModifierKeys (juce::ModifierKeys::leftButtonModifier)));
    CHECK (sectionPresses == 1);
    CHECK (mergePresses == 0);
}
```

- [ ] **Step 2: Run to verify they fail**

Run: `cmake --build build --target forge_tests 2>&1 | tail -4`
Expected: compile error (`onMergePressed` etc. undeclared).

- [ ] **Step 3: Implement.**

`SectionViewState.h`: add `#include <juce_graphics/juce_graphics.h>` and `<vector>`, define `MergeDragPreview` (above) before `SectionViewState`, and add `std::optional<MergeDragPreview> merge;` to `SectionViewState`.

`TrackNotePreview.h`: after `onSectionReleased`, add the three merge callbacks (signatures above); in the private section add `static constexpr int mergeDragThresholdPixels = 3;`, `bool mergePressActive = false;`, `bool mergeDragStarted = false;`.

`TrackNotePreview.cpp`: replace the section-press block in `mouseDown` with

```cpp
        // Only the left button picks up a section, so another button can never drag one.
        sectionPressActive = false;
        mergePressActive = false;
        if (sectionView != nullptr && e.mods.isLeftButtonDown())
        {
            const auto hit = hitTestSection (sectionsOf (track), tick, viewState.getPixelsPerTick(), 5);
            if (e.mods.isAltDown() && onMergePressed)
            {
                mergePressActive = true;
                mergeDragStarted = false;
                onMergePressed (hit, e.mods);
            }
            else if (onSectionPressed)
            {
                sectionPressActive = true;
                sectionDragStarted = false;
                sectionPressX = e.getPosition().x;
                sectionPressTick = tick;
                onSectionPressed (hit, tick, e.mods);
            }
        }
```

and at the top of `mouseDrag` / `mouseUp`:

```cpp
        if (mergePressActive)
        {
            if (! mergeDragStarted && e.getDistanceFromDragStart() < mergeDragThresholdPixels)
                return;
            mergeDragStarted = true;
            const bool valid = onMergeDragged && onMergeDragged (e.getScreenPosition(), e.mods);
            const bool copy = e.mods.isCtrlDown() || e.mods.isCommandDown();
            setMouseCursor (! valid ? juce::MouseCursor::NotAllowedCursor
                                    : copy ? juce::MouseCursor::CopyingCursor
                                           : juce::MouseCursor::DraggingHandCursor);
            return;
        }
```
```cpp
        if (mergePressActive)
        {
            mergePressActive = false;
            mergeDragStarted = false;
            setMouseCursor (juce::MouseCursor::NormalCursor);
            if (onMergeReleased)
                onMergeReleased (e.getScreenPosition(), e.mods);
            return;
        }
```

`TrackRowComponent.h` / `.cpp`: add `std::function<void (juce::int64, const SectionHit&, const juce::ModifierKeys&)> onMergePressed;`, `std::function<bool (juce::Point<int>, const juce::ModifierKeys&)> onMergeDragged;`, `std::function<void (juce::Point<int>, const juce::ModifierKeys&)> onMergeReleased;` beside the section callbacks, and forward them in the constructor next to the `notePreview.onSection*` assignments:

```cpp
    notePreview.onMergePressed = [this] (const SectionHit& hit, const juce::ModifierKeys& mods)
    {
        if (onMergePressed)
            onMergePressed (getTrackId(), hit, mods);
    };
    notePreview.onMergeDragged = [this] (juce::Point<int> p, const juce::ModifierKeys& mods)
    {
        return onMergeDragged && onMergeDragged (p, mods);
    };
    notePreview.onMergeReleased = [this] (juce::Point<int> p, const juce::ModifierKeys& mods)
    {
        if (onMergeReleased)
            onMergeReleased (p, mods);
    };
```

- [ ] **Step 4: Run to verify they pass**

Run: `cmake --build build --target forge_tests 2>&1 | tail -2 && ./build/Tests/forge_tests "[track-note-preview],[track-row]" 2>&1 | tail -3`
Expected: all pass (existing section-gesture tests included).

- [ ] **Step 5: Commit**

```bash
git add Source/UI/SectionViewState.h Source/UI/TrackNotePreview.* Source/UI/TrackRowComponent.* Tests/TrackNotePreview_tests.cpp
git commit -m "feat(ui): an Alt press on a strip reports a merge gesture

Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>"
```

---

### Task 5: `TrackListComponent` merge gesture — press, preview, commit, cancel

**Files:**
- Modify: `Source/UI/TrackListComponent.h`, `Source/UI/TrackListComponent.cpp`
- Test: `Tests/TrackListComponent_tests.cpp`

**Interfaces:**
- Consumes: Task 4's row callbacks and `SectionViewState::merge`; Task 2's `canMergeInto`, `mergeSections`.
- Produces on `TrackListComponent` (private; `TrackListComponentTestAccess` is a friend):
  ```cpp
  void mergePressed (juce::int64 trackId, const SectionHit& hit, const juce::ModifierKeys& mods);
  bool mergeDragged (juce::Point<int> screenPos, const juce::ModifierKeys& mods);   // true when the row under the pointer can take the notes
  void mergeReleased (juce::Point<int> screenPos, const juce::ModifierKeys& mods);
  juce::int64 rowTrackIdAt (juce::Point<int> screenPos) const;                       // -1 over no row
  ```
  `cancelSectionDrag()` also drops an in-flight merge (so Esc works) and returns true then.

Behaviour: `mergePressed` ignores a press on the conductor or with `SectionZone::None`. Otherwise the carried refs are the whole canvas selection when the pressed section is in it, else just the pressed section (which becomes the selection, mirrored to the heads). A drag sets `sectionView.merge` (target = row under the pointer, `valid = canMergeInto(...)`, `copy` = Ctrl/Cmd now, ghosts = the carried sections' ranges). A release whose preview exists and is valid calls `mergeSections(..., copy)` once, then selects the target's sections; any other release changes nothing. `rebuild()` drops the gesture and preview.

- [ ] **Step 1: Write the failing tests.** In `TrackListComponentTestAccess` (top of `Tests/TrackListComponent_tests.cpp`) add:

```cpp
        static void mergePress (TrackListComponent& c, juce::int64 trackId, SectionHit hit, juce::ModifierKeys mods = {}) { c.mergePressed (trackId, hit, mods); }
        static bool mergeDrag (TrackListComponent& c, juce::Point<int> screen, juce::ModifierKeys mods = {}) { return c.mergeDragged (screen, mods); }
        static void mergeRelease (TrackListComponent& c, juce::Point<int> screen, juce::ModifierKeys mods = {}) { c.mergeReleased (screen, mods); }
```

Append tests (each builds a list of 300 px height with `rebuild`, like the section tests; `screenOf` returns the centre of a track's row in screen coordinates):

```cpp
namespace
{
    juce::Point<int> screenOf (TrackListComponent& list, juce::int64 trackId)
    {
        auto* row = TrackListComponentTestAccess::rowFor (list, trackId);
        REQUIRE (row != nullptr);
        return row->localPointToGlobal (row->getLocalBounds().getCentre());
    }

    struct MergeFixture
    {
        SongDocument doc;
        juce::ValueTree source, target;
        juce::int64 sourceId = 0, targetId = 0;
        std::unique_ptr<TrackListComponent> list;

        MergeFixture()
        {
            source = playbacktest::addTrack (doc, "S");
            target = playbacktest::addTrack (doc, "T");
            playbacktest::addNote (source, 60, 0, 480);
            sourceId = (juce::int64) source.getProperty (SongIDs::trackId);
            targetId = (juce::int64) target.getProperty (SongIDs::trackId);
            list = std::make_unique<TrackListComponent> (doc);
            list->setBounds (0, 0, 600, 300);
            TrackListComponentTestAccess::rebuild (*list);
        }

        int notesIn (const juce::ValueTree& t) const { return SongDocument::getNotesNode (t).getNumChildren(); }
    };

    const juce::ModifierKeys altMods { juce::ModifierKeys::leftButtonModifier | juce::ModifierKeys::altModifier };
    const juce::ModifierKeys altCtrlMods { juce::ModifierKeys::leftButtonModifier | juce::ModifierKeys::altModifier | juce::ModifierKeys::ctrlModifier };
}

TEST_CASE ("TrackListComponent: an Alt-drag onto another row moves the notes on release, in one undo step", "[track-list][merge]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    MergeFixture f;
    using A = TrackListComponentTestAccess;

    A::mergePress (*f.list, f.sourceId, { 0, SectionZone::Body }, altMods);
    CHECK (A::mergeDrag (*f.list, screenOf (*f.list, f.targetId), altMods));
    REQUIRE (A::sectionView (*f.list).merge.has_value());
    CHECK (A::sectionView (*f.list).merge->valid);
    CHECK_FALSE (A::sectionView (*f.list).merge->copy);
    CHECK (f.notesIn (f.target) == 0);   // nothing touched mid-drag

    A::mergeRelease (*f.list, screenOf (*f.list, f.targetId), altMods);
    CHECK (f.notesIn (f.target) == 1);
    CHECK (f.notesIn (f.source) == 0);
    CHECK_FALSE (A::sectionView (*f.list).merge.has_value());

    f.doc.undo();
    CHECK (f.notesIn (f.source) == 1);
    CHECK (f.notesIn (f.target) == 0);
}

TEST_CASE ("TrackListComponent: Ctrl at release makes the merge a copy", "[track-list][merge]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    MergeFixture f;
    using A = TrackListComponentTestAccess;

    A::mergePress (*f.list, f.sourceId, { 0, SectionZone::Body }, altMods);
    A::mergeDrag (*f.list, screenOf (*f.list, f.targetId), altMods);
    CHECK_FALSE (A::sectionView (*f.list).merge->copy);
    A::mergeDrag (*f.list, screenOf (*f.list, f.targetId), altCtrlMods);   // Ctrl pressed mid-drag
    CHECK (A::sectionView (*f.list).merge->copy);
    A::mergeRelease (*f.list, screenOf (*f.list, f.targetId), altCtrlMods);

    CHECK (f.notesIn (f.target) == 1);
    CHECK (f.notesIn (f.source) == 1);
}

TEST_CASE ("TrackListComponent: releasing over the source, the conductor or nothing merges nothing", "[track-list][merge]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    MergeFixture f;
    using A = TrackListComponentTestAccess;
    const auto conductorId = (juce::int64) f.doc.getConductorTrack().getProperty (SongIDs::trackId);

    for (const auto where : { screenOf (*f.list, f.sourceId), screenOf (*f.list, conductorId),
                              f.list->localPointToGlobal (juce::Point<int> (5, 299)) })
    {
        A::mergePress (*f.list, f.sourceId, { 0, SectionZone::Body }, altMods);
        CHECK_FALSE (A::mergeDrag (*f.list, where, altMods));
        A::mergeRelease (*f.list, where, altMods);
    }
    CHECK (f.notesIn (f.target) == 0);
    CHECK (f.notesIn (f.source) == 1);
    CHECK_FALSE (f.doc.canUndo());
}

TEST_CASE ("TrackListComponent: a merge press that never drags, or is cancelled, merges nothing", "[track-list][merge]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    MergeFixture f;
    using A = TrackListComponentTestAccess;

    A::mergePress (*f.list, f.sourceId, { 0, SectionZone::Body }, altMods);
    A::mergeRelease (*f.list, screenOf (*f.list, f.targetId), altMods);   // no drag: a click
    CHECK (f.notesIn (f.target) == 0);

    CHECK_FALSE (f.list->cancelSectionDrag());   // nothing in flight
    A::mergePress (*f.list, f.sourceId, { 0, SectionZone::Body }, altMods);
    A::mergeDrag (*f.list, screenOf (*f.list, f.targetId), altMods);
    CHECK (f.list->cancelSectionDrag());
    CHECK_FALSE (A::sectionView (*f.list).merge.has_value());
    A::mergeDrag (*f.list, screenOf (*f.list, f.targetId), altMods);   // pointer still down after Esc
    A::mergeRelease (*f.list, screenOf (*f.list, f.targetId), altMods);
    CHECK (f.notesIn (f.target) == 0);
    CHECK_FALSE (f.doc.canUndo());
}

TEST_CASE ("TrackListComponent: a merge carries only the pressed section when it is not in the selection", "[track-list][merge]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    MergeFixture f;
    using A = TrackListComponentTestAccess;
    playbacktest::addNote (f.source, 62, 480, 480);
    splitAt (f.doc, { f.sourceId }, 480);
    TrackListComponentTestAccess::rebuild (*f.list);
    const auto sections = sectionsOf (f.source);
    REQUIRE (sections.size() == 2);

    A::mergePress (*f.list, f.sourceId, { sections[0].id, SectionZone::Body }, altMods);
    A::mergeDrag (*f.list, screenOf (*f.list, f.targetId), altMods);
    A::mergeRelease (*f.list, screenOf (*f.list, f.targetId), altMods);
    CHECK (f.notesIn (f.target) == 1);
    CHECK (f.notesIn (f.source) == 1);
}

TEST_CASE ("TrackListComponent: a press on empty strip or the conductor starts no merge", "[track-list][merge]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    MergeFixture f;
    using A = TrackListComponentTestAccess;
    const auto conductorId = (juce::int64) f.doc.getConductorTrack().getProperty (SongIDs::trackId);

    A::mergePress (*f.list, f.sourceId, { 0, SectionZone::None }, altMods);
    CHECK_FALSE (A::mergeDrag (*f.list, screenOf (*f.list, f.targetId), altMods));
    A::mergePress (*f.list, conductorId, { 0, SectionZone::Body }, altMods);
    CHECK_FALSE (A::mergeDrag (*f.list, screenOf (*f.list, f.targetId), altMods));
    CHECK_FALSE (A::sectionView (*f.list).merge.has_value());
}
```

- [ ] **Step 2: Run to verify they fail**

Run: `cmake --build build --target forge_tests 2>&1 | tail -4`
Expected: compile error (`mergePressed` etc. undeclared).

- [ ] **Step 3: Implement.** In `TrackListComponent.h` add `#include "UI/NoteMerge.h"` (with the other UI includes), next to `SectionGesture`:

```cpp
    struct MergeGesture
    {
        std::vector<SectionRef> refs;          // the sections being carried
        std::vector<SectionRange> ghosts;      // their ranges, for the preview
    };
    std::optional<MergeGesture> mergeGesture;
```

declare, beside `sectionReleased`:

```cpp
    // Alt-drag of section(s) onto another row (merge). Like the section gestures, the
    // press only records the gesture, the drag only updates sectionView.merge, and the
    // release commits once through mergeSections; a release with no preview is a click.
    void mergePressed (juce::int64 trackId, const SectionHit& hit, const juce::ModifierKeys& mods);
    bool mergeDragged (juce::Point<int> screenPos, const juce::ModifierKeys& mods);
    void mergeReleased (juce::Point<int> screenPos, const juce::ModifierKeys& mods);
    juce::int64 rowTrackIdAt (juce::Point<int> screenPos) const;
```

and update the `cancelSectionDrag` comment ("Escape during a section or merge drag"). In `TrackListComponent.cpp`:

- In `rebuild()` add `mergeGesture.reset(); sectionView.merge.reset();` beside `gesture.reset(); sectionView.drag.reset();`.
- In the row wiring (after the `onSectionReleased` line):

```cpp
        row->onMergePressed = [this] (juce::int64 trackId, const SectionHit& hit, const juce::ModifierKeys& m) { mergePressed (trackId, hit, m); };
        row->onMergeDragged = [this] (juce::Point<int> p, const juce::ModifierKeys& m) { return mergeDragged (p, m); };
        row->onMergeReleased = [this] (juce::Point<int> p, const juce::ModifierKeys& m) { mergeReleased (p, m); };
```
- Rewrite `cancelSectionDrag`:

```cpp
bool TrackListComponent::cancelSectionDrag()
{
    if (! gesture && ! mergeGesture)
        return false;
    gesture.reset();
    mergeGesture.reset();
    sectionView.drag.reset();
    sectionView.merge.reset();
    content.repaint();
    return true;
}
```
- New functions (after `sectionReleased`):

```cpp
void TrackListComponent::mergePressed (juce::int64 trackId, const SectionHit& hit, const juce::ModifierKeys&)
{
    gesture.reset();
    mergeGesture.reset();
    sectionView.drag.reset();
    sectionView.merge.reset();
    if (hit.zone == SectionZone::None || isConductorTrack (trackId))
        return;

    const SectionRef ref { trackId, hit.sectionId };
    if (sectionView.selected.count (ref) == 0)
    {
        sectionView.selected = { ref };
        canvasAnchor = ref;
        mirrorCanvasToHeads (trackId);
    }

    MergeGesture g;
    g.refs.assign (sectionView.selected.begin(), sectionView.selected.end());
    for (const auto& r : g.refs)
    {
        const auto sections = sectionsOf (doc.findTrackById (r.trackId));
        for (const auto& s : sections)
            if (s.id == (r.sectionId == 0 && ! sections.empty() ? sections.front().id : r.sectionId))
                g.ghosts.push_back (s);
    }
    mergeGesture = std::move (g);
    content.repaint();
}

juce::int64 TrackListComponent::rowTrackIdAt (juce::Point<int> screenPos) const
{
    const auto p = content.getLocalPoint (nullptr, screenPos);
    for (auto* row : content.rows)
        if (row->getBounds().contains (p))
            return row->getTrackId();
    return -1;
}

bool TrackListComponent::mergeDragged (juce::Point<int> screenPos, const juce::ModifierKeys& mods)
{
    if (! mergeGesture)
        return false;
    MergeDragPreview preview;
    preview.targetTrackId = rowTrackIdAt (screenPos);
    preview.valid = preview.targetTrackId >= 0 && canMergeInto (doc, mergeGesture->refs, preview.targetTrackId);
    preview.copy = mods.isCtrlDown() || mods.isCommandDown();
    preview.pointerScreen = screenPos;
    preview.ghosts = mergeGesture->ghosts;
    sectionView.merge = std::move (preview);
    content.repaint();
    return sectionView.merge->valid;
}

void TrackListComponent::mergeReleased (juce::Point<int> screenPos, const juce::ModifierKeys& mods)
{
    if (! mergeGesture)
        return;
    const auto g = *mergeGesture;
    const bool dragged = sectionView.merge.has_value();
    mergeGesture.reset();
    sectionView.merge.reset();
    content.repaint();
    if (! dragged)
        return;   // a click: the press already selected the section

    const auto target = rowTrackIdAt (screenPos);
    if (target < 0 || ! canMergeInto (doc, g.refs, target))
        return;

    const bool copy = mods.isCtrlDown() || mods.isCommandDown();
    if (! mergeSections (doc, g.refs, target, copy).changed)
        return;

    // Select what was merged into, so the result can be moved or merged again.
    sectionView.selected.clear();
    for (const auto& s : sectionsOf (doc.findTrackById (target)))
        sectionView.selected.insert ({ target, s.id });
    canvasAnchor.reset();
    mirrorCanvasToHeads (target);
}
```

- [ ] **Step 4: Run to verify they pass**

Run: `cmake --build build --target forge_tests 2>&1 | tail -2 && ./build/Tests/forge_tests "[track-list]" 2>&1 | tail -3`
Expected: all `[track-list]` cases pass.

- [ ] **Step 5: Commit**

```bash
git add Source/UI/TrackListComponent.* Tests/TrackListComponent_tests.cpp
git commit -m "feat(ui): Alt-drag a section onto another track to merge its notes

Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>"
```

---

### Task 6: Paint the merge preview

**Files:**
- Modify: `Source/UI/SongsmithColours.h`, `Source/UI/TrackNotePreview.h`, `Source/UI/TrackNotePreview.cpp`
- Test: `Tests/TrackNotePreview_tests.cpp`

**Interfaces:**
- Consumes: `SectionViewState::merge` (Task 4), set by Task 5.
- Produces: `SongsmithColours::mergeTarget` (`0xFF7FD2FF`) and `mergeTargetFill` (`0x337FD2FF`); `TrackNotePreview::paintMergePreview (juce::Graphics&) const` called last from `paint`.

Behaviour: on the row whose `trackId == merge->targetTrackId` and `merge->valid`: a 2 px outline in `mergeTarget`, each ghost section filled with `mergeTargetFill` at its own ticks, and a "Move" / "Copy" label drawn at the pointer's x (converted from screen) near the top of the strip.

- [ ] **Step 1: Write the failing test** (append to `Tests/TrackNotePreview_tests.cpp`)

```cpp
TEST_CASE ("TrackNotePreview: the merge target row is outlined and shows ghosts; other rows are not", "[track-note-preview][merge]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SongDocument doc;
    auto track = playbacktest::addTrack (doc);
    const auto id = (juce::int64) track.getProperty (SongIDs::trackId);
    TimelineViewState viewState;
    viewState.setPixelsPerTick (0.1);
    SectionViewState sectionView;
    TrackNotePreview preview (track, viewState);
    preview.setSectionView (&sectionView, id);
    preview.setBounds (0, 0, previewWidth, previewHeight);

    auto render = [&]
    {
        juce::Image image (juce::Image::ARGB, previewWidth, previewHeight, true, juce::SoftwareImageType());
        juce::Graphics g (image);
        preview.paint (g);
        return image;
    };

    const auto plain = render();

    MergeDragPreview m;
    m.targetTrackId = id + 1;   // some other row
    m.valid = true;
    m.ghosts = { { 1, 0, 480 } };
    sectionView.merge = m;
    CHECK (render().getPixelAt (0, previewHeight / 2) == plain.getPixelAt (0, previewHeight / 2));   // not the target: unchanged

    sectionView.merge->targetTrackId = id;
    const auto outlined = render();
    CHECK (outlined.getPixelAt (0, previewHeight / 2) != plain.getPixelAt (0, previewHeight / 2));   // outline at the edge
    CHECK (outlined.getPixelAt (viewState.xForTick (240), previewHeight / 2)
           != plain.getPixelAt (viewState.xForTick (240), previewHeight / 2));                         // ghost fill

    sectionView.merge->valid = false;
    CHECK (render().getPixelAt (0, previewHeight / 2) == plain.getPixelAt (0, previewHeight / 2));    // invalid target: no highlight
}
```

- [ ] **Step 2: Run to verify it fails**

Run: `cmake --build build --target forge_tests 2>&1 | tail -2 && ./build/Tests/forge_tests "[merge]" 2>&1 | tail -6`
Expected: FAIL (nothing painted for the merge).

- [ ] **Step 3: Implement.** `SongsmithColours.h`, next to `sectionSelectedFill`:

```cpp
    constexpr juce::uint32 mergeTarget          = 0xFF7FD2FF;
    constexpr juce::uint32 mergeTargetFill      = 0x337FD2FF;
```

`TrackNotePreview.h`: declare `void paintMergePreview (juce::Graphics& g) const;` beside `paintSections`. `TrackNotePreview.cpp`: call `paintMergePreview (g);` as the last step of `paint`, and define

```cpp
    void TrackNotePreview::paintMergePreview (juce::Graphics& g) const
    {
        if (sectionView == nullptr || ! sectionView->merge)
            return;
        const auto& m = *sectionView->merge;
        if (! m.valid || m.targetTrackId != trackId)
            return;

        const auto bounds = getLocalBounds();
        for (const auto& s : m.ghosts)
        {
            const int x0 = viewState.xForTick (s.startTick);
            const int x1 = std::max (x0 + 1, viewState.xForTick (s.endTick));
            g.setColour (juce::Colour (SongsmithColours::mergeTargetFill));
            g.fillRect (x0, bounds.getY(), x1 - x0, bounds.getHeight());
        }
        g.setColour (juce::Colour (SongsmithColours::mergeTarget));
        g.drawRect (bounds, 2);

        const int labelX = std::clamp (getLocalPoint (nullptr, m.pointerScreen).x + 12, 0, std::max (0, bounds.getWidth() - 48));
        g.setFont (12.0f);
        g.drawText (m.copy ? "Copy" : "Move", labelX, bounds.getY() + 2, 48, 14, juce::Justification::centredLeft);
    }
```

- [ ] **Step 4: Run to verify it passes**

Run: `cmake --build build --target forge_tests 2>&1 | tail -2 && ./build/Tests/forge_tests "[track-note-preview]" 2>&1 | tail -3`
Expected: all pass.

- [ ] **Step 5: Commit**

```bash
git add Source/UI/SongsmithColours.h Source/UI/TrackNotePreview.* Tests/TrackNotePreview_tests.cpp
git commit -m "feat(ui): outline, ghost and label the merge target row while dragging

Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>"
```

---

### Task 7: Docs, Alt-key guard check, full verification

**Files:**
- Modify: `docs/ARCHITECTURE.md` (§9.15), `docs/UI_GUIDE.md`, `docs/TESTING.md`, `docs/superpowers/specs/2026-10-10-songsmith-merge-tracks-design.md`, `CLAUDE.md` (docs-map row)

- [ ] **Step 1: Run the full suite and record the count**

Run: `cmake --build build 2>&1 | tail -2 && ctest --test-dir build --output-on-failure 2>&1 | tail -4`
Expected: all tests pass; note the new total (847 + the tests added above).

- [ ] **Step 2: Update the docs.**
  - `docs/ARCHITECTURE.md` §9.15: add a short "Merging into another track" paragraph — `NoteMerge` (`canMergeInto`, `mergeSections`, the overlap rules, one transaction, no-op writes nothing), the `MergeGesture` (Alt press; drag only updates `SectionViewState::merge`; one commit on release; Ctrl/Cmd read live for copy; Esc via `cancelSectionDrag`; `rebuild()` cancels), and the intentional limits (strays in `EVENTS` are not carried; source sections left as they are; inserted notes outside every target section are attributed to the nearest but do not resize it).
  - `docs/UI_GUIDE.md`: describe Alt-drag onto another row (Move/Copy label, outline and ghosts, Ctrl/Cmd for copy, Esc cancels, not-allowed cursor).
  - `docs/TESTING.md`: update the total and mention `NoteMerge_tests.cpp`.
  - Spec: append an "Implementation notes (2026-10-10)" section recording that the label sits at the pointer's x on the target row rather than floating by the cursor, that the preview threshold is 3 px of distance in any direction, that Alt starts a merge from any non-empty hit including the edge zones, and that `MergeResult` counts each carried note in exactly one of inserted/dropped/extended.
  - `CLAUDE.md`: add the new spec and plan to the docs map (one row, same style as its neighbours).

- [ ] **Step 3: Commit**

```bash
git add docs CLAUDE.md
git commit -m "docs: merge-into-another-track architecture, UI guide and test counts

Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>"
```

- [ ] **Step 4: Build the Windows UI and report.** Run `./build-windows.sh forge_ui 2>&1 | tail -3` and confirm it links with no new warnings. Do NOT deploy or push; tell the user the build is ready, and list the manual checks only they can do: Alt tap on Windows must not steal focus to the menu bar mid-drag (if it does, swallow the Alt-up while a merge gesture is active); Alt-drag move and Ctrl copy feel right; Esc cancels; undo reverses a merge in one step.

---

## Self-review notes

- **Spec coverage:** semantics (Tasks 1-3), gesture and preview (Tasks 4-6), wiring/forwarding (Task 4), undo/no-op (Tasks 2, 3, 5), channel/drum/provenance (Task 2), section attribution (Task 2), Esc cancel (Task 5), docs and manual-check list (Task 7).
- **Type consistency:** `mergeSections`/`canMergeInto`/`MergeResult` (Task 2) are used unchanged in Tasks 3 and 5; `MergeDragPreview` (Task 4) is filled in Task 5 and read in Task 6; the row/preview callback signatures in Task 4 match the `TrackListComponent` handlers in Task 5.
