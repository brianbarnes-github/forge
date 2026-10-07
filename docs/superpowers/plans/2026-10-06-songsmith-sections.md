# Songsmith Sections Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Let the user split each track of the main canvas into sections (`S`), then select, move, resize and delete them, applied to the selected tracks, with destructive (note-cutting) semantics.

**Architecture:** Data model B: flat `NOTES` stay as they are; each `NOTE` gets a `sectionId` and each `MIDI_TRACK` gets a `SECTIONS` child. A pure, JUCE-UI-free module `SectionEdit` reads and mutates sections through `SongDocument` (one undo transaction per call). `TrackListComponent` owns multi-track selection and section selection/drag state and drives edits; `TrackNotePreview` only paints sections and reports pointer gestures. Nothing is mutated mid-drag (a document change rebuilds every row and would destroy the component holding the mouse); the drag is a visual preview committed on mouse-up.

**Tech Stack:** C++17, JUCE (`juce_data_structures`, `juce_gui_basics`), Catch2. No new dependencies.

**Spec:** `docs/superpowers/specs/2026-10-06-songsmith-sections-design.md`.

## Global Constraints

- `Source/Core/**` is untouched (engine/UI boundary). New logic lives in `Source/UI/`.
- Playback, MIDI export, the editor and the ABC pipeline keep reading the flat `NOTES`; they must not need to know about sections.
- Every mutation goes through the document's `UndoManager`, except the non-undoable normalisation (`materialise`) and id minting. One gesture or key press = one undo transaction (`beginNewTransaction()` once, then `newTransaction=false` on every `SongDocument` call). A call that changes nothing must not open a transaction.
- Property names: `sectionId` on `NOTE`; `SECTION{sectionId, startTick, endTick}` (half-open `[startTick, endTick)`, at least 1 tick). Section ids are `juce::int64`, never child indexes, and are minted non-undoably.
- `sourceTrackIndex`/`sourceEventIndex` are copied verbatim, never regenerated: both halves of a cut note keep the original pair (user decision).
- Cutting or shifting a note in time clears `onOrder`, `offOrder` and sets `offSynthesized=false` (the existing `SourceRollEditor::markTimingEdited` rule), so it exports as new material.
- Sections may overlap; membership is by `sectionId`. Moves do not snap; snap is deferred (user decision).
- `S` with the pointer over no section and no marker set does nothing at all (user decision).
- Custom error types, not strings; types on everything; prefer free functions over classes; no new dependencies.
- TDD: write the failing test, see it fail, then implement. Prefer integration tests through `SongDocument`. The GUI is never launched from agents.
- Commits: conventional commits ending with `Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>`. Never push without explicit approval.
- Build/test: `cmake --build build` then `ctest --test-dir build --output-on-failure` (single: `-R <name>`). `Tests/CMakeLists.txt` lists `Source/UI/**` `.cpp` files explicitly: every new `.cpp` goes in BOTH it and the root `CMakeLists.txt`.

## Review Focus

1. Split exactly on a note's start or end tick: no zero-length note, nothing cut, nothing duplicated — Task 3.
2. Drag a section left past tick 0, or several companion sections where one would cross 0: the whole move clamps together — Task 3.
3. Undo after a split that cut notes: one step restores sections, tags and the original single note — Task 3.
4. Save and reopen after edits: sections and tags survive; a file with none still opens — Task 1.
5. Track with no notes, the conductor track, or no selection: nothing changes, no crash, no empty undo step — Tasks 3, 6.
6. A section drag must not mutate the document until mouse-up (rows are rebuilt on any change) — Task 5.
7. Playback and the preview diff after a cut: two attacks, both halves classified consistently — Task 4.

---

### Task 0: Commit the spec and plan

The spec already contains the corrections found while planning (virtual default section and materialising, no primary track, click selects and sets the marker, `S` target rule, `EVENTS` out of scope, provenance decision).

- [ ] **Step 1: Commit**

```bash
git add docs/superpowers/specs/2026-10-06-songsmith-sections-design.md docs/superpowers/plans/2026-10-06-songsmith-sections.md
git commit -m "docs: songsmith sections design and implementation plan

Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>"
```

---

### Task 1: Schema identifiers, section-id minting, file round trip

**Files:**
- Modify: `Source/UI/SongDocument.h`, `Source/UI/SongDocument.cpp`
- Test: `Tests/SongDocument_tests.cpp`, `Tests/SongFileRoundTrip_tests.cpp`

**Interfaces:**
- Produces: `SongIDs::SECTIONS`, `SongIDs::SECTION` (node types), `SongIDs::sectionId` (property; `SECTION` reuses `SongIDs::startTick` and `SongIDs::endTick`), `juce::int64 SongDocument::mintSectionId()` (public, non-undoable, monotonic, starts at 1, survives save/load, and on a file without `nextSectionId` starts above the largest existing section id).

- [ ] **Step 1: Write the failing tests**

Append to `Tests/SongDocument_tests.cpp`:

```cpp
TEST_CASE ("SongDocument: mintSectionId is monotonic, starts at 1 and is not undone", "[song-document][sections]")
{
    SongDocument doc;
    CHECK (doc.mintSectionId() == 1);
    CHECK (doc.mintSectionId() == 2);

    doc.addTrack ("T", 0xFF336699, 1, 1);
    doc.undo();
    CHECK (doc.mintSectionId() == 3);   // undo never rewinds the counter
}

TEST_CASE ("SongDocument: mintSectionId starts above existing ids when the counter is absent", "[song-document][sections]")
{
    SongDocument doc;
    auto track = doc.addTrackBulk ("T", 0xFF336699, 1, 1);
    juce::ValueTree sections (SongIDs::SECTIONS);
    juce::ValueTree section (SongIDs::SECTION);
    section.setProperty (SongIDs::sectionId, (juce::int64) 7, nullptr);
    section.setProperty (SongIDs::startTick, 0, nullptr);
    section.setProperty (SongIDs::endTick, 480, nullptr);
    sections.addChild (section, -1, nullptr);
    track.addChild (sections, -1, nullptr);

    CHECK (doc.mintSectionId() == 8);
}
```

Append to `Tests/SongFileRoundTrip_tests.cpp` (reuse its existing helpers for building a document, writing with `writeSongBytes`, and reading back with `readSongBytes` then `SongDocument::replaceContents`; mirror the nearest existing round-trip test for the exact calls):

```cpp
TEST_CASE ("song file: sections and note tags round-trip, and the counter survives", "[song-file][sections]")
{
    SongDocument doc;
    auto track = doc.addTrackBulk ("T", 0xFF336699, 1, 1);
    juce::ValueTree sections (SongIDs::SECTIONS);
    juce::ValueTree section (SongIDs::SECTION);
    const auto id = doc.mintSectionId();
    section.setProperty (SongIDs::sectionId, id, nullptr);
    section.setProperty (SongIDs::startTick, 0, nullptr);
    section.setProperty (SongIDs::endTick, 960, nullptr);
    sections.addChild (section, -1, nullptr);
    track.addChild (sections, -1, nullptr);
    juce::ValueTree note (SongIDs::NOTE);
    note.setProperty (SongIDs::pitch, 60, nullptr);
    note.setProperty (SongIDs::startTick, 0, nullptr);
    note.setProperty (SongIDs::durationTicks, 480, nullptr);
    note.setProperty (SongIDs::sectionId, id, nullptr);
    SongDocument::appendChildBulk (SongDocument::getNotesNode (track), note);

    SongDocument reloaded;
    reloaded.replaceContents (readSongBytes (writeSongBytes (doc.getTree())));

    const auto loadedTrack = reloaded.getTrack (1);
    const auto loadedSection = loadedTrack.getChildWithName (SongIDs::SECTIONS).getChild (0);
    CHECK ((juce::int64) loadedSection.getProperty (SongIDs::sectionId) == id);
    CHECK ((int) loadedSection.getProperty (SongIDs::endTick) == 960);
    CHECK ((juce::int64) SongDocument::getNotesNode (loadedTrack).getChild (0).getProperty (SongIDs::sectionId) == id);
    CHECK (reloaded.mintSectionId() == id + 1);
}

TEST_CASE ("song file: a song saved before sections existed still loads", "[song-file][sections]")
{
    SongDocument doc;   // no SECTIONS anywhere, no nextSectionId property
    doc.getTree().removeProperty (juce::Identifier ("nextSectionId"), nullptr);

    SongDocument reloaded;
    REQUIRE_NOTHROW (reloaded.replaceContents (readSongBytes (writeSongBytes (doc.getTree()))));
}
```

- [ ] **Step 2: Run to verify failure**

Run: `cmake --build build 2>&1 | grep -E "error" | head`
Expected: compile errors (`SECTIONS`, `SECTION`, `sectionId`, `mintSectionId` undeclared).

- [ ] **Step 3: Implement**

`Source/UI/SongDocument.h`: in `SongIDs` add after `EVENT`:

```cpp
    extern const juce::Identifier SECTIONS;  // MIDI_TRACK child holding SECTION nodes
    extern const juce::Identifier SECTION;   // one section: sectionId + startTick/endTick (half-open)
```

and after `offOrder`'s group (NOTE properties):

```cpp
    extern const juce::Identifier sectionId;        // NOTE: owning SECTION.sectionId; SECTION: its own id
```

(`SECTION` reuses `startTick` and `endTick`.) In `SongDocument`, public section, after `mintImportBatch`:

```cpp
    // Mints the next section id (starts at 1; above every existing id when a
    // file predates sections). Non-undoable, like mintTrackId.
    juce::int64 mintSectionId();
```

`Source/UI/SongDocument.cpp`: define the identifiers beside their neighbours:

```cpp
    const juce::Identifier SECTIONS ("SECTIONS");
    const juce::Identifier SECTION ("SECTION");
    const juce::Identifier sectionId ("sectionId");
```

add `static const juce::Identifier nextSectionId ("nextSectionId");` beside the other counters, and:

```cpp
juce::int64 SongDocument::mintSectionId()
{
    auto next = (juce::int64) tree.getProperty (SongIDs::nextSectionId, 0);
    if (next < 1)
    {
        next = 1;
        for (auto track : getSourceMidiNode())
            for (auto section : track.getChildWithName (SongIDs::SECTIONS))
                next = std::max (next, (juce::int64) section.getProperty (SongIDs::sectionId, 0) + 1);
    }
    tree.setProperty (SongIDs::nextSectionId, next + 1, nullptr);
    return next;
}
```

Do NOT add `nextSectionId` to the required counters in `validateLoaded` (old files lack it).

- [ ] **Step 4: Run to verify pass**

Run: `cmake --build build && ctest --test-dir build -R "sections" --output-on-failure`
Expected: the four new tests PASS. If `replaceContents` drops the unknown `nextSectionId` property, fix it so every SONG property is kept, then rerun.

- [ ] **Step 5: Commit**

```bash
git add Source/UI/SongDocument.h Source/UI/SongDocument.cpp Tests/SongDocument_tests.cpp Tests/SongFileRoundTrip_tests.cpp
git commit -m "feat(ui): section ids in the song schema

Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>"
```

---

### Task 2: Read model (sections of a track, hit testing, companions)

**Files:**
- Create: `Source/UI/SectionEdit.h`, `Source/UI/SectionEdit.cpp`
- Modify: `CMakeLists.txt`, `Tests/CMakeLists.txt` (add `Source/UI/SectionEdit.cpp` to the UI source lists, like `GridLines.cpp`; add `SectionEdit_tests.cpp` to the test list)
- Test: `Tests/SectionEdit_tests.cpp`

**Interfaces (all in namespace `lotro`):**
- Produces:
```cpp
struct SectionRange { juce::int64 id = 0; int startTick = 0; int endTick = 0; };
struct SectionRef   { juce::int64 trackId = 0; juce::int64 sectionId = 0;
                      bool operator< (const SectionRef&) const; bool operator== (const SectionRef&) const; };
enum class SectionZone { None, Body, LeftEdge, RightEdge };
struct SectionHit   { juce::int64 sectionId = 0; SectionZone zone = SectionZone::None; };
enum class SectionEdge { Left, Right };

std::vector<SectionRange> sectionsOf (const juce::ValueTree& track);
juce::int64 sectionIdOfNote (const juce::ValueTree& note, const std::vector<SectionRange>& sections);
SectionHit hitTestSection (const std::vector<SectionRange>& sections, int tick, double pixelsPerTick, int edgeSlopPixels);
std::vector<SectionRef> withCompanions (const SongDocument& doc, const std::set<juce::int64>& selectedTrackIds, SectionRef clicked);
void markNoteTimingEdited (SongDocument& doc, juce::ValueTree note);   // moved from SourceRollEditor::markTimingEdited
```

- [ ] **Step 1: Write the failing tests** (`Tests/SectionEdit_tests.cpp`)

```cpp
#include "PlaybackTestSupport.h"
#include "UI/SectionEdit.h"
#include "UI/SongDocument.h"

#include <catch2/catch_test_macros.hpp>

using namespace lotro;
using namespace lotro::playbacktest;

namespace
{
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

TEST_CASE ("sections: a track with notes and no stored sections reads as one virtual section", "[sections]")
{
    SongDocument doc;
    auto t = addTrack (doc);
    addNote (t, 60, 480, 480);
    addNote (t, 62, 960, 960);

    const auto sections = sectionsOf (t);
    REQUIRE (sections.size() == 1);
    CHECK (sections[0].id == 0);
    CHECK (sections[0].startTick == 0);
    CHECK (sections[0].endTick == 1920);
}

TEST_CASE ("sections: an empty track and the conductor have no sections", "[sections]")
{
    SongDocument doc;
    CHECK (sectionsOf (addTrack (doc)).empty());
    CHECK (sectionsOf (doc.getConductorTrack()).empty());
}

TEST_CASE ("sections: stored sections are returned in stored order and ignore the virtual rule", "[sections]")
{
    SongDocument doc;
    auto t = addTrack (doc);
    addNote (t, 60, 0, 480);
    auto s1 = addStoredSection (doc, t, 0, 480);
    auto s2 = addStoredSection (doc, t, 480, 960);

    const auto sections = sectionsOf (t);
    REQUIRE (sections.size() == 2);
    CHECK (sections[0].id == (juce::int64) s1.getProperty (SongIDs::sectionId));
    CHECK (sections[1].endTick == 960);
    (void) s2;
}

TEST_CASE ("sections: a note's section is its tag, else the first section containing its start, else the first", "[sections]")
{
    const std::vector<SectionRange> sections { { 5, 0, 480 }, { 6, 480, 960 } };

    auto note = [] (int start, juce::int64 tag)
    {
        juce::ValueTree n (SongIDs::NOTE);
        n.setProperty (SongIDs::startTick, start, nullptr);
        if (tag != 0)
            n.setProperty (SongIDs::sectionId, tag, nullptr);
        return n;
    };

    CHECK (sectionIdOfNote (note (0, 6), sections) == 6);     // tag wins over position
    CHECK (sectionIdOfNote (note (500, 0), sections) == 6);   // untagged: by position
    CHECK (sectionIdOfNote (note (500, 99), sections) == 6);  // dangling tag: by position
    CHECK (sectionIdOfNote (note (5000, 0), sections) == 5);  // outside everything: the first
    CHECK (sectionIdOfNote (note (0, 0), {}) == 0);
}

TEST_CASE ("sections: hit testing finds edges within the slop, then the narrowest body", "[sections]")
{
    const std::vector<SectionRange> sections { { 1, 0, 1000 }, { 2, 400, 600 } };
    constexpr double ppt = 0.1;   // 5 px of slop = 50 ticks

    CHECK (hitTestSection (sections, 20, ppt, 5).zone == SectionZone::LeftEdge);
    CHECK (hitTestSection (sections, 20, ppt, 5).sectionId == 1);
    CHECK (hitTestSection (sections, 980, ppt, 5).zone == SectionZone::RightEdge);
    CHECK (hitTestSection (sections, 500, ppt, 5).sectionId == 2);   // narrowest body
    CHECK (hitTestSection (sections, 500, ppt, 5).zone == SectionZone::Body);
    CHECK (hitTestSection (sections, 200, ppt, 5).sectionId == 1);
    CHECK (hitTestSection (sections, 1500, ppt, 5).zone == SectionZone::None);
    CHECK (hitTestSection ({}, 10, ppt, 5).zone == SectionZone::None);
}

TEST_CASE ("sections: companions are the sections starting at the same tick on the other selected tracks", "[sections]")
{
    SongDocument doc;
    auto a = addTrack (doc, "A");
    auto b = addTrack (doc, "B");
    auto c = addTrack (doc, "C");
    for (auto t : { a, b, c })
        addNote (t, 60, 0, 960);
    const auto sa = addStoredSection (doc, a, 0, 480);
    const auto sb = addStoredSection (doc, b, 0, 960);
    addStoredSection (doc, c, 480, 960);

    const auto idA = (juce::int64) a.getProperty (SongIDs::trackId);
    const auto idB = (juce::int64) b.getProperty (SongIDs::trackId);
    const auto idC = (juce::int64) c.getProperty (SongIDs::trackId);
    const SectionRef clicked { idA, (juce::int64) sa.getProperty (SongIDs::sectionId) };

    const auto both = withCompanions (doc, { idA, idB, idC }, clicked);
    REQUIRE (both.size() == 2);   // C's section starts at 480, not 0
    CHECK (both[0] == clicked);
    CHECK (both[1].trackId == idB);
    CHECK (both[1].sectionId == (juce::int64) sb.getProperty (SongIDs::sectionId));

    // A clicked track outside the selection acts alone.
    CHECK (withCompanions (doc, { idB, idC }, clicked).size() == 1);
}
```

- [ ] **Step 2: Run to verify failure**

Run: `cmake --build build 2>&1 | grep error | head -3`
Expected: `UI/SectionEdit.h: No such file`.

- [ ] **Step 3: Implement**

`Source/UI/SectionEdit.h`:

```cpp
#pragma once

#include "UI/SongDocument.h"

#include <juce_data_structures/juce_data_structures.h>

#include <set>
#include <vector>

// Sections of a track (spec: docs/superpowers/specs/2026-10-06-songsmith-sections-design.md).
// Pure document logic: no UI, no Source/Core.
namespace lotro
{

struct SectionRange
{
    juce::int64 id = 0;   // 0 = the virtual default section of a track that has none stored
    int startTick = 0;    // half-open [startTick, endTick)
    int endTick = 0;
};

struct SectionRef
{
    juce::int64 trackId = 0;
    juce::int64 sectionId = 0;
    bool operator< (const SectionRef& o) const { return trackId != o.trackId ? trackId < o.trackId : sectionId < o.sectionId; }
    bool operator== (const SectionRef& o) const { return trackId == o.trackId && sectionId == o.sectionId; }
};

enum class SectionZone { None, Body, LeftEdge, RightEdge };

struct SectionHit
{
    juce::int64 sectionId = 0;
    SectionZone zone = SectionZone::None;
};

enum class SectionEdge { Left, Right };

// Stored sections in stored order. A non-conductor track with notes but none
// stored reads as one virtual section {0, [0, last note end)}; a track with no
// notes (and the conductor) has none. Never mutates.
std::vector<SectionRange> sectionsOf (const juce::ValueTree& track);

// The note's section: its tag if that names a section, else the first section
// containing its startTick, else the first section, else 0.
juce::int64 sectionIdOfNote (const juce::ValueTree& note, const std::vector<SectionRange>& sections);

// The section under `tick`: an edge when within edgeSlopPixels of a section's
// start or end (nearest wins), else the narrowest section containing the tick.
SectionHit hitTestSection (const std::vector<SectionRange>& sections, int tick,
                           double pixelsPerTick, int edgeSlopPixels);

// `clicked` plus, when its track is in `selectedTrackIds`, the section starting
// at the same tick on every other selected track. `clicked` is first.
std::vector<SectionRef> withCompanions (const SongDocument& doc, const std::set<juce::int64>& selectedTrackIds,
                                        SectionRef clicked);

// Clears the raw-MIDI ordering of a note whose timing changed, so it exports as
// new material with a real note-off. Joins the caller's open transaction.
void markNoteTimingEdited (SongDocument& doc, juce::ValueTree note);

} // namespace lotro
```

`Source/UI/SectionEdit.cpp`:

```cpp
#include "UI/SectionEdit.h"

#include <algorithm>
#include <cmath>

namespace lotro
{

namespace
{
    std::vector<SectionRange> storedSections (const juce::ValueTree& track)
    {
        std::vector<SectionRange> out;
        const auto node = track.getChildWithName (SongIDs::SECTIONS);
        for (int i = 0; i < node.getNumChildren(); ++i)
        {
            const auto s = node.getChild (i);
            out.push_back ({ (juce::int64) s.getProperty (SongIDs::sectionId, 0),
                             (int) s.getProperty (SongIDs::startTick, 0),
                             (int) s.getProperty (SongIDs::endTick, 0) });
        }
        return out;
    }
}

std::vector<SectionRange> sectionsOf (const juce::ValueTree& track)
{
    if (! track.isValid() || (bool) track.getProperty (SongIDs::isConductor, false))
        return {};

    auto stored = storedSections (track);
    if (! stored.empty())
        return stored;

    const auto notes = SongDocument::getNotesNode (track);
    if (notes.getNumChildren() == 0)
        return {};

    int lastEnd = 1;
    for (int i = 0; i < notes.getNumChildren(); ++i)
    {
        const auto n = notes.getChild (i);
        lastEnd = std::max (lastEnd, (int) n.getProperty (SongIDs::startTick) + (int) n.getProperty (SongIDs::durationTicks));
    }
    return { { 0, 0, lastEnd } };
}

juce::int64 sectionIdOfNote (const juce::ValueTree& note, const std::vector<SectionRange>& sections)
{
    if (sections.empty())
        return 0;

    const auto tag = (juce::int64) note.getProperty (SongIDs::sectionId, 0);
    if (std::any_of (sections.begin(), sections.end(), [tag] (const SectionRange& s) { return tag != 0 && s.id == tag; }))
        return tag;

    const int start = (int) note.getProperty (SongIDs::startTick, 0);
    for (const auto& s : sections)
        if (s.startTick <= start && start < s.endTick)
            return s.id;
    return sections.front().id;
}

SectionHit hitTestSection (const std::vector<SectionRange>& sections, int tick, double pixelsPerTick, int edgeSlopPixels)
{
    SectionHit best;
    double bestDistance = 1.0e300;

    for (const auto& s : sections)
    {
        const double toStart = std::abs ((double) (tick - s.startTick)) * pixelsPerTick;
        const double toEnd = std::abs ((double) (tick - s.endTick)) * pixelsPerTick;
        if (toStart <= edgeSlopPixels && toStart < bestDistance && tick <= s.endTick)
        {
            best = { s.id, SectionZone::LeftEdge };
            bestDistance = toStart;
        }
        if (toEnd <= edgeSlopPixels && toEnd < bestDistance && tick >= s.startTick)
        {
            best = { s.id, SectionZone::RightEdge };
            bestDistance = toEnd;
        }
    }
    if (best.zone != SectionZone::None)
        return best;

    int narrowest = std::numeric_limits<int>::max();
    for (const auto& s : sections)
        if (s.startTick <= tick && tick < s.endTick && s.endTick - s.startTick < narrowest)
        {
            narrowest = s.endTick - s.startTick;
            best = { s.id, SectionZone::Body };
        }
    return best;
}

std::vector<SectionRef> withCompanions (const SongDocument& doc, const std::set<juce::int64>& selectedTrackIds,
                                        SectionRef clicked)
{
    std::vector<SectionRef> out { clicked };
    if (selectedTrackIds.count (clicked.trackId) == 0)
        return out;

    const auto clickedTrack = doc.findTrackById (clicked.trackId);
    int clickedStart = -1;
    for (const auto& s : sectionsOf (clickedTrack))
        if (s.id == clicked.sectionId)
            clickedStart = s.startTick;
    if (clickedStart < 0)
        return out;

    for (const auto trackId : selectedTrackIds)
    {
        if (trackId == clicked.trackId)
            continue;
        for (const auto& s : sectionsOf (doc.findTrackById (trackId)))
            if (s.startTick == clickedStart)
                out.push_back ({ trackId, s.id });
    }
    return out;
}

void markNoteTimingEdited (SongDocument& doc, juce::ValueTree note)
{
    if (note.hasProperty (SongIDs::onOrder))
        doc.removeProperty (note, SongIDs::onOrder, false);
    if (note.hasProperty (SongIDs::offOrder))
        doc.removeProperty (note, SongIDs::offOrder, false);
    if ((bool) note.getProperty (SongIDs::offSynthesized, false))
        doc.setProperty (note, SongIDs::offSynthesized, false, false);
}

} // namespace lotro
```

(Add `#include <limits>`.) Make `SourceRollEditor::markTimingEdited` delegate: replace its body with `markNoteTimingEdited (doc, note);` and add `#include "SectionEdit.h"` to `SourceRollEditor.cpp`.

- [ ] **Step 4: Run to verify pass**

Run: `cmake --build build && ctest --test-dir build -R "sections|SourceRollEditor" --output-on-failure`
Expected: all PASS (the existing `SourceRollEditor` tests still pass through the delegate).

- [ ] **Step 5: Commit**

```bash
git add Source/UI/SectionEdit.* Source/UI/SourceRollEditor.cpp CMakeLists.txt Tests/CMakeLists.txt Tests/SectionEdit_tests.cpp
git commit -m "feat(ui): read model for track sections

Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>"
```

---

### Task 3: Section mutations (split, move, resize, delete)

**Files:**
- Modify: `Source/UI/SectionEdit.h`, `Source/UI/SectionEdit.cpp`
- Test: `Tests/SectionEdit_tests.cpp`

**Interfaces:**
- Consumes: Task 2's read model.
- Produces:
```cpp
void splitAt        (SongDocument&, const std::vector<juce::int64>& trackIds, int tick);
void moveSections   (SongDocument&, const std::vector<SectionRef>&, int deltaTicks);
void resizeSections (SongDocument&, const std::vector<SectionRef>&, SectionEdge edge, int tick);
void deleteSections (SongDocument&, const std::vector<SectionRef>&);
```
All materialise the touched tracks first (non-undoable), resolve a `sectionId` of 0 to the track's first section, change nothing and open no transaction when there is nothing to do, and otherwise run in exactly one undo transaction.

- [ ] **Step 1: Write the failing tests** (append to `Tests/SectionEdit_tests.cpp`)

```cpp
namespace
{
    struct N { int pitch; int start; int dur; juce::int64 section; };

    std::vector<N> notesOf (const juce::ValueTree& track)
    {
        std::vector<N> out;
        const auto notes = SongDocument::getNotesNode (track);
        for (int i = 0; i < notes.getNumChildren(); ++i)
        {
            const auto n = notes.getChild (i);
            out.push_back ({ (int) n.getProperty (SongIDs::pitch), (int) n.getProperty (SongIDs::startTick),
                             (int) n.getProperty (SongIDs::durationTicks), (juce::int64) n.getProperty (SongIDs::sectionId, 0) });
        }
        std::sort (out.begin(), out.end(), [] (const N& a, const N& b) { return a.start < b.start; });
        return out;
    }

    juce::int64 idOf (const juce::ValueTree& t) { return (juce::int64) t.getProperty (SongIDs::trackId); }
}

TEST_CASE ("splitAt: divides a section and moves the later notes to the new one", "[sections][split]")
{
    SongDocument doc;
    auto t = addTrack (doc);
    addNote (t, 60, 0, 480);
    addNote (t, 62, 960, 480);

    splitAt (doc, { idOf (t) }, 720);

    const auto sections = sectionsOf (t);
    REQUIRE (sections.size() == 2);
    CHECK (sections[0].startTick == 0);
    CHECK (sections[0].endTick == 720);
    CHECK (sections[1].startTick == 720);
    CHECK (sections[1].endTick == 1440);
    CHECK (sections[0].id != 0);

    const auto notes = notesOf (t);
    REQUIRE (notes.size() == 2);
    CHECK (notes[0].section == sections[0].id);
    CHECK (notes[1].section == sections[1].id);
}

TEST_CASE ("splitAt: a note straddling the tick is cut in two with the same provenance", "[sections][split]")
{
    SongDocument doc;
    auto t = addTrack (doc);
    addNote (t, 60, 0, 960, 90, 3);
    auto note = SongDocument::getNotesNode (t).getChild (0);
    note.setProperty (SongIDs::sourceTrackIndex, 2, nullptr);
    note.setProperty (SongIDs::sourceEventIndex, 7, nullptr);
    note.setProperty (SongIDs::onOrder, 4, nullptr);
    note.setProperty (SongIDs::offOrder, 5, nullptr);

    splitAt (doc, { idOf (t) }, 400);

    const auto notes = notesOf (t);
    REQUIRE (notes.size() == 2);
    CHECK (notes[0].start == 0);
    CHECK (notes[0].dur == 400);
    CHECK (notes[1].start == 400);
    CHECK (notes[1].dur == 560);
    CHECK (notes[0].pitch == 60);
    CHECK (notes[1].pitch == 60);
    CHECK (notes[0].section != notes[1].section);

    const auto notesNode = SongDocument::getNotesNode (t);
    for (int i = 0; i < 2; ++i)
    {
        const auto n = notesNode.getChild (i);
        CHECK ((int) n.getProperty (SongIDs::velocity) == 90);
        CHECK ((int) n.getProperty (SongIDs::channel) == 3);
        CHECK ((int) n.getProperty (SongIDs::sourceTrackIndex) == 2);   // both halves keep the original pair
        CHECK ((int) n.getProperty (SongIDs::sourceEventIndex) == 7);
        CHECK_FALSE (n.hasProperty (SongIDs::onOrder));
        CHECK_FALSE (n.hasProperty (SongIDs::offOrder));
    }
}

TEST_CASE ("splitAt: a tick on a note's start or end cuts nothing and makes no zero-length note", "[sections][split]")
{
    SongDocument doc;
    auto t = addTrack (doc);
    addNote (t, 60, 0, 480);
    addNote (t, 62, 480, 480);

    splitAt (doc, { idOf (t) }, 480);

    const auto notes = notesOf (t);
    REQUIRE (notes.size() == 2);
    CHECK (notes[0].dur == 480);
    CHECK (notes[1].dur == 480);
    CHECK (notes[0].section != notes[1].section);
}

TEST_CASE ("splitAt: on an edge, outside every section, on an empty track or with no tracks does nothing", "[sections][split]")
{
    SongDocument doc;
    auto t = addTrack (doc);
    auto empty = addTrack (doc, "E");
    addNote (t, 60, 0, 960);

    splitAt (doc, { idOf (t) }, 0);
    splitAt (doc, { idOf (t) }, 960);
    splitAt (doc, { idOf (t) }, 5000);
    splitAt (doc, { idOf (empty) }, 100);
    splitAt (doc, {}, 100);

    CHECK (sectionsOf (t).size() == 1);
    CHECK (sectionsOf (empty).empty());
    CHECK_FALSE (doc.canUndo());
}

TEST_CASE ("splitAt: applies to every named track in one undo step", "[sections][split]")
{
    SongDocument doc;
    auto a = addTrack (doc, "A");
    auto b = addTrack (doc, "B");
    addNote (a, 60, 0, 960);
    addNote (b, 64, 0, 960);

    splitAt (doc, { idOf (a), idOf (b) }, 480);
    CHECK (sectionsOf (a).size() == 2);
    CHECK (sectionsOf (b).size() == 2);

    doc.undo();
    CHECK (notesOf (a).size() == 1);     // the cut note is whole again
    CHECK (notesOf (b).size() == 1);
    CHECK (notesOf (a)[0].dur == 960);
    CHECK (sectionsOf (a).size() == 1);
    CHECK (sectionsOf (b).size() == 1);
    CHECK_FALSE (doc.canUndo());          // exactly one transaction was recorded
}

TEST_CASE ("splitAt: overlapping sections are both split by a tick inside both", "[sections][split]")
{
    SongDocument doc;
    auto t = addTrack (doc);
    addNote (t, 60, 0, 480);
    splitAt (doc, { idOf (t) }, 240);            // [0,240) [240,480)
    const auto first = sectionsOf (t);
    moveSections (doc, { { idOf (t), first[1].id } }, -120);   // second now [120,360): overlaps the first

    splitAt (doc, { idOf (t) }, 200);            // 200 is inside both [0,240) and [120,360)
    CHECK (sectionsOf (t).size() == 4);
}

TEST_CASE ("moveSections: shifts the range and its notes, leaving other sections alone", "[sections][move]")
{
    SongDocument doc;
    auto t = addTrack (doc);
    addNote (t, 60, 0, 480);
    addNote (t, 62, 960, 480);
    splitAt (doc, { idOf (t) }, 720);
    const auto sections = sectionsOf (t);

    moveSections (doc, { { idOf (t), sections[1].id } }, 480);

    const auto moved = sectionsOf (t);
    CHECK (moved[0].startTick == 0);
    CHECK (moved[1].startTick == 1200);
    CHECK (moved[1].endTick == 1920);
    const auto notes = notesOf (t);
    CHECK (notes[0].start == 0);
    CHECK (notes[1].start == 1440);
}

TEST_CASE ("moveSections: overlap keeps every note and the sections drag apart again", "[sections][move]")
{
    SongDocument doc;
    auto t = addTrack (doc);
    addNote (t, 60, 0, 480);
    addNote (t, 62, 960, 480);
    splitAt (doc, { idOf (t) }, 720);
    const auto s = sectionsOf (t);

    moveSections (doc, { { idOf (t), s[1].id } }, -720);   // onto the first section
    CHECK (notesOf (t).size() == 2);
    CHECK (notesOf (t)[1].start == 240);

    moveSections (doc, { { idOf (t), s[1].id } }, 720);     // and back out
    CHECK (notesOf (t)[1].start == 960);
}

TEST_CASE ("moveSections: clamps so no section starts before tick 0, moving companions together", "[sections][move]")
{
    SongDocument doc;
    auto a = addTrack (doc, "A");
    auto b = addTrack (doc, "B");
    addNote (a, 60, 100, 200);
    addNote (b, 64, 400, 200);
    splitAt (doc, { idOf (a), idOf (b) }, 50);   // each track: [0,50) holds no notes, the second section starts at 50
    const auto sa = sectionsOf (a);
    const auto sb = sectionsOf (b);

    // Move A's second section (start 50) and B's second (start 50) left by 500: clamps to -50.
    moveSections (doc, { { idOf (a), sa[1].id }, { idOf (b), sb[1].id } }, -500);

    CHECK (sectionsOf (a)[1].startTick == 0);
    CHECK (sectionsOf (b)[1].startTick == 0);
    CHECK (notesOf (a)[0].start == 50);    // 100 - 50
    CHECK (notesOf (b)[0].start == 350);   // 400 - 50: the same delta for both
}

TEST_CASE ("moveSections: a zero or fully clamped delta changes nothing and records no undo step", "[sections][move]")
{
    SongDocument doc;
    auto t = addTrack (doc);
    addNote (t, 60, 0, 480);
    splitAt (doc, { idOf (t) }, 240);
    doc.getUndoManager().clearUndoHistory();
    const auto s = sectionsOf (t);

    moveSections (doc, { { idOf (t), s[0].id } }, 0);
    moveSections (doc, { { idOf (t), s[0].id } }, -100);   // already at 0
    CHECK_FALSE (doc.canUndo());
}

TEST_CASE ("resizeSections: shrinking the right edge deletes later notes and trims a crossing note", "[sections][resize]")
{
    SongDocument doc;
    auto t = addTrack (doc);
    addNote (t, 60, 0, 300);
    addNote (t, 62, 400, 400);
    addNote (t, 64, 900, 100);
    const auto id = idOf (t);
    const auto sec = sectionsOf (t)[0];   // virtual id 0: resolved after materialising

    resizeSections (doc, { { id, sec.id } }, SectionEdge::Right, 600);

    CHECK (sectionsOf (t)[0].endTick == 600);
    const auto notes = notesOf (t);
    REQUIRE (notes.size() == 2);
    CHECK (notes[0].start == 0);
    CHECK (notes[0].dur == 300);
    CHECK (notes[1].start == 400);
    CHECK (notes[1].dur == 200);   // trimmed at 600
}

TEST_CASE ("resizeSections: shrinking the left edge deletes earlier notes and trims a crossing note", "[sections][resize]")
{
    SongDocument doc;
    auto t = addTrack (doc);
    addNote (t, 60, 0, 300);
    addNote (t, 62, 400, 400);
    addNote (t, 64, 900, 100);
    const auto id = idOf (t);
    const auto sec = sectionsOf (t)[0];   // virtual id 0: resolved after materialising

    resizeSections (doc, { { id, sec.id } }, SectionEdge::Left, 600);

    CHECK (sectionsOf (t)[0].startTick == 600);
    const auto notes = notesOf (t);
    REQUIRE (notes.size() == 2);
    CHECK (notes[0].start == 600);   // 400..800 trimmed to 600..800
    CHECK (notes[0].dur == 200);
    CHECK (notes[1].start == 900);
}

TEST_CASE ("resizeSections: growing only extends the range and keeps at least one tick", "[sections][resize]")
{
    SongDocument doc;
    auto t = addTrack (doc);
    addNote (t, 60, 0, 480);
    const auto id = idOf (t);
    const auto sec = sectionsOf (t)[0];

    resizeSections (doc, { { id, sec.id } }, SectionEdge::Right, 2000);
    CHECK (sectionsOf (t)[0].endTick == 2000);
    CHECK (notesOf (t).size() == 1);

    resizeSections (doc, { { id, sectionsOf (t)[0].id } }, SectionEdge::Right, -50);   // before the start
    CHECK (sectionsOf (t)[0].endTick == 1);   // start 0 + 1 tick
}

TEST_CASE ("deleteSections: removes the section and its notes only", "[sections][delete]")
{
    SongDocument doc;
    auto t = addTrack (doc);
    addNote (t, 60, 0, 480);
    addNote (t, 62, 960, 480);
    splitAt (doc, { idOf (t) }, 720);
    const auto s = sectionsOf (t);

    deleteSections (doc, { { idOf (t), s[1].id } });

    REQUIRE (sectionsOf (t).size() == 1);
    CHECK (sectionsOf (t)[0].id == s[0].id);
    REQUIRE (notesOf (t).size() == 1);
    CHECK (notesOf (t)[0].pitch == 60);

    doc.undo();
    CHECK (sectionsOf (t).size() == 2);
    CHECK (notesOf (t).size() == 2);
}

TEST_CASE ("deleteSections: deleting every section leaves an empty track with nothing to edit", "[sections][delete]")
{
    SongDocument doc;
    auto t = addTrack (doc);
    addNote (t, 60, 0, 480);
    deleteSections (doc, { { idOf (t), sectionsOf (t)[0].id } });

    CHECK (notesOf (t).empty());
    CHECK (sectionsOf (t).empty());
}

TEST_CASE ("sections: operations ignore the conductor, unknown tracks and unknown sections", "[sections]")
{
    SongDocument doc;
    auto t = addTrack (doc);
    addNote (t, 60, 0, 480);
    const auto conductor = (juce::int64) doc.getConductorTrack().getProperty (SongIDs::trackId);

    splitAt (doc, { conductor, 9999 }, 100);
    moveSections (doc, { { conductor, 1 }, { 9999, 1 }, { idOf (t), 424242 } }, 10);
    deleteSections (doc, { { conductor, 1 }, { idOf (t), 424242 } });

    CHECK (notesOf (t).size() == 1);
    CHECK_FALSE (doc.canUndo());
}

TEST_CASE ("sections: an edited note loses its raw-MIDI ordering and synthesized flag", "[sections][move]")
{
    SongDocument doc;
    auto t = addTrack (doc);
    addNote (t, 60, 0, 480);
    auto n = SongDocument::getNotesNode (t).getChild (0);
    n.setProperty (SongIDs::onOrder, 1, nullptr);
    n.setProperty (SongIDs::offOrder, 2, nullptr);
    n.setProperty (SongIDs::offSynthesized, true, nullptr);

    moveSections (doc, { { idOf (t), sectionsOf (t)[0].id } }, 10);

    CHECK_FALSE (n.hasProperty (SongIDs::onOrder));
    CHECK_FALSE (n.hasProperty (SongIDs::offOrder));
    CHECK_FALSE ((bool) n.getProperty (SongIDs::offSynthesized));
}
```

(The tests use `<algorithm>`; add the include.)

- [ ] **Step 2: Run to verify failure**

Run: `cmake --build build 2>&1 | grep error | head -3`
Expected: `splitAt` / `moveSections` / `resizeSections` / `deleteSections` not declared.

- [ ] **Step 3: Implement**

Declare the four functions in `SectionEdit.h` (signatures in **Interfaces** above). In `SectionEdit.cpp`, inside the anonymous namespace add (after `storedSections`):

```cpp
    juce::ValueTree findSectionNode (const juce::ValueTree& track, juce::int64 id)
    {
        const auto node = track.getChildWithName (SongIDs::SECTIONS);
        for (int i = 0; i < node.getNumChildren(); ++i)
            if ((juce::int64) node.getChild (i).getProperty (SongIDs::sectionId, 0) == id)
                return node.getChild (i);
        return {};
    }

    // Non-undoable: gives a track real stored sections and tags every note.
    void materialise (SongDocument& doc, juce::ValueTree track)
    {
        if (! track.isValid() || (bool) track.getProperty (SongIDs::isConductor, false))
            return;

        auto sectionsNode = track.getChildWithName (SongIDs::SECTIONS);
        if (! sectionsNode.isValid())
        {
            sectionsNode = juce::ValueTree (SongIDs::SECTIONS);
            track.addChild (sectionsNode, -1, nullptr);
        }

        if (sectionsNode.getNumChildren() == 0)
        {
            const auto virtualSections = sectionsOf (track);   // empty when there are no notes
            for (const auto& v : virtualSections)
            {
                juce::ValueTree s (SongIDs::SECTION);
                s.setProperty (SongIDs::sectionId, doc.mintSectionId(), nullptr);
                s.setProperty (SongIDs::startTick, v.startTick, nullptr);
                s.setProperty (SongIDs::endTick, v.endTick, nullptr);
                sectionsNode.addChild (s, -1, nullptr);
            }
        }

        const auto sections = storedSections (track);
        auto notes = SongDocument::getNotesNode (track);
        for (int i = 0; i < notes.getNumChildren(); ++i)
        {
            auto note = notes.getChild (i);
            const auto id = sectionIdOfNote (note, sections);
            if (id != 0 && (juce::int64) note.getProperty (SongIDs::sectionId, 0) != id)
                note.setProperty (SongIDs::sectionId, id, nullptr);
        }
    }

    struct Resolved
    {
        juce::ValueTree track;
        juce::ValueTree sectionNode;
        SectionRange range;
    };

    std::vector<Resolved> resolve (SongDocument& doc, const std::vector<SectionRef>& refs)
    {
        std::set<juce::int64> done;
        for (const auto& r : refs)
            if (done.insert (r.trackId).second)
                materialise (doc, doc.findTrackById (r.trackId));

        std::vector<Resolved> out;
        for (const auto& r : refs)
        {
            auto track = doc.findTrackById (r.trackId);
            if (! track.isValid() || (bool) track.getProperty (SongIDs::isConductor, false))
                continue;

            const auto stored = storedSections (track);
            auto id = r.sectionId;
            if (id == 0 && ! stored.empty())
                id = stored.front().id;

            auto node = findSectionNode (track, id);
            if (! node.isValid())
                continue;
            if (std::any_of (out.begin(), out.end(), [&] (const Resolved& o) { return o.sectionNode == node; }))
                continue;

            out.push_back ({ track, node, { id, (int) node.getProperty (SongIDs::startTick), (int) node.getProperty (SongIDs::endTick) } });
        }
        return out;
    }

    std::vector<juce::ValueTree> membersOf (const juce::ValueTree& track, juce::int64 sectionId)
    {
        const auto sections = storedSections (track);
        std::vector<juce::ValueTree> out;
        const auto notes = SongDocument::getNotesNode (track);
        for (int i = 0; i < notes.getNumChildren(); ++i)
            if (sectionIdOfNote (notes.getChild (i), sections) == sectionId)
                out.push_back (notes.getChild (i));
        return out;
    }
```

Then the four functions:

```cpp
void splitAt (SongDocument& doc, const std::vector<juce::int64>& trackIds, int tick)
{
    std::vector<juce::ValueTree> tracks;
    for (const auto id : trackIds)
    {
        auto track = doc.findTrackById (id);
        if (track.isValid() && ! (bool) track.getProperty (SongIDs::isConductor, false)
            && std::any_of (sectionsOf (track).begin(), sectionsOf (track).end(),
                            [tick] (const SectionRange& s) { return s.startTick < tick && tick < s.endTick; }))
            tracks.push_back (track);
    }
    if (tracks.empty())
        return;

    doc.getUndoManager().beginNewTransaction();

    for (auto track : tracks)
    {
        materialise (doc, track);
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

void moveSections (SongDocument& doc, const std::vector<SectionRef>& refs, int deltaTicks)
{
    const auto resolved = resolve (doc, refs);
    if (resolved.empty() || deltaTicks == 0)
        return;

    int minStart = std::numeric_limits<int>::max();
    for (const auto& r : resolved)
        minStart = std::min (minStart, r.range.startTick);
    const int delta = std::max (deltaTicks, -minStart);
    if (delta == 0)
        return;

    doc.getUndoManager().beginNewTransaction();
    for (const auto& r : resolved)
    {
        const auto members = membersOf (r.track, r.range.id);   // before the range moves
        doc.setProperty (r.sectionNode, SongIDs::startTick, r.range.startTick + delta, false);
        doc.setProperty (r.sectionNode, SongIDs::endTick, r.range.endTick + delta, false);
        for (auto note : members)
        {
            doc.setProperty (note, SongIDs::startTick, (int) note.getProperty (SongIDs::startTick) + delta, false);
            markNoteTimingEdited (doc, note);
        }
    }
}

void resizeSections (SongDocument& doc, const std::vector<SectionRef>& refs, SectionEdge edge, int tick)
{
    auto resolved = resolve (doc, refs);

    struct Change { Resolved r; int newStart; int newEnd; };
    std::vector<Change> changes;
    for (const auto& r : resolved)
    {
        int newStart = r.range.startTick, newEnd = r.range.endTick;
        if (edge == SectionEdge::Left)
            newStart = std::clamp (tick, 0, r.range.endTick - 1);
        else
            newEnd = std::max (tick, r.range.startTick + 1);
        if (newStart != r.range.startTick || newEnd != r.range.endTick)
            changes.push_back ({ r, newStart, newEnd });
    }
    if (changes.empty())
        return;

    doc.getUndoManager().beginNewTransaction();
    for (const auto& c : changes)
    {
        const auto members = membersOf (c.r.track, c.r.range.id);
        auto notesNode = SongDocument::getNotesNode (c.r.track);

        doc.setProperty (c.r.sectionNode, SongIDs::startTick, c.newStart, false);
        doc.setProperty (c.r.sectionNode, SongIDs::endTick, c.newEnd, false);

        for (auto note : members)
        {
            const int start = (int) note.getProperty (SongIDs::startTick);
            const int end = start + (int) note.getProperty (SongIDs::durationTicks);

            if (start >= c.newEnd || end <= c.newStart)
            {
                doc.removeChild (notesNode, note, false);
            }
            else if (end > c.newEnd)
            {
                doc.setProperty (note, SongIDs::durationTicks, c.newEnd - start, false);
                markNoteTimingEdited (doc, note);
            }
            else if (start < c.newStart)
            {
                doc.setProperty (note, SongIDs::startTick, c.newStart, false);
                doc.setProperty (note, SongIDs::durationTicks, end - c.newStart, false);
                markNoteTimingEdited (doc, note);
            }
        }
    }
}

void deleteSections (SongDocument& doc, const std::vector<SectionRef>& refs)
{
    const auto resolved = resolve (doc, refs);
    if (resolved.empty())
        return;

    doc.getUndoManager().beginNewTransaction();
    for (const auto& r : resolved)
    {
        auto notesNode = SongDocument::getNotesNode (r.track);
        for (auto note : membersOf (r.track, r.range.id))
            doc.removeChild (notesNode, note, false);
        doc.removeChild (r.track.getChildWithName (SongIDs::SECTIONS), r.sectionNode, false);
    }
}
```

Notes on the code: `splitAt` calls `sectionsOf (track)` twice inside the `any_of` call; store it in a local `const auto sections = sectionsOf (track);` instead (the two temporaries' iterators would not match). `moveSections` and `deleteSections` must compute `membersOf` BEFORE mutating the section range (membership falls back to position for untagged notes; after `materialise` every note is tagged, so either order is safe, but keep it before).

- [ ] **Step 4: Run to verify pass**

Run: `cmake --build build && ctest --test-dir build -R "sections" --output-on-failure`
Expected: PASS. Two tests in this task have loose comments that need care: in "overlapping sections are both split" the first section after the move is `[0,240)` and the second `[120,360)`, so a split at 200 divides both (2 + 2 = 4 sections); fix the test comment if the assertion text is confusing. If "clamps ... companions" fails because `splitAt (..., 50)` leaves sections starting at 0 and 50, re-read the expected values: the second sections start at 50, a delta of -500 clamps to -50.

- [ ] **Step 5: Commit**

```bash
git add Source/UI/SectionEdit.* Tests/SectionEdit_tests.cpp
git commit -m "feat(ui): split, move, resize and delete track sections

Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>"
```

---

### Task 4: Downstream integration (playback, export, preview diff)

**Files:**
- Modify: `Source/UI/PreviewNoteDiff.cpp`
- Test: `Tests/SectionEdit_tests.cpp` (playback and export), `Tests/PreviewNoteDiff_tests.cpp`

**Interfaces:**
- Consumes: `splitAt`, `moveSections`, `deleteSections` (Task 3); `buildSnapshot` (`UI/Playback/PlaybackSnapshot.h`); `buildRawMidiFile` (`UI/MidiExport.h`).
- Produces: `diffPreviewNotes` matches notes sharing one `(sourceTrackIndex, sourceEventIndex)` key by order of start tick (the k-th assembled note with the k-th pipelined note of that key; an assembled note with no counterpart is `Dropped`).

- [ ] **Step 1: Write the failing tests**

Append to `Tests/SectionEdit_tests.cpp` (add includes `"UI/Playback/PlaybackSnapshot.h"` and `"UI/MidiExport.h"`):

```cpp
TEST_CASE ("sections: playback hears a cut note as two attacks and a deleted section as silence", "[sections][integration]")
{
    SongDocument doc;
    auto t = addTrack (doc);
    addNote (t, 60, 0, 960);

    splitAt (doc, { idOf (t) }, 480);
    {
        const auto snap = buildSnapshot (doc);
        int ons = 0;
        for (const auto& e : snap->events())
            ons += e.kind == PlaybackEventKind::NoteOn ? 1 : 0;
        CHECK (ons == 2);
    }

    deleteSections (doc, { { idOf (t), sectionsOf (t)[1].id } });
    {
        const auto snap = buildSnapshot (doc);
        int ons = 0;
        for (const auto& e : snap->events())
            ons += e.kind == PlaybackEventKind::NoteOn ? 1 : 0;
        CHECK (ons == 1);
    }
}

TEST_CASE ("sections: a moved section exports at its new position", "[sections][integration]")
{
    SongDocument doc;
    auto t = addTrack (doc);
    addNote (t, 60, 0, 480);
    splitAt (doc, { idOf (t) }, 240);
    moveSections (doc, { { idOf (t), sectionsOf (t)[1].id } }, 960);

    const auto file = buildRawMidiFile (doc);
    REQUIRE (file.tracks.size() >= 2);
    int latestOn = -1;
    for (const auto& e : file.tracks[1].events)
        if (! e.bytes.empty() && (e.bytes[0] & 0xF0) == 0x90 && e.bytes.size() > 2 && e.bytes[2] > 0)
            latestOn = std::max (latestOn, e.tick);
    CHECK (latestOn == 1200);   // the second half: 240 + 960
}
```

Append to `Tests/PreviewNoteDiff_tests.cpp` (reuse its `makeNote` helper and `PreviewResult` setup, as in the existing tests):

```cpp
TEST_CASE ("PreviewNoteDiff: two notes with the same provenance key are matched in start order", "[previewnotediff][sections]")
{
    PreviewResult result;
    Track assembledTrack;
    assembledTrack.notes.push_back (makeNote (60, 0, 400, 100, 2, 7));
    assembledTrack.notes.push_back (makeNote (60, 400, 560, 100, 2, 7));
    result.assembled.tracks.push_back (assembledTrack);

    Track pipelinedTrack;
    pipelinedTrack.notes.push_back (makeNote (72, 0, 400, 100, 2, 7));   // both fold up an octave
    pipelinedTrack.notes.push_back (makeNote (72, 400, 560, 100, 2, 7));
    result.pipelined.tracks.push_back (pipelinedTrack);

    const auto diff = diffPreviewNotes (result);
    REQUIRE (diff.size() == 2);
    for (const auto& n : diff)
    {
        CHECK (n.state == NoteState::WillFold);
        REQUIRE (n.postPitch.has_value());
        CHECK (*n.postPitch == 72);
    }
}

TEST_CASE ("PreviewNoteDiff: halves that the pipeline removed are both Dropped; a lone survivor pairs with the first", "[previewnotediff][sections]")
{
    PreviewResult dropped;
    Track a;
    a.notes.push_back (makeNote (60, 0, 400, 100, 2, 7));
    a.notes.push_back (makeNote (60, 400, 560, 100, 2, 7));
    dropped.assembled.tracks.push_back (a);
    dropped.pipelined.tracks.push_back (Track {});

    for (const auto& n : diffPreviewNotes (dropped))
        CHECK (n.state == NoteState::Dropped);

    PreviewResult partial;
    partial.assembled.tracks.push_back (a);
    Track survivor;
    survivor.notes.push_back (makeNote (60, 0, 400, 100, 2, 7));
    partial.pipelined.tracks.push_back (survivor);

    const auto diff = diffPreviewNotes (partial);
    REQUIRE (diff.size() == 2);
    CHECK (diff[0].startTick == 0);
    CHECK (diff[0].state == NoteState::Normal);
    CHECK (diff[1].startTick == 400);
    CHECK (diff[1].state == NoteState::Dropped);
}
```

- [ ] **Step 2: Run to verify failure**

Run: `cmake --build build && ctest --test-dir build -R "sections|previewnotediff" --output-on-failure`
Expected: the playback/export tests PASS already (flat notes); the second `PreviewNoteDiff` test FAILS (`diff[1].state` is `Normal` with last-write-wins).

- [ ] **Step 3: Implement**

Rewrite the body of `diffPreviewNotes` in `Source/UI/PreviewNoteDiff.cpp`:

```cpp
std::vector<PreviewNote> diffPreviewNotes (const PreviewResult& result)
{
    using Key = std::pair<int, int>;

    // Notes that share a provenance key (the two halves of a cut note, or
    // editor-created notes, all keyed -1/-1) are matched in start-tick order.
    std::map<Key, std::vector<std::pair<int, int>>> pipelinedByKey;   // (startTick, pitch)
    for (const auto& track : result.pipelined.tracks)
        for (const auto& note : track.notes)
            pipelinedByKey[{ note.sourceTrackIndex, note.sourceEventIndex }].push_back ({ note.startTick, note.pitch });
    for (auto& [key, list] : pipelinedByKey)
        std::sort (list.begin(), list.end());

    std::map<Key, std::vector<int>> assembledStarts;
    for (const auto& track : result.assembled.tracks)
        for (const auto& note : track.notes)
            assembledStarts[{ note.sourceTrackIndex, note.sourceEventIndex }].push_back (note.startTick);
    for (auto& [key, list] : assembledStarts)
        std::sort (list.begin(), list.end());

    std::vector<PreviewNote> diff;

    for (const auto& track : result.assembled.tracks)
    {
        for (const auto& note : track.notes)
        {
            PreviewNote pn;
            pn.sourceTrackIndex = note.sourceTrackIndex;
            pn.sourceEventIndex = note.sourceEventIndex;
            pn.prePitch         = note.pitch;
            pn.startTick        = note.startTick;
            pn.durationTicks    = note.durationTicks;
            pn.velocity         = note.velocity;

            const Key key { note.sourceTrackIndex, note.sourceEventIndex };
            const auto& starts = assembledStarts[key];
            const auto rank = (size_t) (std::lower_bound (starts.begin(), starts.end(), note.startTick) - starts.begin());

            const auto it = pipelinedByKey.find (key);
            if (it == pipelinedByKey.end() || rank >= it->second.size())
            {
                pn.state = NoteState::Dropped;
            }
            else if (it->second[rank].second == note.pitch)
            {
                pn.state = NoteState::Normal;
            }
            else
            {
                pn.state    = NoteState::WillFold;
                pn.postPitch = it->second[rank].second;
            }

            diff.push_back (pn);
        }
    }

    return diff;
}
```

Add `#include <algorithm>`. A note that shares both key and start tick with another (not produced by sections) keeps the same rank, as before this change.

- [ ] **Step 4: Run to verify pass**

Run: `cmake --build build && ctest --test-dir build --output-on-failure 2>&1 | tail -5`
Expected: ALL tests pass, including every pre-existing `PreviewNoteDiff` and `PreviewPipeline` test. If an existing editor-created-note test relied on last-write-wins, read it and decide whether the new order-based matching is the correct reading of that test; do not weaken a test without saying so.

- [ ] **Step 5: Commit**

```bash
git add Source/UI/PreviewNoteDiff.cpp Tests/PreviewNoteDiff_tests.cpp Tests/SectionEdit_tests.cpp
git commit -m "fix(ui): match notes that share a provenance key in start order

Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>"
```

---

### Task 5: Multi-track selection

**Files:**
- Modify: `Source/UI/TrackRowComponent.h/.cpp`, `Source/UI/TrackNotePreview.h/.cpp`, `Source/UI/TrackListComponent.h/.cpp`
- Test: `Tests/TrackListComponent_tests.cpp`, `Tests/TrackRowComponent_tests.cpp`, `Tests/TrackNotePreview_tests.cpp`

**Interfaces:**
- Produces on `TrackListComponent`: `const std::set<juce::int64>& getSelectedTrackIds() const`, `void selectAllTracks()` (every non-conductor track), `void clearSelection()` (existing; clears the set too), `juce::int64 getSelectedTrackId()` (existing; the last-clicked anchor). Public so `SongsmithMainComponent` can reach them.
- Changes `TrackRowComponent::onTrackSelected` and `TrackNotePreview::onNonToggleClick` to carry the click's modifiers: `std::function<void (juce::int64, const juce::ModifierKeys&)>` and `std::function<void (const juce::ModifierKeys&)>`.
- Selection rules in `TrackListComponent::selectTrack (juce::int64 trackId, const juce::ModifierKeys& mods, bool fromStrip)`: Shift = range from the anchor row to this row (by display order); Ctrl/Cmd = toggle this track; plain click from the strip on a track already inside a selection of more than one = keep the selection; otherwise select only this track. The anchor becomes `trackId` except on Shift. The conductor is never selected by range or select-all.

- [ ] **Step 1: Write the failing tests**

In `Tests/TrackListComponent_tests.cpp`, extend `TrackListComponentTestAccess` with
`static void click (TrackListComponent& c, juce::int64 id, juce::ModifierKeys mods, bool fromStrip) { c.selectTrack (id, mods, fromStrip); }` and `static const std::set<juce::int64>& selected (TrackListComponent& c) { return c.selectedTrackIds; }`, update every existing `c.selectTrack (id)` call to `c.selectTrack (id, {}, false)`, then add (using the file's existing helpers for building a document with several imported tracks; `playbacktest::addTrack`/`addNote` work too):

```cpp
TEST_CASE ("TrackListComponent: click selects one track, ctrl-click toggles, shift-click selects a range", "[track-list][selection]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SongDocument doc;
    std::vector<juce::int64> ids;
    for (int i = 0; i < 4; ++i)
    {
        auto t = playbacktest::addTrack (doc, "T");
        playbacktest::addNote (t, 60, 0, 480);
        ids.push_back ((juce::int64) t.getProperty (SongIDs::trackId));
    }
    TrackListComponent list (doc);
    list.setBounds (0, 0, 600, 300);
    TrackListComponentTestAccess::rebuild (list);

    const juce::ModifierKeys none, ctrl (juce::ModifierKeys::ctrlModifier), shift (juce::ModifierKeys::shiftModifier);

    TrackListComponentTestAccess::click (list, ids[0], none, false);
    CHECK (list.getSelectedTrackIds() == std::set<juce::int64> { ids[0] });

    TrackListComponentTestAccess::click (list, ids[2], ctrl, false);
    CHECK (list.getSelectedTrackIds() == std::set<juce::int64> { ids[0], ids[2] });

    TrackListComponentTestAccess::click (list, ids[2], ctrl, false);   // toggles off
    CHECK (list.getSelectedTrackIds() == std::set<juce::int64> { ids[0] });

    TrackListComponentTestAccess::click (list, ids[3], shift, false);   // anchor ids[0] .. ids[3]
    CHECK (list.getSelectedTrackIds() == std::set<juce::int64> { ids[0], ids[1], ids[2], ids[3] });
}

TEST_CASE ("TrackListComponent: a plain strip click keeps a multi-track selection it is part of", "[track-list][selection]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SongDocument doc;
    std::vector<juce::int64> ids;
    for (int i = 0; i < 3; ++i)
    {
        auto t = playbacktest::addTrack (doc, "T");
        playbacktest::addNote (t, 60, 0, 480);
        ids.push_back ((juce::int64) t.getProperty (SongIDs::trackId));
    }
    TrackListComponent list (doc);
    list.setBounds (0, 0, 600, 300);
    TrackListComponentTestAccess::rebuild (list);

    list.selectAllTracks();
    CHECK (list.getSelectedTrackIds().size() == 3);   // the conductor is never selected

    TrackListComponentTestAccess::click (list, ids[1], {}, true);    // strip click, inside the selection
    CHECK (list.getSelectedTrackIds().size() == 3);

    TrackListComponentTestAccess::click (list, ids[1], {}, false);   // info-column click: select only it
    CHECK (list.getSelectedTrackIds() == std::set<juce::int64> { ids[1] });

    list.clearSelection();
    CHECK (list.getSelectedTrackIds().empty());
}

TEST_CASE ("TrackListComponent: rebuild drops selected tracks that no longer exist", "[track-list][selection]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SongDocument doc;
    auto a = playbacktest::addTrack (doc, "A");
    auto b = playbacktest::addTrack (doc, "B");
    playbacktest::addNote (a, 60, 0, 480);
    playbacktest::addNote (b, 60, 0, 480);
    TrackListComponent list (doc);
    list.setBounds (0, 0, 600, 300);
    TrackListComponentTestAccess::rebuild (list);
    list.selectAllTracks();

    doc.removeTrack ((juce::int64) b.getProperty (SongIDs::trackId));
    TrackListComponentTestAccess::rebuild (list);

    CHECK (list.getSelectedTrackIds() == std::set<juce::int64> { (juce::int64) a.getProperty (SongIDs::trackId) });
}
```

In `Tests/TrackRowComponent_tests.cpp` change the two `row.onTrackSelected` lambdas (lines ~75, ~107) to `[&] (juce::int64 id, const juce::ModifierKeys&) { selectedId = id; }`. In `Tests/TrackNotePreview_tests.cpp` (if a test assigns `onNonToggleClick`, change its lambda to take `const juce::ModifierKeys&`; grep first).

- [ ] **Step 2: Run to verify failure**

Run: `cmake --build build 2>&1 | grep error | head -5`
Expected: errors for `getSelectedTrackIds`, `selectAllTracks`, `selectTrack` overload, `selectedTrackIds`.

- [ ] **Step 3: Implement**

`TrackNotePreview.h`: `std::function<void (const juce::ModifierKeys&)> onNonToggleClick;` and in `mouseDown` call `onNonToggleClick (e.mods)`.

`TrackRowComponent.h/.cpp`: `std::function<void (juce::int64, const juce::ModifierKeys&)> onTrackSelected;`. The strip forwards with `fromStrip` known from the callback source, so give the row TWO callbacks: `onTrackSelected` (info-column click, from `TrackRowComponent::mouseDown (const MouseEvent& e)` → `onTrackSelected (getTrackId(), e.mods)`) and `onStripSelected` (the preview's `onNonToggleClick` → `onStripSelected (getTrackId(), mods)`), both `std::function<void (juce::int64, const juce::ModifierKeys&)>`. Update the row constructor wiring accordingly.

`TrackListComponent.h`: add

```cpp
    const std::set<juce::int64>& getSelectedTrackIds() const noexcept { return selectedTrackIds; }
    void selectAllTracks();
```

private `void selectTrack (juce::int64 trackId, const juce::ModifierKeys& mods, bool fromStrip);` (replacing the one-argument version), `void applySelectionToRows();`, member `std::set<juce::int64> selectedTrackIds;`, and `#include <set>`.

`TrackListComponent.cpp`:

```cpp
void TrackListComponent::selectTrack (juce::int64 trackId, const juce::ModifierKeys& mods, bool fromStrip)
{
    const auto isConductorId = [this] (juce::int64 id)
    {
        const auto t = doc.findTrackById (id);
        return ! t.isValid() || (bool) t.getProperty (SongIDs::isConductor, false);
    };

    if (trackId == -1)
    {
        selectedTrackIds.clear();
        selectedTrackId = -1;
    }
    else if (mods.isShiftDown() && selectedTrackId != -1)
    {
        int from = -1, to = -1;
        for (int i = 0; i < content.rows.size(); ++i)
        {
            if (content.rows[i]->getTrackId() == selectedTrackId) from = i;
            if (content.rows[i]->getTrackId() == trackId) to = i;
        }
        if (from >= 0 && to >= 0)
            for (int i = std::min (from, to); i <= std::max (from, to); ++i)
                if (! isConductorId (content.rows[i]->getTrackId()))
                    selectedTrackIds.insert (content.rows[i]->getTrackId());
    }
    else if (mods.isCtrlDown() || mods.isCommandDown())
    {
        if (selectedTrackIds.erase (trackId) == 0 && ! isConductorId (trackId))
            selectedTrackIds.insert (trackId);
        selectedTrackId = trackId;
    }
    else if (fromStrip && selectedTrackIds.size() > 1 && selectedTrackIds.count (trackId) > 0)
    {
        selectedTrackId = trackId;   // keep the multi-selection
    }
    else
    {
        selectedTrackIds.clear();
        if (! isConductorId (trackId))
            selectedTrackIds.insert (trackId);
        selectedTrackId = trackId;
    }
    applySelectionToRows();
}

void TrackListComponent::applySelectionToRows()
{
    for (auto* row : content.rows)
        row->setSelected (selectedTrackIds.count (row->getTrackId()) > 0 || row->getTrackId() == selectedTrackId);
}

void TrackListComponent::selectAllTracks()
{
    selectedTrackIds.clear();
    for (int i = 0; i < doc.getNumTracks(); ++i)
    {
        const auto t = doc.getTrack (i);
        if (! (bool) t.getProperty (SongIDs::isConductor, false))
            selectedTrackIds.insert ((juce::int64) t.getProperty (SongIDs::trackId));
    }
    applySelectionToRows();
}

void TrackListComponent::clearSelection() { selectTrack (-1, {}, false); }
```

Selecting the conductor row with a plain click keeps today's behaviour (`selectedTrackId` set, row highlighted, but not in `selectedTrackIds`). `rebuild()`: wire `row->onTrackSelected = [this] (juce::int64 id, const juce::ModifierKeys& m) { selectTrack (id, m, false); };` and `row->onStripSelected = [this] (...) { selectTrack (id, m, true); };`; replace the `row->setSelected (...)` line with a call to `applySelectionToRows()` after the loop; and replace the stale-selection block with:

```cpp
    for (auto it = selectedTrackIds.begin(); it != selectedTrackIds.end();)
        it = doc.findTrackById (*it).isValid() ? std::next (it) : selectedTrackIds.erase (it);
    if (selectedTrackId != -1 && ! doc.findTrackById (selectedTrackId).isValid())
        selectedTrackId = -1;
    applySelectionToRows();
```

- [ ] **Step 4: Run to verify pass**

Run: `cmake --build build && ctest --test-dir build --output-on-failure 2>&1 | tail -5`
Expected: all PASS.

- [ ] **Step 5: Commit**

```bash
git add Source/UI/TrackRowComponent.* Source/UI/TrackNotePreview.* Source/UI/TrackListComponent.* Tests
git commit -m "feat(ui): select several tracks at once on the main canvas

Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>"
```

---

### Task 6: Section painting and gestures

**Files:**
- Create: `Source/UI/SectionViewState.h`
- Modify: `Source/UI/TrackNotePreview.h/.cpp`, `Source/UI/TrackRowComponent.h/.cpp`, `Source/UI/TrackListComponent.h/.cpp`, `Source/UI/SongsmithColours.h`
- Test: `Tests/TrackNotePreview_tests.cpp`, `Tests/TrackListComponent_tests.cpp`

**Interfaces:**
- Produces `SectionViewState` (header-only, owned by `TrackListComponent`, shared by every preview like `TimelineViewState`):

```cpp
struct SectionDragPreview
{
    enum class Kind { Move, ResizeLeft, ResizeRight } kind = Kind::Move;
    int deltaTicks = 0;   // Move
    int edgeTick = 0;     // ResizeLeft / ResizeRight
};
struct SectionViewState
{
    std::set<SectionRef> selected;
    std::optional<SectionDragPreview> drag;
};
```

- `TrackNotePreview::setSectionView (const SectionViewState* state, juce::int64 trackId)` (null = paint no sections, so existing tests are unchanged); callbacks `std::function<void (const SectionHit&, int tick)> onSectionPressed;`, `std::function<void (int tick)> onSectionDragged;`, `std::function<void (int tick)> onSectionReleased;`.
- `TrackListComponent`: private `SectionViewState sectionView;` and the section gesture state; `TrackNotePreview` mouse handling forwards to it; the list commits through `moveSections`/`resizeSections` on mouse-up.
- Colours in `SongsmithColours.h`: `sectionFill = 0x22FFFFFF`, `sectionSelectedFill = 0x44FFD27F`, `sectionEdge = 0xAAFFFFFF`, `sectionSelectedEdge = 0xFFFFD27F`.

- [ ] **Step 1: Write the failing tests**

`Tests/TrackNotePreview_tests.cpp` (add `#include "UI/SectionViewState.h"`):

```cpp
TEST_CASE ("TrackNotePreview: sections are drawn as blocks with edges, selected ones highlighted", "[track-note-preview][sections]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SongDocument doc;
    auto track = playbacktest::addTrack (doc);
    playbacktest::addNote (track, 60, 0, 960);
    splitAt (doc, { (juce::int64) track.getProperty (SongIDs::trackId) }, 480);
    const auto sections = sectionsOf (track);
    REQUIRE (sections.size() == 2);

    TimelineViewState viewState;
    viewState.setPixelsPerTick (0.1);
    SectionViewState sectionView;
    sectionView.selected.insert ({ (juce::int64) track.getProperty (SongIDs::trackId), sections[1].id });

    TrackNotePreview preview (track, viewState);
    preview.setSectionView (&sectionView, (juce::int64) track.getProperty (SongIDs::trackId));
    preview.setBounds (0, 0, previewWidth, previewHeight);

    juce::Image image (juce::Image::ARGB, previewWidth, previewHeight, true, juce::SoftwareImageType());
    juce::Graphics g (image);
    preview.paint (g);

    const int y = previewHeight - 4;   // below the note line
    const auto unselected = image.getPixelAt (viewState.xForTick (240), y);
    const auto selected = image.getPixelAt (viewState.xForTick (720), y);
    CHECK (selected != unselected);                                   // highlighted
    CHECK (unselected != juce::Colour (SongsmithColours::background)); // a block is drawn
    CHECK (gridLineDrawnAt (image, viewState, 480));                  // a visible edge at the split
}

TEST_CASE ("TrackNotePreview: with no section view nothing section-related is drawn", "[track-note-preview][sections]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    juce::ValueTree track (SongIDs::MIDI_TRACK);
    TimelineViewState viewState;
    viewState.setPixelsPerTick (0.1);
    TrackNotePreview preview (track, viewState);
    preview.setBounds (0, 0, previewWidth, previewHeight);

    juce::Image image (juce::Image::ARGB, previewWidth, previewHeight, true, juce::SoftwareImageType());
    juce::Graphics g (image);
    preview.paint (g);
    CHECK (image.getPixelAt (50, previewHeight - 4) == juce::Colour (SongsmithColours::background));
}

TEST_CASE ("TrackNotePreview: pressing in a section reports the hit, the tick and the modifiers", "[track-note-preview][sections]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SongDocument doc;
    auto track = playbacktest::addTrack (doc);
    playbacktest::addNote (track, 60, 0, 1920);
    TimelineViewState viewState;
    viewState.setPixelsPerTick (0.1);
    SectionViewState sectionView;
    TrackNotePreview preview (track, viewState);
    preview.setSectionView (&sectionView, (juce::int64) track.getProperty (SongIDs::trackId));
    preview.setBounds (0, 0, previewWidth, previewHeight);

    SectionHit hit;
    int pressedTick = -1;
    preview.onSectionPressed = [&] (const SectionHit& h, int tick) { hit = h; pressedTick = tick; };

    const int x = viewState.xForTick (960);
    preview.mouseDown (juce::MouseEvent (juce::Desktop::getInstance().getMainMouseSource(), { (float) x, 20.0f },
                                         juce::ModifierKeys(), 0, 0, 0, 0, 0, &preview, &preview,
                                         juce::Time::getCurrentTime(), { (float) x, 20.0f }, juce::Time::getCurrentTime(), 1, false));

    CHECK (hit.zone == SectionZone::Body);
    CHECK (pressedTick == 960);
}
```

(For the synthetic `MouseEvent`, copy how `Tests/PlayheadOverlay_tests.cpp`'s `mouseAt` helper builds one and reuse that helper's shape; the constructor arguments above are the JUCE signature and may need adjusting.)

`Tests/TrackListComponent_tests.cpp`: add `static SectionViewState& sectionView (TrackListComponent& c) { return c.sectionView; }`, `static void press (TrackListComponent&, juce::int64 trackId, SectionHit, int tick)`, `static void drag (TrackListComponent&, int tick)`, `static void release (TrackListComponent&, int tick)` to the access struct, each forwarding to the private handlers named below, then:

```cpp
TEST_CASE ("TrackListComponent: selecting a section also selects its companions on the other selected tracks", "[track-list][sections]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SongDocument doc;
    std::vector<juce::int64> ids;
    for (int i = 0; i < 2; ++i)
    {
        auto t = playbacktest::addTrack (doc, "T");
        playbacktest::addNote (t, 60, 0, 960);
        ids.push_back ((juce::int64) t.getProperty (SongIDs::trackId));
    }
    splitAt (doc, ids, 480);
    TrackListComponent list (doc);
    list.setBounds (0, 0, 600, 300);
    TrackListComponentTestAccess::rebuild (list);
    list.selectAllTracks();

    const auto first = sectionsOf (doc.findTrackById (ids[0]))[1];
    TrackListComponentTestAccess::press (list, ids[0], { first.id, SectionZone::Body }, 700);

    CHECK (TrackListComponentTestAccess::sectionView (list).selected.size() == 2);
}

TEST_CASE ("TrackListComponent: a section drag changes nothing in the document until release", "[track-list][sections]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SongDocument doc;
    auto t = playbacktest::addTrack (doc);
    playbacktest::addNote (t, 60, 0, 960);
    const auto id = (juce::int64) t.getProperty (SongIDs::trackId);
    splitAt (doc, { id }, 480);
    TrackListComponent list (doc);
    list.setBounds (0, 0, 600, 300);
    TrackListComponentTestAccess::rebuild (list);

    const auto second = sectionsOf (t)[1];
    TrackListComponentTestAccess::press (list, id, { second.id, SectionZone::Body }, 700);
    TrackListComponentTestAccess::drag (list, 900);

    CHECK (sectionsOf (t)[1].startTick == 480);   // untouched mid-drag
    REQUIRE (TrackListComponentTestAccess::sectionView (list).drag.has_value());
    CHECK (TrackListComponentTestAccess::sectionView (list).drag->deltaTicks == 200);

    TrackListComponentTestAccess::release (list, 900);
    CHECK (sectionsOf (t)[1].startTick == 680);
    CHECK_FALSE (TrackListComponentTestAccess::sectionView (list).drag.has_value());

    doc.undo();
    CHECK (sectionsOf (t)[1].startTick == 480);   // one undo step for the whole drag
}

TEST_CASE ("TrackListComponent: dragging an edge resizes, a press on empty strip selects no section", "[track-list][sections]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SongDocument doc;
    auto t = playbacktest::addTrack (doc);
    playbacktest::addNote (t, 60, 0, 960);
    const auto id = (juce::int64) t.getProperty (SongIDs::trackId);
    TrackListComponent list (doc);
    list.setBounds (0, 0, 600, 300);
    TrackListComponentTestAccess::rebuild (list);

    const auto sec = sectionsOf (t)[0];
    TrackListComponentTestAccess::press (list, id, { sec.id, SectionZone::RightEdge }, 960);
    TrackListComponentTestAccess::drag (list, 600);
    TrackListComponentTestAccess::release (list, 600);
    CHECK (sectionsOf (t)[0].endTick == 600);

    TrackListComponentTestAccess::press (list, id, {}, 5000);   // SectionHit{} has zone None
    CHECK (TrackListComponentTestAccess::sectionView (list).selected.empty());
}

TEST_CASE ("TrackListComponent: rebuild forgets selected sections that no longer exist", "[track-list][sections]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SongDocument doc;
    auto t = playbacktest::addTrack (doc);
    playbacktest::addNote (t, 60, 0, 960);
    const auto id = (juce::int64) t.getProperty (SongIDs::trackId);
    splitAt (doc, { id }, 480);
    TrackListComponent list (doc);
    list.setBounds (0, 0, 600, 300);
    TrackListComponentTestAccess::rebuild (list);

    const auto second = sectionsOf (t)[1];
    TrackListComponentTestAccess::press (list, id, { second.id, SectionZone::Body }, 700);
    deleteSections (doc, { { id, second.id } });
    TrackListComponentTestAccess::rebuild (list);

    CHECK (TrackListComponentTestAccess::sectionView (list).selected.empty());
}
```

- [ ] **Step 2: Run to verify failure**

Run: `cmake --build build 2>&1 | grep error | head -5`
Expected: `SectionViewState.h` not found, `setSectionView`/`onSectionPressed` undeclared.

- [ ] **Step 3: Implement**

`Source/UI/SectionViewState.h`: the two structs above plus `#include "UI/SectionEdit.h"`, `<optional>`, `<set>`.

`SongsmithColours.h`: add the four constants beside the existing ones.

`TrackNotePreview`: add `void setSectionView (const SectionViewState*, juce::int64 trackId)`, the three callbacks, members `const SectionViewState* sectionView = nullptr; juce::int64 trackId = 0;` and `bool sectionPressActive = false;`. In `paint()`, after the grid and before the notes loop, call `paintSections (g)`:

```cpp
    void TrackNotePreview::paintSections (juce::Graphics& g) const
    {
        if (sectionView == nullptr)
            return;

        const auto bounds = getLocalBounds();
        for (const auto& s : sectionsOf (track))
        {
            int start = s.startTick, end = s.endTick;
            const bool selected = sectionView->selected.count ({ trackId, s.id }) > 0;
            if (selected && sectionView->drag)
            {
                const auto& d = *sectionView->drag;
                if (d.kind == SectionDragPreview::Kind::Move)
                {
                    start += d.deltaTicks;   // the list already clamped the shared delta
                    end += d.deltaTicks;
                }
                else if (d.kind == SectionDragPreview::Kind::ResizeLeft)
                    start = std::clamp (d.edgeTick, 0, end - 1);
                else
                    end = std::max (d.edgeTick, start + 1);
            }

            const int x0 = viewState.xForTick (start);
            const int x1 = std::max (x0 + 1, viewState.xForTick (end));
            g.setColour (juce::Colour (selected ? SongsmithColours::sectionSelectedFill : SongsmithColours::sectionFill));
            g.fillRect (x0, bounds.getY(), x1 - x0, bounds.getHeight());
            g.setColour (juce::Colour (selected ? SongsmithColours::sectionSelectedEdge : SongsmithColours::sectionEdge));
            g.drawVerticalLine (x0, (float) bounds.getY(), (float) bounds.getBottom());
            g.drawVerticalLine (x1 - 1, (float) bounds.getY(), (float) bounds.getBottom());
        }
    }
```

`mouseDown` becomes: ghost toggle → return; `onNonToggleClick (e.mods)`; `onTimelineClicked (tick)`; then, when `sectionView != nullptr && onSectionPressed`, `const auto hit = hitTestSection (sectionsOf (track), tick, viewState.getPixelsPerTick(), 5); sectionPressActive = true; onSectionPressed (hit, tick);`. Add `mouseDrag` (`if (sectionPressActive && onSectionDragged) onSectionDragged (viewState.tickForX (e.x));`) and `mouseUp` (`if (sectionPressActive) { sectionPressActive = false; if (onSectionReleased) onSectionReleased (viewState.tickForX (e.x)); }`). The strip's own mouse drag must not start the row's drag-and-drop (it doesn't today: only the row's `mouseDrag` does).

`TrackRowComponent`: add `void setSectionView (const SectionViewState* s)` which forwards `notePreview.setSectionView (s, getTrackId())`, and forward the three section callbacks the same way the others are forwarded, adding the track id as a first argument: `std::function<void (juce::int64, const SectionHit&, int)> onSectionPressed;` and `std::function<void (int)> onSectionDragged; std::function<void (int)> onSectionReleased;`.

`TrackListComponent`:
- member `SectionViewState sectionView;` declared **before** `content` (rows hold a pointer to it) and private gesture state:

```cpp
    struct SectionGesture
    {
        SectionZone zone = SectionZone::None;
        std::vector<SectionRef> refs;
        int pressTick = 0;
        int minStart = 0;   // earliest start among refs, for clamping the move
    };
    std::optional<SectionGesture> gesture;
```
- `rebuild()`: `row->setSectionView (&sectionView);` and wire the callbacks to `sectionPressed (trackId, hit, tick)`, `sectionDragged (tick)`, `sectionReleased (tick)`. After the stale-track pruning, prune `sectionView.selected` to refs whose section still exists (look it up with `sectionsOf (doc.findTrackById (r.trackId))`, treating id 0 as present while the virtual default exists) and repaint.
- Handlers:

```cpp
void TrackListComponent::sectionPressed (juce::int64 trackId, const SectionHit& hit, int tick)
{
    gesture.reset();
    sectionView.drag.reset();
    if (hit.zone == SectionZone::None)
    {
        sectionView.selected.clear();
        content.repaint();
        return;
    }

    // Track toggling/range selection already ran in selectTrack (the preview calls
    // onNonToggleClick first), so selectedTrackIds is up to date here.
    const auto refs = withCompanions (doc, selectedTrackIds, { trackId, hit.sectionId });
    sectionView.selected = std::set<SectionRef> (refs.begin(), refs.end());

    int minStart = std::numeric_limits<int>::max();
    for (const auto& r : refs)
        for (const auto& s : sectionsOf (doc.findTrackById (r.trackId)))
            if (s.id == r.sectionId)
                minStart = std::min (minStart, s.startTick);
    gesture = SectionGesture { hit.zone, refs, tick, minStart };
    content.repaint();
}

void TrackListComponent::sectionDragged (int tick)
{
    if (! gesture)
        return;
    if (gesture->zone == SectionZone::Body)
        sectionView.drag = SectionDragPreview { SectionDragPreview::Kind::Move,
                                                std::max (tick - gesture->pressTick, -gesture->minStart), 0 };
    else
        sectionView.drag = SectionDragPreview { gesture->zone == SectionZone::LeftEdge ? SectionDragPreview::Kind::ResizeLeft
                                                                                        : SectionDragPreview::Kind::ResizeRight,
                                                0, tick };
    content.repaint();
}

void TrackListComponent::sectionReleased (int tick)
{
    if (! gesture)
        return;
    const auto g = *gesture;
    gesture.reset();
    sectionView.drag.reset();

    if (g.zone == SectionZone::Body)
        moveSections (doc, g.refs, tick - g.pressTick);
    else
        resizeSections (doc, g.refs, g.zone == SectionZone::LeftEdge ? SectionEdge::Left : SectionEdge::Right, tick);
    content.repaint();
}
```

A press that is released without dragging has `tick == pressTick`, so a pure click moves by 0 (no-op, no undo step) and an edge click resizes to roughly its own edge (a click within the 5 px slop but off the exact edge shifts the edge by that few ticks; acceptable, and the user can undo it). Because the modifiers are consumed by `selectTrack`, `sectionPressed` and the row/preview section-press callbacks carry no modifiers: drop the `const juce::ModifierKeys&` parameter from `onSectionPressed` on both `TrackNotePreview` and `TrackRowComponent`, and from the preview test (`preview.onSectionPressed = [&] (const SectionHit& h, int tick) {...}`).

The press order matters: the preview calls `onNonToggleClick` (which runs `selectTrack` and updates `selectedTrackIds`) BEFORE `onSectionPressed`, so `withCompanions` sees the updated track selection.

- [ ] **Step 4: Run to verify pass**

Run: `cmake --build build && ctest --test-dir build --output-on-failure 2>&1 | tail -5`
Expected: all PASS. If the synthetic `MouseEvent` in the preview test does not compile, build it the way `PlayheadOverlay_tests.cpp` does.

- [ ] **Step 5: Commit**

```bash
git add Source/UI Tests
git commit -m "feat(ui): draw, select, move and resize sections on the track strips

Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>"
```

---

### Task 7: Keys (`S`, `Delete`, Ctrl/Cmd+A), docs, deploy

**Files:**
- Modify: `Source/UI/TrackListComponent.h/.cpp`, `Source/UI/SongsmithMainComponent.h/.cpp`, `Source/UI/MainWindow.cpp`
- Modify docs: `docs/UI_GUIDE.md`, `docs/ARCHITECTURE.md`, `docs/TESTING.md`
- Test: `Tests/TrackListComponent_tests.cpp`, `Tests/SongsmithMainComponent_tests.cpp` (or the existing MainWindow key test file if one exists; grep `keyPressed` in `Tests/`)

**Interfaces:**
- Produces on `TrackListComponent`:
  - `void splitSections (std::optional<int> pointerTick, juce::int64 pointerTrackId)`: the tick/track under the pointer (empty when the pointer is not over a track's note strip); picks the split tick (pointer when over a section, else the marker, else nothing) and the tracks (selected tracks, else the pointer's track), then `splitAt`.
  - `void deleteSelectedSections()`: `deleteSections` on `sectionView.selected`, then clears it.
  - `bool splitAtPointer()`: reads `getMouseXYRelative()` and resolves the row/tick under the pointer, then calls `splitSections`; returns whether anything was done.
- `SongsmithMainComponent` forwards `splitSections()`, `deleteSections()`, `selectAllTracks()` to `trackList`.
- `MainWindow::keyPressed` handles plain `S`, `Delete`/`Backspace`, and Ctrl/Cmd+A, returning true only when it acted.

- [ ] **Step 1: Write the failing tests**

```cpp
TEST_CASE ("TrackListComponent: S splits at the pointer's tick on the selected tracks", "[track-list][sections]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SongDocument doc;
    auto a = playbacktest::addTrack (doc, "A");
    auto b = playbacktest::addTrack (doc, "B");
    playbacktest::addNote (a, 60, 0, 960);
    playbacktest::addNote (b, 60, 0, 960);
    const auto idA = (juce::int64) a.getProperty (SongIDs::trackId);
    const auto idB = (juce::int64) b.getProperty (SongIDs::trackId);
    TrackListComponent list (doc);
    list.setBounds (0, 0, 600, 300);
    TrackListComponentTestAccess::rebuild (list);
    list.selectAllTracks();

    list.splitSections (480, idA);

    CHECK (sectionsOf (a).size() == 2);
    CHECK (sectionsOf (b).size() == 2);
    CHECK (sectionsOf (a)[1].startTick == 480);
}

TEST_CASE ("TrackListComponent: S with no selection splits only the track under the pointer", "[track-list][sections]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SongDocument doc;
    auto a = playbacktest::addTrack (doc, "A");
    auto b = playbacktest::addTrack (doc, "B");
    playbacktest::addNote (a, 60, 0, 960);
    playbacktest::addNote (b, 60, 0, 960);
    TrackListComponent list (doc);
    list.setBounds (0, 0, 600, 300);
    TrackListComponentTestAccess::rebuild (list);

    list.splitSections (480, (juce::int64) b.getProperty (SongIDs::trackId));

    CHECK (sectionsOf (a).size() == 1);
    CHECK (sectionsOf (b).size() == 2);
}

TEST_CASE ("TrackListComponent: S falls back to the marker when the pointer is not over a strip, and does nothing without one", "[track-list][sections]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SongDocument doc;
    auto a = playbacktest::addTrack (doc, "A");
    playbacktest::addNote (a, 60, 0, 960);
    SynthVoice synth;
    PlaybackController playback (doc, synth);
    TrackListComponent list (doc);
    list.setPlayback (&playback);
    list.setBounds (0, 0, 600, 300);
    TrackListComponentTestAccess::rebuild (list);
    list.selectAllTracks();

    list.splitSections (std::nullopt, -1);
    CHECK (sectionsOf (a).size() == 1);   // no pointer, no marker: nothing at all
    CHECK_FALSE (doc.canUndo());

    playback.setMarkerTick (600.0);
    list.splitSections (std::nullopt, -1);
    CHECK (sectionsOf (a).size() == 2);
    CHECK (sectionsOf (a)[1].startTick == 600);
}

TEST_CASE ("TrackListComponent: Delete removes the selected sections in one undo step", "[track-list][sections]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SongDocument doc;
    auto a = playbacktest::addTrack (doc, "A");
    playbacktest::addNote (a, 60, 0, 960);
    const auto id = (juce::int64) a.getProperty (SongIDs::trackId);
    splitAt (doc, { id }, 480);
    TrackListComponent list (doc);
    list.setBounds (0, 0, 600, 300);
    TrackListComponentTestAccess::rebuild (list);

    TrackListComponentTestAccess::press (list, id, { sectionsOf (a)[1].id, SectionZone::Body }, 700);
    list.deleteSelectedSections();
    CHECK (sectionsOf (a).size() == 1);

    list.deleteSelectedSections();   // nothing selected any more: no-op
    CHECK (sectionsOf (a).size() == 1);
    doc.undo();
    CHECK (sectionsOf (a).size() == 2);
}
```

`MainWindow` is not unit-constructible, so test the forwarding in `Tests/SongsmithMainComponent_tests.cpp` (build the component the way its existing tests do): after importing two tracks, `selectAllTracks()` makes `getSelectedTrackIds()` reachable through a new `SongsmithMainComponent::getSelectedTrackIdsForTesting()` (add it, returning `trackList.getSelectedTrackIds()`) hold two ids; `deleteSections()` with no section selected leaves `canUndo()` false; `splitSections()` with the pointer outside the window and no marker leaves the document unchanged. The three `MainWindow::keyPressed` lines are one-line forwards and are verified by the user's manual check.

- [ ] **Step 2: Run to verify failure**

Run: `cmake --build build 2>&1 | grep error | head -5`
Expected: `splitSections`, `deleteSelectedSections` undeclared.

- [ ] **Step 3: Implement**

`TrackListComponent`:

```cpp
void TrackListComponent::splitSections (std::optional<int> pointerTick, juce::int64 pointerTrackId)
{
    std::optional<int> tick = pointerTick;
    if (! tick && playback != nullptr)
        if (const auto marker = playback->getMarkerTick())
            tick = (int) std::llround (*marker);
    if (! tick)
        return;

    std::vector<juce::int64> trackIds (selectedTrackIds.begin(), selectedTrackIds.end());
    if (trackIds.empty() && pointerTrackId != -1)
        trackIds.push_back (pointerTrackId);

    splitAt (doc, trackIds, *tick);
}

bool TrackListComponent::splitAtPointer()
{
    // The tick under the pointer, but only when it is over a track's note strip.
    std::optional<int> tick;
    juce::int64 trackId = -1;
    if (isMouseOver (true))
    {
        const auto inContent = content.getLocalPoint (this, getMouseXYRelative());
        for (auto* row : content.rows)
        {
            if (! row->getBounds().contains (inContent))
                continue;
            const auto inRow = inContent - row->getPosition();
            const auto strip = row->notePreviewForTesting().getBounds();
            if (strip.contains (inRow) && row->canDrag())
            {
                tick = timelineView.tickForX (inRow.x - strip.getX());
                trackId = row->getTrackId();
            }
        }
    }
    const auto before = doc.canUndo();
    splitSections (tick, trackId);
    return doc.canUndo() != before || tick.has_value();
}

void TrackListComponent::deleteSelectedSections()
{
    if (sectionView.selected.empty())
        return;
    deleteSections (doc, std::vector<SectionRef> (sectionView.selected.begin(), sectionView.selected.end()));
    sectionView.selected.clear();
    content.repaint();
}
```

(`notePreviewForTesting()` is fine to use here; or add a plain `getNotePreviewBounds()` accessor to `TrackRowComponent` and use that instead. The pointer rule: "over a section" means over a strip of a track that has sections, which `canDrag()` implies. Because sections cover each track from its first note to its last, a pointer over empty strip beyond the last note falls outside every section; `splitAt` then does nothing, as specified.)

`SongsmithMainComponent.h/.cpp`: add

```cpp
    bool splitSections()      { return trackList.splitAtPointer(); }
    void deleteSections()     { trackList.deleteSelectedSections(); }
    void selectAllTracks()    { trackList.selectAllTracks(); }
```

`MainWindow::keyPressed`, before `return false;`:

```cpp
    if (key == juce::KeyPress ('s'))                   { body->getSongsmith().splitSections();    return true; }
    if (key == juce::KeyPress (juce::KeyPress::deleteKey) || key == juce::KeyPress (juce::KeyPress::backspaceKey))
                                                       { body->getSongsmith().deleteSections();   return true; }
    if (key == juce::KeyPress ('a', cmd, 0))           { body->getSongsmith().selectAllTracks();  return true; }
```

Place them after the Cmd+S lines (a plain `S` has no modifiers, so `KeyPress ('s')` does not collide with Cmd+S). Text fields and the Track editor window get keys first, so typing `s` in a text field is unaffected.

- [ ] **Step 4: Run to verify pass**

Run: `cmake --build build && ctest --test-dir build --output-on-failure 2>&1 | tail -5`
Expected: all PASS.

- [ ] **Step 5: Docs, deploy, commit**

Update `docs/UI_GUIDE.md` (the track canvas keys and gestures: `S`, `Delete`, Ctrl/Cmd+A, click/Ctrl/Shift track selection, section click/drag/edge drag), `docs/ARCHITECTURE.md` (a short section: schema, `SectionEdit`, materialisation, selection and drag state in `TrackListComponent`, the "no mutation mid-drag" rule), `docs/TESTING.md` (the new test files and what they pin). Then:

```bash
./build-windows.sh forge_ui && cp build-windows/forge_ui_artefacts/Release/song-smith.exe /mnt/c/Apps/SongSmith/ && ls -l --time-style=full-iso /mnt/c/Apps/SongSmith/song-smith.exe; date
git add -A Source Tests docs CMakeLists.txt
git commit -m "feat(ui): S splits, Delete removes, Ctrl+A selects all on the track canvas

Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>"
```

Do not push; the user watches CI and approves pushes. Do not launch the GUI.
