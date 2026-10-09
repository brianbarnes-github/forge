# Songsmith MIDI Import Options Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Import ▸ MIDI offers two choices (replace/keep the tempo map; expand/merge the tracks), shown in a dialog when the new Preferences ▸ Import setting is "Ask".

**Architecture:** An `ImportOptions` struct flows `MainWindow` → `importMidiFile` → `appendImportedMidi` → `planMidiImport`. The planner (pure, headless-tested) decides the conductor and the merge (it returns one merged `PlannedTrack` carrying its own `Track`, renumbered event orders and remapped note links); the bridge writes it. A small modal `ImportOptionsDialog` collects the choices; a typed `AppSettings` key and a Preferences page decide whether the dialog appears.

**Tech Stack:** C++20, JUCE 8 (`juce_gui_basics`, `juce_data_structures`), Catch2, CMake/Ninja. No new dependencies.

**Spec:** `docs/superpowers/specs/2026-10-08-songsmith-import-options-design.md`

## Global Constraints

- All new logic lives in `Source/UI/`; `Source/Core/` is not touched (skill `forge-engine-ui-boundary`).
- Defaults of `ImportOptions` equal today's behaviour (`TempoMode::keep`, `TrackMode::expanded`); every existing caller and test must pass unchanged.
- Setting key `import.trackOptions`, stored `"ask"` / `"expanded"`; an unrecognised or missing value reads as `ask`; default is **Ask**.
- "Import Expanded Always" = no dialog, default `ImportOptions` (tempo map kept, tracks expanded).
- Merged track name = the file's stem + `" (merged)"`; `sourceProgram`, `sourceMidiChannel`, `defaultChannel` from the first note-bearing track; each note keeps its own MIDI `channel` and `isDrum`.
- A file with fewer than two note-bearing tracks is not merged (no rename).
- Import never touches the `UndoManager` (existing rule); the tempo replace follows it.
- Types on everything, no `any`; no new dependencies; conventional commits; commit trailer `Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>`; never push.
- Test files and UI `.cpp` files are listed explicitly: add each new `.cpp` to BOTH `CMakeLists.txt` (target `forge_ui`) and `Tests/CMakeLists.txt` (sources and test files).
- `juce::Button::triggerClick()` is asynchronous: tests call `button.onClick()` directly.
- Do not launch the GUI.

## Review Focus

1. A later import with `replace` whose PPQ differs from the Song's: the file's tempo/meter/conductor ticks must be rescaled to the Song's time base, not written raw (Task 3).
2. `merged` on a file with a single note-bearing track (or format 0): imports exactly as expanded, no " (merged)" name (Task 2).
3. `merged` over tracks that mix a drum channel and melodic channels: every note keeps its own `isDrum` and `channel`, nothing crashes (Task 2).
4. `merged` tracks with notes on the same tick: link orders are unique, ordered by (tick, source track, index), and the export contains exactly the original events (Tasks 2, 3).
5. Cancel / Escape / title-bar X in the dialog imports nothing and leaves the Song untouched; a hand-edited setting value falls back to Ask (Tasks 4, 5).

---

### Task 1: `ImportOptions` and tempo replace in the planner

**Files:**
- Modify: `Source/UI/MidiImportPlan.h` (options types, `planMidiImport` parameter)
- Modify: `Source/UI/MidiImportPlan.cpp` (conductor decision, Info diagnostic)
- Test: `Tests/MidiImportPlan_tests.cpp`

**Interfaces:**
- Produces: `enum class TempoMode { keep, replace }`, `enum class TrackMode { expanded, merged }`, `struct ImportOptions { TempoMode tempo = TempoMode::keep; TrackMode tracks = TrackMode::expanded; }`, and `MidiImportPlan planMidiImport (const Song&, const RawMidiFile&, bool isFirstImport, Diagnostics&, const ImportOptions& options = {})` in namespace `lotro`.

- [ ] **Step 1: Write the failing tests** — append to `Tests/MidiImportPlan_tests.cpp`

```cpp
TEST_CASE ("MidiImportPlan: tempo replace on a later import writes the file's conductor and drops nothing", "[midiimportplan]")
{
    TrackBody melody;
    melody.ev (0, { 0x90, 60, 100 }).ev (96, { 0x80, 60, 0x40 }).eot();
    const auto p = parse (smf (1, 96, { conductorBody(), melody }));

    ImportOptions options;
    options.tempo = TempoMode::replace;
    Diagnostics diags;
    const auto plan = planMidiImport (p.song, p.raw, false, diags, options);

    CHECK (plan.writesConductor);
    REQUIRE (plan.conductorEvents.size() == 2);
    CHECK (plan.conductorEvents[0].bytes == p.raw.tracks[0].events[0].bytes);
    CHECK (plan.conductorEndTick == 960);
    CHECK (plan.droppedEventCount == 0);
    REQUIRE (diags.size() == 1);
    CHECK (diags[0].severity == Severity::Info);
    CHECK (diags[0].source == "SongModelBridge");
    CHECK (diags[0].message.find ("Replaced") != std::string::npos);
}

TEST_CASE ("MidiImportPlan: tempo replace on a later import relocates a conductor-less file's song-wide metas", "[midiimportplan]")
{
    TrackBody first;
    first.ev (0, { 0xFF, 0x51, 0x03, 0x07, 0xA1, 0x20 }).ev (0, { 0xFF, 0x05, 0x02, 'l', 'a' })
         .ev (0, { 0x90, 60, 100 }).ev (96, { 0x80, 60, 0x40 }).eot();
    const auto p = parse (smf (1, 96, { first }));

    ImportOptions options;
    options.tempo = TempoMode::replace;
    Diagnostics diags;
    const auto plan = planMidiImport (p.song, p.raw, false, diags, options);

    CHECK (plan.writesConductor);
    REQUIRE (plan.conductorEvents.size() == 1);
    CHECK (plan.conductorEvents[0].relocatedFrom == 0);
    CHECK (plan.relocatedEventCount == 1);
    CHECK (plan.droppedEventCount == 0);
    REQUIRE (plan.tracks.size() == 1);
    REQUIRE (plan.tracks[0].events.size() == 1);   // the lyric stays on the track
}

TEST_CASE ("MidiImportPlan: tempo replace on the first import is identical to keep", "[midiimportplan]")
{
    TrackBody melody;
    melody.ev (0, { 0x90, 60, 100 }).ev (96, { 0x80, 60, 0x40 }).eot();
    const auto p = parse (smf (1, 96, { conductorBody(), melody }));

    ImportOptions options;
    options.tempo = TempoMode::replace;
    Diagnostics keepDiags, replaceDiags;
    const auto keep    = planMidiImport (p.song, p.raw, true, keepDiags);
    const auto replace = planMidiImport (p.song, p.raw, true, replaceDiags, options);

    CHECK (replace.writesConductor == keep.writesConductor);
    CHECK (replace.conductorEvents.size() == keep.conductorEvents.size());
    CHECK (replaceDiags.size() == keepDiags.size());   // no "Replaced" line when nothing existed
}
```

- [ ] **Step 2: Run to verify it fails**

Run: `cmake --build build 2>&1 | grep -E " error" | head`
Expected: FAIL — `ImportOptions` / `TempoMode` undeclared.

- [ ] **Step 3: Write minimal implementation**

`Source/UI/MidiImportPlan.h`, after `class MidiImportPlanError`:

```cpp
enum class TempoMode { keep, replace };
enum class TrackMode { expanded, merged };

// What Import > MIDI does beyond the default. The defaults are today's behaviour.
struct ImportOptions
{
    TempoMode tempo  = TempoMode::keep;     // replace: the file's conductor replaces the Song's
    TrackMode tracks = TrackMode::expanded; // merged: all the file's note tracks become one track
};
```

Change the declaration at the bottom:

```cpp
MidiImportPlan planMidiImport (const Song& song, const RawMidiFile& raw, bool isFirstImport,
                               Diagnostics& diagnostics, const ImportOptions& options = {});
```

`Source/UI/MidiImportPlan.cpp`: change the signature the same way, then

```cpp
    Diagnostics local; // only reaches `diagnostics` if planning succeeds
    MidiImportPlan plan;
    // A first import always writes the conductor; so does a replace.
    const bool writeConductor = isFirstImport || options.tempo == TempoMode::replace;
    plan.writesConductor = writeConductor;
```

and replace the two later `if (isFirstImport)` checks inside the raw-track loop (the conductor-track branch and the `isSongWideMetaEvent` branch) with `if (writeConductor)`. Leave the `isFirstImport` parameter's other uses (none) untouched. Just before the final `diagnostics.insert (...)`:

```cpp
    if (! isFirstImport && options.tempo == TempoMode::replace)
        info (local, "Replaced the Song's tempo map and conductor events with the imported file's");
```

- [ ] **Step 4: Run tests to verify they pass**

Run: `cmake --build build 2>&1 | grep -E " error"; ctest --test-dir build -R "MidiImportPlan" --output-on-failure 2>&1 | tail -5`
Expected: all MidiImportPlan tests pass (the new 3 and the existing ones unchanged).

- [ ] **Step 5: Commit**

```bash
git add Source/UI/MidiImportPlan.h Source/UI/MidiImportPlan.cpp Tests/MidiImportPlan_tests.cpp
git commit -m "feat(ui): ImportOptions and tempo replace in the MIDI import planner

Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>"
```

---

### Task 2: Merged tracks in the planner

**Files:**
- Modify: `Source/UI/MidiImportPlan.h` (`PlannedTrack` fields)
- Modify: `Source/UI/MidiImportPlan.cpp` (`mergeNoteTracks`, call site)
- Test: `Tests/MidiImportPlan_tests.cpp`

**Interfaces:**
- Consumes: `ImportOptions`, `TrackMode` (Task 1).
- Produces: `PlannedTrack::mergedTrack` (`std::optional<Track>`, the merged notes/metadata; notes parallel to `noteLinks`) and `PlannedTrack::mergedSongTrackIndices` (`std::vector<int>`, the `Song::tracks` indices that were folded in). For a merged track `songTrackIndex` is the first of them. Unmerged tracks leave both empty.

- [ ] **Step 1: Write the failing tests** — append to `Tests/MidiImportPlan_tests.cpp`

```cpp
namespace
{
    ImportOptions mergedOptions()
    {
        ImportOptions o;
        o.tracks = TrackMode::merged;
        return o;
    }
}

TEST_CASE ("MidiImportPlan: merged folds every note track into one, keeping channels and renumbering orders", "[midiimportplan]")
{
    TrackBody a;   // MIDI channel 1
    a.ev (0, { 0xFF, 0x03, 0x01, 'A' }).ev (0, { 0x90, 60, 100 }).ev (96, { 0x80, 60, 0x40 }).eot();
    TrackBody b;   // MIDI channel 3
    b.ev (0, { 0xFF, 0x03, 0x01, 'B' }).ev (0, { 0x92, 64, 90 }).ev (48, { 0x92, 67, 80 })
     .ev (48, { 0x82, 64, 0x40 }).ev (48, { 0x82, 67, 0x40 }).eot();
    const auto p = parse (smf (1, 96, { conductorBody(), a, b }));

    Diagnostics diags;
    const auto plan = planMidiImport (p.song, p.raw, true, diags, mergedOptions());

    REQUIRE (plan.tracks.size() == 1);
    const auto& t = plan.tracks[0];
    REQUIRE (t.mergedTrack.has_value());
    CHECK (t.mergedTrack->name == "t (merged)");
    CHECK (t.mergedSongTrackIndices == std::vector<int> { 0, 1 });
    CHECK (t.songTrackIndex == 0);
    CHECK (t.rawTrackIndex == 1);

    // Notes stable-sorted by start tick: A60@0, B64@0, B67@48.
    REQUIRE (t.mergedTrack->notes.size() == 3);
    CHECK (t.mergedTrack->notes[0].pitch == 60);
    CHECK (t.mergedTrack->notes[1].pitch == 64);
    CHECK (t.mergedTrack->notes[2].pitch == 67);
    REQUIRE (t.noteLinks.size() == 3);
    CHECK (t.noteLinks[0].channel == 1);
    CHECK (t.noteLinks[1].channel == 3);
    CHECK (t.noteLinks[2].channel == 3);

    // Raw events sorted by (tick, raw track, index): A.name 0, A.on 1, B.name 2, B.on64 3,
    // B.on67 4, A.off 5, B.off64 6, B.off67 7.
    CHECK (t.noteLinks[0].onOrder == 1);  CHECK (t.noteLinks[0].offOrder == 5);
    CHECK (t.noteLinks[1].onOrder == 3);  CHECK (t.noteLinks[1].offOrder == 6);
    CHECK (t.noteLinks[2].onOrder == 4);  CHECK (t.noteLinks[2].offOrder == 7);
    REQUIRE (t.events.size() == 2);       // the two track names
    CHECK (t.events[0].order == 0);
    CHECK (t.events[1].order == 2);

    const auto merged = std::any_of (diags.begin(), diags.end(), [] (const Diagnostic& d)
        { return d.severity == Severity::Info && d.message.find ("Merged 2") != std::string::npos; });
    CHECK (merged);
}

TEST_CASE ("MidiImportPlan: merged leaves note-less tracks alone and mixes drums with melody without losing flags", "[midiimportplan]")
{
    TrackBody melody;
    melody.ev (0, { 0x90, 60, 100 }).ev (96, { 0x80, 60, 0x40 }).eot();
    TrackBody drums;   // MIDI channel 10
    drums.ev (0, { 0x99, 36, 100 }).ev (48, { 0x89, 36, 0x40 }).eot();
    TrackBody markers; // no notes
    markers.ev (0, { 0xFF, 0x03, 0x01, 'X' }).eot();
    const auto p = parse (smf (1, 96, { conductorBody(), melody, markers, drums }));

    Diagnostics diags;
    const auto plan = planMidiImport (p.song, p.raw, true, diags, mergedOptions());

    REQUIRE (plan.tracks.size() == 2);                // merged track + the note-less one
    REQUIRE (plan.tracks[0].mergedTrack.has_value());
    CHECK_FALSE (plan.tracks[1].mergedTrack.has_value());
    CHECK (plan.tracks[1].songTrackIndex == -1);
    const auto& notes = plan.tracks[0].mergedTrack->notes;
    REQUIRE (notes.size() == 2);
    CHECK_FALSE (notes[0].isDrum);
    CHECK (notes[1].isDrum);
    CHECK (plan.tracks[0].noteLinks[1].channel == 10);
}

TEST_CASE ("MidiImportPlan: merged with fewer than two note tracks changes nothing", "[midiimportplan]")
{
    TrackBody melody;
    melody.ev (0, { 0x90, 60, 100 }).ev (96, { 0x80, 60, 0x40 }).eot();
    const auto p = parse (smf (1, 96, { conductorBody(), melody }));

    Diagnostics expandedDiags, mergedDiags;
    const auto expanded = planMidiImport (p.song, p.raw, true, expandedDiags);
    const auto merged   = planMidiImport (p.song, p.raw, true, mergedDiags, mergedOptions());

    REQUIRE (merged.tracks.size() == expanded.tracks.size());
    CHECK_FALSE (merged.tracks[0].mergedTrack.has_value());
    CHECK (merged.tracks[0].songTrackIndex == expanded.tracks[0].songTrackIndex);
    CHECK (merged.tracks[0].noteLinks.size() == expanded.tracks[0].noteLinks.size());
    CHECK (mergedDiags.size() == expandedDiags.size());
}
```

- [ ] **Step 2: Run to verify it fails**

Run: `cmake --build build 2>&1 | grep -E " error" | head`
Expected: FAIL — `mergedTrack` / `mergedSongTrackIndices` are not members of `PlannedTrack`.

- [ ] **Step 3: Write minimal implementation**

`Source/UI/MidiImportPlan.h`: add `#include <optional>` with the other includes and, at the end of `struct PlannedTrack`:

```cpp
    // Set only for a merged track (ImportOptions::tracks == merged): the
    // combined notes (parallel to noteLinks) and the Song::tracks indices they
    // came from. songTrackIndex is then the first of those.
    std::optional<Track>         mergedTrack;
    std::vector<int>             mergedSongTrackIndices;
```

`Source/UI/MidiImportPlan.cpp`: add `#include <algorithm>`, `#include <tuple>` and, in the anonymous namespace after `info (...)`:

```cpp
    // Folds every note-bearing planned track into one. `order` is per raw
    // track, so every raw event of the merged tracks is renumbered by
    // (tick, raw track, original index) and the note links and leftover events
    // are remapped through the same table; each source track's own relative
    // order is preserved. Fewer than two note tracks: nothing to merge.
    void mergeNoteTracks (const Song& song, const RawMidiFile& raw, MidiImportPlan& plan, Diagnostics& local)
    {
        std::vector<size_t> noteTracks;
        for (size_t p = 0; p < plan.tracks.size(); ++p)
            if (plan.tracks[p].songTrackIndex >= 0)
                noteTracks.push_back (p);
        if (noteTracks.size() < 2)
            return;

        struct Key { int tick; int rawTrack; int index; };
        std::vector<Key> keys;
        for (const auto p : noteTracks)
        {
            const int r = plan.tracks[p].rawTrackIndex;
            const auto& events = raw.tracks[(size_t) r].events;
            for (int i = 0; i < (int) events.size(); ++i)
                keys.push_back ({ events[(size_t) i].tick, r, i });
        }
        std::sort (keys.begin(), keys.end(), [] (const Key& a, const Key& b)
                   { return std::tie (a.tick, a.rawTrack, a.index) < std::tie (b.tick, b.rawTrack, b.index); });
        std::map<std::pair<int, int>, int> newOrder;
        for (int k = 0; k < (int) keys.size(); ++k)
            newOrder[{ keys[(size_t) k].rawTrack, keys[(size_t) k].index }] = k;

        const auto& first = plan.tracks[noteTracks.front()];
        PlannedTrack merged;
        merged.rawTrackIndex  = first.rawTrackIndex;
        merged.songTrackIndex = first.songTrackIndex;
        merged.defaultChannel = first.defaultChannel;

        Track track = song.tracks[(size_t) first.songTrackIndex]; // name, channel, program from the first
        track.notes.clear();
        track.name = (song.title.empty() ? std::string ("Merged") : song.title) + " (merged)";

        struct NoteAndLink { Note note; PlannedNoteLink link; };
        std::vector<NoteAndLink> pairs;
        for (const auto p : noteTracks)
        {
            const auto& planned = plan.tracks[p];
            const int r = planned.rawTrackIndex;
            merged.endTick = std::max (merged.endTick, planned.endTick);
            merged.mergedSongTrackIndices.push_back (planned.songTrackIndex);

            const auto& notes = song.tracks[(size_t) planned.songTrackIndex].notes;
            for (size_t n = 0; n < notes.size(); ++n)
            {
                auto link = planned.noteLinks[n];
                link.onOrder = newOrder.at ({ r, link.onOrder });
                if (! link.offSynthesized && link.offOrder >= 0)
                    link.offOrder = newOrder.at ({ r, link.offOrder });
                pairs.push_back ({ notes[n], link });
            }
            for (auto e : planned.events)
            {
                e.order = newOrder.at ({ r, e.order });
                merged.events.push_back (std::move (e));
            }
        }

        std::stable_sort (pairs.begin(), pairs.end(), [] (const NoteAndLink& a, const NoteAndLink& b)
                          { return a.note.startTick < b.note.startTick; });
        for (auto& pair : pairs)
        {
            track.notes.push_back (pair.note);
            merged.noteLinks.push_back (pair.link);
        }
        std::stable_sort (merged.events.begin(), merged.events.end(), [] (const PlannedEvent& a, const PlannedEvent& b)
                          { return std::tie (a.tick, a.order) < std::tie (b.tick, b.order); });
        merged.mergedTrack = std::move (track);

        std::vector<PlannedTrack> result;
        for (size_t p = 0; p < plan.tracks.size(); ++p)
        {
            if (p == noteTracks.front())
                result.push_back (merged);
            else if (std::find (noteTracks.begin(), noteTracks.end(), p) == noteTracks.end())
                result.push_back (std::move (plan.tracks[p]));
        }
        plan.tracks = std::move (result);

        info (local, "Merged " + std::to_string (noteTracks.size()) + " MIDI tracks into one track");
    }
```

In `planMidiImport`, directly after the raw-track `for` loop ends and before the `if (plan.relocatedEventCount > 0)` info lines:

```cpp
    if (options.tracks == TrackMode::merged)
        mergeNoteTracks (song, raw, plan, local);
```

- [ ] **Step 4: Run tests to verify they pass**

Run: `cmake --build build 2>&1 | grep -E " error"; ctest --test-dir build -R "MidiImportPlan" --output-on-failure 2>&1 | tail -5`
Expected: all MidiImportPlan tests pass, including the existing "every raw event ... exactly once" fixture test.

- [ ] **Step 5: Commit**

```bash
git add Source/UI/MidiImportPlan.h Source/UI/MidiImportPlan.cpp Tests/MidiImportPlan_tests.cpp
git commit -m "feat(ui): planner can merge a file's note tracks into one

Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>"
```

---

### Task 3: Bridge applies the options; integration tests

**Files:**
- Modify: `Source/UI/SongModelBridge.h`, `Source/UI/SongModelBridge.cpp`
- Create: `Tests/ImportOptions_tests.cpp`
- Modify: `Tests/CMakeLists.txt` (add `ImportOptions_tests.cpp` after `MidiFidelity_tests.cpp`)

**Interfaces:**
- Consumes: `ImportOptions`, `PlannedTrack::mergedTrack`, `PlannedTrack::mergedSongTrackIndices`, `MidiImportPlan::writesConductor` (Tasks 1–2).
- Produces: `bool appendImportedMidi (SongDocument&, const Song&, const RawMidiFile&, int importBatch, Diagnostics&, const Diagnostics& importerDiagnostics = {}, const ImportOptions& options = {})` and `bool importMidiFile (SongDocument&, const juce::File&, int importBatch, Diagnostics&, const ImportOptions& options = {})`.

- [ ] **Step 1: Write the failing tests** — create `Tests/ImportOptions_tests.cpp`

```cpp
// Import options end to end: tempo replace and track merge through importMidiFile,
// including the lossless export of a merged import.

#include "UI/MidiExport.h"
#include "UI/MidiImportPlan.h"
#include "UI/RawMidi.h"
#include "UI/SongModelBridge.h"
#include "MidiTestBytes.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <juce_core/juce_core.h>

#include <algorithm>
#include <tuple>
#include <utility>
#include <vector>

using namespace lotro;
using namespace miditest;

namespace
{
    struct TempMidi
    {
        juce::File file = juce::File::createTempFile (".mid");
        explicit TempMidi (const Bytes& bytes) { REQUIRE (file.replaceWithData (bytes.data(), bytes.size())); }
        ~TempMidi() { file.deleteFile(); }
    };

    // Tempo `microsPerQuarter` at tick 0 and, optionally, a second tempo at `changeTick`.
    TrackBody conductor (std::uint32_t microsPerQuarter, int changeTick = -1, std::uint32_t changeMicros = 0)
    {
        auto tempo = [] (std::uint32_t us) -> Bytes
        { return { 0xFF, 0x51, 0x03, (std::uint8_t) (us >> 16), (std::uint8_t) (us >> 8), (std::uint8_t) us }; };
        TrackBody c;
        const auto first = tempo (microsPerQuarter);
        c.ev (0, { first[0], first[1], first[2], first[3], first[4], first[5] });
        if (changeTick >= 0)
        {
            const auto second = tempo (changeMicros);
            c.ev ((std::uint32_t) changeTick, { second[0], second[1], second[2], second[3], second[4], second[5] });
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

    int noteTrackCount (const SongDocument& doc)
    {
        int n = 0;
        for (int i = 0; i < doc.getNumTracks(); ++i)
            if (SongDocument::isAssignableTrack (doc.getTrack (i)))
                ++n;
        return n;
    }

    bool containsMessage (const Diagnostics& diags, Severity severity, const std::string& text)
    {
        return std::any_of (diags.begin(), diags.end(), [&] (const Diagnostic& d)
                            { return d.severity == severity && d.message.find (text) != std::string::npos; });
    }

    constexpr std::uint32_t usFor120 = 500000, usFor100 = 600000;
}

TEST_CASE ("ImportOptions: tempo replace swaps the tempo map and conductor events, keep leaves them", "[import-options]")
{
    TempMidi first  (smf (1, 96, { conductor (usFor120), melody (60) }));
    TempMidi second (smf (1, 96, { conductor (usFor100), melody (64) }));

    SECTION ("replace")
    {
        SongDocument doc;
        Diagnostics d1, d2;
        REQUIRE (importMidiFile (doc, first.file, 1, d1));
        ImportOptions options;
        options.tempo = TempoMode::replace;
        REQUIRE (importMidiFile (doc, second.file, 2, d2, options));

        REQUIRE (doc.getTempoMapNode().getNumChildren() == 1);
        CHECK ((double) doc.getTempoMapNode().getChild (0).getProperty (SongIDs::bpm) == Catch::Approx (100.0));
        const auto events = SongDocument::getEventsNode (doc.getConductorTrack());
        REQUIRE (events.getNumChildren() == 1);   // the old conductor's event is gone, not duplicated
        CHECK (noteTrackCount (doc) == 2);        // both imports' tracks remain
        CHECK (containsMessage (d2, Severity::Info, "Replaced"));
        CHECK_FALSE (containsMessage (d2, Severity::Warning, "differs"));
    }

    SECTION ("keep (the default)")
    {
        SongDocument doc;
        Diagnostics d1, d2;
        REQUIRE (importMidiFile (doc, first.file, 1, d1));
        REQUIRE (importMidiFile (doc, second.file, 2, d2));

        CHECK ((double) doc.getTempoMapNode().getChild (0).getProperty (SongIDs::bpm) == Catch::Approx (120.0));
        CHECK (containsMessage (d2, Severity::Warning, "differs"));
    }
}

TEST_CASE ("ImportOptions: tempo replace rescales the file's ticks to the Song's time base", "[import-options]")
{
    // Song at 96 PPQ; the second file is 48 PPQ with a tempo change at its tick 48 (= 96 in the Song).
    TempMidi first  (smf (1, 96, { conductor (usFor120), melody (60) }));
    TempMidi second (smf (1, 48, { conductor (usFor120, 48, usFor100), melody (64) }));

    SongDocument doc;
    Diagnostics d1, d2;
    REQUIRE (importMidiFile (doc, first.file, 1, d1));
    ImportOptions options;
    options.tempo = TempoMode::replace;
    REQUIRE (importMidiFile (doc, second.file, 2, d2, options));

    CHECK ((int) doc.getSourceMidiNode().getProperty (SongIDs::ticksPerQuarter) == 96);
    const auto tempoMap = doc.getTempoMapNode();
    REQUIRE (tempoMap.getNumChildren() == 2);
    CHECK ((int) tempoMap.getChild (1).getProperty (SongIDs::tick) == 96);
    CHECK ((double) tempoMap.getChild (1).getProperty (SongIDs::bpm) == Catch::Approx (100.0));

    const auto events = SongDocument::getEventsNode (doc.getConductorTrack());
    REQUIRE (events.getNumChildren() == 2);
    CHECK ((int) events.getChild (1).getProperty (SongIDs::tick) == 96);
}

TEST_CASE ("ImportOptions: merged imports one track whose notes keep their channels and export losslessly", "[import-options]")
{
    TrackBody a;   // channel 1
    a.ev (0, { 0xFF, 0x03, 0x01, 'A' }).ev (0, { 0x90, 60, 100 }).ev (96, { 0x80, 60, 0x40 }).eot();
    TrackBody b;   // channel 3
    b.ev (0, { 0xFF, 0x03, 0x01, 'B' }).ev (0, { 0x92, 64, 90 }).ev (48, { 0x92, 67, 80 })
     .ev (48, { 0x82, 64, 0x40 }).ev (48, { 0x82, 67, 0x40 }).eot();
    const auto bytes = smf (1, 96, { conductor (usFor120), a, b });
    TempMidi tmp (bytes);

    SongDocument expanded, merged;
    Diagnostics de, dm;
    REQUIRE (importMidiFile (expanded, tmp.file, 1, de));
    ImportOptions options;
    options.tracks = TrackMode::merged;
    REQUIRE (importMidiFile (merged, tmp.file, 1, dm, options));

    CHECK (noteTrackCount (expanded) == 2);
    REQUIRE (noteTrackCount (merged) == 1);
    CHECK (merged.getNumTracks() == expanded.getNumTracks() - 1);

    juce::String name;
    for (int i = 0; i < merged.getNumTracks(); ++i)
        if (SongDocument::isAssignableTrack (merged.getTrack (i)))
            name = merged.getTrack (i).getProperty (SongIDs::name).toString();
    CHECK (name.endsWith (" (merged)"));

    // The same notes (pitch, start, duration, velocity, channel), just on one track.
    auto notesOf = [] (const SongDocument& doc)
    {
        std::vector<std::tuple<int, int, int, int, int>> out;
        for (auto track : doc.getSourceMidiNode())
            for (auto n : SongDocument::getNotesNode (track))
                out.emplace_back ((int) n.getProperty (SongIDs::pitch), (int) n.getProperty (SongIDs::startTick),
                                  (int) n.getProperty (SongIDs::durationTicks), (int) n.getProperty (SongIDs::velocity),
                                  (int) n.getProperty (SongIDs::channel, 1));
        std::sort (out.begin(), out.end());
        return out;
    };
    CHECK (notesOf (merged) == notesOf (expanded));

    // Export: conductor + one track holding exactly the original tracks' events.
    const auto original = readMidiBytes (bytes, "o");
    const auto exported = buildRawMidiFile (merged);
    REQUIRE (exported.tracks.size() == 2);
    std::vector<std::pair<int, Bytes>> want, got;   // compared as sorted multisets: same-tick order may differ
    for (size_t t = 1; t < original.tracks.size(); ++t)
        for (const auto& e : original.tracks[t].events)
            want.emplace_back (e.tick, e.bytes);
    for (const auto& e : exported.tracks[1].events)
        got.emplace_back (e.tick, e.bytes);
    std::sort (want.begin(), want.end());
    std::sort (got.begin(), got.end());
    CHECK (got == want);
    CHECK (std::is_sorted (exported.tracks[1].events.begin(), exported.tracks[1].events.end(),
                           [] (const RawMidiEvent& x, const RawMidiEvent& y) { return x.tick < y.tick; }));
}

TEST_CASE ("ImportOptions: merged on a single note track imports it unchanged", "[import-options]")
{
    TempMidi tmp (smf (1, 96, { conductor (usFor120), melody (60) }));
    SongDocument doc;
    Diagnostics diags;
    ImportOptions options;
    options.tracks = TrackMode::merged;
    REQUIRE (importMidiFile (doc, tmp.file, 1, diags, options));

    REQUIRE (noteTrackCount (doc) == 1);
    for (int i = 0; i < doc.getNumTracks(); ++i)
        if (SongDocument::isAssignableTrack (doc.getTrack (i)))
            CHECK_FALSE (doc.getTrack (i).getProperty (SongIDs::name).toString().endsWith (" (merged)"));
}
```

Add `ImportOptions_tests.cpp` to the test-file list in `Tests/CMakeLists.txt` directly after `MidiFidelity_tests.cpp`.

- [ ] **Step 2: Run to verify it fails**

Run: `cmake --build build 2>&1 | grep -E " error" | head`
Expected: FAIL — `importMidiFile` takes no options argument.

- [ ] **Step 3: Write minimal implementation**

`Source/UI/SongModelBridge.h`: include `"MidiImportPlan.h"` and change the two declarations (keep their doc comments, add one line to each: "`options` default to today's behaviour (keep the tempo map, expand the tracks)."):

```cpp
bool appendImportedMidi (SongDocument& doc, const Song& imported, const RawMidiFile& raw,
                         int importBatch, Diagnostics& diagnostics,
                         const Diagnostics& importerDiagnostics = {},
                         const ImportOptions& options = {});

bool importMidiFile (SongDocument& doc, const juce::File& midiFile, int importBatch,
                     Diagnostics& diagnostics, const ImportOptions& options = {});
```

`Source/UI/SongModelBridge.cpp`:

1. `appendImportedMidi`: add the `options` parameter; call `planMidiImport (imported, raw, isFirstImport, planDiagnostics, options)`; replace the row-mapping loop with:

```cpp
    for (int p = 0; p < (int) plan.tracks.size(); ++p)
    {
        const auto& planned = plan.tracks[(size_t) p];
        if (planned.mergedTrack.has_value())
        {
            for (const int songTrack : planned.mergedSongTrackIndices)
                rowForSongTrack[songTrack] = firstRow + p;   // every folded-in track lands on the merged row
        }
        else if (planned.songTrackIndex >= 0)
            rowForSongTrack[planned.songTrackIndex] = firstRow + p;
    }
```

2. `importMidiFile`: add the parameter and pass it: `appendImportedMidi (doc, imported, raw, importBatch, diagnostics, importerDiagnostics, options)`.

3. `appendImport`: right after `const bool isFirstImport = ...;` add

```cpp
        // A later import whose plan writes the conductor is a tempo replace.
        const bool replacingTempo = plan != nullptr && plan->writesConductor && ! isFirstImport;
```

In the planned-track loop replace the note-track branch with:

```cpp
                if (planned.songTrackIndex >= 0)
                {
                    const Track& source = planned.mergedTrack.has_value()
                                              ? *planned.mergedTrack
                                              : imported.tracks[(size_t) planned.songTrackIndex];
                    trackTree = addSongTrack (source, &planned.noteLinks);
                }
```

Replace the conductor block with:

```cpp
            if (plan->writesConductor) // first import, or a tempo replace
            {
                auto conductor = doc.getConductorTrack();
                auto conductorEvents = SongDocument::getEventsNode (conductor);
                if (replacingTempo)
                    conductorEvents.removeAllChildren (nullptr);   // the file's conductor replaces it wholesale
                appendEvents (conductorEvents, plan->conductorEvents);   // rescaled when the PPQs differ
                conductor.setProperty (SongIDs::endTick, rescaleOther (plan->conductorEndTick), nullptr);
            }
```

Change `if (isFirstImport)` (the tempo/meter map block) to `if (isFirstImport || replacingTempo)` and, at its top, add

```cpp
            auto tempoMapNode = doc.getTempoMapNode();
            auto meterMapNode = doc.getMeterMapNode();
            if (replacingTempo)
            {
                tempoMapNode.removeAllChildren (nullptr);
                meterMapNode.removeAllChildren (nullptr);
            }
            // The conductor events above already counted any inexact rescale, so the
            // map ticks are converted without a second count.
            const auto mapTick = [&] (int tick) { return needsRescale ? rescaleTick (tick, docPpq, importedPpq) : tick; };
```

using `mapTick (change.tick)` for the `tick` property of each `TEMPO_CHANGE` and `METER_CHANGE` (and delete the now-duplicated `auto tempoMapNode` / `auto meterMapNode` declarations inside that block). Update the comment above the `isFirstImport` definition's R1 paragraph with one sentence: "A tempo replace (ImportOptions::tempo == replace) overrides this: the file's maps replace the document's."

- [ ] **Step 4: Run tests to verify they pass**

Run: `cmake --build build 2>&1 | grep -E " error"; ctest --test-dir build 2>&1 | tail -4`
Expected: the whole suite passes (new import-options tests plus every existing bridge, planner and fidelity test, unchanged).

- [ ] **Step 5: Commit**

```bash
git add Source/UI/SongModelBridge.h Source/UI/SongModelBridge.cpp Tests/ImportOptions_tests.cpp Tests/CMakeLists.txt
git commit -m "feat(ui): bridge applies tempo replace and merged tracks on import

Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>"
```

---

### Task 4: `AppSettings` import setting

**Files:**
- Modify: `Source/UI/AppSettings.h`
- Test: `Tests/AppSettings_tests.cpp`

**Interfaces:**
- Produces: `enum class ImportTrackOptions { ask, expandedAlways }`; `AppSettings::importTrackOptions() const`, `AppSettings::setImportTrackOptions (ImportTrackOptions)`; `bool shouldAskImportOptions (const AppSettings&)`.

- [ ] **Step 1: Write the failing tests** — append to `Tests/AppSettings_tests.cpp`

```cpp
TEST_CASE ("AppSettings: import track options default to Ask and round-trip", "[app-settings]")
{
    auto file = juce::File::createTempFile (".settings");
    const juce::ScopeGuard cleanup { [&] { file.deleteFile(); } };
    juce::PropertiesFile props (file, {});
    AppSettings settings (props);
    CHECK (settings.importTrackOptions() == ImportTrackOptions::ask);
    CHECK (shouldAskImportOptions (settings));

    settings.setImportTrackOptions (ImportTrackOptions::expandedAlways);
    juce::PropertiesFile reader (file, {});   // written through while `props` is still alive
    CHECK (AppSettings (reader).importTrackOptions() == ImportTrackOptions::expandedAlways);
    CHECK_FALSE (shouldAskImportOptions (settings));

    settings.setImportTrackOptions (ImportTrackOptions::ask);
    CHECK (shouldAskImportOptions (settings));
}

TEST_CASE ("AppSettings: an unrecognised import setting reads as Ask and leaves the confirmations alone", "[app-settings]")
{
    auto file = juce::File::createTempFile (".settings");
    const juce::ScopeGuard cleanup { [&] { file.deleteFile(); } };
    juce::PropertiesFile props (file, {});
    props.setValue ("import.trackOptions", "banana");
    const AppSettings settings (props);
    CHECK (settings.importTrackOptions() == ImportTrackOptions::ask);
    CHECK (settings.askToSaveUnsavedChanges());
    CHECK (settings.askBeforeReplacingFile());
}
```

- [ ] **Step 2: Run to verify it fails**

Run: `cmake --build build 2>&1 | grep -E " error" | head`
Expected: FAIL — `ImportTrackOptions` undeclared.

- [ ] **Step 3: Write minimal implementation** — `Source/UI/AppSettings.h`

Before `class AppSettings`:

```cpp
// Preferences > Import > "Import Track Options".
enum class ImportTrackOptions
{
    ask,            // Import > MIDI shows the tempo map / track choices dialog
    expandedAlways  // no dialog: keep the tempo map, expand the tracks (today's behaviour)
};
```

In the public section after `setAskBeforeReplacingFile`:

```cpp
    ImportTrackOptions importTrackOptions() const
    {
        return file.getValue (keyImportTrackOptions) == "expanded" ? ImportTrackOptions::expandedAlways
                                                                   : ImportTrackOptions::ask;
    }
    void setImportTrackOptions (ImportTrackOptions value)
    {
        file.setValue (keyImportTrackOptions, value == ImportTrackOptions::expandedAlways ? "expanded" : "ask");
        file.saveIfNeeded();
    }
```

In the private section: `static constexpr const char* keyImportTrackOptions = "import.trackOptions";`. After `shouldConfirmReplace`:

```cpp
// Import > MIDI: show the options dialog only when the user wants asking.
inline bool shouldAskImportOptions (const AppSettings& settings)
{
    return settings.importTrackOptions() == ImportTrackOptions::ask;
}
```

- [ ] **Step 4: Run tests to verify they pass**

Run: `cmake --build build 2>&1 | grep -E " error"; ctest --test-dir build -R "AppSettings" --output-on-failure 2>&1 | tail -4`
Expected: all AppSettings tests pass.

- [ ] **Step 5: Commit**

```bash
git add Source/UI/AppSettings.h Tests/AppSettings_tests.cpp
git commit -m "feat(ui): AppSettings import track options (Ask / Import Expanded Always)

Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>"
```

---

### Task 5: `ImportOptionsDialog`

**Files:**
- Create: `Source/UI/ImportOptionsDialog.h`, `Source/UI/ImportOptionsDialog.cpp`
- Create: `Tests/ImportOptionsDialog_tests.cpp`
- Modify: `CMakeLists.txt` (add `Source/UI/ImportOptionsDialog.cpp` after `Source/UI/AboutBox.cpp`)
- Modify: `Tests/CMakeLists.txt` (add `ImportOptionsDialog_tests.cpp` after `PreferencesDialog_tests.cpp`, and `${CMAKE_SOURCE_DIR}/Source/UI/ImportOptionsDialog.cpp` after the `AboutBox.cpp` source line)

**Interfaces:**
- Consumes: `ImportOptions`, `TempoMode`, `TrackMode` (Task 1); `SongsmithColours`.
- Produces: `class ImportOptionsComponent : public juce::Component` with `ImportOptionsComponent (const juce::String& fileName, bool songHasTempoMap)`, `ImportOptions chosenOptions() const`, `std::function<void (const ImportOptions&)> onAccepted` (OK only), `std::function<void()> onCancelled`, and `...ForTesting()` accessors; `void showImportOptionsDialog (juce::Component* centreAround, const juce::String& fileName, bool songHasTempoMap, std::function<void (const ImportOptions&)> onAccepted)`.

- [ ] **Step 1: Write the failing tests** — create `Tests/ImportOptionsDialog_tests.cpp`

```cpp
#include "UI/ImportOptionsDialog.h"

#include <catch2/catch_test_macros.hpp>

using namespace lotro;

TEST_CASE ("ImportOptionsDialog: defaults are keep the tempo map and expand the tracks", "[import-options-dialog]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    ImportOptionsComponent dialog ("song.mid", true);
    const auto options = dialog.chosenOptions();
    CHECK (options.tempo == TempoMode::keep);
    CHECK (options.tracks == TrackMode::expanded);
}

TEST_CASE ("ImportOptionsDialog: the radios choose replace and merge", "[import-options-dialog]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    ImportOptionsComponent dialog ("song.mid", true);
    dialog.replaceTempoForTesting().setToggleState (true, juce::sendNotification);
    dialog.mergeTracksForTesting().setToggleState (true, juce::sendNotification);
    CHECK_FALSE (dialog.keepTempoForTesting().getToggleState());   // one radio group
    CHECK_FALSE (dialog.expandTracksForTesting().getToggleState());
    const auto options = dialog.chosenOptions();
    CHECK (options.tempo == TempoMode::replace);
    CHECK (options.tracks == TrackMode::merged);
}

TEST_CASE ("ImportOptionsDialog: the tempo group is disabled when the Song has no tempo map yet", "[import-options-dialog]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    ImportOptionsComponent first ("song.mid", false);
    CHECK_FALSE (first.keepTempoForTesting().isEnabled());
    CHECK_FALSE (first.replaceTempoForTesting().isEnabled());
    CHECK (first.expandTracksForTesting().isEnabled());
    CHECK (first.mergeTracksForTesting().isEnabled());

    ImportOptionsComponent later ("song.mid", true);
    CHECK (later.keepTempoForTesting().isEnabled());
    CHECK (later.replaceTempoForTesting().isEnabled());
}

TEST_CASE ("ImportOptionsDialog: OK delivers the chosen options, Cancel delivers nothing", "[import-options-dialog]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    ImportOptionsComponent dialog ("song.mid", true);
    int accepted = 0, cancelled = 0;
    ImportOptions got;
    dialog.onAccepted  = [&] (const ImportOptions& o) { ++accepted; got = o; };
    dialog.onCancelled = [&] { ++cancelled; };
    dialog.mergeTracksForTesting().setToggleState (true, juce::sendNotification);

    dialog.cancelButtonForTesting().onClick();
    CHECK (accepted == 0);
    CHECK (cancelled == 1);

    dialog.okButtonForTesting().onClick();
    CHECK (accepted == 1);
    CHECK (got.tracks == TrackMode::merged);
    CHECK (got.tempo == TempoMode::keep);
}
```

Add the two build-list entries described under **Files**.

- [ ] **Step 2: Run to verify it fails**

Run: `cmake --build build 2>&1 | grep -E "error" | head`
Expected: FAIL — `UI/ImportOptionsDialog.h` not found.

- [ ] **Step 3: Write minimal implementation**

`Source/UI/ImportOptionsDialog.h`:

```cpp
#pragma once

#include "MidiImportPlan.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

namespace lotro
{
    // The Import > MIDI choices: tempo map (keep / replace) and tracks (expand /
    // merge). OK delivers the options, Cancel only reports it; the window
    // closes itself either way (see showImportOptionsDialog).
    class ImportOptionsComponent : public juce::Component
    {
    public:
        // `songHasTempoMap` false (first import): the tempo choice is moot, so it is disabled.
        ImportOptionsComponent (const juce::String& fileName, bool songHasTempoMap);

        void paint (juce::Graphics& g) override;
        void resized() override;

        ImportOptions chosenOptions() const;

        std::function<void (const ImportOptions&)> onAccepted;
        std::function<void()>                      onCancelled;

        juce::ToggleButton& keepTempoForTesting()    { return keepTempo; }
        juce::ToggleButton& replaceTempoForTesting() { return replaceTempo; }
        juce::ToggleButton& expandTracksForTesting() { return expandTracks; }
        juce::ToggleButton& mergeTracksForTesting()  { return mergeTracks; }
        juce::TextButton&   okButtonForTesting()     { return okButton; }
        juce::TextButton&   cancelButtonForTesting() { return cancelButton; }

    private:
        juce::Label        heading, tempoLabel, tracksLabel;
        juce::ToggleButton keepTempo    { "Keep existing tempo map" };
        juce::ToggleButton replaceTempo { "Replace existing tempo map" };
        juce::ToggleButton expandTracks { "Expand into separate tracks" };
        juce::ToggleButton mergeTracks  { "Merge into one track" };
        juce::TextButton   okButton     { "OK" };
        juce::TextButton   cancelButton { "Cancel" };
    };

    // Opens the dialog fully modal. `onAccepted` runs only on OK; Cancel, Escape
    // and the title-bar X import nothing.
    void showImportOptionsDialog (juce::Component* centreAround, const juce::String& fileName,
                                  bool songHasTempoMap, std::function<void (const ImportOptions&)> onAccepted);
}
```

`Source/UI/ImportOptionsDialog.cpp`:

```cpp
#include "ImportOptionsDialog.h"
#include "SongsmithColours.h"

namespace lotro
{
    namespace
    {
        constexpr int dialogWidth  = 380;
        constexpr int dialogHeight = 270;
        constexpr int tempoGroup   = 7101;
        constexpr int tracksGroup  = 7102;

        void styleLabel (juce::Label& label, const juce::String& text, float height, juce::uint32 colour)
        {
            label.setText (text, juce::dontSendNotification);
            label.setFont (juce::FontOptions (height));
            label.setColour (juce::Label::textColourId, juce::Colour (colour));
        }
    }

    ImportOptionsComponent::ImportOptionsComponent (const juce::String& fileName, bool songHasTempoMap)
    {
        styleLabel (heading,     "Import " + fileName, 16.0f, SongsmithColours::text);
        styleLabel (tempoLabel,  songHasTempoMap ? "Tempo map" : "Tempo map (first import: the file's is used)",
                    13.0f, SongsmithColours::textMuted);
        styleLabel (tracksLabel, "Tracks", 13.0f, SongsmithColours::textMuted);
        for (auto* l : { &heading, &tempoLabel, &tracksLabel })
            addAndMakeVisible (*l);

        for (auto* b : { &keepTempo, &replaceTempo })
        {
            b->setRadioGroupId (tempoGroup);
            b->setEnabled (songHasTempoMap);
        }
        for (auto* b : { &expandTracks, &mergeTracks })
            b->setRadioGroupId (tracksGroup);
        for (auto* b : { &keepTempo, &replaceTempo, &expandTracks, &mergeTracks })
        {
            b->setColour (juce::ToggleButton::textColourId, juce::Colour (SongsmithColours::text));
            addAndMakeVisible (*b);
        }
        keepTempo.setToggleState (true, juce::dontSendNotification);
        expandTracks.setToggleState (true, juce::dontSendNotification);

        okButton.onClick     = [this] { if (onAccepted) onAccepted (chosenOptions()); };
        cancelButton.onClick = [this] { if (onCancelled) onCancelled(); };
        addAndMakeVisible (okButton);
        addAndMakeVisible (cancelButton);

        setSize (dialogWidth, dialogHeight);
    }

    ImportOptions ImportOptionsComponent::chosenOptions() const
    {
        ImportOptions options;
        options.tempo  = replaceTempo.getToggleState() ? TempoMode::replace : TempoMode::keep;
        options.tracks = mergeTracks.getToggleState()  ? TrackMode::merged  : TrackMode::expanded;
        return options;
    }

    void ImportOptionsComponent::paint (juce::Graphics& g)
    {
        g.fillAll (juce::Colour (SongsmithColours::background));
    }

    void ImportOptionsComponent::resized()
    {
        auto area = getLocalBounds().reduced (16);
        heading.setBounds (area.removeFromTop (26));
        area.removeFromTop (8);
        tempoLabel.setBounds (area.removeFromTop (20));
        keepTempo.setBounds (area.removeFromTop (24));
        replaceTempo.setBounds (area.removeFromTop (24));
        area.removeFromTop (8);
        tracksLabel.setBounds (area.removeFromTop (20));
        expandTracks.setBounds (area.removeFromTop (24));
        mergeTracks.setBounds (area.removeFromTop (24));

        auto buttons = area.removeFromBottom (32);
        cancelButton.setBounds (buttons.removeFromRight (80));
        buttons.removeFromRight (8);
        okButton.setBounds (buttons.removeFromRight (80));
    }

    void showImportOptionsDialog (juce::Component* centreAround, const juce::String& fileName,
                                  bool songHasTempoMap, std::function<void (const ImportOptions&)> onAccepted)
    {
        auto content = std::make_unique<ImportOptionsComponent> (fileName, songHasTempoMap);
        auto* component = content.get();

        juce::DialogWindow::LaunchOptions options;
        options.content.setOwned (content.release());
        options.dialogTitle = "MIDI File Import";
        options.dialogBackgroundColour = juce::Colour (SongsmithColours::background);
        options.componentToCentreAround = centreAround;
        options.useNativeTitleBar = true;
        options.resizable = false;
        auto* window = options.launchAsync();   // enters modal state

        // The window owns the content that owns these callbacks, so it cannot outlive `window`.
        component->onAccepted = [window, accepted = std::move (onAccepted)] (const ImportOptions& chosen)
        {
            window->exitModalState (1);
            if (accepted) accepted (chosen);
        };
        component->onCancelled = [window] { window->exitModalState (0); };
    }
}
```

- [ ] **Step 4: Run tests to verify they pass**

Run: `cmake --build build 2>&1 | grep -E " error"; ctest --test-dir build -R "ImportOptionsDialog" --output-on-failure 2>&1 | tail -5`
Expected: 4/4 pass. (`window->exitModalState` deletes the window asynchronously in JUCE, so calling `accepted` right after it is safe: the callback is a copy, but `accepted` is a lambda member of the component being torn down, which only happens on the next message-loop turn.)

- [ ] **Step 5: Commit**

```bash
git add Source/UI/ImportOptionsDialog.h Source/UI/ImportOptionsDialog.cpp Tests/ImportOptionsDialog_tests.cpp CMakeLists.txt Tests/CMakeLists.txt
git commit -m "feat(ui): ImportOptionsDialog for the tempo map and track choices

Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>"
```

---

### Task 6: Preferences ▸ Import page

**Files:**
- Create: `Source/UI/Preferences/ImportPreferencesPage.h`, `Source/UI/Preferences/ImportPreferencesPage.cpp`
- Modify: `Source/UI/Preferences/PreferencesDialog.cpp` (registry), `Tests/PreferencesDialog_tests.cpp`
- Modify: `CMakeLists.txt` and `Tests/CMakeLists.txt` (add `Source/UI/Preferences/ImportPreferencesPage.cpp` after `GeneralPreferencesPage.cpp` in both)

**Interfaces:**
- Consumes: `AppSettings`, `ImportTrackOptions` (Task 4); `preferencePages()` registry (existing).
- Produces: `class ImportPreferencesPage : public juce::Component` with `explicit ImportPreferencesPage (AppSettings&)` and `juce::ComboBox& trackOptionsForTesting()`; combo item ids 1 = Ask, 2 = Import Expanded Always.

- [ ] **Step 1: Write the failing tests** — edit `Tests/PreferencesDialog_tests.cpp`

Add `#include "UI/Preferences/ImportPreferencesPage.h"`. In the first test change the page-name check to:

```cpp
    CHECK (dialog.pageNames() == juce::StringArray { "General", "Import" });
```

Append:

```cpp
TEST_CASE ("PreferencesDialog: the Import page is the second page", "[preferences]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    Fixture f;
    PreferencesDialog dialog (f.settings);
    dialog.selectPage (1);
    CHECK (dialog.getSelectedPageIndex() == 1);
    CHECK (dynamic_cast<ImportPreferencesPage*> (dialog.currentPage()) != nullptr);
}

TEST_CASE ("Import page: the dropdown shows the saved choice and writes straight through", "[preferences]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    Fixture f;
    {
        ImportPreferencesPage page (f.settings);
        CHECK (page.trackOptionsForTesting().getSelectedId() == 1);   // Ask by default

        page.trackOptionsForTesting().setSelectedId (2, juce::sendNotification);
        CHECK (f.settings.importTrackOptions() == ImportTrackOptions::expandedAlways);

        page.trackOptionsForTesting().setSelectedId (1, juce::sendNotification);
        CHECK (f.settings.importTrackOptions() == ImportTrackOptions::ask);
    }

    f.settings.setImportTrackOptions (ImportTrackOptions::expandedAlways);
    ImportPreferencesPage reopened (f.settings);                     // a fresh page reflects the saved state
    CHECK (reopened.trackOptionsForTesting().getSelectedId() == 2);
}
```

Add the two build-list entries.

- [ ] **Step 2: Run to verify it fails**

Run: `cmake --build build 2>&1 | grep -E "error" | head`
Expected: FAIL — `ImportPreferencesPage.h` not found.

- [ ] **Step 3: Write minimal implementation**

`Source/UI/Preferences/ImportPreferencesPage.h`:

```cpp
#pragma once

#include "../AppSettings.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace lotro
{
    // Preferences > Import. The choice is applied (and saved) as soon as it changes.
    class ImportPreferencesPage : public juce::Component
    {
    public:
        explicit ImportPreferencesPage (AppSettings& settings);

        void resized() override;

        juce::ComboBox& trackOptionsForTesting() { return trackOptions; }

    private:
        juce::Label    trackOptionsLabel;
        juce::ComboBox trackOptions;
        juce::Label    note;
    };
}
```

`Source/UI/Preferences/ImportPreferencesPage.cpp`:

```cpp
#include "ImportPreferencesPage.h"
#include "../SongsmithColours.h"

namespace lotro
{
    namespace
    {
        constexpr int askId      = 1;
        constexpr int expandedId = 2;
    }

    ImportPreferencesPage::ImportPreferencesPage (AppSettings& settings)
    {
        trackOptionsLabel.setText ("Import Track Options", juce::dontSendNotification);
        trackOptionsLabel.setColour (juce::Label::textColourId, juce::Colour (SongsmithColours::text));
        addAndMakeVisible (trackOptionsLabel);

        trackOptions.addItem ("Ask", askId);
        trackOptions.addItem ("Import Expanded Always", expandedId);
        trackOptions.setSelectedId (settings.importTrackOptions() == ImportTrackOptions::ask ? askId : expandedId,
                                    juce::dontSendNotification);
        trackOptions.onChange = [this, &settings]
        {
            settings.setImportTrackOptions (trackOptions.getSelectedId() == expandedId
                                                ? ImportTrackOptions::expandedAlways
                                                : ImportTrackOptions::ask);
        };
        addAndMakeVisible (trackOptions);

        note.setText ("With Ask, every import shows the tempo map and track choices. "
                      "Merging into one track is only available through Ask.",
                      juce::dontSendNotification);
        note.setFont (juce::FontOptions (12.0f));
        note.setColour (juce::Label::textColourId, juce::Colour (SongsmithColours::textMuted));
        addAndMakeVisible (note);
    }

    void ImportPreferencesPage::resized()
    {
        auto area = getLocalBounds().reduced (12);
        trackOptionsLabel.setBounds (area.removeFromTop (22));
        trackOptions.setBounds (area.removeFromTop (26).removeFromLeft (240));
        area.removeFromTop (6);
        note.setBounds (area.removeFromTop (48));
    }
}
```

`Source/UI/Preferences/PreferencesDialog.cpp`: add `#include "ImportPreferencesPage.h"` and a second registry entry:

```cpp
            { "General", [] (AppSettings& s) -> std::unique_ptr<juce::Component> { return std::make_unique<GeneralPreferencesPage> (s); } },
            { "Import",  [] (AppSettings& s) -> std::unique_ptr<juce::Component> { return std::make_unique<ImportPreferencesPage> (s); } },
```

- [ ] **Step 4: Run tests to verify they pass**

Run: `cmake --build build 2>&1 | grep -E " error"; ctest --test-dir build -R "PreferencesDialog|Import page|General page" --output-on-failure 2>&1 | tail -6`
Expected: all pass (the first test's two-page expectation included).

- [ ] **Step 5: Commit**

```bash
git add Source/UI/Preferences Tests/PreferencesDialog_tests.cpp CMakeLists.txt Tests/CMakeLists.txt
git commit -m "feat(ui): Preferences Import page with the Import Track Options setting

Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>"
```

---

### Task 7: `MainWindow` wiring

**Files:**
- Modify: `Source/UI/MainWindow.h`, `Source/UI/MainWindow.cpp`

**Interfaces:**
- Consumes: `shouldAskImportOptions` (Task 4), `showImportOptionsDialog` (Task 5), `importMidiFile (…, options)` (Task 3).
- Produces: `MainWindow::importMidiWithOptions (const juce::File&, const ImportOptions&)` (private).

`MainWindow.cpp` is not in the test binary, so this task's check is that the UI target compiles plus the manual checklist in Task 8.

- [ ] **Step 1: Edit `MainWindow.h`** — add `#include "MidiImportPlan.h"` with the other includes and, directly under `void openMidiFromPath (const juce::File& file);`:

```cpp
    void importMidiWithOptions (const juce::File& file, const ImportOptions& options);
```

- [ ] **Step 2: Edit `MainWindow.cpp`** — add `#include "ImportOptionsDialog.h"` with the other includes and replace `openMidiFromPath`:

```cpp
void MainWindow::openMidiFromPath (const juce::File& file)
{
    // Import Expanded Always: no dialog, today's behaviour.
    if (! shouldAskImportOptions (appSettings))
    {
        importMidiWithOptions (file, ImportOptions {});
        return;
    }

    const bool songHasTempoMap = songDocument.getTempoMapNode().getNumChildren() > 0;
    const juce::Component::SafePointer<MainWindow> safe (this);
    showImportOptionsDialog (this, file.getFileName(), songHasTempoMap,
        [safe, file] (const ImportOptions& options)
        {
            if (safe != nullptr)
                safe->importMidiWithOptions (file, options);
        });
}

void MainWindow::importMidiWithOptions (const juce::File& file, const ImportOptions& options)
{
    Diagnostics diags;
    importMidiFile (songDocument, file, songDocument.mintImportBatch(), diags, options);
    body->getSongsmith().getDiagnostics().setDiagnostics (std::move (diags));
    body->getSongsmith().fitTrackTimelineToDocument();
}
```

- [ ] **Step 3: Build everything and run the suite**

Run: `cmake --build build 2>&1 | grep -E " error"; cmake --build build --target forge_ui 2>&1 | grep -E " error|Linking"; ctest --test-dir build 2>&1 | tail -3`
Expected: no errors; `forge_ui` links; the whole suite passes.

- [ ] **Step 4: Commit**

```bash
git add Source/UI/MainWindow.h Source/UI/MainWindow.cpp
git commit -m "feat(ui): Import MIDI shows the options dialog when Import Track Options is Ask

Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>"
```

---

### Task 8: Docs, test count and manual checklist

**Files:**
- Modify: `docs/ARCHITECTURE.md` (new §9.17, and the Import ▸ MIDI sentence near the File menu paragraph), `docs/UI_GUIDE.md` (Import ▸ MIDI line, Import dialog and Import page), `docs/TESTING.md` (count from `ctest --test-dir build -N | tail -1`; lines for `ImportOptions_tests`, `ImportOptionsDialog_tests`, the new planner/AppSettings/Preferences tests), `CLAUDE.md` (status line, File-menu sentence unchanged, docs-map row), `docs/superpowers/specs/2026-10-08-songsmith-import-options-design.md` (Status: implemented).

- [ ] **Step 1: Make the doc edits.** Content for ARCHITECTURE §9.17 ("Import options — `Source/UI/{MidiImportPlan,SongModelBridge,ImportOptionsDialog,AppSettings}`, `Source/UI/Preferences/ImportPreferencesPage.*`, `MainWindow`"): the `ImportOptions` flow (`MainWindow::openMidiFromPath` → `importMidiFile` → `appendImportedMidi` → `planMidiImport`); tempo replace (`writesConductor` on a later import, whole-conductor replacement, tick rescale to the Song's time base, the Info diagnostic, no `UndoManager` use); merge (`mergeNoteTracks`, renumbered orders, `mergedTrack` / `mergedSongTrackIndices`, fewer than two note tracks = no merge); the setting (`import.trackOptions`, Ask default) and the dialog; "Import Expanded Always" = no dialog and default options; the parts not headless-testable (`MainWindow` flow, modal behaviour). UI_GUIDE: Import ▸ MIDI line "→ opens the options dialog when Preferences ▸ Import is Ask", the dialog's four radios and OK/Cancel, the Import page. CLAUDE.md: status line gains "MIDI import options (tempo map keep/replace, expand/merge tracks, Preferences ▸ Import)" and a docs-map row for this spec and plan with "code walkthrough: `docs/ARCHITECTURE.md` §9.17".

- [ ] **Step 2: Run the full suite and record the count**

Run: `ctest --test-dir build 2>&1 | tail -3 && ctest --test-dir build -N | tail -1`
Expected: 100% passed; put the number in `docs/TESTING.md` line 3.

- [ ] **Step 3: Commit**

```bash
git add docs CLAUDE.md
git commit -m "docs: MIDI import options, Import preferences page and test count

Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>"
```

- [ ] **Step 4: Hand the manual checks to the user (lead only; no GUI launch by agents)** after the Windows deploy (`./build-windows.sh forge_ui && cp … /mnt/c/Apps/SongSmith/ …` per `CLAUDE.md`):
  1. Preferences ▸ Import shows "Import Track Options" = Ask by default; the tree lists General, Import.
  2. File ▸ Import ▸ MIDI… (and dropping a `.mid`) with Ask opens "MIDI File Import". On an empty Song the tempo group is greyed out.
  3. Cancel, Escape and the title-bar X import nothing and leave the Song untouched.
  4. OK with "Expand" imports as before; OK with "Merge into one track" yields one track named "<file> (merged)" that plays, and File ▸ Export ▸ MIDI gives one track holding all the notes on their original channels.
  5. Import a second file with "Replace existing tempo map": the Diagnostics show "Replaced the Song's tempo map…", the earlier tracks now play at the new tempo; with "Keep" the old tempo stays and the existing "differs" warning appears.
  6. Set Import Track Options to "Import Expanded Always": no dialog, today's behaviour; the choice survives a restart.

---

## Self-review

- **Spec coverage:** options struct and defaults (Task 1); tempo replace incl. whole-conductor replacement, rescale and Info diagnostic (Tasks 1, 3); merge incl. metadata, renumbered orders, <2 tracks, Info diagnostic (Task 2) and round-trip export (Task 3); `AppSettings` key and Ask default, unrecognised value (Task 4); dialog incl. disabled tempo group, Cancel (Task 5); Preferences ▸ Import (Task 6); flow through the shared `openMidiFromPath` for menu and drops (Task 7); docs, count, manual checklist, spec status (Task 8). Out-of-scope items (markers option, remembering the last choice, CLI, `Source/Core/`) are not planned.
- **Placeholder scan:** none; Task 8's doc step lists its content because the docs are descriptive and fully determined by Tasks 1–7.
- **Type consistency:** `TempoMode`, `TrackMode`, `ImportOptions`, `planMidiImport (…, options)`, `PlannedTrack::mergedTrack` / `mergedSongTrackIndices`, `appendImportedMidi (…, importerDiagnostics, options)`, `importMidiFile (…, options)`, `ImportTrackOptions`, `importTrackOptions()` / `setImportTrackOptions()`, `shouldAskImportOptions`, `ImportOptionsComponent` / `showImportOptionsDialog`, `ImportPreferencesPage`, `MainWindow::importMidiWithOptions` are spelled identically across tasks.
- **Review Focus:** each of the five lines has a test: 1 → Task 3 (rescale test); 2 → Tasks 2 and 3 (single note track); 3 → Task 2 (drums + melody); 4 → Tasks 2 and 3 (orders, export multiset); 5 → Task 5 (Cancel) and Task 4 (unrecognised value).
