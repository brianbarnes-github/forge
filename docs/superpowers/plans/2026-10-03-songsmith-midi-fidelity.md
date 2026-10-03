# Songsmith MIDI Fidelity Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Keep every event of an imported MIDI file in Songsmith's `SongDocument` (notes as editable `NOTE`s, everything else as raw `EVENT`s), always have a conductor track, and add File → Export MIDI… that writes the song's MIDI back out event-for-event.

**Architecture:** A JUCE-free SMF reader/writer (`Source/UI/RawMidi`) parses files losslessly. An exact replica of JUCE's note pairing (`Source/UI/JuceNoteReplica`) ties each `importMidi` note back to its raw note-on/off bytes. A pure planner (`Source/UI/MidiImportPlan`) applies the conductor rules and decides which raw events become `NOTE` links vs `EVENT`s. `SongModelBridge` writes that plan into the document. `Source/UI/MidiExport` rebuilds a `RawMidiFile` from the document. `forge_core` is not changed.

**Tech Stack:** C++17, JUCE 8 (`juce_data_structures`, `juce_gui_basics`; `juce_audio_basics` only in tests for the differential check), Catch2 v3, CMake + Ninja.

**Spec:** `docs/superpowers/specs/2026-10-03-songsmith-midi-fidelity-design.md` — read it first; this plan argues from it.

## Global Constraints

- `forge_core` (`Source/Core/**`) is not modified. No new fields on `Song`, `Track`, `Note`, `Config*`, `Constraints/*`.
- `RawMidi.{h,cpp}`, `JuceNoteReplica.{h,cpp}`, `MidiImportPlan.{h,cpp}` and `MidiExport.{h,cpp}` contain no JUCE includes (they may include `Core/MidiImporter.h` for `MidiImportError`, and `MidiImportPlan.cpp` may include `<juce_core/juce_core.h>` for `jassertfalse` only).
- CLI-vs-Songsmith ABC output stays byte-identical, and the post-import diagnostic counts stay equal (`Tests/SongsmithRoundTrip_tests.cpp`).
- Import is non-undoable (bulk path, `nullptr` UndoManager). User edits are one undo transaction per gesture.
- Synthetic ids only (`trackId`), never positional indices, for anything that outlives a call.
- Song-wide meta types: `0x51` tempo, `0x58` time signature, `0x59` key signature, `0x54` SMPTE offset, `0x06` marker, `0x02` copyright.
- "Has a conductor track" = format 1 and the first track contains no note-on with velocity > 0.
- Format 2 → Error diagnostic, document unchanged.
- New-note defaults: `channel` = track's `defaultChannel` (1 if absent), `offVelocity` = 64, `offIsNoteOnZero` = false, `offSynthesized` = false, no `onOrder`/`offOrder`.
- Export: always format 1; conductor first; PPQ = `SOURCE_MIDI.ticksPerQuarter`.
- Conventional commits (`feat:`, `fix:`, `refactor:`, `test:`, `docs:`), each ending with `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`. Never push.
- No new third-party dependencies.
- Never launch the GUI (`song-smith`) from an agent.

## Build / test commands

```bash
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug     # once
cmake --build build
ctest --test-dir build --output-on-failure            # whole suite (329 tests before this plan)
ctest --test-dir build -R "<test-name regex>" --output-on-failure
```

`ctest -R` matches test **names**, not Catch2 tags. Every test in this plan has a name prefix (`RawMidi:`, `JuceNoteReplica:`, `MidiImportPlan:`, `MidiImport:`, `MidiFidelity:`, …) so `-R "RawMidi:"` selects a task's tests.

## Review Focus

1. **A key re-struck within one tick in an unusual order** (e.g. `[on60, on62, off60]` at one tick, or `[on60 v100, off60, on60 v50, off60]`). Expected: every note links to the exact raw bytes JUCE paired it with, and export reproduces the file exactly. Pinned in Task 2 (hand cases + 500 seeded random files vs JUCE) and Task 7 (export of both hand cases).
2. **A second import at a different PPQ, then Export MIDI.** Expected: event and end-of-track ticks are rescaled along with notes (including an LCM raise of existing tracks and the conductor), and the exported file reads back cleanly with every event still present. Pinned in Task 6 and Task 7.
3. **Edit, then export, then re-import.** Moving, creating and deleting notes must produce a file that reads back as the same notes. Pinned in Task 7.
4. **Exporting an empty song** (no import, only the empty conductor). Expected: the menu item is disabled, and `buildRawMidiFile` still produces a valid one-track file. Pinned in Task 7 (builder test). The menu's enablement (`getNumTracks() > 1`) is in Task 9's `MainWindow` code, which isn't compiled into `forge_tests`, so the reviewer checks it by reading.
5. **A multi-channel track (format 0).** Expected: each note keeps its own channel on export, and notes created in the editor use the track's `defaultChannel` (its first channel event), not channel 1. Pinned in Task 5 (links), Task 7 (export) and Task 8 (new note).

---

## File structure

| File | Responsibility |
|---|---|
| `Source/UI/RawMidi.{h,cpp}` (new) | Lossless SMF reader/writer, plain C++ types. `MidiExportError`. |
| `Source/UI/JuceNoteReplica.{h,cpp}` (new) | Exact replica of `juce::MidiFile::readFrom(…, true)`'s per-track note-on view (reorder + pairing) over raw events. |
| `Source/UI/MidiImportPlan.{h,cpp}` (new) | Pure planner: conductor rules, note links, leftover events, parser-disagreement detection. |
| `Source/UI/MidiExport.{h,cpp}` (new) | `buildRawMidiFile(const SongDocument&)`. |
| `Source/UI/SongDocument.{h,cpp}` | New identifiers, `NOTES`/`EVENTS` containers, conductor, helpers. |
| `Source/UI/SongModelBridge.{h,cpp}` | `appendImportedMidi`, raw-aware `importMidiFile`, event rescale, conductor/note-less aware loops. |
| `Source/UI/SourceRollEditor.cpp`, `SourceTrackNoteSource.cpp`, `TrackNotePreview.cpp`, `TrackRowComponent.{h,cpp}`, `TrackListComponent.cpp`, `PianoRollComponent.cpp`, `AssignmentChipComponent.cpp` | Read notes via `getNotesNode`; conductor and note-less row behaviour. |
| `Source/UI/MainWindow.{h,cpp}` | File → Export MIDI…. |
| `CMakeLists.txt`, `Tests/CMakeLists.txt` | Register new sources and tests. |
| `Tests/MidiTestBytes.h` (new) | Byte-level SMF builders for tests. |
| `Tests/RawMidi_tests.cpp`, `JuceNoteReplica_tests.cpp`, `MidiImportPlan_tests.cpp`, `MidiImport_tests.cpp`, `MidiFidelity_tests.cpp` (new) | Tests per unit. |

---

### Task 1: Lossless SMF reader/writer (`RawMidi`)

**Files:**
- Create: `Source/UI/RawMidi.h`, `Source/UI/RawMidi.cpp`, `Tests/MidiTestBytes.h`, `Tests/RawMidi_tests.cpp`
- Modify: `CMakeLists.txt` (`target_sources(forge_ui …)`: add `Source/UI/RawMidi.cpp`), `Tests/CMakeLists.txt` (add `RawMidi_tests.cpp` to the test list and `${CMAKE_SOURCE_DIR}/Source/UI/RawMidi.cpp` to the sources; add `juce::juce_audio_basics` to `target_link_libraries(forge_tests …)`, needed for the JUCE track-count parity check)

**Interfaces:**
- Consumes: `lotro::MidiImportError` from `Source/Core/MidiImporter.h`.
- Produces:
  ```cpp
  namespace lotro {
  struct RawMidiEvent { int tick = 0; std::vector<std::uint8_t> bytes; bool operator== (const RawMidiEvent&) const; bool operator!= (const RawMidiEvent& o) const { return ! (*this == o); } };
  struct RawMidiTrack { std::vector<RawMidiEvent> events; int endTick = 0; bool operator== (const RawMidiTrack&) const; bool operator!= (const RawMidiTrack& o) const { return ! (*this == o); } };
  struct RawMidiFile  { int format = 1; int ticksPerQuarter = 480; std::vector<RawMidiTrack> tracks; bool operator== (const RawMidiFile&) const; bool operator!= (const RawMidiFile& o) const { return ! (*this == o); } };
  class MidiExportError : public std::runtime_error { public: using std::runtime_error::runtime_error; };
  RawMidiFile readMidiFile  (std::istream& input, std::string_view sourceName);           // throws MidiImportError
  RawMidiFile readMidiBytes (const std::vector<std::uint8_t>& bytes, std::string_view sourceName); // throws MidiImportError
  void writeMidiFile (const RawMidiFile& file, std::ostream& output);                     // throws MidiExportError
  std::vector<std::uint8_t> writeMidiBytes (const RawMidiFile& file);                     // throws MidiExportError
  }
  ```
  Event byte layout: channel message = explicit status + data bytes; meta = `FF type <data>` (no length); SysEx = `F0 <data>` / `F7 <data>` (no length). End-of-Track is never stored as an event; its tick is `endTick`.
- `Tests/MidiTestBytes.h` produces namespace `miditest` with `Bytes`, `vlq`, `be16`, `be32`, `chunk`, `TrackBody{ev, eot}`, `smf(format, ppq, tracks)`.

- [ ] **Step 1: Write the test helper header**

`Tests/MidiTestBytes.h`:

```cpp
#pragma once

// Byte-level Standard MIDI File builders for tests. TrackBody::ev writes the
// delta time and then the raw bytes exactly as given, so a test can omit a
// status byte to exercise running status.

#include <cstdint>
#include <initializer_list>
#include <vector>

namespace miditest
{
    using Bytes = std::vector<std::uint8_t>;

    inline void append (Bytes& out, const Bytes& more)
    {
        out.insert (out.end(), more.begin(), more.end());
    }

    inline Bytes vlq (std::uint32_t v)
    {
        Bytes reversed { (std::uint8_t) (v & 0x7F) };
        while ((v >>= 7) != 0)
            reversed.push_back ((std::uint8_t) ((v & 0x7F) | 0x80));
        return Bytes (reversed.rbegin(), reversed.rend());
    }

    inline Bytes be32 (std::uint32_t v)
    {
        return { (std::uint8_t) (v >> 24), (std::uint8_t) (v >> 16), (std::uint8_t) (v >> 8), (std::uint8_t) v };
    }

    inline Bytes be16 (std::uint32_t v)
    {
        return { (std::uint8_t) (v >> 8), (std::uint8_t) v };
    }

    inline Bytes chunk (const char* id, const Bytes& body)
    {
        Bytes out (id, id + 4);
        append (out, be32 ((std::uint32_t) body.size()));
        append (out, body);
        return out;
    }

    struct TrackBody
    {
        Bytes body;

        TrackBody& ev (std::uint32_t delta, std::initializer_list<std::uint8_t> raw)
        {
            append (body, vlq (delta));
            body.insert (body.end(), raw);
            return *this;
        }

        TrackBody& eot (std::uint32_t delta = 0) { return ev (delta, { 0xFF, 0x2F, 0x00 }); }
    };

    inline Bytes header (int format, int numTracks, int ppq)
    {
        Bytes body;
        append (body, be16 ((std::uint32_t) format));
        append (body, be16 ((std::uint32_t) numTracks));
        append (body, be16 ((std::uint32_t) ppq));
        return chunk ("MThd", body);
    }

    inline Bytes smf (int format, int ppq, const std::vector<TrackBody>& tracks)
    {
        Bytes out = header (format, (int) tracks.size(), ppq);
        for (const auto& t : tracks)
            append (out, chunk ("MTrk", t.body));
        return out;
    }
}
```

- [ ] **Step 2: Write the failing tests**

`Tests/RawMidi_tests.cpp`:

```cpp
// RawMidi: lossless SMF reader/writer. Pins event-for-event fidelity
// (order, explicit status, velocity-0 note-ons, SysEx, every meta type,
// End-of-Track tick) and track-structure parity with juce::MidiFile.

#include "UI/RawMidi.h"
#include "MidiTestBytes.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <juce_audio_basics/juce_audio_basics.h>

#include <fstream>
#include <sstream>

using namespace lotro;
using namespace miditest;

namespace
{
    juce::File midiFixture (const std::string& name)
    {
        return juce::File (__FILE__).getParentDirectory().getParentDirectory()
                   .getChildFile ("midi").getChildFile (name);
    }

    Bytes readFileBytes (const juce::File& f)
    {
        juce::MemoryBlock block;
        REQUIRE (f.loadFileAsData (block));
        const auto* p = static_cast<const std::uint8_t*> (block.getData());
        return Bytes (p, p + block.getSize());
    }
}

TEST_CASE ("RawMidi: running status, velocity-0 note-ons, SysEx and meta events are read verbatim", "[rawmidi]")
{
    TrackBody t;
    t.ev (0,  { 0xFF, 0x03, 0x02, 'H', 'i' })      // track name
     .ev (0,  { 0xF0, 0x03, 0x7E, 0x7F, 0xF7 })     // SysEx, length 3
     .ev (0,  { 0x90, 60, 100 })                     // note-on, explicit status
     .ev (10, { 62, 90 })                            // running status
     .ev (5,  { 60, 0 })                             // running status, velocity-0 note-on
     .ev (0,  { 0xFF, 0x7F, 0x01, 0x42 })            // meta does NOT cancel running status (JUCE)
     .ev (5,  { 62, 0 })
     .eot (20);

    const auto raw = readMidiBytes (smf (1, 480, { t }), "t");

    REQUIRE (raw.format == 1);
    REQUIRE (raw.ticksPerQuarter == 480);
    REQUIRE (raw.tracks.size() == 1);
    const auto& ev = raw.tracks[0].events;
    REQUIRE (ev.size() == 7);
    CHECK (ev[0].bytes == Bytes { 0xFF, 0x03, 'H', 'i' });
    CHECK (ev[1].bytes == Bytes { 0xF0, 0x7E, 0x7F, 0xF7 });
    CHECK (ev[2].bytes == Bytes { 0x90, 60, 100 });
    CHECK (ev[3].tick == 10);
    CHECK (ev[3].bytes == Bytes { 0x90, 62, 90 });
    CHECK (ev[4].tick == 15);
    CHECK (ev[4].bytes == Bytes { 0x90, 60, 0 });
    CHECK (ev[5].bytes == Bytes { 0xFF, 0x7F, 0x42 });
    CHECK (ev[6].tick == 20);
    CHECK (ev[6].bytes == Bytes { 0x90, 62, 0 });
    CHECK (raw.tracks[0].endTick == 40);
}

TEST_CASE ("RawMidi: write then read reproduces the file event-for-event", "[rawmidi]")
{
    RawMidiFile file;
    file.format = 1;
    file.ticksPerQuarter = 96;

    RawMidiTrack conductor;
    conductor.events = { { 0, { 0xFF, 0x51, 0x07, 0xA1, 0x20 } },
                         { 0, { 0xFF, 0x58, 0x04, 0x02, 0x18, 0x08 } },
                         { 0, { 0xF0, 0x41, 0x10, 0x42, 0xF7 } },
                         { 0, { 0xF7, 0x01, 0x02 } },                  // F7 escape packet
                         { 384, { 0xFF, 0x06, 'A' } } };               // marker
    conductor.endTick = 1000;                                          // trailing silence
    RawMidiTrack notes;
    notes.events = { { 0, { 0xC3, 0x19 } },
                     { 0, { 0xB3, 0x07, 0x64 } },
                     { 0, { 0x93, 60, 100 } },
                     { 96, { 0x83, 60, 0x40 } },
                     { 96, { 0xE3, 0x00, 0x40 } },
                     { 100, { 0xFF, 0x05, 'l', 'a' } },               // lyric
                     { 100, { 0xFF, 0x60, 0x01 } } };                 // unknown meta type
    notes.endTick = 100;
    file.tracks = { conductor, notes };

    const auto reread = readMidiBytes (writeMidiBytes (file), "w");
    CHECK (reread == file);
}

TEST_CASE ("RawMidi: a track without End-of-Track ends at its last event; events after End-of-Track are kept", "[rawmidi]")
{
    TrackBody noEot;
    noEot.ev (0, { 0x90, 60, 100 }).ev (50, { 0x80, 60, 0 });

    TrackBody afterEot;
    afterEot.ev (0, { 0x90, 60, 100 }).eot (10).ev (5, { 0x80, 60, 0 });

    const auto raw = readMidiBytes (smf (1, 96, { noEot, afterEot }), "t");
    CHECK (raw.tracks[0].endTick == 50);
    REQUIRE (raw.tracks[1].events.size() == 2);
    CHECK (raw.tracks[1].endTick == 10);
    CHECK (raw.tracks[1].events[1].tick == 15);
}

TEST_CASE ("RawMidi: a format-0 file keeps every channel in its single track", "[rawmidi]")
{
    TrackBody t;
    t.ev (0, { 0x90, 60, 100 }).ev (0, { 0x99, 36, 90 }).ev (0, { 0x94, 64, 80 })
     .ev (96, { 0x80, 60, 0 }).ev (0, { 0x89, 36, 0 }).ev (0, { 0x84, 64, 0 }).eot();

    const auto raw = readMidiBytes (smf (0, 96, { t }), "t");
    REQUIRE (raw.format == 0);
    REQUIRE (raw.tracks.size() == 1);
    CHECK (raw.tracks[0].events[1].bytes == Bytes { 0x99, 36, 90 });
    CHECK (raw.tracks[0].events[2].bytes == Bytes { 0x94, 64, 80 });
}

TEST_CASE ("RawMidi: a non-MTrk chunk consumes one of the header's track slots and is skipped", "[rawmidi]")
{
    TrackBody t;
    t.ev (0, { 0x90, 60, 100 }).ev (10, { 0x80, 60, 0 }).eot();

    Bytes bytes = header (1, 2, 96);
    append (bytes, chunk ("XFIH", { 1, 2, 3 }));
    append (bytes, chunk ("MTrk", t.body));

    const auto raw = readMidiBytes (bytes, "t");
    CHECK (raw.tracks.size() == 1);
}

TEST_CASE ("RawMidi: malformed input throws MidiImportError", "[rawmidi]")
{
    TrackBody good;
    good.ev (0, { 0x90, 60, 100 }).eot();

    SECTION ("empty input")             { CHECK_THROWS_AS (readMidiBytes ({}, "t"), MidiImportError); }
    SECTION ("not a MIDI header")       { CHECK_THROWS_AS (readMidiBytes (chunk ("MThx", { 0, 1, 0, 1, 0, 96 }), "t"), MidiImportError); }
    SECTION ("format above 2")          { Bytes b = header (3, 1, 96); append (b, chunk ("MTrk", good.body)); CHECK_THROWS_AS (readMidiBytes (b, "t"), MidiImportError); }
    SECTION ("format 0 with 2 tracks")  { CHECK_THROWS_AS (readMidiBytes (smf (0, 96, { good, good }), "t"), MidiImportError); }
    SECTION ("SMPTE time division")     { Bytes b = header (1, 1, 0xE728); append (b, chunk ("MTrk", good.body)); CHECK_THROWS_AS (readMidiBytes (b, "t"), MidiImportError); }
    SECTION ("chunk runs past the end") { Bytes b = header (1, 1, 96); append (b, be32 (0x4D54726B)); append (b, be32 (100)); append (b, { 0, 0x90 }); CHECK_THROWS_AS (readMidiBytes (b, "t"), MidiImportError); }
    SECTION ("fewer chunks than declared") { Bytes b = header (1, 2, 96); append (b, chunk ("MTrk", good.body)); CHECK_THROWS_AS (readMidiBytes (b, "t"), MidiImportError); }
    SECTION ("trailing bytes")          { Bytes b = smf (1, 96, { good }); b.push_back (0); CHECK_THROWS_AS (readMidiBytes (b, "t"), MidiImportError); }
    SECTION ("unterminated delta time") { TrackBody t; t.body = { 0x81, 0x81, 0x81, 0x81, 0x00 }; CHECK_THROWS_AS (readMidiBytes (smf (1, 96, { t }), "t"), MidiImportError); }
    SECTION ("data byte, no running status") { TrackBody t; t.ev (0, { 60, 100 }); CHECK_THROWS_AS (readMidiBytes (smf (1, 96, { t }), "t"), MidiImportError); }
    SECTION ("truncated channel message") { TrackBody t; t.ev (0, { 0x90, 60 }); CHECK_THROWS_AS (readMidiBytes (smf (1, 96, { t }), "t"), MidiImportError); }
    SECTION ("truncated meta")          { TrackBody t; t.ev (0, { 0xFF, 0x03, 0x05, 'a' }); CHECK_THROWS_AS (readMidiBytes (smf (1, 96, { t }), "t"), MidiImportError); }
    SECTION ("system common byte")      { TrackBody t; t.ev (0, { 0xF2, 0x00, 0x00 }); CHECK_THROWS_AS (readMidiBytes (smf (1, 96, { t }), "t"), MidiImportError); }
}

TEST_CASE ("RawMidi: the writer rejects events out of tick order and empty events", "[rawmidi]")
{
    RawMidiFile file;
    RawMidiTrack t;

    SECTION ("out of order")
    {
        t.events = { { 10, { 0x90, 60, 100 } }, { 5, { 0x80, 60, 0 } } };
        file.tracks = { t };
        CHECK_THROWS_AS (writeMidiBytes (file), MidiExportError);
    }
    SECTION ("empty event")
    {
        t.events = { { 0, {} } };
        file.tracks = { t };
        CHECK_THROWS_AS (writeMidiBytes (file), MidiExportError);
    }
}

TEST_CASE ("RawMidi: writeMidiFile throws MidiExportError when the stream fails", "[rawmidi]")
{
    std::ofstream bad;  // never opened
    RawMidiFile file;
    file.tracks.push_back ({});
    CHECK_THROWS_AS (writeMidiFile (file, bad), MidiExportError);
}

TEST_CASE ("RawMidi: every tracked fixture reads, writes and reads back identically, with JUCE's track count", "[rawmidi]")
{
    auto name = GENERATE (as<std::string>{},
        "Barnes Brothers Band - Pull The Wires.mid", "angels.mid", "anymore.mid", "blue.mid",
        "hold.mid", "land.mid", "leah.mid", "nobody.mid", "right.mid", "state.mid", "syn5.mid", "tellit.mid");

    DYNAMIC_SECTION (name)
    {
        const auto bytes = readFileBytes (midiFixture (name));
        const auto raw = readMidiBytes (bytes, name);

        juce::MidiFile juceFile;
        juce::MemoryInputStream in (bytes.data(), bytes.size(), false);
        REQUIRE (juceFile.readFrom (in, false));
        CHECK ((int) raw.tracks.size() == juceFile.getNumTracks());

        CHECK (readMidiBytes (writeMidiBytes (raw), name) == raw);
    }
}
```

- [ ] **Step 3: Register files and run the tests to verify they fail**

Add `RawMidi_tests.cpp` to the `add_executable(forge_tests …)` list and `${CMAKE_SOURCE_DIR}/Source/UI/RawMidi.cpp` to its sources; add `juce::juce_audio_basics` to `target_link_libraries(forge_tests PRIVATE …)`. Add `Source/UI/RawMidi.cpp` to `target_sources(forge_ui PRIVATE …)` in `CMakeLists.txt`. Create empty `Source/UI/RawMidi.cpp` so CMake configures.

Run: `cmake --build build`
Expected: compile FAIL (`UI/RawMidi.h` not found).

- [ ] **Step 4: Write the header**

`Source/UI/RawMidi.h`:

```cpp
#pragma once

// Lossless Standard MIDI File reader/writer for Songsmith's MIDI-fidelity
// path (docs/superpowers/specs/2026-10-03-songsmith-midi-fidelity-design.md).
// JUCE-free on purpose: juce::MidiFile reorders note events within a tick
// and invents note-offs, so it can't give an exact copy. Track structure
// deliberately matches juce::MidiFile::readFrom (RIFF wrapper, non-MTrk
// chunks consuming a track slot, meta/SysEx not cancelling running status)
// so track indices line up with importMidi's sourceTrackIndex.

#include "Core/MidiImporter.h" // MidiImportError

#include <cstdint>
#include <iosfwd>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace lotro
{

// One event at an absolute tick. bytes layout:
//   channel message: explicit status byte + data bytes (running status resolved)
//   meta event:      FF <type> <data>        (length not stored)
//   SysEx:           F0 <data> or F7 <data>  (length not stored)
struct RawMidiEvent
{
    int                       tick = 0;
    std::vector<std::uint8_t> bytes;

    bool operator== (const RawMidiEvent& other) const;
    bool operator!= (const RawMidiEvent& other) const { return ! (*this == other); }
};

struct RawMidiTrack
{
    std::vector<RawMidiEvent> events;  // file order; End-of-Track is never stored here
    int                       endTick = 0; // first End-of-Track's tick, else the last event's tick

    bool operator== (const RawMidiTrack& other) const;
    bool operator!= (const RawMidiTrack& other) const { return ! (*this == other); }
};

struct RawMidiFile
{
    int                       format          = 1;
    int                       ticksPerQuarter = 480;
    std::vector<RawMidiTrack> tracks;

    bool operator== (const RawMidiFile& other) const;
    bool operator!= (const RawMidiFile& other) const { return ! (*this == other); }
};

class MidiExportError : public std::runtime_error
{
public:
    using std::runtime_error::runtime_error;
};

// Both throw MidiImportError on malformed input or SMPTE time division.
RawMidiFile readMidiFile  (std::istream& input, std::string_view sourceName);
RawMidiFile readMidiBytes (const std::vector<std::uint8_t>& bytes, std::string_view sourceName);

// Writes every status byte explicitly (no running-status compression) and one
// End-of-Track per track at max(endTick, last event tick). Both throw
// MidiExportError on an empty event, events out of tick order, or (for
// writeMidiFile) a failed stream.
std::vector<std::uint8_t> writeMidiBytes (const RawMidiFile& file);
void                      writeMidiFile  (const RawMidiFile& file, std::ostream& output);

} // namespace lotro
```

- [ ] **Step 5: Write the implementation**

`Source/UI/RawMidi.cpp`:

```cpp
#include "RawMidi.h"

#include <istream>
#include <iterator>
#include <limits>
#include <ostream>
#include <string>

namespace lotro
{

bool RawMidiEvent::operator== (const RawMidiEvent& other) const
{
    return tick == other.tick && bytes == other.bytes;
}

bool RawMidiTrack::operator== (const RawMidiTrack& other) const
{
    return endTick == other.endTick && events == other.events;
}

bool RawMidiFile::operator== (const RawMidiFile& other) const
{
    return format == other.format && ticksPerQuarter == other.ticksPerQuarter && tracks == other.tracks;
}

namespace
{
    constexpr std::uint32_t kMThd = 0x4D546864; // "MThd"
    constexpr std::uint32_t kMTrk = 0x4D54726B; // "MTrk"
    constexpr std::uint32_t kRIFF = 0x52494646; // "RIFF"

    class Reader
    {
    public:
        Reader (const std::uint8_t* d, size_t n, std::string_view sourceName)
            : data (d), size (n), name (sourceName) {}

        [[noreturn]] void fail (const std::string& why) const
        {
            throw MidiImportError ("Malformed MIDI data (" + why + "): " + std::string (name));
        }

        size_t remaining() const { return size - pos; }

        void need (size_t n) const
        {
            if (n > remaining())
                fail ("unexpected end of data");
        }

        void skip (size_t n) { need (n); pos += n; }

        Reader sub (size_t n)
        {
            need (n);
            Reader r (data + pos, n, name);
            pos += n;
            return r;
        }

        std::uint8_t peek() const { need (1); return data[pos]; }
        std::uint8_t u8()         { need (1); return data[pos++]; }
        std::uint32_t u16()       { const std::uint32_t hi = u8(); return (hi << 8) | u8(); }
        std::uint32_t u32()       { const std::uint32_t hi = u16(); return (hi << 16) | u16(); }

        std::uint32_t vlq()
        {
            std::uint32_t value = 0;
            for (int i = 0; i < 4; ++i)
            {
                const auto b = u8();
                value = (value << 7) | (b & 0x7Fu);
                if ((b & 0x80u) == 0)
                    return value;
            }
            fail ("unterminated variable-length number");
        }

        std::vector<std::uint8_t> bytes (size_t n)
        {
            need (n);
            std::vector<std::uint8_t> out (data + pos, data + pos + n);
            pos += n;
            return out;
        }

    private:
        const std::uint8_t* data;
        size_t              size;
        size_t              pos = 0;
        std::string_view    name;
    };

    int channelDataLength (std::uint8_t status)
    {
        const int type = status & 0xF0;
        return (type == 0xC0 || type == 0xD0) ? 1 : 2;
    }

    RawMidiTrack readTrack (Reader r)
    {
        RawMidiTrack track;
        bool sawEndOfTrack = false;
        std::uint8_t runningStatus = 0;
        long long tick = 0;

        while (r.remaining() > 0)
        {
            tick += r.vlq();
            if (tick > std::numeric_limits<int>::max())
                r.fail ("tick overflow");

            RawMidiEvent event;
            event.tick = (int) tick;
            std::uint8_t status = r.peek();

            if (status == 0xFF)
            {
                r.u8();
                const auto type    = r.u8();
                const auto length  = r.vlq();
                const auto payload = r.bytes (length);

                if (type == 0x2F)
                {
                    if (! sawEndOfTrack)
                    {
                        track.endTick = event.tick;
                        sawEndOfTrack = true;
                    }
                    continue;
                }

                event.bytes.reserve (2 + payload.size());
                event.bytes.push_back (0xFF);
                event.bytes.push_back (type);
                event.bytes.insert (event.bytes.end(), payload.begin(), payload.end());
            }
            else if (status == 0xF0 || status == 0xF7)
            {
                r.u8();
                const auto length  = r.vlq();
                const auto payload = r.bytes (length);
                event.bytes.reserve (1 + payload.size());
                event.bytes.push_back (status);
                event.bytes.insert (event.bytes.end(), payload.begin(), payload.end());
            }
            else if (status >= 0xF0)
            {
                r.fail ("unsupported system message in a track");
            }
            else
            {
                // Meta and SysEx deliberately leave runningStatus alone:
                // juce::MidiFile's readTrack does the same, and matching it
                // keeps both parsers agreeing on every file importMidi accepts.
                if ((status & 0x80) != 0)
                {
                    r.u8();
                    runningStatus = status;
                }
                else if (runningStatus == 0)
                {
                    r.fail ("data byte with no running status");
                }
                else
                {
                    status = runningStatus;
                }

                event.bytes.push_back (status);
                for (int i = 0; i < channelDataLength (status); ++i)
                    event.bytes.push_back (r.u8());
            }

            if (! sawEndOfTrack)
                track.endTick = event.tick;

            track.events.push_back (std::move (event));
        }

        return track;
    }

    void putVlq (std::vector<std::uint8_t>& out, std::uint32_t v)
    {
        std::uint8_t buffer[5];
        int n = 0;
        buffer[n++] = (std::uint8_t) (v & 0x7F);
        while ((v >>= 7) != 0)
            buffer[n++] = (std::uint8_t) ((v & 0x7F) | 0x80);
        while (n > 0)
            out.push_back (buffer[--n]);
    }

    void put32 (std::vector<std::uint8_t>& out, std::uint32_t v)
    {
        out.push_back ((std::uint8_t) (v >> 24));
        out.push_back ((std::uint8_t) (v >> 16));
        out.push_back ((std::uint8_t) (v >> 8));
        out.push_back ((std::uint8_t) v);
    }

    void put16 (std::vector<std::uint8_t>& out, std::uint32_t v)
    {
        out.push_back ((std::uint8_t) (v >> 8));
        out.push_back ((std::uint8_t) v);
    }
}

RawMidiFile readMidiBytes (const std::vector<std::uint8_t>& bytes, std::string_view sourceName)
{
    const std::string name (sourceName);
    if (bytes.empty())
        throw MidiImportError ("MIDI input is empty: " + name);

    Reader r (bytes.data(), bytes.size(), sourceName);

    // Mirrors juce::MidiFile: a RIFF (RMID) wrapper is searched for "MThd"
    // within its next eight words.
    auto id = r.u32();
    if (id == kRIFF)
    {
        bool found = false;
        for (int i = 0; i < 8 && ! found; ++i)
            found = (r.u32() == kMThd);
        if (! found)
            r.fail ("no MThd header");
    }
    else if (id != kMThd)
    {
        r.fail ("no MThd header");
    }

    // Like JUCE, the declared header length is only validated, never used to
    // skip extra header bytes.
    if (r.u32() > r.remaining())
        r.fail ("header length past end of data");

    RawMidiFile file;
    file.format = (int) r.u16();
    if (file.format > 2)
        r.fail ("unknown format " + std::to_string (file.format));

    const int numTracks = (int) r.u16();
    if (file.format == 0 && numTracks != 1)
        r.fail ("format 0 must have exactly one track");

    const auto division = r.u16();
    if (division == 0 || (division & 0x8000) != 0)
        throw MidiImportError ("SMPTE time format is not supported (time format "
                               + std::to_string ((short) division) + "): " + name);
    file.ticksPerQuarter = (int) division;

    for (int t = 0; t < numTracks; ++t)
    {
        const auto chunkId = r.u32();
        const auto length  = r.u32();
        auto body = r.sub (length);
        if (chunkId == kMTrk)
            file.tracks.push_back (readTrack (body));
    }

    if (r.remaining() != 0)
        r.fail ("trailing bytes after the last chunk");

    return file;
}

RawMidiFile readMidiFile (std::istream& input, std::string_view sourceName)
{
    if (! input.good())
        throw MidiImportError ("MIDI input stream is not readable: " + std::string (sourceName));

    const std::vector<std::uint8_t> bytes ((std::istreambuf_iterator<char> (input)),
                                           std::istreambuf_iterator<char>());
    return readMidiBytes (bytes, sourceName);
}

std::vector<std::uint8_t> writeMidiBytes (const RawMidiFile& file)
{
    std::vector<std::uint8_t> out;
    put32 (out, kMThd);
    put32 (out, 6);
    put16 (out, (std::uint32_t) file.format);
    put16 (out, (std::uint32_t) file.tracks.size());
    put16 (out, (std::uint32_t) file.ticksPerQuarter);

    for (const auto& track : file.tracks)
    {
        std::vector<std::uint8_t> body;
        int lastTick = 0;

        for (const auto& event : track.events)
        {
            if (event.bytes.empty())
                throw MidiExportError ("Cannot write an empty MIDI event");
            if (event.tick < lastTick)
                throw MidiExportError ("MIDI events are out of tick order");

            putVlq (body, (std::uint32_t) (event.tick - lastTick));
            lastTick = event.tick;

            const auto status = event.bytes[0];
            if (status == 0xFF)
            {
                if (event.bytes.size() < 2)
                    throw MidiExportError ("Meta event without a type byte");
                body.push_back (0xFF);
                body.push_back (event.bytes[1]);
                putVlq (body, (std::uint32_t) (event.bytes.size() - 2));
                body.insert (body.end(), event.bytes.begin() + 2, event.bytes.end());
            }
            else if (status == 0xF0 || status == 0xF7)
            {
                body.push_back (status);
                putVlq (body, (std::uint32_t) (event.bytes.size() - 1));
                body.insert (body.end(), event.bytes.begin() + 1, event.bytes.end());
            }
            else
            {
                body.insert (body.end(), event.bytes.begin(), event.bytes.end());
            }
        }

        const int endTick = std::max (track.endTick, lastTick);
        putVlq (body, (std::uint32_t) (endTick - lastTick));
        body.push_back (0xFF);
        body.push_back (0x2F);
        body.push_back (0x00);

        put32 (out, kMTrk);
        put32 (out, (std::uint32_t) body.size());
        out.insert (out.end(), body.begin(), body.end());
    }

    return out;
}

void writeMidiFile (const RawMidiFile& file, std::ostream& output)
{
    const auto bytes = writeMidiBytes (file);
    output.write (reinterpret_cast<const char*> (bytes.data()), (std::streamsize) bytes.size());
    output.flush();
    if (! output)
        throw MidiExportError ("Could not write MIDI data");
}

} // namespace lotro
```

Add `#include <algorithm>` for `std::max`.

- [ ] **Step 6: Run the tests to verify they pass**

Run: `cmake --build build && ctest --test-dir build -R "RawMidi:" --output-on-failure`
Expected: all `RawMidi:` tests PASS (the fixture test runs as one Catch2 test with 12 dynamic sections).

- [ ] **Step 7: Run the whole suite**

Run: `ctest --test-dir build --output-on-failure`
Expected: all pass (329 + the new ones).

- [ ] **Step 8: Commit**

```bash
git add Source/UI/RawMidi.h Source/UI/RawMidi.cpp Tests/MidiTestBytes.h Tests/RawMidi_tests.cpp CMakeLists.txt Tests/CMakeLists.txt
git commit -m "feat(songsmith): add lossless SMF reader/writer

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 2: Exact replica of JUCE's note pairing (`JuceNoteReplica`)

**Files:**
- Create: `Source/UI/JuceNoteReplica.h`, `Source/UI/JuceNoteReplica.cpp`, `Tests/JuceNoteReplica_tests.cpp`
- Modify: `CMakeLists.txt` (forge_ui sources), `Tests/CMakeLists.txt` (test file + source)

**Interfaces:**
- Consumes: `RawMidiTrack`, `RawMidiEvent` (Task 1).
- Produces:
  ```cpp
  namespace lotro {
  struct ReplicaNoteOn
  {
      int  onRawIndex     = -1;   // index into RawMidiTrack::events
      int  onTick         = 0;
      int  channel        = 1;    // 1..16
      int  pitch          = 0;
      int  velocity       = 0;
      bool hasOff         = false; // JUCE found or invented a note-off
      bool offSynthesized = false; // JUCE invented it (key re-struck first)
      int  offRawIndex    = -1;    // raw index of the real off; -1 if none or invented
      int  offTick        = -1;    // valid when hasOff
  };
  // Index in the result == JUCE's note-on ordinal == importMidi's Note::sourceEventIndex.
  std::vector<ReplicaNoteOn> replicateJuceNoteOns (const RawMidiTrack& track);
  }
  ```

- [ ] **Step 1: Write the failing tests**

`Tests/JuceNoteReplica_tests.cpp`:

```cpp
// JuceNoteReplica: must agree note-for-note with juce::MidiFile::readFrom
// (createMatchingNoteOffs = true), which is what importMidi uses. Checked
// against JUCE itself on hand cases from the spec review and 500 seeded
// random files, plus every fixture.

#include "UI/JuceNoteReplica.h"
#include "UI/RawMidi.h"
#include "MidiTestBytes.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <juce_audio_basics/juce_audio_basics.h>

#include <random>

using namespace lotro;
using namespace miditest;

namespace
{
    juce::File midiFixture (const std::string& name)
    {
        return juce::File (__FILE__).getParentDirectory().getParentDirectory()
                   .getChildFile ("midi").getChildFile (name);
    }

    void checkAgainstJuce (const Bytes& bytes)
    {
        juce::MidiFile paired, unpaired;
        {
            juce::MemoryInputStream in (bytes.data(), bytes.size(), false);
            REQUIRE (paired.readFrom (in, true));
        }
        {
            juce::MemoryInputStream in (bytes.data(), bytes.size(), false);
            REQUIRE (unpaired.readFrom (in, false));
        }

        const auto raw = readMidiBytes (bytes, "diff");
        REQUIRE (paired.getNumTracks() == (int) raw.tracks.size());

        for (int t = 0; t < paired.getNumTracks(); ++t)
        {
            const auto& seq = *paired.getTrack (t);
            const auto& rawTrack = raw.tracks[(size_t) t];

            std::vector<const juce::MidiMessageSequence::MidiEventHolder*> ons;
            for (int i = 0; i < seq.getNumEvents(); ++i)
                if (seq.getEventPointer (i)->message.isNoteOn())
                    ons.push_back (seq.getEventPointer (i));

            const auto replica = replicateJuceNoteOns (rawTrack);
            REQUIRE (replica.size() == ons.size());

            int synthesizedCount = 0;
            for (size_t k = 0; k < ons.size(); ++k)
            {
                const auto& m = ons[k]->message;
                const auto& r = replica[k];
                CHECK (r.pitch == m.getNoteNumber());
                CHECK (r.channel == m.getChannel());
                CHECK (r.velocity == (int) m.getVelocity());
                CHECK (r.onTick == (int) m.getTimeStamp());
                CHECK (r.hasOff == (ons[k]->noteOffObject != nullptr));
                if (ons[k]->noteOffObject != nullptr)
                    CHECK (r.offTick == (int) ons[k]->noteOffObject->message.getTimeStamp());

                const auto& onRaw = rawTrack.events[(size_t) r.onRawIndex];
                CHECK (onRaw.tick == r.onTick);
                CHECK ((int) onRaw.bytes[1] == r.pitch);
                CHECK ((int) onRaw.bytes[2] == r.velocity);

                if (r.hasOff && ! r.offSynthesized)
                {
                    const auto& offRaw = rawTrack.events[(size_t) r.offRawIndex];
                    CHECK (offRaw.tick == r.offTick);
                    CHECK ((int) offRaw.bytes[1] == r.pitch);
                    CHECK (((offRaw.bytes[0] & 0x0F) + 1) == r.channel);
                }
                if (r.offSynthesized)
                    ++synthesizedCount;
            }

            // JUCE adds exactly one event per invented note-off.
            CHECK (synthesizedCount == seq.getNumEvents() - unpaired.getTrack (t)->getNumEvents());
        }
    }

    Bytes randomFile (std::mt19937& rng)
    {
        std::uniform_int_distribution<int> delta (0, 2), kind (0, 9), note (60, 62), chan (0, 1), vel (1, 127);
        std::vector<TrackBody> tracks (2);
        for (auto& t : tracks)
        {
            for (int i = 0; i < 40; ++i)
            {
                const auto d  = (std::uint32_t) delta (rng);
                const auto ch = (std::uint8_t) chan (rng);
                const auto nn = (std::uint8_t) note (rng);
                const int  k  = kind (rng);
                if (k <= 3)      t.ev (d, { (std::uint8_t) (0x90 | ch), nn, (std::uint8_t) vel (rng) });
                else if (k <= 5) t.ev (d, { (std::uint8_t) (0x80 | ch), nn, 0x40 });
                else if (k <= 7) t.ev (d, { (std::uint8_t) (0x90 | ch), nn, 0 });
                else if (k == 8) t.ev (d, { (std::uint8_t) (0xB0 | ch), nn, 0x10 }); // CC number == a note number
                else             t.ev (d, { 0xFF, 0x01, 0x01, 'x' });
            }
            t.eot();
        }
        return smf (1, 96, tracks);
    }
}

TEST_CASE ("JuceNoteReplica: JUCE's within-tick swap can move a note-on ahead of another", "[replica]")
{
    // Tick 0: on60, on62, off60; later off62@10, off60@20. JUCE swaps on60
    // with the off60 in its tick group, so on62 becomes note-on ordinal 0.
    TrackBody t;
    t.ev (0, { 0x90, 60, 100 }).ev (0, { 0x90, 62, 100 }).ev (0, { 0x80, 60, 0x40 })
     .ev (10, { 0x80, 62, 0x40 }).ev (10, { 0x80, 60, 0x40 }).eot();
    const auto bytes = smf (1, 96, { t });

    const auto replica = replicateJuceNoteOns (readMidiBytes (bytes, "a").tracks[0]);
    REQUIRE (replica.size() == 2);
    CHECK (replica[0].pitch == 62);
    CHECK (replica[0].onRawIndex == 1);
    CHECK (replica[0].offRawIndex == 3);
    CHECK (replica[0].offTick == 10);
    CHECK (replica[1].pitch == 60);
    CHECK (replica[1].onRawIndex == 0);
    CHECK (replica[1].offRawIndex == 4);
    CHECK (replica[1].offTick == 20);

    checkAgainstJuce (bytes);
}

TEST_CASE ("JuceNoteReplica: a same-tick re-strike gets a JUCE-invented note-off and the later velocity first", "[replica]")
{
    // Tick 0: on60 v100, off60, on60 v50, off60; later off60@10.
    TrackBody t;
    t.ev (0, { 0x90, 60, 100 }).ev (0, { 0x80, 60, 0x40 }).ev (0, { 0x90, 60, 50 }).ev (0, { 0x80, 60, 0x40 })
     .ev (10, { 0x80, 60, 0x40 }).eot();
    const auto bytes = smf (1, 96, { t });

    const auto replica = replicateJuceNoteOns (readMidiBytes (bytes, "b").tracks[0]);
    REQUIRE (replica.size() == 2);
    CHECK (replica[0].velocity == 50);
    CHECK (replica[0].onRawIndex == 2);
    CHECK (replica[0].offSynthesized);
    CHECK (replica[0].offTick == 0);
    CHECK (replica[1].velocity == 100);
    CHECK (replica[1].onRawIndex == 0);
    CHECK (replica[1].offRawIndex == 4);
    CHECK (replica[1].offTick == 10);

    checkAgainstJuce (bytes);
}

TEST_CASE ("JuceNoteReplica: an unmatched note-on has no off", "[replica]")
{
    TrackBody t;
    t.ev (0, { 0x90, 60, 100 }).eot (50);
    const auto replica = replicateJuceNoteOns (readMidiBytes (smf (1, 96, { t }), "c").tracks[0]);
    REQUIRE (replica.size() == 1);
    CHECK_FALSE (replica[0].hasOff);
    CHECK (replica[0].offRawIndex == -1);
}

TEST_CASE ("JuceNoteReplica: agrees with juce::MidiFile on 500 seeded random files", "[replica]")
{
    std::mt19937 rng (20261003);
    for (int i = 0; i < 500; ++i)
        checkAgainstJuce (randomFile (rng));
}

TEST_CASE ("JuceNoteReplica: agrees with juce::MidiFile on every tracked fixture", "[replica]")
{
    auto name = GENERATE (as<std::string>{},
        "Barnes Brothers Band - Pull The Wires.mid", "angels.mid", "anymore.mid", "blue.mid",
        "hold.mid", "land.mid", "leah.mid", "nobody.mid", "right.mid", "state.mid", "syn5.mid", "tellit.mid");

    DYNAMIC_SECTION (name)
    {
        juce::MemoryBlock block;
        REQUIRE (midiFixture (name).loadFileAsData (block));
        const auto* p = static_cast<const std::uint8_t*> (block.getData());
        checkAgainstJuce (Bytes (p, p + block.getSize()));
    }
}
```

- [ ] **Step 2: Register files, run to verify failure**

Add `JuceNoteReplica_tests.cpp` and `${CMAKE_SOURCE_DIR}/Source/UI/JuceNoteReplica.cpp` to `Tests/CMakeLists.txt`; add `Source/UI/JuceNoteReplica.cpp` to `forge_ui`. Create an empty `.cpp`.

Run: `cmake --build build`
Expected: compile FAIL (`UI/JuceNoteReplica.h` not found).

- [ ] **Step 3: Write the header**

`Source/UI/JuceNoteReplica.h`:

```cpp
#pragma once

// An exact replica of what juce::MidiFile::readFrom (stream, true) -- and so
// importMidi -- makes of one track's note-ons, computed on RawMidi events so
// every note can be tied back to the exact raw bytes it came from.
//
// JUCE does two things per track (juce_MidiFile.cpp / juce_MidiMessageSequence.cpp):
//  1. reorderNoteOnsAfterNoteOffs, per group of events sharing a tick: find
//     the FIRST note-on, find the LAST same-channel/key note-off after it in
//     the group, swap them, continue after the first note-on's slot; stop the
//     group as soon as a first note-on has no such off. This can move a
//     note-on past other note-ons.
//  2. updateMatchedPairs: each note-on pairs with the next same-channel/key
//     note-off, or -- if a same-channel/key note-on comes first -- with a
//     note-off JUCE INSERTS into the list just before that re-strike.
// Predicates match JUCE's: note-on = 0x9n with velocity > 0; note-off = 0x8n
// or 0x9n with velocity 0.

#include "RawMidi.h"

#include <vector>

namespace lotro
{

struct ReplicaNoteOn
{
    int  onRawIndex     = -1;
    int  onTick         = 0;
    int  channel        = 1;
    int  pitch          = 0;
    int  velocity       = 0;
    bool hasOff         = false;
    bool offSynthesized = false;
    int  offRawIndex    = -1;
    int  offTick        = -1;
};

// result[k] is JUCE's k-th note-on (importMidi's Note::sourceEventIndex == k).
std::vector<ReplicaNoteOn> replicateJuceNoteOns (const RawMidiTrack& track);

} // namespace lotro
```

- [ ] **Step 4: Write the implementation**

`Source/UI/JuceNoteReplica.cpp`:

```cpp
#include "JuceNoteReplica.h"

#include <algorithm>
#include <utility>

namespace lotro
{

namespace
{
    struct Item
    {
        int  tick       = 0;
        int  rawIndex   = -1; // -1 = a note-off JUCE invented
        int  channel    = 0;  // 0 for non-channel messages, like MidiMessage::getChannel()
        int  noteNumber = -1;
        bool isOn       = false;
        bool isOff      = false;
    };

    Item makeItem (const RawMidiEvent& e, int rawIndex)
    {
        Item item;
        item.tick     = e.tick;
        item.rawIndex = rawIndex;

        const int status = e.bytes.empty() ? 0 : e.bytes[0];
        if (status >= 0x80 && status < 0xF0)
        {
            const int type     = status & 0xF0;
            const int velocity = e.bytes.size() > 2 ? e.bytes[2] : 0;
            item.channel    = (status & 0x0F) + 1;
            item.noteNumber = e.bytes.size() > 1 ? e.bytes[1] : 0;
            item.isOn       = type == 0x90 && velocity > 0;
            item.isOff      = type == 0x80 || (type == 0x90 && velocity == 0);
        }
        return item;
    }

    // juce_MidiFile.cpp reorderNoteOnsAfterNoteOffs, on [begin, end) of one tick group.
    void reorderGroup (std::vector<Item>& list, size_t begin, size_t end)
    {
        size_t it = begin;
        while (it < end)
        {
            size_t firstOn = it;
            while (firstOn < end && ! list[firstOn].isOn)
                ++firstOn;
            if (firstOn == end)
                return;

            size_t lastOff = end;
            for (size_t j = end; j-- > firstOn + 1;)
            {
                if (list[j].isOff
                    && list[j].channel == list[firstOn].channel
                    && list[j].noteNumber == list[firstOn].noteNumber)
                {
                    lastOff = j;
                    break;
                }
            }
            if (lastOff == end)
                return;

            std::swap (list[firstOn], list[lastOff]);
            it = firstOn + 1;
        }
    }
}

std::vector<ReplicaNoteOn> replicateJuceNoteOns (const RawMidiTrack& track)
{
    std::vector<Item> list;
    list.reserve (track.events.size());
    for (int i = 0; i < (int) track.events.size(); ++i)
        list.push_back (makeItem (track.events[(size_t) i], i));

    // MidiMessageSequence::sort is a stable sort by timestamp.
    std::stable_sort (list.begin(), list.end(),
                      [] (const Item& a, const Item& b) { return a.tick < b.tick; });

    for (size_t begin = 0; begin < list.size();)
    {
        size_t end = begin;
        while (end < list.size() && list[end].tick == list[begin].tick)
            ++end;
        reorderGroup (list, begin, end);
        begin = end;
    }

    // juce_MidiMessageSequence.cpp updateMatchedPairs.
    std::vector<ReplicaNoteOn> result;
    for (size_t i = 0; i < list.size(); ++i)
    {
        const Item on = list[i]; // copy: the insert below may reallocate
        if (! on.isOn)
            continue;

        const auto& onBytes = track.events[(size_t) on.rawIndex].bytes;

        ReplicaNoteOn r;
        r.onRawIndex = on.rawIndex;
        r.onTick     = on.tick;
        r.channel    = on.channel;
        r.pitch      = on.noteNumber;
        r.velocity   = onBytes.size() > 2 ? onBytes[2] : 0;

        for (size_t j = i + 1; j < list.size(); ++j)
        {
            const Item m = list[j];
            if (m.noteNumber != on.noteNumber || m.channel != on.channel)
                continue;

            if (m.isOff)
            {
                r.hasOff      = true;
                r.offRawIndex = m.rawIndex;
                r.offTick     = m.tick;
                break;
            }

            if (m.isOn)
            {
                Item invented;
                invented.tick       = m.tick;
                invented.channel    = on.channel;
                invented.noteNumber = on.noteNumber;
                invented.isOff      = true;
                list.insert (list.begin() + (std::ptrdiff_t) j, invented);

                r.hasOff         = true;
                r.offSynthesized = true;
                r.offTick        = invented.tick;
                break;
            }
        }

        result.push_back (r);
    }

    return result;
}

} // namespace lotro
```

- [ ] **Step 5: Run the tests to verify they pass**

Run: `cmake --build build && ctest --test-dir build -R "JuceNoteReplica:" --output-on-failure`
Expected: PASS. If the random test fails, the failing assertion shows the first disagreement. Fix the replica to match JUCE, never the reverse.

- [ ] **Step 6: Whole suite, then commit**

Run: `ctest --test-dir build --output-on-failure` → all pass.

```bash
git add Source/UI/JuceNoteReplica.h Source/UI/JuceNoteReplica.cpp Tests/JuceNoteReplica_tests.cpp CMakeLists.txt Tests/CMakeLists.txt
git commit -m "feat(songsmith): replicate JUCE's MIDI note pairing on raw events

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 3: `NOTES`/`EVENTS` containers, new identifiers and helpers

Moves notes under a `NOTES` child of each `MIDI_TRACK` and adds an empty `EVENTS` child. There is no conductor yet, so behaviour is unchanged. Afterwards the suite must be green with only mechanical test edits.

**Files:**
- Modify: `Source/UI/SongDocument.h`, `Source/UI/SongDocument.cpp`, `Source/UI/SongModelBridge.cpp`, `Source/UI/SourceTrackNoteSource.cpp`, `Source/UI/SourceRollEditor.cpp`, `Source/UI/TrackRowComponent.cpp`, `Source/UI/TrackNotePreview.cpp`, `Source/UI/TrackListComponent.cpp`, `Source/UI/PianoRollComponent.cpp`
- Modify tests (mechanical): `Tests/SongDocument_tests.cpp`, `Tests/SongModelBridge_tests.cpp`, `Tests/SongsmithRoundTrip_tests.cpp`, `Tests/SourceRollEditor_tests.cpp`, `Tests/SourceTrackNoteSource_tests.cpp`, `Tests/PianoRollComponent_tests.cpp`, `Tests/PreviewAssignedPanel_tests.cpp`, `Tests/PreviewPipeline_tests.cpp`, plus any other test the audit in Step 6 finds

**Interfaces:**
- Produces (in `SongIDs`): `NOTES`, `EVENTS`, `EVENT`, `data`, `order`, `channel`, `offVelocity`, `offIsNoteOnZero`, `onOrder`, `offOrder`, `offSynthesized`, `isConductor`, `endTick`, `defaultChannel`, `relocatedFrom`. The `juce::Identifier` string equals the C++ name.
- Produces (on `SongDocument`):
  ```cpp
  static juce::ValueTree getNotesNode  (const juce::ValueTree& track); // invalid if track invalid
  static juce::ValueTree getEventsNode (const juce::ValueTree& track);
  static bool isAssignableTrack (const juce::ValueTree& track); // MIDI_TRACK, not isConductor, >= 1 NOTE
  int  getNumAssignableTracks() const;
  void removeProperty (juce::ValueTree targetTree, const juce::Identifier& propertyId, bool newTransaction = true);
  ```
  `addTrack`/`addTrackBulk` create `NOTES` and `EVENTS` children and set `endTick = 0`.

- [ ] **Step 1: Write the failing tests** (append to `Tests/SongDocument_tests.cpp`)

```cpp
TEST_CASE ("SongDocument: new tracks get empty NOTES and EVENTS containers and endTick 0", "[songdocument][fidelity]")
{
    SongDocument doc;
    auto undoable = doc.addTrack ("A", 0, 0, 1);
    auto bulk = doc.addTrackBulk ("B", 0, 0, 1);

    for (auto track : { undoable, bulk })
    {
        auto notes = SongDocument::getNotesNode (track);
        auto events = SongDocument::getEventsNode (track);
        REQUIRE (notes.isValid());
        REQUIRE (events.isValid());
        CHECK (notes.hasType (SongIDs::NOTES));
        CHECK (events.hasType (SongIDs::EVENTS));
        CHECK (notes.getNumChildren() == 0);
        CHECK (events.getNumChildren() == 0);
        CHECK ((int) track.getProperty (SongIDs::endTick) == 0);
    }
}

TEST_CASE ("SongDocument: isAssignableTrack needs at least one note and no conductor flag", "[songdocument][fidelity]")
{
    SongDocument doc;
    auto track = doc.addTrack ("A", 0, 0, 1);
    CHECK_FALSE (SongDocument::isAssignableTrack (track));
    CHECK (doc.getNumAssignableTracks() == 0);

    SongDocument::appendChildBulk (SongDocument::getNotesNode (track), juce::ValueTree (SongIDs::NOTE));
    CHECK (SongDocument::isAssignableTrack (track));
    CHECK (doc.getNumAssignableTracks() == 1);

    track.setProperty (SongIDs::isConductor, true, nullptr);
    CHECK_FALSE (SongDocument::isAssignableTrack (track));
    CHECK_FALSE (SongDocument::isAssignableTrack ({}));
}

TEST_CASE ("SongDocument: removeProperty is undoable and joins an open transaction when asked", "[songdocument][fidelity]")
{
    SongDocument doc;
    auto track = doc.addTrack ("A", 0, 0, 1);
    juce::ValueTree note (SongIDs::NOTE);
    note.setProperty (SongIDs::onOrder, 3, nullptr);
    SongDocument::appendChildBulk (SongDocument::getNotesNode (track), note);

    doc.setProperty (note, SongIDs::startTick, 10);
    doc.removeProperty (note, SongIDs::onOrder, false);
    CHECK_FALSE (note.hasProperty (SongIDs::onOrder));

    doc.undo(); // one transaction: both the setProperty and the removeProperty
    CHECK ((int) note.getProperty (SongIDs::onOrder) == 3);
    CHECK_FALSE (note.hasProperty (SongIDs::startTick));
}
```

- [ ] **Step 2: Run to verify failure**

Run: `cmake --build build`
Expected: compile FAIL (`getNotesNode` / `SongIDs::NOTES` undeclared).

- [ ] **Step 3: Add identifiers and helpers**

In `Source/UI/SongDocument.h`, inside `namespace SongIDs`, after `METER_CHANGE`:

```cpp
    extern const juce::Identifier NOTES;   // MIDI_TRACK child holding NOTE nodes
    extern const juce::Identifier EVENTS;  // MIDI_TRACK child holding EVENT nodes
    extern const juce::Identifier EVENT;   // one non-note raw MIDI event
```

and after `sourceEventIndex`:

```cpp
    // MIDI-fidelity properties (2026-10-03 spec).
    extern const juce::Identifier channel;          // NOTE: 1..16
    extern const juce::Identifier offVelocity;      // NOTE: note-off velocity
    extern const juce::Identifier offIsNoteOnZero;  // NOTE: off written as note-on velocity 0
    extern const juce::Identifier onOrder;          // NOTE: raw index of its note-on (imported only)
    extern const juce::Identifier offOrder;         // NOTE: raw index of its note-off (imported only)
    extern const juce::Identifier offSynthesized;   // NOTE: JUCE invented its note-off on import
    extern const juce::Identifier isConductor;      // MIDI_TRACK: the song's conductor track
    extern const juce::Identifier endTick;          // MIDI_TRACK: End-of-Track tick
    extern const juce::Identifier defaultChannel;   // MIDI_TRACK: channel for editor-created notes
    extern const juce::Identifier data;             // EVENT: juce::MemoryBlock of raw bytes
    extern const juce::Identifier order;            // EVENT: raw index within its source track
    extern const juce::Identifier relocatedFrom;    // EVENT: raw source track when moved to the conductor
```

In `SongDocument.cpp`'s `namespace SongIDs`, define each (`const juce::Identifier NOTES ("NOTES");` … `const juce::Identifier relocatedFrom ("relocatedFrom");`).

In the class, under `// --- Query helpers ---`:

```cpp
    // A MIDI_TRACK's NOTES / EVENTS containers (invalid if `track` is).
    static juce::ValueTree getNotesNode (const juce::ValueTree& track);
    static juce::ValueTree getEventsNode (const juce::ValueTree& track);

    // True for a MIDI_TRACK that can be dragged onto a part: not the
    // conductor, and holding at least one NOTE.
    static bool isAssignableTrack (const juce::ValueTree& track);
    int getNumAssignableTracks() const;
```

and under the mutations:

```cpp
    // Undoable property removal; newTransaction mirrors setProperty's.
    void removeProperty (juce::ValueTree targetTree, const juce::Identifier& propertyId,
                         bool newTransaction = true);
```

In `SongDocument.cpp`:

```cpp
juce::ValueTree SongDocument::getNotesNode (const juce::ValueTree& track)
{
    return track.getChildWithName (SongIDs::NOTES);
}

juce::ValueTree SongDocument::getEventsNode (const juce::ValueTree& track)
{
    return track.getChildWithName (SongIDs::EVENTS);
}

bool SongDocument::isAssignableTrack (const juce::ValueTree& track)
{
    return track.hasType (SongIDs::MIDI_TRACK)
        && ! (bool) track.getProperty (SongIDs::isConductor, false)
        && getNotesNode (track).getNumChildren() > 0;
}

int SongDocument::getNumAssignableTracks() const
{
    int count = 0;
    for (auto track : getSourceMidiNode())
        if (isAssignableTrack (track))
            ++count;
    return count;
}

void SongDocument::removeProperty (juce::ValueTree targetTree, const juce::Identifier& propertyId,
                                    bool newTransaction)
{
    if (newTransaction)
        undoManager.beginNewTransaction();

    targetTree.removeProperty (propertyId, &undoManager);
}
```

In both `addTrack` and `addTrackBulk`, before `getSourceMidiNode().addChild (track, …)`:

```cpp
    track.setProperty (SongIDs::endTick, 0, nullptr);
    track.addChild (juce::ValueTree (SongIDs::NOTES), -1, nullptr);
    track.addChild (juce::ValueTree (SongIDs::EVENTS), -1, nullptr);
```

(The track isn't in the document yet, so `nullptr` is correct even in the undoable `addTrack`: undoing the `addChild` of the whole track removes them with it.)

- [ ] **Step 4: Migrate every production call site to `getNotesNode`**

Apply exactly these edits:

- `SourceTrackNoteSource.cpp`: `getNumNotes()` returns `isTrackLive() ? SongDocument::getNotesNode (track).getNumChildren() : 0;` and `getNote()` reads `auto noteNode = SongDocument::getNotesNode (track).getChild (index);`.
- `SourceRollEditor.cpp`:
  - In `selectPitch`, `hitTestNote` and `updateRubberBandSelection`, replace `track.getNumChildren()`/`track.getChild (i)` with a local `auto notes = SongDocument::getNotesNode (track);` and `notes.getNumChildren()`/`notes.getChild (i)`.
  - In `pruneSelection` (line ~46), the predicate becomes `return n.getParent() != SongDocument::getNotesNode (track);`.
  - `deleteSelection`: `doc.removeChild (SongDocument::getNotesNode (track), note, false);`.
  - `createNoteAt`: `doc.addChild (SongDocument::getNotesNode (track), note);`.
- `TrackRowComponent.cpp` `buildSecondLine`: `auto notes = SongDocument::getNotesNode (track); const int numNotes = notes.getNumChildren();` and `notes.getChild (i)` in the pitch loop.
- `TrackNotePreview.cpp` (both loops): iterate `auto notes = SongDocument::getNotesNode (track);` children and drop the `hasType (SongIDs::NOTE)` filter.
- `TrackListComponent.cpp` `documentEndTick`: iterate `SongDocument::getNotesNode (track)` children and drop the `hasType` filter.
- `PianoRollComponent.cpp` `drawGhostTracks`: iterate `SongDocument::getNotesNode (ghostTrack)` and drop the filter. In the selection-highlight check (line ~594) use `SongDocument::getNotesNode (sourceEditor->getTrackNode()).getChild (i)`.
- `SongModelBridge.cpp`:
  - `rescaleExistingDocumentTicks`: inner loop over `SongDocument::getNotesNode (trackTree)`.
  - `appendImportedSong`: `SongDocument::appendChildBulk (SongDocument::getNotesNode (trackTree), noteTree);`.
  - `buildConfigAndRawSong`: `for (auto noteTree : SongDocument::getNotesNode (trackTree))`.

- [ ] **Step 5: Build**

Run: `cmake --build build`
Expected: compiles.

- [ ] **Step 6: Migrate tests mechanically and audit**

Run: `grep -nE "getNumChildren|getChild ?\(|addChild ?\(|appendChildBulk|removeChild" Tests/*.cpp`.
For every hit where the receiver is a `MIDI_TRACK` (`track`, `trackTree`, `existingTrack`, `incomingTrack`, `secondTrackTree`, `fixture.track`, `other`, `ghostTrack`, the result of `doc.getTrack (…)`, `doc.addTrack (…)`, `doc.findTrackById (…)`), insert the `NOTES` hop:

- `track.addChild (note, -1, nullptr)` → `SongDocument::getNotesNode (track).addChild (note, -1, nullptr)`
- `track.getNumChildren()` → `SongDocument::getNotesNode (track).getNumChildren()`
- `track.getChild (n)` → `SongDocument::getNotesNode (track).getChild (n)`
- `doc.addChild (track, note…)` / `doc.removeChild (track, …)` → pass `SongDocument::getNotesNode (track)` as the parent
- `SongDocument::appendChildBulk (trackTree, noteTree)` → `SongDocument::appendChildBulk (SongDocument::getNotesNode (trackTree), noteTree)`

Leave receivers that are `SOURCE_MIDI`, `PARTS`, `PART`, `TEMPO_MAP` or `METER_MAP` untouched (e.g. `tempoMapNode.getChild (0)`, `doc.getSourceMidiNode()`). In `SongDocument_tests.cpp`, the test at line ~319 appends a hand-built `MIDI_TRACK` via `appendChildBulk (doc.getSourceMidiNode(), bulkTrack)`; that's a `SOURCE_MIDI` receiver, so leave it.

- [ ] **Step 7: Run the whole suite**

Run: `cmake --build build && ctest --test-dir build --output-on-failure`
Expected: all pass, including the three new `SongDocument:` tests.

- [ ] **Step 8: Commit**

```bash
git add Source/UI Tests
git commit -m "refactor(songsmith): hold notes under a NOTES container and add an EVENTS container

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 4: The conductor track, and conductor/note-less-aware code

**Files:**
- Modify: `Source/UI/SongDocument.{h,cpp}`, `Source/UI/SongModelBridge.cpp`, `Source/UI/TrackListComponent.cpp`, `Source/UI/TrackRowComponent.{h,cpp}`, `Source/UI/AssignmentChipComponent.cpp`
- Modify tests: `Tests/SongDocument_tests.cpp`, `Tests/SongModelBridge_tests.cpp`, `Tests/SongsmithRoundTrip_tests.cpp`, `Tests/TrackListComponent_tests.cpp`, `Tests/PreviewAssignedPanel_tests.cpp`, `Tests/PreviewPipeline_tests.cpp`, plus any other test the audit in Step 6 finds

**Interfaces:**
- Consumes: Task 3 helpers.
- Produces: `juce::ValueTree SongDocument::getConductorTrack() const;`, always `SOURCE_MIDI` child 0, `isConductor = true`, minted `trackId`, `importBatch = 0`, name `"Conductor"`, `NOTES`/`EVENTS`, `endTick = 0`. `removeTrack` on it is a no-op. `assignTrackToPart`, `synthesiseDefaultParts` and `buildConfigAndRawSong` skip non-assignable tracks. `TrackRowComponent` gets a `displayIndex` of 0 for the conductor (painted as no number).

- [ ] **Step 1: Write the failing tests** (append to `Tests/SongDocument_tests.cpp`)

```cpp
TEST_CASE ("SongDocument: a fresh document has exactly one empty conductor track at child 0", "[songdocument][fidelity]")
{
    SongDocument doc;
    REQUIRE (doc.getNumTracks() == 1);
    auto conductor = doc.getConductorTrack();
    REQUIRE (conductor.isValid());
    CHECK (conductor == doc.getTrack (0));
    CHECK ((bool) conductor.getProperty (SongIDs::isConductor));
    CHECK ((int) conductor.getProperty (SongIDs::importBatch) == 0);
    CHECK ((juce::int64) conductor.getProperty (SongIDs::trackId) > 0);
    CHECK (SongDocument::getNotesNode (conductor).getNumChildren() == 0);
    CHECK (SongDocument::getEventsNode (conductor).getNumChildren() == 0);
    CHECK_FALSE (doc.canUndo());
    CHECK (doc.getNumAssignableTracks() == 0);
}

TEST_CASE ("SongDocument: removeTrack refuses the conductor and opens no transaction", "[songdocument][fidelity]")
{
    SongDocument doc;
    const auto conductorId = (juce::int64) doc.getConductorTrack().getProperty (SongIDs::trackId);
    doc.removeTrack (conductorId);
    CHECK (doc.getConductorTrack().isValid());
    CHECK_FALSE (doc.canUndo());
}

TEST_CASE ("SongDocument: assignTrackToPart refuses the conductor and note-less tracks", "[songdocument][fidelity]")
{
    SongDocument doc;
    auto part = doc.addPart ("Lute of Ages", "");
    const auto partId = (juce::int64) part.getProperty (SongIDs::partId);
    auto noteless = doc.addTrack ("Lyrics", 0, 0, 1);

    CHECK_FALSE (doc.assignTrackToPart (partId, (juce::int64) doc.getConductorTrack().getProperty (SongIDs::trackId)));
    CHECK_FALSE (doc.assignTrackToPart (partId, (juce::int64) noteless.getProperty (SongIDs::trackId)));
    CHECK (SongDocument::getNumAssignments (part) == 0);
}
```

Append to `Tests/SongModelBridge_tests.cpp`:

```cpp
TEST_CASE ("SongModelBridge: synthesiseDefaultParts and buildConfigAndRawSong skip the conductor and unassigned note-less tracks", "[songmodelbridge][fidelity]")
{
    SongDocument doc;
    auto noteless = doc.addTrackBulk ("Lyrics", 0, 0, 1);
    auto withNotes = doc.addTrackBulk ("Melody", 0, 0, 1);
    juce::ValueTree note (SongIDs::NOTE);
    note.setProperty (SongIDs::pitch, 60, nullptr);
    note.setProperty (SongIDs::startTick, 0, nullptr);
    note.setProperty (SongIDs::durationTicks, 480, nullptr);
    note.setProperty (SongIDs::velocity, 100, nullptr);
    SongDocument::appendChildBulk (SongDocument::getNotesNode (withNotes), note);

    synthesiseDefaultParts (doc);
    REQUIRE (doc.getNumParts() == 1);
    CHECK ((juce::int64) SongDocument::getAssignment (doc.getPart (0), 0).getProperty (SongIDs::trackId)
           == (juce::int64) withNotes.getProperty (SongIDs::trackId));

    auto built = buildConfigAndRawSong (doc);
    REQUIRE (built.rawSong.tracks.size() == 1);
    CHECK (built.rawSong.tracks[0].name == "Melody");
    REQUIRE (built.config.instruments.size() == 1);
    CHECK (built.config.instruments[0].sources[0].midiTrackIndex == 0);
    juce::ignoreUnused (noteless);
}

TEST_CASE ("SongModelBridge: a track the user emptied but kept assigned still reaches the raw Song", "[songmodelbridge][fidelity]")
{
    SongDocument doc;
    auto track = doc.addTrackBulk ("Emptied", 0, 0, 1);
    auto part = doc.addPart ("Lute of Ages", "");
    doc.addAssignment (part, (juce::int64) track.getProperty (SongIDs::trackId), 0, 0, "octaveShift");

    auto built = buildConfigAndRawSong (doc);
    REQUIRE (built.rawSong.tracks.size() == 1);
    CHECK (built.config.instruments[0].sources[0].midiTrackIndex == 0);
}
```

- [ ] **Step 2: Run to verify failure**

Run: `cmake --build build`
Expected: compile FAIL (`getConductorTrack` undeclared).

- [ ] **Step 3: Implement the conductor in `SongDocument`**

Header: add `juce::ValueTree getConductorTrack() const;` under the query helpers, with the comment `// The song's conductor MIDI_TRACK: always SOURCE_MIDI child 0, never assignable or removable.`

`.cpp` — at the end of the constructor, after `tree.addChild (meterMap, …)`:

```cpp
    // Every song has exactly one conductor track, created before any import
    // (2026-10-03 MIDI-fidelity spec). Not undoable: it is part of the empty
    // document, not an edit.
    juce::ValueTree conductor (SongIDs::MIDI_TRACK);
    conductor.setProperty (SongIDs::trackId, mintTrackId(), nullptr);
    conductor.setProperty (SongIDs::name, "Conductor", nullptr);
    conductor.setProperty (SongIDs::colorArgb, 0, nullptr);
    conductor.setProperty (SongIDs::sourceMidiChannel, 0, nullptr);
    conductor.setProperty (SongIDs::importBatch, 0, nullptr);
    conductor.setProperty (SongIDs::isConductor, true, nullptr);
    conductor.setProperty (SongIDs::endTick, 0, nullptr);
    conductor.addChild (juce::ValueTree (SongIDs::NOTES), -1, nullptr);
    conductor.addChild (juce::ValueTree (SongIDs::EVENTS), -1, nullptr);
    sourceMidi.addChild (conductor, -1, nullptr);
```

(`sourceMidi` is the local `SOURCE_MIDI` tree created earlier in the constructor; add the conductor to it before or after it joins `tree`, either works.)

```cpp
juce::ValueTree SongDocument::getConductorTrack() const
{
    return getSourceMidiNode().getChild (0);
}
```

`removeTrack`, after the validity check:

```cpp
    if ((bool) track.getProperty (SongIDs::isConductor, false))
        return; // the conductor can't be deleted
```

`assignTrackToPart`: replace `if (! findTrackById (trackId).isValid()) return false;` with `if (! isAssignableTrack (findTrackById (trackId))) return false;`.

- [ ] **Step 4: Make the bridge and UI conductor/note-less aware**

`SongModelBridge.cpp`:

- Family colour counting in `appendImportedSong`: inside the `for (int i = 0; i < doc.getNumTracks(); ++i)` loop, add `if (! SongDocument::isAssignableTrack (existing)) continue;` before incrementing.
- `synthesiseDefaultParts`: after `auto trackTree = doc.getTrack (i);` add `if (! SongDocument::isAssignableTrack (trackTree)) continue;`.
- `buildConfigAndRawSong`: collect the set of assigned trackIds first, then skip the conductor and any track that has no notes and no assignment, and index by `rawSong.tracks.size()`:

```cpp
    std::set<juce::int64> assignedTrackIds;
    for (auto partTree : doc.getPartsNode())
        for (int a = 0; a < SongDocument::getNumAssignments (partTree); ++a)
            assignedTrackIds.insert ((juce::int64) SongDocument::getAssignment (partTree, a).getProperty (SongIDs::trackId));

    std::map<juce::int64, int> trackIdToIndex;
    for (int i = 0; i < sourceMidiNode.getNumChildren(); ++i)
    {
        auto trackTree = sourceMidiNode.getChild (i);
        const auto trackId = (juce::int64) trackTree.getProperty (SongIDs::trackId);

        // The conductor and unassigned note-less tracks never reach forge_core:
        // the CLI's importMidi drops note-less tracks, and an extra one here
        // would add an "unreferenced track" diagnostic the CLI never emits.
        if ((bool) trackTree.getProperty (SongIDs::isConductor, false))
            continue;
        if (SongDocument::getNotesNode (trackTree).getNumChildren() == 0
            && assignedTrackIds.count (trackId) == 0)
            continue;

        Track track;
        // … unchanged field/note copying …

        trackIdToIndex[trackId] = (int) rawSong.tracks.size();
        rawSong.tracks.push_back (track);
    }
```

`TrackListComponent.cpp`:

- In `rebuild()`, pass `i` (not `i + 1`) as the display index. The conductor is child 0, so other rows are numbered from 1.
- In `paint()`, change `if (doc.getNumTracks() == 0)` to `if (doc.getNumTracks() <= 1)`, with the comment `// only the conductor: nothing imported yet`.

`TrackRowComponent.cpp` `paint()`: draw the index only when `index > 0`:

```cpp
    if (index > 0)
        g.drawText (juce::String (index), indexArea, juce::Justification::centredLeft);
```

Update the constructor comment in `TrackRowComponent.h`: "displayIndex is the row number shown (the track's SOURCE_MIDI child index; 0 = the conductor, drawn unnumbered)".

`AssignmentChipComponent.cpp`: `trackLabel = "Tk" + juce::String (position);`. The conductor occupies position 0 and can never be assigned, so assignable tracks start at 1. Update the M8 comment to say so.

- [ ] **Step 5: Build and run the new tests**

Run: `cmake --build build && ctest --test-dir build -R "SongDocument:|SongModelBridge:" --output-on-failure`
Expected: the new tests PASS. Existing tests that count tracks fail; fix them in Step 6.

- [ ] **Step 6: Update existing tests for the conductor (keep each test's intent)**

The conductor is child 0 of every document. Apply these rules to every failing test, and audit with `grep -nE "getNumTracks|getTrack ?\(|indexOf|colourOf" Tests/*.cpp`:

- A fresh or failed-import document now has `getNumTracks() == 1`. `CHECK (doc.getNumTracks() == 0)` → `CHECK (doc.getNumTracks() == 1)`. Where the intent is "nothing imported", prefer `CHECK (doc.getNumAssignableTracks() == 0)`.
- Absolute counts after adding N tracks: `== N` → `== N + 1`.
- Absolute positions: `doc.getTrack (k)` → `doc.getTrack (k + 1)`. `doc.getTrack (doc.getNumTracks() - 1)` is unchanged.
- The conductor mints `trackId` 1, so the first track a test adds now gets `trackId` 2. Any assertion on a literal trackId value shifts by one; assertions comparing two minted ids are unaffected.
- `SongDocument_tests.cpp` removal tests (lines ~278–304, ~364–367): add 1 to every expected count, and `getTrack (0)` → `getTrack (1)`.
- `SongModelBridge_tests.cpp`:
  - Line ~295: `REQUIRE (doc.getNumTracks() == 1 + (int) (…))`.
  - Lines ~296–333, ~500, ~565, ~624, ~661, ~722–828: `getTrack (k)` → `getTrack (k + 1)`.
  - Line ~887: `REQUIRE (doc.getNumTracks() == 1)`.
  - Colour test (~1033–1080): index the document by `+1`. The conductor is not counted in family shading, so the expected colours themselves don't change.
- `SongsmithRoundTrip_tests.cpp`: `tracksAfterFirst` is read from `doc.getNumTracks()` after the first import, so it already includes the conductor. Only lines ~523/553 (`== 0` → `== 1`) change in this task. (Task 6 changes the positional alignment, once note-less tracks start arriving.)
- `TrackListComponent_tests.cpp` (~130, 153, 237): row counts `+ 1`. If a test checks the empty-state message on a fresh document, it still holds (`getNumTracks() <= 1`).

- [ ] **Step 7: Run the whole suite**

Run: `ctest --test-dir build --output-on-failure`
Expected: all pass. In particular `SongsmithRoundTrip_tests` "matches the CLI's ad-hoc path for every tracked fixture" passes unchanged (ABC bytes and diagnostic counts).

- [ ] **Step 8: Commit**

```bash
git add Source/UI Tests
git commit -m "feat(songsmith): give every song a conductor track and keep it out of conversion

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 5: Import planner (`MidiImportPlan`)

A pure function that decides the conductor rules, links every `importMidi` note to its raw bytes, and lists leftover raw events. It doesn't touch the document.

**Files:**
- Create: `Source/UI/MidiImportPlan.h`, `Source/UI/MidiImportPlan.cpp`, `Tests/MidiImportPlan_tests.cpp`
- Modify: `CMakeLists.txt`, `Tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `Song`/`Track`/`Note` (Core), `importMidi` (in tests), `RawMidiFile` (Task 1), `replicateJuceNoteOns` (Task 2), `Diagnostics`.
- Produces:
  ```cpp
  namespace lotro {
  class MidiImportPlanError : public std::runtime_error { public: using std::runtime_error::runtime_error; };
  struct PlannedEvent    { int tick = 0; int order = 0; int relocatedFrom = -1; std::vector<std::uint8_t> bytes; };
  struct PlannedNoteLink { int channel = 1; int onOrder = -1; int offOrder = -1; bool offSynthesized = false; int offVelocity = 64; bool offIsNoteOnZero = false; };
  struct PlannedTrack
  {
      int rawTrackIndex = 0;
      int songTrackIndex = -1;          // index into Song::tracks; -1 = note-less track
      std::string name;                 // note-less tracks only
      int sourceMidiChannel = 0;        // note-less tracks only
      int sourceProgram = 0;            // note-less tracks only
      int defaultChannel = 1;
      int endTick = 0;
      std::vector<PlannedNoteLink> noteLinks; // parallel to Song::tracks[songTrackIndex].notes
      std::vector<PlannedEvent> events;
  };
  struct MidiImportPlan
  {
      bool writesConductor = false;     // first import only
      std::vector<PlannedEvent> conductorEvents;
      int conductorEndTick = 0;
      std::vector<PlannedTrack> tracks; // raw order, file's conductor excluded
      int relocatedEventCount = 0;
      int droppedEventCount = 0;
  };
  bool isSongWideMetaEvent (const RawMidiEvent& event);
  bool hasConductorTrack (const RawMidiFile& raw);
  // Throws MidiImportPlanError for format 2 or when the two parsers disagree.
  // Appends Info/Warning diagnostics (source "SongModelBridge") only on success.
  MidiImportPlan planMidiImport (const Song& song, const RawMidiFile& raw, bool isFirstImport, Diagnostics& diagnostics);
  }
  ```

- [ ] **Step 1: Write the failing tests**

`Tests/MidiImportPlan_tests.cpp`:

```cpp
// MidiImportPlan: conductor rules, note links and leftover events, decided
// purely from importMidi's Song and the raw file.

#include "UI/MidiImportPlan.h"
#include "UI/RawMidi.h"
#include "Core/MidiImporter.h"
#include "MidiTestBytes.h"

#include <catch2/catch_test_macros.hpp>

#include <juce_core/juce_core.h>

#include <numeric>
#include <sstream>

using namespace lotro;
using namespace miditest;

namespace
{
    struct Parsed { Song song; RawMidiFile raw; };

    Parsed parse (const Bytes& bytes)
    {
        Diagnostics diags;
        std::istringstream in (std::string (bytes.begin(), bytes.end()), std::ios::binary);
        Parsed p { importMidi (in, "t", diags), readMidiBytes (bytes, "t") };
        return p;
    }

    Bytes fixtureBytes (const std::string& name)
    {
        juce::MemoryBlock block;
        REQUIRE (juce::File (__FILE__).getParentDirectory().getParentDirectory()
                     .getChildFile ("midi").getChildFile (name).loadFileAsData (block));
        const auto* d = static_cast<const std::uint8_t*> (block.getData());
        return Bytes (d, d + block.getSize());
    }

    TrackBody conductorBody()
    {
        TrackBody c;
        c.ev (0, { 0xFF, 0x51, 0x03, 0x07, 0xA1, 0x20 })   // tempo
         .ev (0, { 0xF0, 0x05, 0x7E, 0x7F, 0x09, 0x01, 0xF7 }) // GM on
         .eot (960);
        return c;
    }
}

TEST_CASE ("MidiImportPlan: a file with a conductor puts it in the conductor verbatim on first import", "[midiimportplan]")
{
    TrackBody melody;
    melody.ev (0, { 0xFF, 0x03, 0x01, 'M' }).ev (0, { 0xC0, 0x19 }).ev (0, { 0xB0, 0x07, 0x64 })
          .ev (0, { 0x90, 60, 100 }).ev (96, { 0x80, 60, 0x40 }).eot();
    const auto p = parse (smf (1, 96, { conductorBody(), melody }));

    Diagnostics diags;
    const auto plan = planMidiImport (p.song, p.raw, true, diags);

    CHECK (plan.writesConductor);
    REQUIRE (plan.conductorEvents.size() == 2);
    CHECK (plan.conductorEvents[0].bytes == p.raw.tracks[0].events[0].bytes);
    CHECK (plan.conductorEvents[1].order == 1);
    CHECK (plan.conductorEvents[1].relocatedFrom == -1);
    CHECK (plan.conductorEndTick == 960);
    CHECK (plan.relocatedEventCount == 0);

    REQUIRE (plan.tracks.size() == 1);
    const auto& t = plan.tracks[0];
    CHECK (t.rawTrackIndex == 1);
    CHECK (t.songTrackIndex == 0);
    CHECK (t.defaultChannel == 1);
    REQUIRE (t.noteLinks.size() == 1);
    CHECK (t.noteLinks[0].onOrder == 3);
    CHECK (t.noteLinks[0].offOrder == 4);
    CHECK (t.noteLinks[0].offVelocity == 0x40);
    CHECK_FALSE (t.noteLinks[0].offIsNoteOnZero);
    REQUIRE (t.events.size() == 3); // name, program, CC
    CHECK (t.events[0].order == 0);
    CHECK (t.events[2].order == 2);
    CHECK (diags.empty());
}

TEST_CASE ("MidiImportPlan: a later import drops the file's whole conductor track and counts it", "[midiimportplan]")
{
    TrackBody melody;
    melody.ev (0, { 0x90, 60, 100 }).ev (96, { 0x80, 60, 0x40 }).eot();
    const auto p = parse (smf (1, 96, { conductorBody(), melody }));

    Diagnostics diags;
    const auto plan = planMidiImport (p.song, p.raw, false, diags);

    CHECK_FALSE (plan.writesConductor);
    CHECK (plan.conductorEvents.empty());
    CHECK (plan.droppedEventCount == 2);
    REQUIRE (diags.size() == 1);
    CHECK (diags[0].severity == Severity::Info);
    CHECK (diags[0].source == "SongModelBridge");
}

TEST_CASE ("MidiImportPlan: without a conductor, song-wide metas move into it and everything else stays", "[midiimportplan]")
{
    // Format 1, notes in the first track -> no conductor.
    TrackBody first;
    first.ev (0, { 0xFF, 0x51, 0x03, 0x07, 0xA1, 0x20 })   // tempo     -> relocated
         .ev (0, { 0xFF, 0x05, 0x02, 'l', 'a' })            // lyric     -> stays
         .ev (0, { 0x90, 60, 100 })
         .ev (48, { 0xFF, 0x06, 0x01, 'B' })                // marker    -> relocated
         .ev (48, { 0x80, 60, 0x40 }).eot();
    TrackBody second;
    second.ev (0, { 0xFF, 0x59, 0x02, 0x00, 0x00 })        // key sig   -> relocated
          .ev (0, { 0x91, 64, 90 }).ev (96, { 0x91, 64, 0 }).eot();
    const auto p = parse (smf (1, 96, { first, second }));

    Diagnostics diags;
    const auto plan = planMidiImport (p.song, p.raw, true, diags);

    REQUIRE (plan.conductorEvents.size() == 3);
    CHECK (plan.relocatedEventCount == 3);
    CHECK (plan.conductorEvents[0].relocatedFrom == 0);
    CHECK (plan.conductorEvents[0].order == 0);
    CHECK (plan.conductorEvents[1].relocatedFrom == 0);
    CHECK (plan.conductorEvents[1].order == 3);
    CHECK (plan.conductorEvents[2].relocatedFrom == 1);
    CHECK (plan.conductorEvents[2].order == 0);

    REQUIRE (plan.tracks.size() == 2);
    REQUIRE (plan.tracks[0].events.size() == 1);
    CHECK (plan.tracks[0].events[0].bytes == Bytes { 0xFF, 0x05, 'l', 'a' });
    CHECK (plan.tracks[1].events.empty());
    REQUIRE (plan.tracks[1].noteLinks.size() == 1);
    CHECK (plan.tracks[1].noteLinks[0].channel == 2);
    CHECK (plan.tracks[1].noteLinks[0].offIsNoteOnZero);
    CHECK (plan.tracks[1].noteLinks[0].offVelocity == 0);
    CHECK (plan.tracks[1].defaultChannel == 2);
    REQUIRE (diags.size() == 1);
    CHECK (diags[0].severity == Severity::Info);
}

TEST_CASE ("MidiImportPlan: format 0 keeps each note's own channel", "[midiimportplan]")
{
    TrackBody t;
    t.ev (0, { 0xFF, 0x51, 0x03, 0x07, 0xA1, 0x20 })
     .ev (0, { 0x94, 64, 80 }).ev (0, { 0x99, 36, 90 })
     .ev (96, { 0x84, 64, 0 }).ev (0, { 0x89, 36, 0 }).eot();
    const auto p = parse (smf (0, 96, { t }));

    Diagnostics diags;
    const auto plan = planMidiImport (p.song, p.raw, true, diags);

    REQUIRE (plan.conductorEvents.size() == 1);
    REQUIRE (plan.tracks.size() == 1);
    REQUIRE (plan.tracks[0].noteLinks.size() == 2);
    CHECK (plan.tracks[0].noteLinks[0].channel == 5);
    CHECK (plan.tracks[0].noteLinks[1].channel == 10);
    CHECK (plan.tracks[0].defaultChannel == 5);
}

TEST_CASE ("MidiImportPlan: a note-less non-conductor track is planned from raw data", "[midiimportplan]")
{
    TrackBody lyrics;
    lyrics.ev (0, { 0xFF, 0x03, 0x01, 'a' }).ev (0, { 0xFF, 0x03, 0x01, 'b' })   // last name wins
          .ev (0, { 0xC2, 0x05 }).ev (10, { 0xFF, 0x05, 0x01, 'x' }).eot();
    TrackBody melody;
    melody.ev (0, { 0x90, 60, 100 }).ev (96, { 0x80, 60, 0x40 }).eot();
    const auto p = parse (smf (1, 96, { conductorBody(), lyrics, melody }));

    Diagnostics diags;
    const auto plan = planMidiImport (p.song, p.raw, true, diags);

    REQUIRE (plan.tracks.size() == 2);
    const auto& t = plan.tracks[0];
    CHECK (t.songTrackIndex == -1);
    CHECK (t.rawTrackIndex == 1);
    CHECK (t.name == "b");
    CHECK (t.sourceMidiChannel == 3);
    CHECK (t.sourceProgram == 5);
    CHECK (t.defaultChannel == 3);
    CHECK (t.events.size() == 4);
    CHECK (plan.tracks[1].songTrackIndex == 0);
}

TEST_CASE ("MidiImportPlan: skipped notes and unclaimed offs stay as events; the review's re-strike case links the right bytes", "[midiimportplan]")
{
    // Tick 0: on60 v100, off60, on60 v50, off60; later off60@10. importMidi
    // keeps one note: v100, duration 10 (on raw 0, off raw 4).
    TrackBody t;
    t.ev (0, { 0x90, 60, 100 }).ev (0, { 0x80, 60, 0x40 }).ev (0, { 0x90, 60, 50 }).ev (0, { 0x80, 60, 0x40 })
     .ev (10, { 0x80, 60, 0x40 }).eot();
    const auto p = parse (smf (1, 96, { conductorBody(), t }));

    Diagnostics diags;
    const auto plan = planMidiImport (p.song, p.raw, true, diags);

    REQUIRE (plan.tracks.size() == 1);
    REQUIRE (plan.tracks[0].noteLinks.size() == 1);
    CHECK (plan.tracks[0].noteLinks[0].onOrder == 0);
    CHECK (plan.tracks[0].noteLinks[0].offOrder == 4);
    REQUIRE (plan.tracks[0].events.size() == 3);
    CHECK (plan.tracks[0].events[0].order == 1);
    CHECK (plan.tracks[0].events[1].order == 2);
    CHECK (plan.tracks[0].events[2].order == 3);
    CHECK (diags.empty());
}

TEST_CASE ("MidiImportPlan: format 2 and parser disagreement throw MidiImportPlanError", "[midiimportplan]")
{
    TrackBody melody;
    melody.ev (0, { 0x90, 60, 100 }).ev (96, { 0x80, 60, 0x40 }).eot();

    SECTION ("format 2")
    {
        auto p = parse (smf (1, 96, { conductorBody(), melody }));
        p.raw.format = 2;
        Diagnostics diags;
        CHECK_THROWS_AS (planMidiImport (p.song, p.raw, true, diags), MidiImportPlanError);
        CHECK (diags.empty());
    }
    SECTION ("a note the raw parser can't find")
    {
        auto p = parse (smf (1, 96, { conductorBody(), melody }));
        p.song.tracks[0].notes[0].sourceEventIndex = 7;
        Diagnostics diags;
        CHECK_THROWS_AS (planMidiImport (p.song, p.raw, true, diags), MidiImportPlanError);
    }
    SECTION ("a raw track index out of range")
    {
        auto p = parse (smf (1, 96, { conductorBody(), melody }));
        p.song.tracks[0].notes[0].sourceTrackIndex = 9;
        Diagnostics diags;
        CHECK_THROWS_AS (planMidiImport (p.song, p.raw, true, diags), MidiImportPlanError);
    }
}

TEST_CASE ("MidiImportPlan: every raw event of every fixture is accounted for exactly once", "[midiimportplan]")
{
    for (const auto* name : { "blue.mid", "leah.mid", "angels.mid", "syn5.mid" })
    {
        const auto p = parse (fixtureBytes (name));
        Diagnostics diags;
        const auto plan = planMidiImport (p.song, p.raw, true, diags);
        CHECK (diags.empty());

        CHECK (plan.conductorEvents.size() == p.raw.tracks[0].events.size());
        for (const auto& t : plan.tracks)
        {
            size_t claimed = 0;
            for (const auto& link : t.noteLinks)
                claimed += link.offSynthesized ? 1 : 2;
            CHECK (claimed + t.events.size() == p.raw.tracks[(size_t) t.rawTrackIndex].events.size());
        }
    }
}
```

- [ ] **Step 2: Register files, run to verify failure**

Add `MidiImportPlan_tests.cpp` + `${CMAKE_SOURCE_DIR}/Source/UI/MidiImportPlan.cpp` to `Tests/CMakeLists.txt` and `Source/UI/MidiImportPlan.cpp` to `forge_ui`. Create an empty `.cpp`.

Run: `cmake --build build` → compile FAIL (header missing).

- [ ] **Step 3: Write the header**

`Source/UI/MidiImportPlan.h`: the declarations in **Interfaces** above, with includes `"Core/Diagnostics.h"`, `"Core/Song.h"`, `"RawMidi.h"`, `<cstdint>`, `<stdexcept>`, `<string>`, `<vector>`, and this comment at the top:

```cpp
// Pure import planning for the MIDI-fidelity path (2026-10-03 spec, "Conductor
// rules" and "Import"). Given importMidi's Song and the lossless RawMidiFile
// of the same bytes, decides what goes into the song's conductor, which raw
// events each Song note is tied to, and which raw events become EVENT nodes.
// Never touches a SongDocument.
```

- [ ] **Step 4: Write the implementation**

`Source/UI/MidiImportPlan.cpp`:

```cpp
#include "MidiImportPlan.h"
#include "JuceNoteReplica.h"

#include <juce_core/juce_core.h> // jassertfalse only

#include <map>

namespace lotro
{

namespace
{
    bool isChannelMessage (const RawMidiEvent& e)
    {
        return ! e.bytes.empty() && e.bytes[0] >= 0x80 && e.bytes[0] < 0xF0;
    }

    int channelOf (const RawMidiEvent& e) { return (e.bytes[0] & 0x0F) + 1; }

    bool isVelocityNoteOn (const RawMidiEvent& e)
    {
        return isChannelMessage (e) && (e.bytes[0] & 0xF0) == 0x90 && e.bytes.size() > 2 && e.bytes[2] > 0;
    }

    int firstChannel (const RawMidiTrack& track, int fallback)
    {
        for (const auto& e : track.events)
            if (isChannelMessage (e))
                return channelOf (e);
        return fallback;
    }

    int firstProgram (const RawMidiTrack& track)
    {
        for (const auto& e : track.events)
            if (isChannelMessage (e) && (e.bytes[0] & 0xF0) == 0xC0 && e.bytes.size() > 1)
                return e.bytes[1];
        return 0;
    }

    // importMidi overwrites the name on every 0x03, so the last one wins.
    std::string lastTrackName (const RawMidiTrack& track, std::string fallback)
    {
        for (const auto& e : track.events)
            if (e.bytes.size() >= 2 && e.bytes[0] == 0xFF && e.bytes[1] == 0x03)
                fallback.assign (e.bytes.begin() + 2, e.bytes.end());
        return fallback;
    }

    bool hasEligibleNote (const std::vector<ReplicaNoteOn>& replica)
    {
        for (const auto& on : replica)
            if (on.hasOff && on.offTick > on.onTick)
                return true;
        return false;
    }

    void linkNotes (const Track& songTrack, const RawMidiTrack& rawTrack, PlannedTrack& planned,
                    std::vector<bool>& claimed, Diagnostics& diagnostics)
    {
        const auto replica = replicateJuceNoteOns (rawTrack);

        // importMidi keeps exactly the note-ons JUCE paired with a later off.
        std::vector<int> eligible;
        for (int k = 0; k < (int) replica.size(); ++k)
            if (replica[(size_t) k].hasOff && replica[(size_t) k].offTick > replica[(size_t) k].onTick)
                eligible.push_back (k);

        const auto where = " on MIDI track " + std::to_string (planned.rawTrackIndex);
        if (eligible.size() != songTrack.notes.size())
            throw MidiImportPlanError ("the two MIDI parsers disagree on the note count" + where);

        for (size_t n = 0; n < songTrack.notes.size(); ++n)
        {
            const auto& note = songTrack.notes[n];
            if (note.sourceTrackIndex != planned.rawTrackIndex || note.sourceEventIndex != eligible[n])
                throw MidiImportPlanError ("the two MIDI parsers disagree on note order" + where);

            const auto& on = replica[(size_t) eligible[n]];

            PlannedNoteLink link;
            link.channel        = on.channel;
            link.onOrder        = on.onRawIndex;
            link.offSynthesized = on.offSynthesized;
            claimed[(size_t) on.onRawIndex] = true;

            if (! on.offSynthesized)
            {
                const auto& off = rawTrack.events[(size_t) on.offRawIndex].bytes;
                link.offOrder        = on.offRawIndex;
                link.offIsNoteOnZero = (off[0] & 0xF0) == 0x90;
                link.offVelocity     = off.size() > 2 ? off[2] : 0;
                claimed[(size_t) on.offRawIndex] = true;
            }

            if (note.pitch != on.pitch || note.velocity != on.velocity || note.startTick != on.onTick
                || note.durationTicks != on.offTick - on.onTick || note.isDrum != (on.channel == 10))
            {
                jassertfalse; // the replica disagrees with JUCE: a bug in JuceNoteReplica
                Diagnostic d;
                d.source           = "SongModelBridge";
                d.severity         = Severity::Warning;
                d.message          = "Note link mismatch" + where + "; exported MIDI may differ from the original";
                d.sourceTrackIndex = planned.rawTrackIndex;
                d.sourceEventIndex = eligible[n];
                diagnostics.push_back (std::move (d));
            }

            planned.noteLinks.push_back (link);
        }
    }

    void info (Diagnostics& diagnostics, std::string message)
    {
        Diagnostic d;
        d.source   = "SongModelBridge";
        d.severity = Severity::Info;
        d.message  = std::move (message);
        diagnostics.push_back (std::move (d));
    }
}

bool isSongWideMetaEvent (const RawMidiEvent& event)
{
    if (event.bytes.size() < 2 || event.bytes[0] != 0xFF)
        return false;

    switch (event.bytes[1])
    {
        case 0x51: case 0x58: case 0x59: case 0x54: case 0x06: case 0x02: return true;
        default: return false;
    }
}

bool hasConductorTrack (const RawMidiFile& raw)
{
    if (raw.format != 1 || raw.tracks.empty())
        return false;

    for (const auto& e : raw.tracks.front().events)
        if (isVelocityNoteOn (e))
            return false;
    return true;
}

MidiImportPlan planMidiImport (const Song& song, const RawMidiFile& raw, bool isFirstImport,
                               Diagnostics& diagnostics)
{
    if (raw.format == 2)
        throw MidiImportPlanError ("MIDI format 2 is not supported");

    Diagnostics local; // only reaches `diagnostics` if planning succeeds
    MidiImportPlan plan;
    plan.writesConductor = isFirstImport;
    const bool fileHasConductor = hasConductorTrack (raw);

    std::map<int, int> songTrackForRaw;
    for (int s = 0; s < (int) song.tracks.size(); ++s)
    {
        const auto& notes = song.tracks[(size_t) s].notes;
        const int rawIndex = notes.empty() ? -1 : notes.front().sourceTrackIndex;
        if (rawIndex < 0 || rawIndex >= (int) raw.tracks.size())
            throw MidiImportPlanError ("the two MIDI parsers disagree on the track count");
        songTrackForRaw[rawIndex] = s;
    }

    for (int r = 0; r < (int) raw.tracks.size(); ++r)
    {
        const auto& rawTrack = raw.tracks[(size_t) r];

        if (fileHasConductor && r == 0)
        {
            if (isFirstImport)
            {
                for (int i = 0; i < (int) rawTrack.events.size(); ++i)
                    plan.conductorEvents.push_back ({ rawTrack.events[(size_t) i].tick, i, -1, rawTrack.events[(size_t) i].bytes });
                plan.conductorEndTick = rawTrack.endTick;
            }
            else
            {
                plan.droppedEventCount += (int) rawTrack.events.size();
            }
            continue;
        }

        PlannedTrack planned;
        planned.rawTrackIndex  = r;
        planned.endTick        = rawTrack.endTick;
        planned.defaultChannel = firstChannel (rawTrack, 1);
        std::vector<bool> claimed (rawTrack.events.size(), false);

        const auto found = songTrackForRaw.find (r);
        if (found != songTrackForRaw.end())
        {
            planned.songTrackIndex = found->second;
            linkNotes (song.tracks[(size_t) found->second], rawTrack, planned, claimed, local);
        }
        else
        {
            if (hasEligibleNote (replicateJuceNoteOns (rawTrack)))
                throw MidiImportPlanError ("the two MIDI parsers disagree about notes on MIDI track " + std::to_string (r));

            planned.name              = lastTrackName (rawTrack, "Track " + std::to_string (r));
            planned.sourceMidiChannel = firstChannel (rawTrack, 0);
            planned.sourceProgram     = firstProgram (rawTrack);
        }

        for (int i = 0; i < (int) rawTrack.events.size(); ++i)
        {
            if (claimed[(size_t) i])
                continue;

            const auto& e = rawTrack.events[(size_t) i];
            if (! fileHasConductor && isSongWideMetaEvent (e))
            {
                if (isFirstImport)
                {
                    plan.conductorEvents.push_back ({ e.tick, i, r, e.bytes });
                    ++plan.relocatedEventCount;
                }
                else
                {
                    ++plan.droppedEventCount;
                }
                continue;
            }

            planned.events.push_back ({ e.tick, i, -1, e.bytes });
        }

        plan.tracks.push_back (std::move (planned));
    }

    if (plan.relocatedEventCount > 0)
        info (local, "Moved " + std::to_string (plan.relocatedEventCount)
                     + " song-wide event(s) (tempo, meter, key, SMPTE, marker, copyright) into the conductor track");
    if (plan.droppedEventCount > 0)
        info (local, "Dropped " + std::to_string (plan.droppedEventCount)
                     + " song-wide event(s) from a later import; the first import's conductor track is kept");

    diagnostics.insert (diagnostics.end(), local.begin(), local.end());
    return plan;
}

} // namespace lotro
```

(Relocated events are appended in raw-track order, then raw order within a track. Export sorts the conductor by tick, then `relocatedFrom`, then `order` (Task 7), so this order isn't load-bearing.)

- [ ] **Step 5: Run the tests**

Run: `cmake --build build && ctest --test-dir build -R "MidiImportPlan:" --output-on-failure`
Expected: PASS.

- [ ] **Step 6: Whole suite, commit**

Run: `ctest --test-dir build --output-on-failure` → all pass.

```bash
git add Source/UI/MidiImportPlan.h Source/UI/MidiImportPlan.cpp Tests/MidiImportPlan_tests.cpp CMakeLists.txt Tests/CMakeLists.txt
git commit -m "feat(songsmith): plan lossless MIDI imports (conductor rules, note links, events)

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 6: Write the plan into the document (`appendImportedMidi`, raw-aware `importMidiFile`)

**Files:**
- Modify: `Source/UI/SongModelBridge.h`, `Source/UI/SongModelBridge.cpp`
- Create: `Tests/MidiImport_tests.cpp`
- Modify: `Tests/CMakeLists.txt`, `Tests/SongsmithRoundTrip_tests.cpp` (positional alignment)

**Interfaces:**
- Consumes: `planMidiImport`, `MidiImportPlan` (Task 5); `readMidiBytes` (Task 1); `getNotesNode`/`getEventsNode`/`getConductorTrack` (Tasks 3–4).
- Produces:
  ```cpp
  // Raw-aware import. On a planning failure (format 2, parser disagreement)
  // appends one Error diagnostic (source "SongModelBridge"), leaves the
  // document unchanged and returns false. On success, appends
  // `importerDiagnostics` (with trackIndex remapped to the document row --
  // the MIDI_TRACK's SOURCE_MIDI child index), then the plan's diagnostics,
  // then the bridge's own, and returns true.
  bool appendImportedMidi (SongDocument& doc, const Song& imported, const RawMidiFile& raw,
                           int importBatch, Diagnostics& diagnostics,
                           const Diagnostics& importerDiagnostics = {});
  ```
  `appendImportedSong` keeps its signature (the raw-less path): it creates `NOTE`s only, each with `channel = isDrum ? 10 : 1` and `offVelocity = 64`, and never touches the conductor or `EVENTS`.
  `EVENT` nodes: `tick` (int), `order` (int), optional `relocatedFrom` (int), `data` (`juce::MemoryBlock`).

- [ ] **Step 1: Write the failing tests**

`Tests/MidiImport_tests.cpp`:

```cpp
// MidiImport: importMidiFile's raw-aware path writes every raw event into
// the document (conductor, NOTE links, EVENTs), rescales events with notes,
// and refuses files the two parsers disagree on.

#include "UI/SongModelBridge.h"
#include "UI/RawMidi.h"
#include "MidiTestBytes.h"

#include <catch2/catch_test_macros.hpp>

#include <juce_core/juce_core.h>

using namespace lotro;
using namespace miditest;

namespace
{
    juce::File midiFixture (const std::string& name)
    {
        return juce::File (__FILE__).getParentDirectory().getParentDirectory()
                   .getChildFile ("midi").getChildFile (name);
    }

    struct TempMidi
    {
        juce::File file = juce::File::createTempFile (".mid");
        explicit TempMidi (const Bytes& bytes) { REQUIRE (file.replaceWithData (bytes.data(), bytes.size())); }
        ~TempMidi() { file.deleteFile(); }
    };

    Bytes eventBytes (const juce::ValueTree& event)
    {
        const auto* block = event.getProperty (SongIDs::data).getBinaryData();
        REQUIRE (block != nullptr);
        const auto* d = static_cast<const std::uint8_t*> (block->getData());
        return Bytes (d, d + block->getSize());
    }

    TrackBody conductorBody()
    {
        TrackBody c;
        c.ev (0, { 0xFF, 0x51, 0x03, 0x07, 0xA1, 0x20 }).ev (0, { 0xF0, 0x05, 0x7E, 0x7F, 0x09, 0x01, 0xF7 }).eot (960);
        return c;
    }
}

TEST_CASE ("MidiImport: a fixture's conductor, notes and events all land in the document", "[midiimport]")
{
    const auto file = midiFixture ("angels.mid");
    juce::MemoryBlock block;
    REQUIRE (file.loadFileAsData (block));
    const auto* d = static_cast<const std::uint8_t*> (block.getData());
    const auto raw = readMidiBytes (Bytes (d, d + block.getSize()), "angels");

    SongDocument doc;
    Diagnostics diags;
    REQUIRE (importMidiFile (doc, file, 1, diags));

    auto conductor = doc.getConductorTrack();
    CHECK (SongDocument::getEventsNode (conductor).getNumChildren() == (int) raw.tracks[0].events.size());
    CHECK ((int) conductor.getProperty (SongIDs::endTick) == raw.tracks[0].endTick);

    // Every other raw track became a MIDI_TRACK, note-less ones included.
    REQUIRE (doc.getNumTracks() == (int) raw.tracks.size());
    for (int r = 1; r < (int) raw.tracks.size(); ++r)
    {
        auto track = doc.getTrack (r);
        CHECK ((int) track.getProperty (SongIDs::sourceTrackIndex) == r);
        CHECK ((int) track.getProperty (SongIDs::endTick) == raw.tracks[(size_t) r].endTick);

        int claimed = 0;
        for (auto note : SongDocument::getNotesNode (track))
            claimed += (bool) note.getProperty (SongIDs::offSynthesized) ? 1 : 2;
        CHECK (claimed + SongDocument::getEventsNode (track).getNumChildren()
               == (int) raw.tracks[(size_t) r].events.size());

        for (auto event : SongDocument::getEventsNode (track))
        {
            const int order = (int) event.getProperty (SongIDs::order);
            CHECK (eventBytes (event) == raw.tracks[(size_t) r].events[(size_t) order].bytes);
            CHECK ((int) event.getProperty (SongIDs::tick) == raw.tracks[(size_t) r].events[(size_t) order].tick);
        }
    }
}

TEST_CASE ("MidiImport: linked NOTEs carry channel, orders and note-off style", "[midiimport]")
{
    TrackBody melody;
    melody.ev (0, { 0x93, 60, 100 }).ev (96, { 0x93, 60, 0 }).ev (0, { 0x93, 62, 90 }).ev (96, { 0x83, 62, 0x30 }).eot();
    TempMidi tmp (smf (1, 96, { conductorBody(), melody }));

    SongDocument doc;
    Diagnostics diags;
    REQUIRE (importMidiFile (doc, tmp.file, 1, diags));

    auto notes = SongDocument::getNotesNode (doc.getTrack (1));
    REQUIRE (notes.getNumChildren() == 2);
    CHECK ((int) notes.getChild (0).getProperty (SongIDs::channel) == 4);
    CHECK ((int) notes.getChild (0).getProperty (SongIDs::onOrder) == 0);
    CHECK ((int) notes.getChild (0).getProperty (SongIDs::offOrder) == 1);
    CHECK ((bool) notes.getChild (0).getProperty (SongIDs::offIsNoteOnZero));
    CHECK ((int) notes.getChild (1).getProperty (SongIDs::offVelocity) == 0x30);
    CHECK_FALSE ((bool) notes.getChild (1).getProperty (SongIDs::offIsNoteOnZero));
    CHECK ((int) doc.getTrack (1).getProperty (SongIDs::defaultChannel) == 4);
}

TEST_CASE ("MidiImport: format 2 is refused with an Error and the document is unchanged", "[midiimport]")
{
    TrackBody melody;
    melody.ev (0, { 0x90, 60, 100 }).ev (96, { 0x80, 60, 0x40 }).eot();
    TempMidi tmp (smf (2, 96, { melody }));

    SongDocument doc;
    Diagnostics diags;
    CHECK_FALSE (importMidiFile (doc, tmp.file, 1, diags));
    REQUIRE (diags.size() == 1);
    CHECK (diags[0].severity == Severity::Error);
    CHECK (doc.getNumTracks() == 1);
    CHECK (doc.getTempoMapNode().getNumChildren() == 0);
    CHECK (SongDocument::getEventsNode (doc.getConductorTrack()).getNumChildren() == 0);
    CHECK (doc.getTree().getProperty (SongIDs::inputMidiPath).toString().isEmpty());
}

TEST_CASE ("MidiImport: a later import keeps the first conductor and reports what it dropped", "[midiimport]")
{
    TrackBody melody;
    melody.ev (0, { 0x90, 60, 100 }).ev (96, { 0x80, 60, 0x40 }).eot();
    TempMidi first (smf (1, 96, { conductorBody(), melody }));
    TempMidi second (smf (1, 96, { conductorBody(), melody }));

    SongDocument doc;
    Diagnostics d1, d2;
    REQUIRE (importMidiFile (doc, first.file, 1, d1));
    const int conductorEvents = SongDocument::getEventsNode (doc.getConductorTrack()).getNumChildren();

    REQUIRE (importMidiFile (doc, second.file, 2, d2));
    CHECK (SongDocument::getEventsNode (doc.getConductorTrack()).getNumChildren() == conductorEvents);
    CHECK (doc.getNumTracks() == 3);
    bool sawDropInfo = false;
    for (const auto& d : d2)
        sawDropInfo |= d.severity == Severity::Info && d.message.find ("Dropped 2 song-wide") != std::string::npos;
    CHECK (sawDropInfo);
}

TEST_CASE ("MidiImport: a later lower-PPQ import rescales its EVENT ticks and endTick", "[midiimport]")
{
    TrackBody firstMelody;
    firstMelody.ev (0, { 0x90, 60, 100 }).ev (480, { 0x80, 60, 0x40 }).eot();
    TrackBody secondMelody;
    secondMelody.ev (0, { 0x90, 60, 100 }).ev (48, { 0xB0, 0x07, 0x50 }).ev (48, { 0x80, 60, 0x40 }).eot (24);
    TempMidi first (smf (1, 480, { conductorBody(), firstMelody }));
    TempMidi second (smf (1, 96, { conductorBody(), secondMelody }));

    SongDocument doc;
    Diagnostics d1, d2;
    REQUIRE (importMidiFile (doc, first.file, 1, d1));
    REQUIRE (importMidiFile (doc, second.file, 2, d2));

    auto incoming = doc.getTrack (2);
    auto events = SongDocument::getEventsNode (incoming);
    REQUIRE (events.getNumChildren() == 1);
    CHECK ((int) events.getChild (0).getProperty (SongIDs::tick) == 240);  // 48 * 480/96
    CHECK ((int) incoming.getProperty (SongIDs::endTick) == 600);          // 120 * 480/96
}

TEST_CASE ("MidiImport: an LCM raise rescales existing EVENT ticks and endTicks, conductor included", "[midiimport]")
{
    TrackBody firstMelody;
    firstMelody.ev (0, { 0x90, 60, 100 }).ev (60, { 0xB0, 0x07, 0x50 }).ev (60, { 0x80, 60, 0x40 }).eot();
    TrackBody secondMelody;
    secondMelody.ev (0, { 0x90, 60, 100 }).ev (480, { 0x80, 60, 0x40 }).eot();
    TrackBody firstConductor;
    firstConductor.ev (0, { 0xFF, 0x51, 0x03, 0x07, 0xA1, 0x20 }).ev (120, { 0xFF, 0x06, 0x01, 'A' }).eot (240);
    TempMidi first (smf (1, 120, { firstConductor, firstMelody }));
    TempMidi second (smf (1, 480, { conductorBody(), secondMelody }));

    SongDocument doc;
    Diagnostics d1, d2;
    REQUIRE (importMidiFile (doc, first.file, 1, d1));
    REQUIRE (importMidiFile (doc, second.file, 2, d2));
    REQUIRE ((int) doc.getSourceMidiNode().getProperty (SongIDs::ticksPerQuarter) == 480);

    auto conductorEvents = SongDocument::getEventsNode (doc.getConductorTrack());
    CHECK ((int) conductorEvents.getChild (1).getProperty (SongIDs::tick) == 480);
    CHECK ((int) doc.getConductorTrack().getProperty (SongIDs::endTick) == 960);
    CHECK ((int) SongDocument::getEventsNode (doc.getTrack (1)).getChild (0).getProperty (SongIDs::tick) == 240);
    CHECK ((int) doc.getTrack (1).getProperty (SongIDs::endTick) == 480);
}

TEST_CASE ("MidiImport: importer diagnostics point at the document row of their track", "[midiimport]")
{
    TrackBody lyrics;
    lyrics.ev (0, { 0xFF, 0x05, 0x01, 'x' }).eot();
    TrackBody melody;
    melody.ev (0, { 0x90, 60, 100 }).ev (0, { 0x90, 64, 100 }).ev (96, { 0x80, 60, 0x40 }).eot(); // 64 never ends
    TempMidi tmp (smf (1, 96, { conductorBody(), lyrics, melody }));

    SongDocument doc;
    Diagnostics diags;
    REQUIRE (importMidiFile (doc, tmp.file, 1, diags));

    bool found = false;
    for (const auto& d : diags)
        if (d.source == "MidiImporter")
        {
            found = true;
            CHECK (d.trackIndex == 2); // row 0 conductor, row 1 lyrics, row 2 melody
        }
    CHECK (found);
}
```

Also update `Tests/SongsmithRoundTrip_tests.cpp`, where note-less tracks now interleave. In each multi-import test (~lines 226–240, 309–320, 351–360, 475–490), build the list of the second import's assignable tracks and align that with `directSong.tracks`:

```cpp
    std::vector<juce::ValueTree> secondImportTracks;
    for (int t = tracksAfterFirst; t < doc.getNumTracks(); ++t)
        if (SongDocument::isAssignableTrack (doc.getTrack (t)))
            secondImportTracks.push_back (doc.getTrack (t));

    REQUIRE (secondImportTracks.size() == directSong.tracks.size());
    for (size_t t = 0; t < directSong.tracks.size(); ++t)
    {
        auto trackTree = secondImportTracks[t];
        // … the existing per-note comparison, unchanged …
    }
```

`REQUIRE (doc.getNumTracks() == tracksAfterFirst + (int) directSong.tracks.size())` becomes the `secondImportTracks.size()` check above.

- [ ] **Step 2: Register and run to verify failure**

Add `MidiImport_tests.cpp` to `Tests/CMakeLists.txt`.
Run: `cmake --build build && ctest --test-dir build -R "MidiImport:" --output-on-failure`
Expected: FAIL. The conductor has no events yet, and note-less tracks are missing.

- [ ] **Step 3: Implement**

In `SongModelBridge.h`, add `#include "RawMidi.h"` and the `appendImportedMidi` declaration from **Interfaces**, with its comment. Update `importMidiFile`'s comment: "parses with both importMidi and readMidiFile and appends via appendImportedMidi; returns false (Error diagnostic, document unchanged) if either parser fails or they disagree".

In `SongModelBridge.cpp`, add `#include "MidiImportPlan.h"`, then:

1. **Make the current `appendImportedSong` body an internal function** in the anonymous namespace:
   `void appendImport (SongDocument& doc, const Song& imported, const MidiImportPlan* plan, int importBatch, Diagnostics& diagnostics)`.
   The public `appendImportedSong` becomes `{ appendImport (doc, imported, nullptr, importBatch, diagnostics); }`.

2. **Rescale events in `rescaleExistingDocumentTicks`.** For every track (conductor included), after the notes loop:

```cpp
            for (auto eventTree : SongDocument::getEventsNode (trackTree))
                eventTree.setProperty (SongIDs::tick, (int) eventTree.getProperty (SongIDs::tick) * factor, nullptr);
            trackTree.setProperty (SongIDs::endTick, (int) trackTree.getProperty (SongIDs::endTick) * factor, nullptr);
```

3. **In `appendImport`, extract the note-writing loop into a lambda** that also writes the link fields:

```cpp
    int rescaledEventValueCount = 0;
    auto rescaleOther = [&] (int tick)
    {
        if (! needsRescale)
            return tick;
        ++rescaledEventValueCount;
        if (! isExactRescale (tick, docPpq, importedPpq))
            ++roundedValueCount;
        return rescaleTick (tick, docPpq, importedPpq);
    };

    auto appendEvents = [&] (juce::ValueTree eventsNode, const std::vector<PlannedEvent>& events)
    {
        for (const auto& e : events)
        {
            juce::ValueTree eventTree (SongIDs::EVENT);
            eventTree.setProperty (SongIDs::tick, rescaleOther (e.tick), nullptr);
            eventTree.setProperty (SongIDs::order, e.order, nullptr);
            if (e.relocatedFrom >= 0)
                eventTree.setProperty (SongIDs::relocatedFrom, e.relocatedFrom, nullptr);
            eventTree.setProperty (SongIDs::data, juce::var (juce::MemoryBlock (e.bytes.data(), e.bytes.size())), nullptr);
            SongDocument::appendChildBulk (eventsNode, eventTree);
        }
    };

    auto addSongTrack = [&] (const Track& track, const std::vector<PlannedNoteLink>* links) -> juce::ValueTree
    {
        const auto family = SongsmithColours::gmFamilyFor (track.sourceProgram, track.sourceMidiChannel);
        const auto colorArgb = (int) SongsmithColours::trackColourFor (family, familyCounts[(size_t) family]++);
        auto trackTree = doc.addTrackBulk (track.name, colorArgb, track.sourceMidiChannel, importBatch);
        trackTree.setProperty (SongIDs::sourceProgram, track.sourceProgram, nullptr);

        for (size_t n = 0; n < track.notes.size(); ++n)
        {
            const auto& note = track.notes[n];
            // … existing startTick/durationTicks rescale + counters, unchanged …

            juce::ValueTree noteTree (SongIDs::NOTE);
            // … existing seven setProperty calls, unchanged …

            if (links != nullptr)
            {
                const auto& link = (*links)[n];
                noteTree.setProperty (SongIDs::channel, link.channel, nullptr);
                noteTree.setProperty (SongIDs::onOrder, link.onOrder, nullptr);
                if (! link.offSynthesized)
                    noteTree.setProperty (SongIDs::offOrder, link.offOrder, nullptr);
                noteTree.setProperty (SongIDs::offSynthesized, link.offSynthesized, nullptr);
                noteTree.setProperty (SongIDs::offVelocity, link.offVelocity, nullptr);
                noteTree.setProperty (SongIDs::offIsNoteOnZero, link.offIsNoteOnZero, nullptr);
            }
            else
            {
                noteTree.setProperty (SongIDs::channel, note.isDrum ? 10 : 1, nullptr);
                noteTree.setProperty (SongIDs::offVelocity, 64, nullptr);
            }

            SongDocument::appendChildBulk (SongDocument::getNotesNode (trackTree), noteTree);
        }
        return trackTree;
    };
```

4. **Replace the `for (const auto& track : imported.tracks)` loop** with:

```cpp
    if (plan == nullptr)
    {
        for (const auto& track : imported.tracks)
            addSongTrack (track, nullptr);
    }
    else
    {
        for (const auto& planned : plan->tracks)
        {
            juce::ValueTree trackTree;
            if (planned.songTrackIndex >= 0)
            {
                trackTree = addSongTrack (imported.tracks[(size_t) planned.songTrackIndex], &planned.noteLinks);
            }
            else
            {
                // Note-less tracks take their family's next shade without
                // advancing it, so they never shift note tracks' colours.
                const auto family = SongsmithColours::gmFamilyFor (planned.sourceProgram, planned.sourceMidiChannel);
                const auto colorArgb = (int) SongsmithColours::trackColourFor (family, familyCounts[(size_t) family]);
                trackTree = doc.addTrackBulk (planned.name, colorArgb, planned.sourceMidiChannel, importBatch);
                trackTree.setProperty (SongIDs::sourceProgram, planned.sourceProgram, nullptr);
            }

            trackTree.setProperty (SongIDs::sourceTrackIndex, planned.rawTrackIndex, nullptr);
            trackTree.setProperty (SongIDs::defaultChannel, planned.defaultChannel, nullptr);
            trackTree.setProperty (SongIDs::endTick, rescaleOther (planned.endTick), nullptr);
            appendEvents (SongDocument::getEventsNode (trackTree), planned.events);
        }

        if (plan->writesConductor) // first import only, so never rescaled
        {
            auto conductor = doc.getConductorTrack();
            appendEvents (SongDocument::getEventsNode (conductor), plan->conductorEvents);
            conductor.setProperty (SongIDs::endTick, plan->conductorEndTick, nullptr);
        }
    }
```

5. **Rescale-diagnostic condition:** change `else if (needsRescale && rescaledNoteCount > 0)` to `else if (needsRescale && (rescaledNoteCount > 0 || rescaledEventValueCount > 0))`.

6. **Add `appendImportedMidi`:**

```cpp
bool appendImportedMidi (SongDocument& doc, const Song& imported, const RawMidiFile& raw,
                         int importBatch, Diagnostics& diagnostics,
                         const Diagnostics& importerDiagnostics)
{
    const bool isFirstImport = doc.getTempoMapNode().getNumChildren() == 0;

    Diagnostics planDiagnostics;
    MidiImportPlan plan;
    try
    {
        plan = planMidiImport (imported, raw, isFirstImport, planDiagnostics);
    }
    catch (const MidiImportPlanError& e)
    {
        Diagnostic d;
        d.source   = "SongModelBridge";
        d.severity = Severity::Error;
        d.message  = std::string ("Could not import MIDI file: ") + e.what();
        diagnostics.push_back (std::move (d));
        return false;
    }

    // importMidi's trackIndex counts only tracks with notes; point it at the
    // document row (SOURCE_MIDI child index) instead.
    const int firstRow = doc.getNumTracks();
    std::map<int, int> rowForSongTrack;
    for (int p = 0; p < (int) plan.tracks.size(); ++p)
        if (plan.tracks[(size_t) p].songTrackIndex >= 0)
            rowForSongTrack[plan.tracks[(size_t) p].songTrackIndex] = firstRow + p;

    for (auto d : importerDiagnostics)
    {
        if (d.trackIndex >= 0)
        {
            const auto found = rowForSongTrack.find (d.trackIndex);
            d.trackIndex = found != rowForSongTrack.end() ? found->second : -1;
        }
        diagnostics.push_back (std::move (d));
    }
    diagnostics.insert (diagnostics.end(), planDiagnostics.begin(), planDiagnostics.end());

    appendImport (doc, imported, &plan, importBatch, diagnostics);
    return true;
}
```

7. **Rewrite `importMidiFile`'s parsing.** Read the file into memory once with `juce::File::loadFileAsData`, then parse twice:

```cpp
bool importMidiFile (SongDocument& doc, const juce::File& midiFile, int importBatch,
                     Diagnostics& diagnostics)
{
    juce::MemoryBlock block;
    if (! midiFile.existsAsFile() || ! midiFile.loadFileAsData (block))
    {
        Diagnostic d;
        d.source   = "SongModelBridge";
        d.severity = Severity::Error;
        d.message  = "Could not open MIDI file: " + midiFile.getFullPathName().toStdString();
        diagnostics.push_back (std::move (d));
        return false;
    }

    const auto sourceName = midiFile.getFileNameWithoutExtension().toStdString();
    const auto* begin = static_cast<const std::uint8_t*> (block.getData());
    const std::vector<std::uint8_t> bytes (begin, begin + block.getSize());

    Song imported;
    RawMidiFile raw;
    Diagnostics importerDiagnostics;
    try
    {
        std::istringstream input (std::string (bytes.begin(), bytes.end()), std::ios::binary);
        imported = importMidi (input, sourceName, importerDiagnostics);
        raw      = readMidiBytes (bytes, sourceName);
    }
    catch (const MidiImportError& e)
    {
        Diagnostic d;
        d.source   = "SongModelBridge";
        d.severity = Severity::Error;
        d.message  = std::string ("Malformed MIDI file: ") + e.what();
        diagnostics.push_back (std::move (d));
        return false;
    }

    if (! appendImportedMidi (doc, imported, raw, importBatch, diagnostics, importerDiagnostics))
        return false;

    // R3: first import's filename wins for SONG.inputMidiPath.
    if (doc.getTree().getProperty (SongIDs::inputMidiPath).toString().isEmpty())
        doc.getTree().setProperty (SongIDs::inputMidiPath, midiFile.getFullPathName(), nullptr);

    return true;
}
```

`loadFileAsData` on an empty file succeeds with an empty block; `importMidi` then throws `MidiImportError` ("MIDI input is empty"), which still gives exactly one Error diagnostic. `std::ifstream` is no longer needed; keep `<sstream>`.

- [ ] **Step 4: Run the tests**

Run: `cmake --build build && ctest --test-dir build -R "MidiImport:|SongsmithRoundTrip:|SongModelBridge:" --output-on-failure`
Expected: all PASS, including the ABC byte-identical pin.

- [ ] **Step 5: Whole suite, commit**

Run: `ctest --test-dir build --output-on-failure` → all pass.

```bash
git add Source/UI/SongModelBridge.h Source/UI/SongModelBridge.cpp Tests/MidiImport_tests.cpp Tests/SongsmithRoundTrip_tests.cpp Tests/CMakeLists.txt
git commit -m "feat(songsmith): keep every imported MIDI event in the song document

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 7: Export (`buildRawMidiFile`) and the fidelity integration tests

**Files:**
- Create: `Source/UI/MidiExport.h`, `Source/UI/MidiExport.cpp`, `Tests/MidiFidelity_tests.cpp`
- Modify: `CMakeLists.txt`, `Tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `SongDocument` + `SongIDs` (Tasks 3–4, 6), `RawMidiFile` (Task 1).
- Produces: `RawMidiFile buildRawMidiFile (const SongDocument& doc);` in `MidiExport.h` (namespace `lotro`). It's a pure function and never throws. It reads `NOTE.channel` (default 1), `offVelocity` (default 64), `offIsNoteOnZero` (default false), `offSynthesized` (default false), `onOrder`/`offOrder` (optional), `EVENT.order`/`relocatedFrom` (optional), and `MIDI_TRACK.endTick` (default 0).

- [ ] **Step 1: Write the failing tests**

`Tests/MidiFidelity_tests.cpp`:

```cpp
// MidiFidelity: import -> export reproduces the original MIDI event-for-event
// (spec "Goals"), including the hard note-pairing cases, and survives edits,
// mixed PPQ and an empty song.

#include "UI/MidiExport.h"
#include "UI/MidiImportPlan.h"
#include "UI/RawMidi.h"
#include "UI/SongModelBridge.h"
#include "MidiTestBytes.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <juce_core/juce_core.h>

#include <algorithm>
#include <tuple>

using namespace lotro;
using namespace miditest;

namespace
{
    juce::File midiFixture (const std::string& name)
    {
        return juce::File (__FILE__).getParentDirectory().getParentDirectory()
                   .getChildFile ("midi").getChildFile (name);
    }

    struct TempMidi
    {
        juce::File file = juce::File::createTempFile (".mid");
        explicit TempMidi (const Bytes& bytes) { REQUIRE (file.replaceWithData (bytes.data(), bytes.size())); }
        ~TempMidi() { file.deleteFile(); }
    };

    RawMidiFile readFixture (const juce::File& f)
    {
        juce::MemoryBlock block;
        REQUIRE (f.loadFileAsData (block));
        const auto* d = static_cast<const std::uint8_t*> (block.getData());
        return readMidiBytes (Bytes (d, d + block.getSize()), "f");
    }

    RawMidiFile importThenExport (const juce::File& f)
    {
        SongDocument doc;
        Diagnostics diags;
        REQUIRE (importMidiFile (doc, f, 1, diags));
        return buildRawMidiFile (doc);
    }

    // (pitch, start, duration, velocity, channel), sorted, for every NOTE of every track.
    std::vector<std::tuple<int, int, int, int, int>> allNotes (const SongDocument& doc)
    {
        std::vector<std::tuple<int, int, int, int, int>> out;
        for (auto track : doc.getSourceMidiNode())
            for (auto n : SongDocument::getNotesNode (track))
                out.emplace_back ((int) n.getProperty (SongIDs::pitch), (int) n.getProperty (SongIDs::startTick),
                                  (int) n.getProperty (SongIDs::durationTicks), (int) n.getProperty (SongIDs::velocity),
                                  (int) n.getProperty (SongIDs::channel, 1));
        std::sort (out.begin(), out.end());
        return out;
    }

    TrackBody conductorBody()
    {
        TrackBody c;
        c.ev (0, { 0xFF, 0x51, 0x03, 0x07, 0xA1, 0x20 }).eot (960);
        return c;
    }
}

TEST_CASE ("MidiFidelity: every tracked fixture exports exactly as it was imported", "[midifidelity]")
{
    auto name = GENERATE (as<std::string>{},
        "Barnes Brothers Band - Pull The Wires.mid", "angels.mid", "anymore.mid", "blue.mid",
        "hold.mid", "land.mid", "leah.mid", "nobody.mid", "right.mid", "state.mid", "syn5.mid", "tellit.mid");

    DYNAMIC_SECTION (name)
    {
        const auto original = readFixture (midiFixture (name));
        REQUIRE (hasConductorTrack (original)); // all fixtures have one
        CHECK (importThenExport (midiFixture (name)) == original);
    }
}

TEST_CASE ("MidiFidelity: the review's note-pairing cases export exactly", "[midifidelity]")
{
    TrackBody swapCase;
    swapCase.ev (0, { 0x90, 60, 100 }).ev (0, { 0x90, 62, 100 }).ev (0, { 0x80, 60, 0x40 })
            .ev (10, { 0x80, 62, 0x40 }).ev (10, { 0x80, 60, 0x40 }).eot();
    TrackBody restrikeCase;
    restrikeCase.ev (0, { 0x90, 60, 100 }).ev (0, { 0x80, 60, 0x40 }).ev (0, { 0x90, 60, 50 }).ev (0, { 0x80, 60, 0x40 })
                .ev (10, { 0x80, 60, 0x40 }).eot();

    for (const auto& body : { swapCase, restrikeCase })
    {
        const auto bytes = smf (1, 96, { conductorBody(), body });
        TempMidi tmp (bytes);
        CHECK (importThenExport (tmp.file) == readMidiBytes (bytes, "r"));
    }
}

TEST_CASE ("MidiFidelity: a file without a conductor exports with its song-wide metas relocated to track 0", "[midifidelity]")
{
    TrackBody first;
    first.ev (0, { 0xFF, 0x51, 0x03, 0x07, 0xA1, 0x20 }).ev (0, { 0xFF, 0x05, 0x02, 'l', 'a' })
         .ev (0, { 0x90, 60, 100 }).ev (48, { 0xFF, 0x06, 0x01, 'B' }).ev (48, { 0x80, 60, 0x40 }).eot();
    TrackBody second;
    second.ev (0, { 0xFF, 0x59, 0x02, 0x00, 0x00 }).ev (0, { 0x91, 64, 90 }).ev (96, { 0x91, 64, 0 }).eot();
    TempMidi tmp (smf (1, 96, { first, second }));

    RawMidiFile expected;
    expected.format = 1;
    expected.ticksPerQuarter = 96;
    RawMidiTrack conductor;
    conductor.events = { { 0, { 0xFF, 0x51, 0x07, 0xA1, 0x20 } },  // tick 0, from track 0
                         { 0, { 0xFF, 0x59, 0x00, 0x00 } },        // tick 0, from track 1
                         { 48, { 0xFF, 0x06, 'B' } } };            // tick 48, from track 0
    conductor.endTick = 0;   // a created conductor has endTick 0; the writer extends it to the last event
    RawMidiTrack t0;
    t0.events = { { 0, { 0xFF, 0x05, 'l', 'a' } }, { 0, { 0x90, 60, 100 } }, { 96, { 0x80, 60, 0x40 } } };
    t0.endTick = 96;
    RawMidiTrack t1;
    t1.events = { { 0, { 0x91, 64, 90 } }, { 96, { 0x91, 64, 0 } } };
    t1.endTick = 96;
    expected.tracks = { conductor, t0, t1 };

    // Compare after a write/read cycle so the writer's End-of-Track rule applies to both sides.
    CHECK (readMidiBytes (writeMidiBytes (importThenExport (tmp.file)), "x")
           == readMidiBytes (writeMidiBytes (expected), "y"));
}

TEST_CASE ("MidiFidelity: a format-0 file exports as format 1 with each note on its own channel", "[midifidelity]")
{
    TrackBody t;
    t.ev (0, { 0xFF, 0x51, 0x03, 0x07, 0xA1, 0x20 }).ev (0, { 0x94, 64, 80 }).ev (0, { 0x99, 36, 90 })
     .ev (96, { 0x84, 64, 0 }).ev (0, { 0x89, 36, 0 }).eot();
    TempMidi tmp (smf (0, 96, { t }));

    const auto exported = importThenExport (tmp.file);
    REQUIRE (exported.format == 1);
    REQUIRE (exported.tracks.size() == 2);
    CHECK (exported.tracks[0].events.size() == 1);
    REQUIRE (exported.tracks[1].events.size() == 4);
    CHECK (exported.tracks[1].events[0].bytes == Bytes { 0x94, 64, 80 });
    CHECK (exported.tracks[1].events[1].bytes == Bytes { 0x99, 36, 90 });
}

TEST_CASE ("MidiFidelity: an empty song exports one empty conductor track", "[midifidelity]")
{
    SongDocument doc;
    const auto exported = buildRawMidiFile (doc);
    CHECK (exported.format == 1);
    CHECK (exported.ticksPerQuarter == 480);
    REQUIRE (exported.tracks.size() == 1);
    CHECK (exported.tracks[0].events.empty());
    CHECK (readMidiBytes (writeMidiBytes (exported), "e") == exported);
}

TEST_CASE ("MidiFidelity: an edited song exports a file that re-imports as the same notes", "[midifidelity]")
{
    // Distinct pitches, no overlaps, so re-import pairing can't legitimately differ.
    TrackBody melody;
    melody.ev (0, { 0xB0, 0x07, 0x64 })
          .ev (0, { 0x90, 60, 100 }).ev (96, { 0x80, 60, 0x40 })
          .ev (0, { 0x90, 62, 90 }).ev (96, { 0x90, 62, 0 })
          .ev (0, { 0x90, 64, 80 }).ev (96, { 0x80, 64, 0x10 }).eot();
    TempMidi source (smf (1, 96, { conductorBody(), melody }));

    SongDocument doc;
    Diagnostics diags;
    REQUIRE (importMidiFile (doc, source.file, 1, diags));

    auto track = doc.getTrack (1);
    auto notes = SongDocument::getNotesNode (track);
    REQUIRE (notes.getNumChildren() == 3);

    // Move one note (as the editor does), delete one, add one.
    auto moved = notes.getChild (0);
    doc.setProperty (moved, SongIDs::startTick, (int) moved.getProperty (SongIDs::startTick) + 7);
    doc.removeProperty (moved, SongIDs::onOrder, false);
    doc.removeProperty (moved, SongIDs::offOrder, false);
    doc.setProperty (moved, SongIDs::offSynthesized, false, false);
    doc.removeChild (notes, notes.getChild (1));
    juce::ValueTree added (SongIDs::NOTE);
    added.setProperty (SongIDs::pitch, 72, nullptr);
    added.setProperty (SongIDs::startTick, 0, nullptr);
    added.setProperty (SongIDs::durationTicks, 120, nullptr);
    added.setProperty (SongIDs::velocity, 99, nullptr);
    added.setProperty (SongIDs::channel, 3, nullptr);
    doc.addChild (notes, added);

    TempMidi tmp (writeMidiBytes (buildRawMidiFile (doc)));
    SongDocument reimported;
    Diagnostics diags2;
    REQUIRE (importMidiFile (reimported, tmp.file, 1, diags2));
    CHECK (allNotes (reimported) == allNotes (doc));
}

TEST_CASE ("MidiFidelity: a mixed-PPQ song exports a readable file that keeps every event", "[midifidelity]")
{
    SongDocument doc;
    Diagnostics d1, d2;
    REQUIRE (importMidiFile (doc, midiFixture ("blue.mid"), 1, d1));       // 120 PPQ
    REQUIRE (importMidiFile (doc, midiFixture ("anymore.mid"), 2, d2));    // 960 PPQ -> LCM raise

    const auto exported = buildRawMidiFile (doc);
    const auto reread = readMidiBytes (writeMidiBytes (exported), "m");
    CHECK (reread.ticksPerQuarter == (int) doc.getSourceMidiNode().getProperty (SongIDs::ticksPerQuarter));
    REQUIRE (reread.tracks.size() == (size_t) doc.getNumTracks());

    for (int t = 0; t < doc.getNumTracks(); ++t)
    {
        auto track = doc.getTrack (t);
        int expected = SongDocument::getEventsNode (track).getNumChildren();
        for (auto n : SongDocument::getNotesNode (track))
            expected += (bool) n.getProperty (SongIDs::offSynthesized) ? 1 : 2;
        CHECK ((int) reread.tracks[(size_t) t].events.size() == expected);
    }
}
```

- [ ] **Step 2: Register and run to verify failure**

Add the test file and `${CMAKE_SOURCE_DIR}/Source/UI/MidiExport.cpp` to `Tests/CMakeLists.txt`, and `Source/UI/MidiExport.cpp` to `forge_ui`. Create an empty `.cpp`.
Run: `cmake --build build` → compile FAIL (header missing).

- [ ] **Step 3: Write the header**

`Source/UI/MidiExport.h`:

```cpp
#pragma once

// Builds the song's source MIDI back into a RawMidiFile (2026-10-03 spec,
// "Export"). Format 1, conductor first, then every other MIDI_TRACK in
// document order. Unedited imports come back event-for-event; new or
// re-timed notes sort after a tick's original events (note-offs first).
// LOTRO parts/assignments never affect it.

#include "RawMidi.h"
#include "SongDocument.h"

namespace lotro
{

RawMidiFile buildRawMidiFile (const SongDocument& doc);

} // namespace lotro
```

- [ ] **Step 4: Write the implementation**

`Source/UI/MidiExport.cpp`:

```cpp
#include "MidiExport.h"

#include <algorithm>
#include <tuple>

namespace lotro
{

namespace
{
    // Sort key within one track: tick, then group (0 = new note-off,
    // 1 = original material, 2 = new note-on), then relocatedFrom, then
    // original order. Stable sort keeps document order for exact ties.
    struct Item
    {
        int tick = 0;
        int group = 1;
        int relocatedFrom = -1;
        int order = 0;
        std::vector<std::uint8_t> bytes;

        auto key() const { return std::make_tuple (tick, group, relocatedFrom, order); }
    };

    std::vector<std::uint8_t> blockBytes (const juce::var& v)
    {
        if (const auto* block = v.getBinaryData())
        {
            const auto* d = static_cast<const std::uint8_t*> (block->getData());
            return std::vector<std::uint8_t> (d, d + block->getSize());
        }
        return {};
    }

    RawMidiTrack buildTrack (const juce::ValueTree& track)
    {
        std::vector<Item> items;

        for (auto event : SongDocument::getEventsNode (track))
        {
            Item item;
            item.tick          = (int) event.getProperty (SongIDs::tick);
            item.order         = (int) event.getProperty (SongIDs::order, 0);
            item.relocatedFrom = (int) event.getProperty (SongIDs::relocatedFrom, -1);
            item.bytes         = blockBytes (event.getProperty (SongIDs::data));
            if (! item.bytes.empty())
                items.push_back (std::move (item));
        }

        for (auto note : SongDocument::getNotesNode (track))
        {
            const int channel  = juce::jlimit (1, 16, (int) note.getProperty (SongIDs::channel, 1));
            const int pitch    = juce::jlimit (0, 127, (int) note.getProperty (SongIDs::pitch));
            const int velocity = juce::jlimit (1, 127, (int) note.getProperty (SongIDs::velocity));
            const int start    = (int) note.getProperty (SongIDs::startTick);
            const int end      = start + juce::jmax (0, (int) note.getProperty (SongIDs::durationTicks));

            Item on;
            on.tick  = start;
            on.bytes = { (std::uint8_t) (0x90 | (channel - 1)), (std::uint8_t) pitch, (std::uint8_t) velocity };
            if (note.hasProperty (SongIDs::onOrder)) { on.group = 1; on.order = (int) note.getProperty (SongIDs::onOrder); }
            else                                     { on.group = 2; }
            items.push_back (std::move (on));

            if ((bool) note.getProperty (SongIDs::offSynthesized, false))
                continue;

            Item off;
            off.tick = end;
            if ((bool) note.getProperty (SongIDs::offIsNoteOnZero, false))
                off.bytes = { (std::uint8_t) (0x90 | (channel - 1)), (std::uint8_t) pitch, 0 };
            else
                off.bytes = { (std::uint8_t) (0x80 | (channel - 1)), (std::uint8_t) pitch,
                              (std::uint8_t) juce::jlimit (0, 127, (int) note.getProperty (SongIDs::offVelocity, 64)) };
            if (note.hasProperty (SongIDs::offOrder)) { off.group = 1; off.order = (int) note.getProperty (SongIDs::offOrder); }
            else                                      { off.group = 0; }
            items.push_back (std::move (off));
        }

        std::stable_sort (items.begin(), items.end(),
                          [] (const Item& a, const Item& b) { return a.key() < b.key(); });

        RawMidiTrack out;
        out.endTick = (int) track.getProperty (SongIDs::endTick, 0);
        for (auto& item : items)
        {
            out.endTick = std::max (out.endTick, item.tick);
            out.events.push_back ({ item.tick, std::move (item.bytes) });
        }
        return out;
    }
}

RawMidiFile buildRawMidiFile (const SongDocument& doc)
{
    RawMidiFile file;
    file.format          = 1;
    file.ticksPerQuarter = (int) doc.getSourceMidiNode().getProperty (SongIDs::ticksPerQuarter, 480);

    file.tracks.push_back (buildTrack (doc.getConductorTrack()));
    for (auto track : doc.getSourceMidiNode())
        if (! (bool) track.getProperty (SongIDs::isConductor, false))
            file.tracks.push_back (buildTrack (track));

    return file;
}

} // namespace lotro
```

`out.endTick = max(endTick, last event)` matches the writer's rule. The fixture equality test relies on it, because a raw track's `endTick` already is ≥ its last event's tick.

- [ ] **Step 5: Run the tests**

Run: `cmake --build build && ctest --test-dir build -R "MidiFidelity:" --output-on-failure`
Expected: PASS. If a fixture fails, Catch prints the two `RawMidiFile`s' first differing event. Fix import or export, never the test's expectation of exact equality.

- [ ] **Step 6: Whole suite, commit**

Run: `ctest --test-dir build --output-on-failure` → all pass.

```bash
git add Source/UI/MidiExport.h Source/UI/MidiExport.cpp Tests/MidiFidelity_tests.cpp CMakeLists.txt Tests/CMakeLists.txt
git commit -m "feat(songsmith): rebuild the song's MIDI for export, event-for-event

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 8: Editing interactions (new-note defaults, re-timed notes)

**Files:**
- Modify: `Source/UI/SourceRollEditor.h`, `Source/UI/SourceRollEditor.cpp`, `Tests/SourceRollEditor_tests.cpp`

**Interfaces:**
- Consumes: `SongDocument::removeProperty` (Task 3); `SongIDs` fidelity identifiers.
- Produces: `SourceRollEditor::createNoteAt` writes `channel`/`offVelocity`/`offIsNoteOnZero`/`offSynthesized`. Any gesture that actually changes a note's `startTick` or `durationTicks` (move, left/right resize, quantize) removes `onOrder`/`offOrder` and sets `offSynthesized = false` in that gesture's transaction. A private helper `void markTimingEdited (juce::ValueTree note);` does this.

- [ ] **Step 1: Write the failing tests** (append to `Tests/SourceRollEditor_tests.cpp`)

```cpp
namespace
{
    void markImported (juce::ValueTree note)
    {
        note.setProperty (SongIDs::onOrder, 4, nullptr);
        note.setProperty (SongIDs::offOrder, 9, nullptr);
        note.setProperty (SongIDs::offSynthesized, true, nullptr);
    }
}

TEST_CASE ("SourceRollEditor: a created note takes the track's defaultChannel and standard note-off fields", "[source-roll-editor][fidelity]")
{
    Fixture f;
    f.track.setProperty (SongIDs::defaultChannel, 7, nullptr);
    const int before = SongDocument::getNotesNode (f.track).getNumChildren();

    f.editor.mouseDown ({ viewportWidth - 5, viewportHeight - 5 }, {}, true);

    auto notes = SongDocument::getNotesNode (f.track);
    REQUIRE (notes.getNumChildren() == before + 1);
    auto created = notes.getChild (notes.getNumChildren() - 1);
    CHECK ((int) created.getProperty (SongIDs::channel) == 7);
    CHECK ((int) created.getProperty (SongIDs::offVelocity) == 64);
    CHECK_FALSE ((bool) created.getProperty (SongIDs::offIsNoteOnZero));
    CHECK_FALSE ((bool) created.getProperty (SongIDs::offSynthesized));
    CHECK_FALSE (created.hasProperty (SongIDs::onOrder));
    CHECK_FALSE (created.hasProperty (SongIDs::offOrder));
}

TEST_CASE ("SourceRollEditor: a created note on a track with no defaultChannel uses channel 1", "[source-roll-editor][fidelity]")
{
    Fixture f;
    f.editor.mouseDown ({ viewportWidth - 5, viewportHeight - 5 }, {}, true);
    auto notes = SongDocument::getNotesNode (f.track);
    CHECK ((int) notes.getChild (notes.getNumChildren() - 1).getProperty (SongIDs::channel) == 1);
}

TEST_CASE ("SourceRollEditor: moving a note in time clears its import orders in the same undo step", "[source-roll-editor][fidelity]")
{
    Fixture f;
    markImported (f.noteA);
    const auto start = f.centreOf (f.noteA);

    f.editor.mouseDown (start, {}, false);
    f.editor.mouseDrag (start.translated (60, 0));
    f.editor.mouseUp (start.translated (60, 0));

    CHECK_FALSE (f.noteA.hasProperty (SongIDs::onOrder));
    CHECK_FALSE (f.noteA.hasProperty (SongIDs::offOrder));
    CHECK_FALSE ((bool) f.noteA.getProperty (SongIDs::offSynthesized));

    f.doc.undo();
    CHECK ((int) f.noteA.getProperty (SongIDs::startTick) == 0);
    CHECK ((int) f.noteA.getProperty (SongIDs::onOrder) == 4);
    CHECK ((int) f.noteA.getProperty (SongIDs::offOrder) == 9);
    CHECK ((bool) f.noteA.getProperty (SongIDs::offSynthesized));
}

TEST_CASE ("SourceRollEditor: a pitch-only drag keeps a note's import orders", "[source-roll-editor][fidelity]")
{
    Fixture f;
    markImported (f.noteA);
    const auto start = f.centreOf (f.noteA);
    const int rowHeight = juce::roundToInt (f.geometry.noteBounds (Fixture::toNote (f.noteA)).height);

    f.editor.mouseDown (start, {}, false);
    f.editor.mouseDrag (start.translated (0, -rowHeight));
    f.editor.mouseUp (start.translated (0, -rowHeight));

    REQUIRE ((int) f.noteA.getProperty (SongIDs::pitch) != 60);
    CHECK ((int) f.noteA.getProperty (SongIDs::startTick) == 0);
    CHECK ((int) f.noteA.getProperty (SongIDs::onOrder) == 4);
    CHECK ((bool) f.noteA.getProperty (SongIDs::offSynthesized));
}

TEST_CASE ("SourceRollEditor: resizing and quantizing clear import orders only on notes whose timing changed", "[source-roll-editor][fidelity]")
{
    Fixture f;
    markImported (f.noteA);
    markImported (f.noteB);

    // Right-edge resize of noteA.
    auto b = f.geometry.noteBounds (Fixture::toNote (f.noteA));
    const juce::Point<int> rightEdge { (int) (b.x + b.width) - 1, (int) (b.y + b.height / 2) };
    f.editor.mouseDown (rightEdge, {}, false);
    f.editor.mouseDrag (rightEdge.translated (40, 0));
    f.editor.mouseUp (rightEdge.translated (40, 0));
    CHECK_FALSE (f.noteA.hasProperty (SongIDs::onOrder));

    // Quantize noteB alone: it already sits on the 1/4 grid, so nothing changes and it keeps its orders.
    f.editor.setGridTicks (ticksPerQuarter);
    f.editor.mouseDown (f.centreOf (f.noteB), {}, false);
    f.editor.mouseUp (f.centreOf (f.noteB));
    REQUIRE (f.editor.quantizeSelection());
    CHECK ((int) f.noteB.getProperty (SongIDs::onOrder) == 4);
}

TEST_CASE ("SourceRollEditor: deleting a note leaves the track's events alone", "[source-roll-editor][fidelity]")
{
    Fixture f;
    juce::ValueTree event (SongIDs::EVENT);
    event.setProperty (SongIDs::tick, 0, nullptr);
    SongDocument::getEventsNode (f.track).addChild (event, -1, nullptr);

    f.editor.mouseDown (f.centreOf (f.noteA), {}, false);
    f.editor.mouseUp (f.centreOf (f.noteA));
    REQUIRE (f.editor.deleteSelection());
    CHECK (SongDocument::getEventsNode (f.track).getNumChildren() == 1);
}
```

Before writing these, check the fixture's accessors against `SourceRollEditor.h`:
- The test uses `setGridTicks(int)`. If the editor's setter is named differently, use the real name.
- `noteBounds` returns a float/int rectangle with fields `x`, `y`, `width`, `height`, as used in `centreOf`. Adapt the arithmetic to the real type.
- Double-click creation: `mouseDown (pos, {}, true)` on empty canvas creates a note, as in the existing "double-clicking an empty cell" test. Use the same empty-cell position that test uses if `(viewportWidth - 5, viewportHeight - 5)` isn't empty in this fixture.

- [ ] **Step 2: Run to verify failure**

Run: `cmake --build build && ctest --test-dir build -R "SourceRollEditor:" --output-on-failure`
Expected: the new `[fidelity]` tests FAIL (no `channel` on created notes; orders not cleared).

- [ ] **Step 3: Implement**

`SourceRollEditor.h` (private): `void markTimingEdited (juce::ValueTree note);`

`SourceRollEditor.cpp`:

```cpp
// A note whose timing changed is no longer at its imported position in the
// file, so it exports as new material with a real note-off (2026-10-03
// MIDI-fidelity spec, "Editing interactions"). Joins the caller's open
// transaction.
void SourceRollEditor::markTimingEdited (juce::ValueTree note)
{
    if (note.hasProperty (SongIDs::onOrder))
        doc.removeProperty (note, SongIDs::onOrder, false);
    if (note.hasProperty (SongIDs::offOrder))
        doc.removeProperty (note, SongIDs::offOrder, false);
    if ((bool) note.getProperty (SongIDs::offSynthesized, false))
        doc.setProperty (note, SongIDs::offSynthesized, false, false);
}
```

Call `markTimingEdited (…)` right after each `doc.setProperty (…, SongIDs::startTick, …)` and `doc.setProperty (…, SongIDs::durationTicks, …)`. Each of those already sits inside an `if (old != new)` guard, so calls happen only on a real change:
- `mouseDrag` Move: after the `startTick` set. **Not** after the `pitch` set.
- `ResizeRight`: after the `durationTicks` set.
- `ResizeLeft`: after both sets.
- `quantizeSelection`: after each of its two sets.

`createNoteAt`, before `doc.addChild (…)`:

```cpp
    note.setProperty (SongIDs::channel, (int) track.getProperty (SongIDs::defaultChannel, 1), nullptr);
    note.setProperty (SongIDs::offVelocity, 64, nullptr);
    note.setProperty (SongIDs::offIsNoteOnZero, false, nullptr);
    note.setProperty (SongIDs::offSynthesized, false, nullptr);
```

- [ ] **Step 4: Run tests, whole suite, commit**

Run: `cmake --build build && ctest --test-dir build --output-on-failure` → all pass.

```bash
git add Source/UI/SourceRollEditor.h Source/UI/SourceRollEditor.cpp Tests/SourceRollEditor_tests.cpp
git commit -m "feat(songsmith): give edited and new notes export-ready MIDI fields

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 9: Track-list rows and File → Export MIDI…

**Files:**
- Modify: `Source/UI/TrackRowComponent.{h,cpp}`, `Source/UI/MainWindow.{h,cpp}`, `Tests/TrackRowComponent_tests.cpp`

**Interfaces:**
- Consumes: `SongDocument::isAssignableTrack`, `getEventsNode`, `getNumTracks` (Tasks 3–4); `buildRawMidiFile` (Task 7); `writeMidiFile`, `MidiExportError` (Task 1).
- Produces:
  - `TrackRowComponent`: `bool canDrag() const;` (= `SongDocument::isAssignableTrack (track)`), `juce::String buildSecondLineForTesting() const { return buildSecondLine(); }`.
  - The conductor's second line is `"<N> events"` and its first line is `"Conductor"`. A note-less track's second line is `"0 notes · <N> events"`. Neither row starts a drag nor fires `onTrackDoubleClicked`.
  - `MainWindow`: a `FileExportMidi` command and `void exportMidiAs();`.

- [ ] **Step 1: Write the failing tests** (append to `Tests/TrackRowComponent_tests.cpp`)

```cpp
TEST_CASE ("TrackRowComponent: the conductor row reads 'N events' and can't be dragged", "[trackrow][fidelity]")
{
    SongDocument doc;
    auto conductor = doc.getConductorTrack();
    for (int i = 0; i < 3; ++i)
        SongDocument::getEventsNode (conductor).addChild (juce::ValueTree (SongIDs::EVENT), -1, nullptr);

    TimelineViewState view;
    TrackRowComponent row (conductor, 0, view);
    CHECK (row.buildSecondLineForTesting() == "3 events");
    CHECK_FALSE (row.canDrag());
}

TEST_CASE ("TrackRowComponent: a note-less track reads '0 notes · N events' and can't be dragged", "[trackrow][fidelity]")
{
    SongDocument doc;
    auto track = doc.addTrackBulk ("Lyrics", 0, 0, 1);
    SongDocument::getEventsNode (track).addChild (juce::ValueTree (SongIDs::EVENT), -1, nullptr);

    TimelineViewState view;
    TrackRowComponent row (track, 1, view);
    CHECK (row.buildSecondLineForTesting() == juce::String::fromUTF8 ("0 notes \xc2\xb7 1 events"));
    CHECK_FALSE (row.canDrag());
}

TEST_CASE ("TrackRowComponent: double-clicking a non-assignable row does not fire onTrackDoubleClicked", "[trackrow][fidelity]")
{
    SongDocument doc;
    TimelineViewState view;
    TrackRowComponent row (doc.getConductorTrack(), 0, view);
    bool fired = false;
    row.onTrackDoubleClicked = [&] (juce::int64) { fired = true; };
    row.setBounds (0, 0, 400, TrackRowComponent::rowHeight);
    row.mouseDoubleClick (eventAt (row, { 5, 5 }, 2)); // eventAt: the file's existing MouseEvent helper
    CHECK_FALSE (fired);
}
```

`eventAt (juce::Component&, juce::Point<int>, int numClicks)` already exists in this test file's anonymous namespace, so these tests must sit after it.

- [ ] **Step 2: Run to verify failure**

Run: `cmake --build build`
Expected: compile FAIL (`buildSecondLineForTesting`, `canDrag` undeclared).

- [ ] **Step 3: Implement the rows**

`TrackRowComponent.h` (public): `bool canDrag() const;` and `juce::String buildSecondLineForTesting() const { return buildSecondLine(); }`.

`TrackRowComponent.cpp`:

```cpp
bool TrackRowComponent::canDrag() const
{
    return SongDocument::isAssignableTrack (track);
}
```

At the top of `buildSecondLine()`:

```cpp
    const int numEvents = SongDocument::getEventsNode (track).getNumChildren();
    if ((bool) track.getProperty (SongIDs::isConductor, false))
        return juce::String (numEvents) + " events";

    auto notes = SongDocument::getNotesNode (track);
    const int numNotes = notes.getNumChildren();
    if (numNotes == 0)
        return juce::String (numNotes) + " notes" + juce::String::fromUTF8 (" \xc2\xb7 ") + juce::String (numEvents) + " events";
```

(Keep the rest, which handles tracks with notes, unchanged; it already uses `notes` from Task 3.)

In `paint()`, the first line shows the track's `name` property, which is already `"Conductor"` for the conductor. Draw the conductor's name in `textMuted` instead of `text`:

```cpp
    const bool isConductor = (bool) track.getProperty (SongIDs::isConductor, false);
    g.setColour (juce::Colour (isConductor ? textMuted : text));
```

`mouseDrag`: after the 4-pixel threshold check, add `if (! canDrag()) return;`.
`mouseDoubleClick`: `if (canDrag() && onTrackDoubleClicked) onTrackDoubleClicked (getTrackId());`.
In the constructor's `notePreview.onNonToggleDoubleClick` lambda: `if (canDrag() && onTrackDoubleClicked) …`.

- [ ] **Step 4: Implement File → Export MIDI…**

`MainWindow.h`: add `FileExportMidi,` to `CommandId`, right after `FileSaveAbc`, and `void exportMidiAs();` to the private members.

`MainWindow.cpp`:

- Includes: `#include "MidiExport.h"`, `#include "RawMidi.h"`, `#include <fstream>`.
- `getMenuForIndex`, File menu, after `FileSaveAbc`:

```cpp
        // Enabled once the song has anything besides its conductor (spec:
        // "at least one non-conductor track").
        m.addItem (FileExportMidi, "Export MIDI...", songDocument.getNumTracks() > 1, false);
```

- `menuItemSelected`: `case FileExportMidi: exportMidiAs(); break;`, following the existing switch style.
- Implementation:

```cpp
void MainWindow::exportMidiAs()
{
    // Default to <input-stem>.mid next to the first import; the chooser warns
    // before overwriting.
    const auto inputMidiPath = songDocument.getTree().getProperty (SongIDs::inputMidiPath).toString();
    juce::File defaultPath;
    if (inputMidiPath.isNotEmpty())
        defaultPath = juce::File (inputMidiPath).withFileExtension (".mid");

    fileChooser = std::make_unique<juce::FileChooser> ("Export MIDI", defaultPath, "*.mid;*.midi");

    fileChooser->launchAsync (juce::FileBrowserComponent::saveMode
                            | juce::FileBrowserComponent::canSelectFiles
                            | juce::FileBrowserComponent::warnAboutOverwriting,
        [this] (const juce::FileChooser& fc)
        {
            auto file = fc.getResult();
            if (file == juce::File()) return;
            if (! file.getFileName().endsWithIgnoreCase (".mid") && ! file.getFileName().endsWithIgnoreCase (".midi"))
                file = file.withFileExtension (".mid");

            try
            {
                std::ofstream out (file.getFullPathName().toStdString(), std::ios::binary | std::ios::trunc);
                writeMidiFile (buildRawMidiFile (songDocument), out);
            }
            catch (const MidiExportError& e)
            {
                juce::NativeMessageBox::showMessageBoxAsync (
                    juce::MessageBoxIconType::WarningIcon,
                    "Export MIDI failed",
                    "Could not write " + file.getFullPathName() + ": " + e.what());
            }
        });
}
```

An `ofstream` that fails to open leaves the stream in a failed state, so `writeMidiFile` throws `MidiExportError`. That covers unwritable paths.

- [ ] **Step 5: Run tests, whole suite**

Run: `cmake --build build && ctest --test-dir build --output-on-failure`
Expected: all pass. Also build the GUI target (`cmake --build build --target forge_ui`) to confirm `MainWindow.cpp` compiles; it isn't part of `forge_tests`. **Do not launch it.**

- [ ] **Step 6: Commit**

```bash
git add Source/UI/TrackRowComponent.h Source/UI/TrackRowComponent.cpp Source/UI/MainWindow.h Source/UI/MainWindow.cpp Tests/TrackRowComponent_tests.cpp
git commit -m "feat(songsmith): show conductor and note-less rows, add File > Export MIDI

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 10: Documentation

**Files:**
- Modify: `docs/ARCHITECTURE.md`, `docs/UI_GUIDE.md`, `docs/TESTING.md`, `CLAUDE.md` (status line only, if it mentions what the document holds), and `docs/DELTAS_FROM_SPEC.md` only if `lotro-abc-converter-spec.md` says anything about dropping non-note events (check with `grep -niE "controller|program change|sysex|meta" lotro-abc-converter-spec.md`)

- [ ] **Step 1: `docs/ARCHITECTURE.md`**
  - Line ~181: change "Only place in the repo that includes `juce_audio_basics` / `juce::MidiFile`" to say `MidiImporter` is the only *conversion* reader, and that Songsmith also reads files losslessly through `Source/UI/RawMidi` (JUCE-free).
  - §9 (GUI): add the `SONG > SOURCE_MIDI > MIDI_TRACK > NOTES/EVENTS` schema, the conductor (child 0, `isConductor`), the new `NOTE`/`EVENT`/`MIDI_TRACK` properties, and a short walkthrough: import (`importMidi` + `readMidiFile` → `planMidiImport` → `appendImportedMidi`) and export (`buildRawMidiFile` → `writeMidiFile`).
  - Update the `buildConfigAndRawSong` description (skips the conductor and unassigned note-less tracks; indexes by raw-song position) and `synthesiseDefaultParts` (skips non-assignable tracks).
  - Add the four new files to the file map table near line ~769.
- [ ] **Step 2: `docs/UI_GUIDE.md`**: the File menu diagram gains `Export MIDI...`. Add a row describing the conductor row (unnumbered, "N events", not draggable) and note-less rows ("0 notes · N events", not draggable).
- [ ] **Step 3: `docs/TESTING.md`**: update the test count to the number `ctest` reports. Add one line each for `RawMidi_tests`, `JuceNoteReplica_tests` (differential vs JUCE), `MidiImportPlan_tests`, `MidiImport_tests` and `MidiFidelity_tests` (exact import → export on all 12 fixtures).
- [ ] **Step 4: Verify and commit**

Run: `ctest --test-dir build --output-on-failure` → all pass; note the count for TESTING.md.

```bash
git add docs CLAUDE.md
git commit -m "docs: describe Songsmith's lossless MIDI model, conductor track and Export MIDI

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```
