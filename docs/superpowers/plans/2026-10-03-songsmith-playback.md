# Songsmith Playback Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Play the Song's source MIDI inside Songsmith through a SoundFont synth, with one shared transport/playhead across the main track canvas and the MIDI editor, and main-header mute/solo governing what is audible.

**Architecture:** A pure `buildSnapshot()` flattens the `SongDocument` into an immutable, seconds-timed event list (one TinySoundFont virtual channel per (track, MIDI channel)). A device-free `PlaybackEngine` walks that list per audio block, dispatching to an `EventSink` (production: `SynthVoice` wrapping TinySoundFont; tests: a recording fake). A message-thread `PlaybackController` owns the transport, mute/solo state and snapshot rebuilds (AsyncUpdater-coalesced), and hands snapshots to the audio thread through a lock-free `HandOff`. UI components (`TransportStrip`, `TimelineRuler`, `PlayheadOverlay`, M/S buttons) observe the controller.

**Tech Stack:** C++17, JUCE (`juce_audio_basics`, new `juce_audio_devices` for `forge_ui` only), TinySoundFont (MIT, new, vendored single header), Catch2.

**Spec:** `docs/superpowers/specs/2026-10-03-songsmith-playback-design.md` (r2, commit `bfa2857`).

## Global Constraints

- New code lives in `Source/UI/Playback/`; `Source/Core/**` is untouched (engine/UI boundary).
- TinySoundFont: MIT single header vendored at `Source/ThirdParty/tinysoundfont/tsf.h`, pinned to upstream commit `853a0a171759f1ddba0de1442133a75912bbeffa` (schellingb/TinySoundFont, 2026-07-19); no submodule. **Mention it as a new dependency** in the Task 1 commit message and in the final summary.
- `juce_audio_devices` is linked to `forge_ui` only; `Tests/CMakeLists.txt` must not link it, so `AudioOutput.cpp` is compiled into `forge_ui` only and no other file includes `juce_audio_devices`.
- `TimGM6mb.sf2` (GPL-2, md5 `1f1ad87ae6f87033d9a591eca567d919`) is **never committed** and never produced by CI: it lives at git-ignored `resources/soundfonts/TimGM6mb.sf2`, copied next to the exe by a CMake post-build step when present.
- Audio thread: no allocation, locks, exceptions, or deallocation. `tsf_set_max_voices` and all 256 virtual channels are initialised on the message thread before a `tsf` instance is published.
- Playhead and event times are **seconds** (double); the snapshot keeps a `TempoMap` for tick↔seconds. Default tempo 120 BPM before the first `TEMPO_CHANGE` / when `TEMPO_MAP` is empty; `SONG.tempoBpm` is ignored.
- Mute/solo: solo additive, mute beats solo, session-only (never in the `.songsmith` file, never undoable), keyed by `trackId`, kept across `removeTrack`, cleared on New/Open/Close. No controls in the editor window.
- Playback is of **source MIDI only**; the LOTRO preview canvas gets no playhead or playback.
- Custom error type `PlaybackError` (kinds `SoundFontMissing`, `SoundFontInvalid`, `AudioDeviceUnavailable`); no generic string errors. Types/annotations everywhere, no `any`-style escape hatches; prefer free functions over classes where practical.
- TDD: write the failing test first, see it fail, then implement. Prefer integration tests over unit tests for business logic. The GUI is never launched from agents; `forge_tests` must pass without a sound card or a SoundFont.
- Commit messages: conventional commits, ending with `Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>`. Never push without explicit approval.
- Build/test: `cmake --build build` then `ctest --test-dir build --output-on-failure` (single test: `-R <name>`); Windows cross-check: `./build-windows.sh forge_tests` / `forge_ui`. Known Wine hang at `TrackEditorWindow: pops up centred over the application window` is pre-existing: if a full Wine run hangs there, run the new test prefixes only (e.g. `./build-windows.sh forge_tests "[playback]"`).

## Review Focus

Failure modes the spec implies but no obvious task tests; each is pinned in the owning task:

1. Song with no notes (fresh/empty document): Play is a no-op, no events, no crash, playhead stays 0 — Task 5 (`Transport::play`), Task 10.
2. A track using channel 10 with no program-change event: still plays on the drum bank (setup events carry `isDrum`) — Task 3.
3. Open/New/Close while playing: playback stops, playhead 0, mute/solo cleared, no stuck notes — Task 10.
4. Out-of-range or degenerate note data (duration 0, pitch >127, velocity 0, missing `channel`) and bad tempo data (bpm ≤ 0): clamped, never a crash or a hang — Tasks 2 and 3.
5. More than 64 tracks, or more than 256 (track, channel) pairs: audible flags beyond 64 work; extra pairs are dropped without crashing — Task 3.
6. Renaming a track or changing its colour during playback must not cut held notes (only note/event/tempo changes rebuild) — Task 10.

---

## File Structure

Create (all `Source/UI/Playback/`): `PlaybackError.h`, `TempoMap.h/.cpp`, `PlaybackSnapshot.h/.cpp`, `MuteSoloState.h/.cpp`, `Transport.h/.cpp`, `HandOff.h`, `EventSink.h`, `PlaybackEngine.h/.cpp`, `SynthVoice.h/.cpp`, `PlaybackController.h/.cpp`, `AudioOutput.h/.cpp` (forge_ui only), `TransportStrip.h/.cpp`, `TimelineRuler.h/.cpp`, `PlayheadOverlay.h/.cpp`.

Create tests (`Tests/`): `PlaybackError_tests.cpp`, `TempoMap_tests.cpp`, `PlaybackSnapshot_tests.cpp`, `MuteSoloState_tests.cpp`, `Transport_tests.cpp`, `HandOff_tests.cpp`, `PlaybackEngine_tests.cpp`, `SynthVoice_tests.cpp`, `PlaybackController_tests.cpp`, `TransportStrip_tests.cpp`, `PlayheadOverlay_tests.cpp`, shared helper `PlaybackTestSupport.h`.

Modify: `CMakeLists.txt`, `Tests/CMakeLists.txt`, `.gitignore`, `Source/UI/MainWindow.{h,cpp}`, `Source/UI/SongsmithMainComponent.{h,cpp}`, `Source/UI/TrackListComponent.{h,cpp}`, `Source/UI/TrackRowComponent.{h,cpp}`, `Source/UI/TrackEditorWindow.{h,cpp}`, `Source/UI/PianoRollComponent.{h,cpp}`, `CLAUDE.md`, `docs/{ARCHITECTURE,UI_GUIDE,TESTING,BUILD}.md`, `docs/songsmith-ui-map.html`.

Every new `.cpp` under `Source/UI/Playback/` except `AudioOutput.cpp` is listed in **both** `forge_ui` (`CMakeLists.txt` `target_sources`) and `forge_tests` (`Tests/CMakeLists.txt`, as `${CMAKE_SOURCE_DIR}/Source/UI/Playback/X.cpp`), each new `_tests.cpp` in `forge_tests`. Includes use the `Source`-rooted form `"UI/Playback/X.h"` (that dir is a public include path of `forge_core`).

---

### Task 1: Dependencies, build scaffolding, `PlaybackError`

**Files:**
- Create: `Source/ThirdParty/tinysoundfont/tsf.h`, `Source/ThirdParty/tinysoundfont/README.md`, `Source/UI/Playback/PlaybackError.h`, `Tests/PlaybackError_tests.cpp`
- Modify: `CMakeLists.txt`, `Tests/CMakeLists.txt`, `.gitignore`, `docs/BUILD.md`

**Interfaces:**
- Produces: `enum class PlaybackErrorKind { SoundFontMissing, SoundFontInvalid, AudioDeviceUnavailable }`; `class PlaybackError : public std::runtime_error { PlaybackError (PlaybackErrorKind, const std::string&); PlaybackErrorKind kind() const noexcept; }`. `tsf.h` includable as `#include <tsf.h>` (SYSTEM include dir) from `forge_ui` and `forge_tests`. `resources/soundfonts/` git-ignored.

- [ ] **Step 1: Write the failing test** — `Tests/PlaybackError_tests.cpp`:

```cpp
#include "UI/Playback/PlaybackError.h"

#include <catch2/catch_test_macros.hpp>

using namespace lotro;

TEST_CASE ("PlaybackError: carries its kind and a plain-English message", "[playback]")
{
    const PlaybackError e (PlaybackErrorKind::SoundFontInvalid, "That file is not a SoundFont.");
    CHECK (e.kind() == PlaybackErrorKind::SoundFontInvalid);
    CHECK (std::string (e.what()) == "That file is not a SoundFont.");
}
```

- [ ] **Step 2: Run to verify it fails** — add `PlaybackError_tests.cpp` to `forge_tests` in `Tests/CMakeLists.txt` (next to `SongFileRoundTrip_tests.cpp`), then:
  `cmake --build build --target forge_tests` → Expected: FAIL, `UI/Playback/PlaybackError.h: No such file`.

- [ ] **Step 3: Implement** — `Source/UI/Playback/PlaybackError.h` (mirrors `SongFileError.h`):

```cpp
#pragma once

#include <stdexcept>
#include <string>

namespace lotro
{

enum class PlaybackErrorKind
{
    SoundFontMissing,
    SoundFontInvalid,
    AudioDeviceUnavailable
};

// Thrown by SynthVoice (SoundFont loading) and AudioOutput (device start-up).
// The message is plain English and shown verbatim in the user's error dialog.
class PlaybackError : public std::runtime_error
{
public:
    PlaybackError (PlaybackErrorKind kindIn, const std::string& message)
        : std::runtime_error (message), errorKind (kindIn) {}

    PlaybackErrorKind kind() const noexcept { return errorKind; }

private:
    PlaybackErrorKind errorKind;
};

} // namespace lotro
```

Vendor TinySoundFont (pinned):

```bash
mkdir -p Source/ThirdParty/tinysoundfont
curl -fsSL https://raw.githubusercontent.com/schellingb/TinySoundFont/853a0a171759f1ddba0de1442133a75912bbeffa/tsf.h -o Source/ThirdParty/tinysoundfont/tsf.h
grep -c "tsf_set_max_voices\|tsf_channel_set_bank_preset\|tsf_channel_midi_control\|tsf_active_voice_count" Source/ThirdParty/tinysoundfont/tsf.h
```
Expected: the grep count is ≥ 8 (the API the plan relies on is present). `Source/ThirdParty/tinysoundfont/README.md`:

```markdown
TinySoundFont (MIT) — https://github.com/schellingb/TinySoundFont
Vendored file: tsf.h at commit 853a0a171759f1ddba0de1442133a75912bbeffa (2026-07-19).
Exactly one translation unit (Source/UI/Playback/SynthVoice.cpp) defines TSF_IMPLEMENTATION.
```

`.gitignore` — append:

```
# Local-only GPL-2 SoundFont (never committed; see docs/BUILD.md)
resources/soundfonts/
```

`CMakeLists.txt` — in the `forge_ui` section: add `Source/UI/Playback/*.cpp` entries later per task; now add only the include dir, module, and sf2 copy. After the existing `target_link_libraries(forge_ui PRIVATE ...)` add `juce::juce_audio_devices` to that list, and below it:

```cmake
target_include_directories(forge_ui SYSTEM PRIVATE Source/ThirdParty/tinysoundfont)

# The GPL-2 SoundFont is deliberately not in the repo or in CI. When a local copy
# exists, copy it next to the exe so the default SoundFont lookup finds it.
set(FORGE_SOUNDFONT "${CMAKE_SOURCE_DIR}/resources/soundfonts/TimGM6mb.sf2")
if(EXISTS "${FORGE_SOUNDFONT}")
    add_custom_command(TARGET forge_ui POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${FORGE_SOUNDFONT}" "$<TARGET_FILE_DIR:forge_ui>")
endif()
```

`Tests/CMakeLists.txt` — after `target_include_directories(forge_tests PRIVATE ${FORGE_BUILD_INFO_DIR})` add:

```cmake
target_include_directories(forge_tests SYSTEM PRIVATE ${CMAKE_SOURCE_DIR}/Source/ThirdParty/tinysoundfont)
```

`docs/BUILD.md` — add a short "Playback dependencies" section: TinySoundFont vendored; `juce_audio_devices` needs ALSA dev headers on Linux (`libasound2-dev`); the SoundFont is a local git-ignored file: `mkdir -p resources/soundfonts && cp /mnt/c/Apps/NewPlayer/resources/TimGM6mb.sf2 resources/soundfonts/ && md5sum resources/soundfonts/TimGM6mb.sf2` must print `1f1ad87ae6f87033d9a591eca567d919`.

Place the local SoundFont now: run the `cp`/`md5sum` commands above and confirm the md5, then `git status --short` must **not** list `resources/`.

- [ ] **Step 4: Run to verify it passes**

Run: `cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug && cmake --build build && ctest --test-dir build -R "PlaybackError" --output-on-failure`
Expected: PASS; `forge_ui` still links (confirms `juce_audio_devices` + ALSA resolve); `ls build/forge_ui_artefacts/Debug/TimGM6mb.sf2` exists.

- [ ] **Step 5: Commit**

```bash
git add Source/ThirdParty/tinysoundfont Source/UI/Playback/PlaybackError.h Tests/PlaybackError_tests.cpp Tests/CMakeLists.txt CMakeLists.txt .gitignore docs/BUILD.md
git commit -m "feat(playback): vendor TinySoundFont (new dep), link juce_audio_devices, add PlaybackError

New dependencies: TinySoundFont (MIT, vendored tsf.h @ 853a0a1) and the juce_audio_devices
module (forge_ui only). The GPL-2 SoundFont stays a git-ignored local file.

Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>"
```

---

### Task 2: `TempoMap` (ticks ↔ seconds)

**Files:**
- Create: `Source/UI/Playback/TempoMap.h`, `Source/UI/Playback/TempoMap.cpp`, `Tests/TempoMap_tests.cpp`
- Modify: `CMakeLists.txt` (`forge_ui` sources), `Tests/CMakeLists.txt`

**Interfaces:**
- Produces: `struct TempoPoint { int tick = 0; double bpm = 120.0; };` `class TempoMap { TempoMap(); TempoMap (std::vector<TempoPoint> points, int ticksPerQuarter); double ticksToSeconds (double tick) const; double secondsToTicks (double seconds) const; int getTicksPerQuarter() const noexcept; static constexpr double defaultBpm = 120.0; }`. Points with `bpm <= 0` are discarded; if no point at tick 0, 120 BPM is assumed from 0; later duplicate-tick points win.

- [ ] **Step 1: Write the failing tests** — `Tests/TempoMap_tests.cpp`:

```cpp
#include "UI/Playback/TempoMap.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace lotro;
using Catch::Approx;

TEST_CASE ("TempoMap: an empty map is 120 BPM", "[playback][tempo]")
{
    const TempoMap map ({}, 480);
    CHECK (map.ticksToSeconds (480.0) == Approx (0.5));
    CHECK (map.secondsToTicks (0.5) == Approx (480.0));
}

TEST_CASE ("TempoMap: tempo before the first change is 120 BPM", "[playback][tempo]")
{
    const TempoMap map ({ { 480, 60.0 } }, 480);
    CHECK (map.ticksToSeconds (480.0) == Approx (0.5));          // 120 BPM up to tick 480
    CHECK (map.ticksToSeconds (960.0) == Approx (0.5 + 1.0));    // then 60 BPM
}

TEST_CASE ("TempoMap: mid-song change is integrated piecewise and inverts exactly", "[playback][tempo]")
{
    const TempoMap map ({ { 0, 120.0 }, { 480, 60.0 }, { 960, 240.0 } }, 480);
    CHECK (map.ticksToSeconds (960.0) == Approx (1.5));
    CHECK (map.ticksToSeconds (1440.0) == Approx (1.5 + 0.25));
    for (double tick : { 0.0, 100.0, 480.0, 700.0, 960.0, 1500.0 })
        CHECK (map.secondsToTicks (map.ticksToSeconds (tick)) == Approx (tick).margin (1e-6));
}

TEST_CASE ("TempoMap: seconds are independent of PPQ", "[playback][tempo]")
{
    const TempoMap low ({ { 0, 100.0 }, { 480, 150.0 } }, 480);
    const TempoMap high ({ { 0, 100.0 }, { 960, 150.0 } }, 960);   // same song at twice the PPQ
    CHECK (high.ticksToSeconds (1920.0) == Approx (low.ticksToSeconds (960.0)));
}

TEST_CASE ("TempoMap: bad data never produces a hang or NaN", "[playback][tempo]")
{
    const TempoMap map ({ { 0, 0.0 }, { 10, -5.0 } }, 0);   // bpm <= 0 and ppq 0
    CHECK (map.getTicksPerQuarter() == 480);
    CHECK (map.ticksToSeconds (480.0) == Approx (0.5));
    CHECK (map.ticksToSeconds (-10.0) == Approx (0.0));
}

TEST_CASE ("TempoMap: a later change at the same tick wins", "[playback][tempo]")
{
    const TempoMap map ({ { 0, 120.0 }, { 0, 60.0 } }, 480);
    CHECK (map.ticksToSeconds (480.0) == Approx (1.0));
}
```

- [ ] **Step 2: Run to verify it fails** — add `TempoMap_tests.cpp` and `${CMAKE_SOURCE_DIR}/Source/UI/Playback/TempoMap.cpp` to `forge_tests`; `cmake --build build --target forge_tests` → FAIL (header missing).

- [ ] **Step 3: Implement** — `TempoMap.h`:

```cpp
#pragma once

#include <vector>

namespace lotro
{

struct TempoPoint
{
    int tick = 0;
    double bpm = 120.0;
};

// Piecewise-linear tick <-> seconds conversion over a song's tempo changes.
// Seconds are the playback time domain: unlike ticks they do not change when
// an import rescales the document's PPQ, and they do not depend on the
// device sample rate.
class TempoMap
{
public:
    static constexpr double defaultBpm = 120.0;

    TempoMap() : TempoMap ({}, 480) {}
    TempoMap (std::vector<TempoPoint> points, int ticksPerQuarterIn);

    double ticksToSeconds (double tick) const;
    double secondsToTicks (double seconds) const;
    int getTicksPerQuarter() const noexcept { return ticksPerQuarter; }

private:
    struct Segment
    {
        double startTick;
        double startSeconds;
        double secondsPerTick;
    };

    std::vector<Segment> segments;   // never empty; first starts at tick 0
    int ticksPerQuarter;
};

} // namespace lotro
```

`TempoMap.cpp`:

```cpp
#include "UI/Playback/TempoMap.h"

#include <algorithm>

namespace lotro
{

TempoMap::TempoMap (std::vector<TempoPoint> points, int ticksPerQuarterIn)
    : ticksPerQuarter (ticksPerQuarterIn > 0 ? ticksPerQuarterIn : 480)
{
    points.erase (std::remove_if (points.begin(), points.end(),
                                  [] (const TempoPoint& p) { return ! (p.bpm > 0.0) || p.tick < 0; }),
                  points.end());
    std::stable_sort (points.begin(), points.end(),
                      [] (const TempoPoint& a, const TempoPoint& b) { return a.tick < b.tick; });
    if (points.empty() || points.front().tick > 0)
        points.insert (points.begin(), TempoPoint { 0, defaultBpm });

    double seconds = 0.0;
    for (size_t i = 0; i < points.size(); ++i)
    {
        if (i > 0)
            seconds += (double) (points[i].tick - points[i - 1].tick) * segments.back().secondsPerTick;
        segments.push_back ({ (double) points[i].tick, seconds, 60.0 / (points[i].bpm * (double) ticksPerQuarter) });
    }
}

double TempoMap::ticksToSeconds (double tick) const
{
    tick = std::max (0.0, tick);
    auto it = std::upper_bound (segments.begin(), segments.end(), tick,
                                [] (double t, const Segment& s) { return t < s.startTick; });
    const Segment& seg = *(it - 1);
    return seg.startSeconds + (tick - seg.startTick) * seg.secondsPerTick;
}

double TempoMap::secondsToTicks (double seconds) const
{
    seconds = std::max (0.0, seconds);
    auto it = std::upper_bound (segments.begin(), segments.end(), seconds,
                                [] (double s, const Segment& seg) { return s < seg.startSeconds; });
    const Segment& seg = *(it - 1);
    return seg.startTick + (seconds - seg.startSeconds) / seg.secondsPerTick;
}

} // namespace lotro
```

Add `Source/UI/Playback/TempoMap.cpp` to `forge_ui` `target_sources` in `CMakeLists.txt`.

- [ ] **Step 4: Run to verify it passes** — `cmake --build build --target forge_tests && ctest --test-dir build -R "TempoMap" --output-on-failure` → PASS (6 tests).

- [ ] **Step 5: Commit**

```bash
git add Source/UI/Playback/TempoMap.* Tests/TempoMap_tests.cpp Tests/CMakeLists.txt CMakeLists.txt
git commit -m "feat(playback): add TempoMap tick/seconds conversion

Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>"
```

---

### Task 3: `PlaybackSnapshot` and `buildSnapshot`

**Files:**
- Create: `Source/UI/Playback/PlaybackSnapshot.h/.cpp`, `Tests/PlaybackTestSupport.h`, `Tests/PlaybackSnapshot_tests.cpp`
- Modify: `CMakeLists.txt`, `Tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `TempoMap`; `SongDocument` (`getTempoMapNode()`, `getSourceMidiNode()`, `getNumTracks()`, `getTrack(i)`, `getNotesNode()`, `getEventsNode()`), `SongIDs::{NOTE,EVENT,TEMPO_CHANGE,pitch,startTick,durationTicks,velocity,channel,tick,bpm,data,trackId,isConductor,ticksPerQuarter}`.
- Produces (all in `namespace lotro`):

```cpp
enum class PlaybackEventKind : std::uint8_t { Program, Control, PitchBend, NoteOff, NoteOn };  // enumerator order == same-tick firing order
struct PlaybackEvent { int tick; double seconds; PlaybackEventKind kind; int virtualChannel; int trackIndex; int data1; int data2; };
// data1/data2: NoteOn = key, velocity(1..127); NoteOff = key, 0; Program = program, isDrum(0/1);
//              Control = controller, value; PitchBend = 14-bit value (0..16383), 0
struct VirtualChannel { juce::int64 trackId; int trackIndex; int midiChannel; bool isDrum; };
constexpr int kMaxVirtualChannels = 256;
class PlaybackSnapshot
{
public:
    PlaybackSnapshot (TempoMap, std::vector<PlaybackEvent>, std::vector<VirtualChannel>, std::vector<juce::int64> trackIds);
    const TempoMap& tempo() const noexcept;
    const std::vector<PlaybackEvent>& events() const noexcept;      // sorted by (tick, kind), stable
    const std::vector<VirtualChannel>& channels() const noexcept;
    const std::vector<juce::int64>& trackIds() const noexcept;      // index == trackIndex == SOURCE_MIDI child index
    int numTracks() const noexcept;
    int trackIndexForId (juce::int64) const noexcept;               // -1 if unknown
    double endSeconds() const noexcept;                             // last NoteOff time; 0 if no notes
    size_t firstEventAtOrAfter (double seconds) const noexcept;
    const std::vector<int>& channelsOfTrack (int trackIndex) const; // virtual channel indices
    bool isAudible (int trackIndex) const noexcept;                 // atomic read (audio thread)
    void setAudible (int trackIndex, bool) noexcept;                // atomic write (message thread)
    bool appliedAudible (int trackIndex) const noexcept;            // audio-thread-only bookkeeping
    void setAppliedAudible (int trackIndex, bool) const noexcept;   // (mutable)
};
std::shared_ptr<PlaybackSnapshot> buildSnapshot (const SongDocument&);
TempoMap tempoMapFromDocument (const SongDocument&);
```

- [ ] **Step 1: Write the test helpers and failing tests.** `Tests/PlaybackTestSupport.h`:

```cpp
#pragma once

#include "UI/SongDocument.h"

#include <juce_data_structures/juce_data_structures.h>

#include <cstdint>
#include <vector>

// Builders for hand-made Songs used by the playback tests.
namespace lotro::playbacktest
{

inline void addNote (juce::ValueTree track, int pitch, int startTick, int durationTicks,
                     int velocity = 100, int channel = 1)
{
    juce::ValueTree note (SongIDs::NOTE);
    note.setProperty (SongIDs::pitch, pitch, nullptr);
    note.setProperty (SongIDs::startTick, startTick, nullptr);
    note.setProperty (SongIDs::durationTicks, durationTicks, nullptr);
    note.setProperty (SongIDs::velocity, velocity, nullptr);
    note.setProperty (SongIDs::channel, channel, nullptr);
    SongDocument::appendChildBulk (SongDocument::getNotesNode (track), note);
}

inline void addEvent (juce::ValueTree track, int tick, std::vector<std::uint8_t> bytes)
{
    juce::ValueTree event (SongIDs::EVENT);
    event.setProperty (SongIDs::tick, tick, nullptr);
    event.setProperty (SongIDs::data, juce::var (juce::MemoryBlock (bytes.data(), bytes.size())), nullptr);
    SongDocument::appendChildBulk (SongDocument::getEventsNode (track), event);
}

inline void addTempo (SongDocument& doc, int tick, double bpm)
{
    juce::ValueTree change (SongIDs::TEMPO_CHANGE);
    change.setProperty (SongIDs::tick, tick, nullptr);
    change.setProperty (SongIDs::bpm, bpm, nullptr);
    SongDocument::appendChildBulk (doc.getTempoMapNode(), change);
}

inline juce::ValueTree addTrack (SongDocument& doc, const char* name = "T", int channel = 1)
{
    return doc.addTrackBulk (name, 0xff336699, channel, 1);
}

} // namespace lotro::playbacktest
```

`Tests/PlaybackSnapshot_tests.cpp`:

```cpp
#include "PlaybackTestSupport.h"
#include "UI/Playback/PlaybackSnapshot.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace lotro;
using namespace lotro::playbacktest;
using Catch::Approx;

namespace
{
    std::vector<PlaybackEvent> ofKind (const PlaybackSnapshot& s, PlaybackEventKind k)
    {
        std::vector<PlaybackEvent> out;
        for (const auto& e : s.events())
            if (e.kind == k) out.push_back (e);
        return out;
    }
}

TEST_CASE ("buildSnapshot: an empty song has no events and zero length", "[playback][snapshot]")
{
    SongDocument doc;
    const auto snap = buildSnapshot (doc);
    CHECK (snap->events().empty());
    CHECK (snap->endSeconds() == 0.0);
}

TEST_CASE ("buildSnapshot: a note becomes timed NoteOn/NoteOff in seconds at 120 BPM", "[playback][snapshot]")
{
    SongDocument doc;
    auto t = addTrack (doc);
    addNote (t, 60, 480, 240, 90, 1);   // 0.5 s .. 0.75 s at 120 BPM / 480 PPQ

    const auto snap = buildSnapshot (doc);
    const auto on = ofKind (*snap, PlaybackEventKind::NoteOn);
    const auto off = ofKind (*snap, PlaybackEventKind::NoteOff);
    REQUIRE (on.size() == 1);
    REQUIRE (off.size() == 1);
    CHECK (on[0].seconds == Approx (0.5));
    CHECK (on[0].data1 == 60);
    CHECK (on[0].data2 == 90);
    CHECK (off[0].seconds == Approx (0.75));
    CHECK (snap->endSeconds() == Approx (0.75));
}

TEST_CASE ("buildSnapshot: a mid-song tempo change is honoured", "[playback][snapshot]")
{
    SongDocument doc;
    addTempo (doc, 0, 120.0);
    addTempo (doc, 480, 60.0);
    auto t = addTrack (doc);
    addNote (t, 60, 960, 480);   // starts after 0.5 s @120 + 1.0 s @60 = 1.5 s

    const auto snap = buildSnapshot (doc);
    CHECK (ofKind (*snap, PlaybackEventKind::NoteOn)[0].seconds == Approx (1.5));
}

TEST_CASE ("buildSnapshot: NoteOff sorts before NoteOn at the same tick, controllers before both", "[playback][snapshot]")
{
    SongDocument doc;
    auto t = addTrack (doc);
    addNote (t, 60, 0, 480);
    addNote (t, 60, 480, 480);                 // retrigger exactly at the previous note's off
    addEvent (t, 480, { 0xB0, 7, 100 });       // CC7 on channel 1 at the same tick

    const auto snap = buildSnapshot (doc);
    std::vector<PlaybackEventKind> at480;
    for (const auto& e : snap->events())
        if (e.tick == 480) at480.push_back (e.kind);
    REQUIRE (at480.size() == 3);
    CHECK (at480[0] == PlaybackEventKind::Control);
    CHECK (at480[1] == PlaybackEventKind::NoteOff);
    CHECK (at480[2] == PlaybackEventKind::NoteOn);
}

TEST_CASE ("buildSnapshot: program, control and pitch bend events are decoded; others ignored", "[playback][snapshot]")
{
    SongDocument doc;
    auto t = addTrack (doc, "T", 3);
    addNote (t, 60, 0, 480, 100, 3);
    addEvent (t, 0, { 0xC2, 24 });              // program 24 on channel 3
    addEvent (t, 0, { 0xB2, 64, 127 });         // CC64 sustain
    addEvent (t, 10, { 0xE2, 0x00, 0x60 });     // bend: 0x00 | (0x60 << 7) = 12288
    addEvent (t, 20, { 0xF0, 0x01, 0xF7 });     // SysEx: ignored
    addEvent (t, 30, { 0xFF, 0x2F, 0x00 });     // meta: ignored
    addEvent (t, 40, { 0xD2, 50 });             // channel pressure: ignored

    const auto snap = buildSnapshot (doc);
    const auto programs = ofKind (*snap, PlaybackEventKind::Program);
    REQUIRE (programs.size() == 2);              // 1 setup event + the real one
    CHECK (programs.back().data1 == 24);
    const auto controls = ofKind (*snap, PlaybackEventKind::Control);
    REQUIRE (controls.size() == 1);
    CHECK (controls[0].data1 == 64);
    CHECK (controls[0].data2 == 127);
    const auto bends = ofKind (*snap, PlaybackEventKind::PitchBend);
    REQUIRE (bends.size() == 1);
    CHECK (bends[0].data1 == 12288);
    CHECK (snap->events().size() == 6);   // setup program, real program, CC, bend, NoteOn, NoteOff
}

TEST_CASE ("buildSnapshot: each (track, channel) pair gets its own virtual channel", "[playback][snapshot]")
{
    SongDocument doc;
    auto a = addTrack (doc, "A", 1);
    auto b = addTrack (doc, "B", 1);             // same MIDI channel as A
    addNote (a, 60, 0, 480, 100, 1);
    addNote (a, 62, 0, 480, 100, 2);             // A also uses channel 2
    addNote (b, 64, 0, 480, 100, 1);
    addEvent (a, 0, { 0xC0, 5 });
    addEvent (b, 0, { 0xC0, 40 });

    const auto snap = buildSnapshot (doc);
    CHECK (snap->channels().size() == 3);
    int programFor5 = -1, programFor40 = -1;
    for (const auto& e : snap->events())
        if (e.kind == PlaybackEventKind::Program && e.data1 == 5) programFor5 = e.virtualChannel;
        else if (e.kind == PlaybackEventKind::Program && e.data1 == 40) programFor40 = e.virtualChannel;
    CHECK (programFor5 != programFor40);
    CHECK (snap->channelsOfTrack (snap->trackIndexForId ((juce::int64) a.getProperty (SongIDs::trackId))).size() == 2);
}

TEST_CASE ("buildSnapshot: channel 10 uses the drum bank even with no program-change event", "[playback][snapshot]")
{
    SongDocument doc;
    auto t = addTrack (doc, "Drums", 10);
    addNote (t, 36, 0, 120, 100, 10);

    const auto snap = buildSnapshot (doc);
    REQUIRE (snap->channels().size() == 1);
    CHECK (snap->channels()[0].isDrum);
    const auto programs = ofKind (*snap, PlaybackEventKind::Program);
    REQUIRE (programs.size() == 1);
    CHECK (programs[0].data2 == 1);
    CHECK (programs[0].seconds == 0.0);
}

TEST_CASE ("buildSnapshot: the conductor's events are ignored for sound", "[playback][snapshot]")
{
    SongDocument doc;
    addEvent (doc.getConductorTrack(), 0, { 0xB0, 7, 100 });
    CHECK (buildSnapshot (doc)->events().empty());
}

TEST_CASE ("buildSnapshot: degenerate note data is clamped", "[playback][snapshot]")
{
    SongDocument doc;
    auto t = addTrack (doc);
    addNote (t, 200, 0, 0, 0, 1);       // pitch > 127, zero length, velocity 0
    juce::ValueTree bare (SongIDs::NOTE);   // no properties at all
    SongDocument::appendChildBulk (SongDocument::getNotesNode (t), bare);

    const auto snap = buildSnapshot (doc);
    const auto on = ofKind (*snap, PlaybackEventKind::NoteOn);
    const auto off = ofKind (*snap, PlaybackEventKind::NoteOff);
    REQUIRE (on.size() == 2);
    CHECK (on[0].data1 == 127);
    CHECK (on[0].data2 >= 1);
    CHECK (off[0].tick > on[0].tick);   // zero-length becomes at least one tick
}

TEST_CASE ("buildSnapshot: more than 64 tracks keep independent audible flags", "[playback][snapshot]")
{
    SongDocument doc;
    for (int i = 0; i < 70; ++i)
        addNote (addTrack (doc), 60, 0, 480);

    const auto snap = buildSnapshot (doc);
    REQUIRE (snap->numTracks() == 71);   // 70 + conductor
    snap->setAudible (69, false);
    CHECK (! snap->isAudible (69));
    CHECK (snap->isAudible (68));
    CHECK (snap->isAudible (1));
}

TEST_CASE ("buildSnapshot: more than 256 (track, channel) pairs are dropped without crashing", "[playback][snapshot]")
{
    SongDocument doc;
    for (int i = 0; i < 20; ++i)
    {
        auto t = addTrack (doc);
        for (int ch = 1; ch <= 16; ++ch)
            addNote (t, 60, 0, 480, 100, ch);
    }
    const auto snap = buildSnapshot (doc);
    CHECK ((int) snap->channels().size() == kMaxVirtualChannels);
}
```

- [ ] **Step 2: Run to verify they fail** — add `PlaybackSnapshot_tests.cpp` + `${CMAKE_SOURCE_DIR}/Source/UI/Playback/PlaybackSnapshot.cpp` to `forge_tests` (and `TempoMap.cpp` already there); build → FAIL (missing header).

- [ ] **Step 3: Implement** — `PlaybackSnapshot.h`:

```cpp
#pragma once

#include "UI/Playback/TempoMap.h"

#include <juce_core/juce_core.h>

#include <atomic>
#include <cstdint>
#include <memory>
#include <vector>

namespace lotro
{

class SongDocument;

enum class PlaybackEventKind : std::uint8_t { Program, Control, PitchBend, NoteOff, NoteOn };

struct PlaybackEvent
{
    int tick = 0;
    double seconds = 0.0;
    PlaybackEventKind kind = PlaybackEventKind::NoteOn;
    int virtualChannel = 0;
    int trackIndex = 0;
    int data1 = 0;
    int data2 = 0;
};

struct VirtualChannel
{
    juce::int64 trackId = -1;
    int trackIndex = 0;
    int midiChannel = 1;
    bool isDrum = false;
};

constexpr int kMaxVirtualChannels = 256;

// Immutable (apart from the audible flags) flattened view of a Song for the
// audio thread. Built on the message thread by buildSnapshot().
class PlaybackSnapshot
{
public:
    PlaybackSnapshot (TempoMap tempoIn, std::vector<PlaybackEvent> eventsIn,
                      std::vector<VirtualChannel> channelsIn, std::vector<juce::int64> trackIdsIn);

    const TempoMap& tempo() const noexcept { return tempoMap; }
    const std::vector<PlaybackEvent>& events() const noexcept { return eventList; }
    const std::vector<VirtualChannel>& channels() const noexcept { return channelList; }
    const std::vector<juce::int64>& trackIds() const noexcept { return trackIdList; }
    int numTracks() const noexcept { return (int) trackIdList.size(); }
    int trackIndexForId (juce::int64 trackId) const noexcept;
    double endSeconds() const noexcept { return endTime; }
    size_t firstEventAtOrAfter (double seconds) const noexcept;
    const std::vector<int>& channelsOfTrack (int trackIndex) const { return byTrack[(size_t) trackIndex]; }

    bool isAudible (int trackIndex) const noexcept { return audible[(size_t) trackIndex].load (std::memory_order_acquire) != 0; }
    void setAudible (int trackIndex, bool shouldBeAudible) noexcept { audible[(size_t) trackIndex].store (shouldBeAudible ? 1 : 0, std::memory_order_release); }

    // Audio-thread-only: which tracks the engine has already released voices for.
    bool appliedAudible (int trackIndex) const noexcept { return applied[(size_t) trackIndex] != 0; }
    void setAppliedAudible (int trackIndex, bool value) const noexcept { applied[(size_t) trackIndex] = value ? 1 : 0; }

private:
    TempoMap tempoMap;
    std::vector<PlaybackEvent> eventList;
    std::vector<VirtualChannel> channelList;
    std::vector<juce::int64> trackIdList;
    std::vector<std::vector<int>> byTrack;
    std::unique_ptr<std::atomic<std::uint8_t>[]> audible;
    mutable std::vector<std::uint8_t> applied;
    double endTime = 0.0;
};

TempoMap tempoMapFromDocument (const SongDocument& doc);
std::shared_ptr<PlaybackSnapshot> buildSnapshot (const SongDocument& doc);

} // namespace lotro
```

`PlaybackSnapshot.cpp`:

```cpp
#include "UI/Playback/PlaybackSnapshot.h"

#include "UI/SongDocument.h"

#include <algorithm>
#include <map>

namespace lotro
{

PlaybackSnapshot::PlaybackSnapshot (TempoMap tempoIn, std::vector<PlaybackEvent> eventsIn,
                                    std::vector<VirtualChannel> channelsIn, std::vector<juce::int64> trackIdsIn)
    : tempoMap (std::move (tempoIn)), eventList (std::move (eventsIn)),
      channelList (std::move (channelsIn)), trackIdList (std::move (trackIdsIn)),
      byTrack (trackIdList.size()),
      audible (new std::atomic<std::uint8_t>[trackIdList.size()]),
      applied (trackIdList.size(), 1)
{
    for (size_t i = 0; i < trackIdList.size(); ++i)
        audible[i].store (1, std::memory_order_relaxed);
    for (size_t c = 0; c < channelList.size(); ++c)
        byTrack[(size_t) channelList[c].trackIndex].push_back ((int) c);
    for (const auto& e : eventList)
        if (e.kind == PlaybackEventKind::NoteOff)
            endTime = std::max (endTime, e.seconds);
}

int PlaybackSnapshot::trackIndexForId (juce::int64 trackId) const noexcept
{
    for (size_t i = 0; i < trackIdList.size(); ++i)
        if (trackIdList[i] == trackId)
            return (int) i;
    return -1;
}

size_t PlaybackSnapshot::firstEventAtOrAfter (double seconds) const noexcept
{
    return (size_t) (std::lower_bound (eventList.begin(), eventList.end(), seconds,
                                       [] (const PlaybackEvent& e, double s) { return e.seconds < s; })
                     - eventList.begin());
}

TempoMap tempoMapFromDocument (const SongDocument& doc)
{
    std::vector<TempoPoint> points;
    const auto node = doc.getTempoMapNode();
    for (int i = 0; i < node.getNumChildren(); ++i)
    {
        const auto change = node.getChild (i);
        points.push_back ({ (int) change.getProperty (SongIDs::tick, 0), (double) change.getProperty (SongIDs::bpm, 0.0) });
    }
    return TempoMap (std::move (points), (int) doc.getSourceMidiNode().getProperty (SongIDs::ticksPerQuarter, 480));
}

std::shared_ptr<PlaybackSnapshot> buildSnapshot (const SongDocument& doc)
{
    const TempoMap tempo = tempoMapFromDocument (doc);

    std::vector<PlaybackEvent> events;
    std::vector<VirtualChannel> channels;
    std::vector<juce::int64> trackIds;
    std::map<std::pair<int, int>, int> channelIndex;   // (trackIndex, midiChannel) -> virtual channel

    auto channelFor = [&] (int trackIndex, juce::int64 trackId, int midiChannel) -> int
    {
        const auto key = std::make_pair (trackIndex, midiChannel);
        if (auto it = channelIndex.find (key); it != channelIndex.end())
            return it->second;
        if ((int) channels.size() >= kMaxVirtualChannels)
            return -1;
        const int vch = (int) channels.size();
        channels.push_back ({ trackId, trackIndex, midiChannel, midiChannel == 10 });
        channelIndex.emplace (key, vch);
        return vch;
    };

    auto push = [&] (int tick, PlaybackEventKind kind, int vch, int trackIndex, int d1, int d2)
    {
        events.push_back ({ tick, tempo.ticksToSeconds ((double) tick), kind, vch, trackIndex, d1, d2 });
    };

    for (int i = 0; i < doc.getNumTracks(); ++i)
        trackIds.push_back ((juce::int64) doc.getTrack (i).getProperty (SongIDs::trackId, (juce::int64) -1));

    // `events` collects the real events through `push`; the per-channel setup
    // events are prepended afterwards so that a stable sort keeps them ahead of
    // a track's own tick-0 program change.
    for (int i = 0; i < doc.getNumTracks(); ++i)
    {
        const auto track = doc.getTrack (i);
        if ((bool) track.getProperty (SongIDs::isConductor, false))
            continue;
        const auto trackId = trackIds[(size_t) i];

        const auto notes = SongDocument::getNotesNode (track);
        for (int n = 0; n < notes.getNumChildren(); ++n)
        {
            const auto note = notes.getChild (n);
            const int midiChannel = juce::jlimit (1, 16, (int) note.getProperty (SongIDs::channel, 1));
            const int vch = channelFor (i, trackId, midiChannel);
            if (vch < 0)
                continue;
            const int pitch = juce::jlimit (0, 127, (int) note.getProperty (SongIDs::pitch, 60));
            const int velocity = juce::jlimit (1, 127, (int) note.getProperty (SongIDs::velocity, 64));
            const int start = std::max (0, (int) note.getProperty (SongIDs::startTick, 0));
            const int duration = std::max (1, (int) note.getProperty (SongIDs::durationTicks, 1));
            push (start, PlaybackEventKind::NoteOn, vch, i, pitch, velocity);
            push (start + duration, PlaybackEventKind::NoteOff, vch, i, pitch, 0);
        }

        const auto eventNodes = SongDocument::getEventsNode (track);
        for (int n = 0; n < eventNodes.getNumChildren(); ++n)
        {
            const auto node = eventNodes.getChild (n);
            const auto* bytes = node.getProperty (SongIDs::data).getBinaryData();
            if (bytes == nullptr || bytes->getSize() < 2)
                continue;
            const auto* b = static_cast<const std::uint8_t*> (bytes->getData());
            const int status = b[0];
            if (status < 0x80 || status >= 0xF0)
                continue;   // SysEx, meta, system-common: not played
            const int vch = channelFor (i, trackId, (status & 0x0F) + 1);
            if (vch < 0)
                continue;
            const int tick = std::max (0, (int) node.getProperty (SongIDs::tick, 0));
            switch (status & 0xF0)
            {
                case 0xB0: if (bytes->getSize() >= 3) push (tick, PlaybackEventKind::Control, vch, i, b[1] & 0x7F, b[2] & 0x7F); break;
                case 0xC0: push (tick, PlaybackEventKind::Program, vch, i, b[1] & 0x7F, channels[(size_t) vch].isDrum ? 1 : 0); break;
                case 0xE0: if (bytes->getSize() >= 3) push (tick, PlaybackEventKind::PitchBend, vch, i, (b[1] & 0x7F) | ((b[2] & 0x7F) << 7), 0); break;
                default: break;   // note on/off (carried by NOTE nodes), polyphonic/channel pressure
            }
        }
    }

    std::vector<PlaybackEvent> all;
    for (size_t c = 0; c < channels.size(); ++c)
        all.push_back ({ 0, 0.0, PlaybackEventKind::Program, (int) c, channels[c].trackIndex, 0, channels[c].isDrum ? 1 : 0 });
    all.insert (all.end(), events.begin(), events.end());
    std::stable_sort (all.begin(), all.end(), [] (const PlaybackEvent& a, const PlaybackEvent& b)
    {
        if (a.tick != b.tick) return a.tick < b.tick;
        return (int) a.kind < (int) b.kind;
    });

    return std::make_shared<PlaybackSnapshot> (tempo, std::move (all), std::move (channels), std::move (trackIds));
}

} // namespace lotro
```

Add `Source/UI/Playback/PlaybackSnapshot.cpp` to `forge_ui` `target_sources`.

- [ ] **Step 4: Run to verify it passes** — `cmake --build build --target forge_tests && ctest --test-dir build -R "buildSnapshot" --output-on-failure` → PASS (10 tests).

- [ ] **Step 5: Commit**

```bash
git add Source/UI/Playback/PlaybackSnapshot.* Tests/PlaybackSnapshot_tests.cpp Tests/PlaybackTestSupport.h Tests/CMakeLists.txt CMakeLists.txt
git commit -m "feat(playback): flatten a Song into a seconds-timed PlaybackSnapshot

Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>"
```

---

### Task 4: `MuteSoloState`

**Files:**
- Create: `Source/UI/Playback/MuteSoloState.h/.cpp`, `Tests/MuteSoloState_tests.cpp`
- Modify: `CMakeLists.txt`, `Tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `PlaybackSnapshot` (`trackIds()`, `setAudible`).
- Produces: `class MuteSoloState { void setMuted (juce::int64, bool); void setSoloed (juce::int64, bool); bool isMuted (juce::int64) const; bool isSoloed (juce::int64) const; bool anySolo() const; bool isAudible (juce::int64) const; bool isSilencedBySolo (juce::int64) const; void clear(); void apply (PlaybackSnapshot&) const; }`.

- [ ] **Step 1: Write the failing tests** — `Tests/MuteSoloState_tests.cpp`:

```cpp
#include "PlaybackTestSupport.h"
#include "UI/Playback/MuteSoloState.h"
#include "UI/Playback/PlaybackSnapshot.h"

#include <catch2/catch_test_macros.hpp>

using namespace lotro;
using namespace lotro::playbacktest;

TEST_CASE ("MuteSoloState: nothing muted or soloed means everything is audible", "[playback][mutesolo]")
{
    MuteSoloState s;
    CHECK (s.isAudible (5));
    CHECK (! s.anySolo());
}

TEST_CASE ("MuteSoloState: mute silences a track", "[playback][mutesolo]")
{
    MuteSoloState s;
    s.setMuted (1, true);
    CHECK (! s.isAudible (1));
    CHECK (s.isAudible (2));
}

TEST_CASE ("MuteSoloState: solo is additive and silences everything else", "[playback][mutesolo]")
{
    MuteSoloState s;
    s.setSoloed (1, true);
    s.setSoloed (2, true);
    CHECK (s.isAudible (1));
    CHECK (s.isAudible (2));
    CHECK (! s.isAudible (3));
    CHECK (s.isSilencedBySolo (3));
    CHECK (! s.isSilencedBySolo (1));
}

TEST_CASE ("MuteSoloState: mute beats solo", "[playback][mutesolo]")
{
    MuteSoloState s;
    s.setSoloed (1, true);
    s.setMuted (1, true);
    CHECK (! s.isAudible (1));
}

TEST_CASE ("MuteSoloState: clear resets everything", "[playback][mutesolo]")
{
    MuteSoloState s;
    s.setMuted (1, true);
    s.setSoloed (2, true);
    s.clear();
    CHECK (s.isAudible (1));
    CHECK (s.isAudible (3));
}

TEST_CASE ("MuteSoloState: apply writes the snapshot's audible flags (beyond 64 tracks too)", "[playback][mutesolo]")
{
    SongDocument doc;
    juce::int64 lastId = -1;
    for (int i = 0; i < 70; ++i)
        lastId = (juce::int64) addTrack (doc).getProperty (SongIDs::trackId);

    const auto snap = buildSnapshot (doc);
    MuteSoloState s;
    s.setMuted (lastId, true);
    s.apply (*snap);
    CHECK (! snap->isAudible (snap->trackIndexForId (lastId)));
    CHECK (snap->isAudible (1));
}

TEST_CASE ("MuteSoloState: entries survive for ids that are no longer in the song (undo of removeTrack)", "[playback][mutesolo]")
{
    MuteSoloState s;
    s.setMuted (42, true);
    SongDocument doc;                 // song without track 42
    s.apply (*buildSnapshot (doc));   // must not crash or drop the entry
    CHECK (s.isMuted (42));
}
```

- [ ] **Step 2: Run to verify it fails** — add test + `MuteSoloState.cpp` to `forge_tests`; build → FAIL.

- [ ] **Step 3: Implement** — `MuteSoloState.h`:

```cpp
#pragma once

#include "UI/Playback/PlaybackSnapshot.h"

#include <juce_core/juce_core.h>

#include <set>

namespace lotro
{

// Message-thread owner of the session's mute/solo flags, keyed by trackId.
// Not part of the Song: never saved, never undoable.
class MuteSoloState
{
public:
    void setMuted (juce::int64 trackId, bool muted);
    void setSoloed (juce::int64 trackId, bool soloed);
    bool isMuted (juce::int64 trackId) const { return muted.count (trackId) != 0; }
    bool isSoloed (juce::int64 trackId) const { return soloed.count (trackId) != 0; }
    bool anySolo() const { return ! soloed.empty(); }

    // Mute beats solo; solo is additive.
    bool isAudible (juce::int64 trackId) const;
    // True when the track is silent only because some other track is soloed.
    bool isSilencedBySolo (juce::int64 trackId) const { return anySolo() && ! isSoloed (trackId) && ! isMuted (trackId); }

    void clear();

    // Writes every track's audible flag into `snapshot`.
    void apply (PlaybackSnapshot& snapshot) const;

private:
    std::set<juce::int64> muted;
    std::set<juce::int64> soloed;
};

} // namespace lotro
```

`MuteSoloState.cpp`:

```cpp
#include "UI/Playback/MuteSoloState.h"

namespace lotro
{

void MuteSoloState::setMuted (juce::int64 trackId, bool value)
{
    if (value) muted.insert (trackId); else muted.erase (trackId);
}

void MuteSoloState::setSoloed (juce::int64 trackId, bool value)
{
    if (value) soloed.insert (trackId); else soloed.erase (trackId);
}

bool MuteSoloState::isAudible (juce::int64 trackId) const
{
    if (isMuted (trackId))
        return false;
    return ! anySolo() || isSoloed (trackId);
}

void MuteSoloState::clear()
{
    muted.clear();
    soloed.clear();
}

void MuteSoloState::apply (PlaybackSnapshot& snapshot) const
{
    for (int i = 0; i < snapshot.numTracks(); ++i)
        snapshot.setAudible (i, isAudible (snapshot.trackIds()[(size_t) i]));
}

} // namespace lotro
```

Add `MuteSoloState.cpp` to `forge_ui` sources.

- [ ] **Step 4: Run to verify it passes** — `ctest --test-dir build -R "MuteSoloState" --output-on-failure` → PASS (7).

- [ ] **Step 5: Commit** — `git add Source/UI/Playback/MuteSoloState.* Tests/MuteSoloState_tests.cpp Tests/CMakeLists.txt CMakeLists.txt && git commit -m "feat(playback): add session mute/solo state keyed by trackId" -m "Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>"` (use the full trailer format as in Task 1).

---

### Task 5: `Transport` and `previousBarTick`

**Files:**
- Create: `Source/UI/Playback/Transport.h/.cpp`, `Tests/Transport_tests.cpp`
- Modify: `CMakeLists.txt`, `Tests/CMakeLists.txt`

**Interfaces:**
- Produces:

```cpp
class Transport
{
public:
    bool isPlaying() const noexcept;
    double getPositionSeconds() const noexcept;
    double getPlayStartSeconds() const noexcept;
    unsigned getSeekGeneration() const noexcept;     // bumps on every message-thread position jump
    // message thread
    void play (double endSeconds);        // no-op when endSeconds <= 0; restarts from 0 when at/after the end
    void pause();                         // leaves the position
    void stop();                          // halts; position returns to where play() last started
    void seek (double seconds);           // clamps >= 0; bumps the seek generation
    void goToStart();                     // == seek (0)
    void goToEnd (double endSeconds);     // == seek (endSeconds); playback then auto-stops
    // audio thread
    void advance (double seconds, double endSeconds) noexcept;   // at/after endSeconds: position = end, playing = false
};
double previousBarTick (double tick, int ticksPerQuarter, int numerator, int denominator);
```

- [ ] **Step 1: Write the failing tests** — `Tests/Transport_tests.cpp`:

```cpp
#include "UI/Playback/Transport.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace lotro;
using Catch::Approx;

TEST_CASE ("Transport: play on a song with no length is a no-op", "[playback][transport]")
{
    Transport t;
    t.play (0.0);
    CHECK (! t.isPlaying());
    CHECK (t.getPositionSeconds() == 0.0);
}

TEST_CASE ("Transport: play starts from the current position; pause keeps it", "[playback][transport]")
{
    Transport t;
    t.seek (2.0);
    t.play (10.0);
    CHECK (t.isPlaying());
    t.advance (1.5, 10.0);
    CHECK (t.getPositionSeconds() == Approx (3.5));
    t.pause();
    CHECK (! t.isPlaying());
    CHECK (t.getPositionSeconds() == Approx (3.5));
}

TEST_CASE ("Transport: stop returns to where play started", "[playback][transport]")
{
    Transport t;
    t.seek (2.0);
    t.play (10.0);
    t.advance (3.0, 10.0);
    t.stop();
    CHECK (! t.isPlaying());
    CHECK (t.getPositionSeconds() == Approx (2.0));
}

TEST_CASE ("Transport: reaching the end auto-stops with the playhead at the end; play then restarts from 0", "[playback][transport]")
{
    Transport t;
    t.play (4.0);
    t.advance (5.0, 4.0);
    CHECK (! t.isPlaying());
    CHECK (t.getPositionSeconds() == Approx (4.0));
    t.play (4.0);
    CHECK (t.isPlaying());
    CHECK (t.getPositionSeconds() == Approx (0.0));
}

TEST_CASE ("Transport: seeks bump the generation and clamp at zero", "[playback][transport]")
{
    Transport t;
    const auto g0 = t.getSeekGeneration();
    t.seek (-3.0);
    CHECK (t.getPositionSeconds() == 0.0);
    CHECK (t.getSeekGeneration() != g0);
    t.goToEnd (7.0);
    CHECK (t.getPositionSeconds() == Approx (7.0));
    t.goToStart();
    CHECK (t.getPositionSeconds() == 0.0);
}

TEST_CASE ("previousBarTick: steps to the start of the current bar, or the previous one when on a bar line", "[playback][transport]")
{
    // 480 PPQ, 4/4 -> 1920 ticks per bar
    CHECK (previousBarTick (2500.0, 480, 4, 4) == 1920.0);
    CHECK (previousBarTick (3840.0, 480, 4, 4) == 1920.0);
    CHECK (previousBarTick (100.0, 480, 4, 4) == 0.0);
    CHECK (previousBarTick (0.0, 480, 4, 4) == 0.0);
    CHECK (previousBarTick (1000.0, 480, 3, 4) == 0.0);       // 1440 per bar
    CHECK (previousBarTick (1000.0, 480, 0, 0) == 0.0);       // bad meter never divides by zero
}
```

- [ ] **Step 2: Run to verify it fails** — add test + `Transport.cpp`; build → FAIL.

- [ ] **Step 3: Implement** — `Transport.h`:

```cpp
#pragma once

#include <atomic>

namespace lotro
{

// The one playback clock shared by every window. State is atomic so the audio
// thread can advance it while the message thread reads it. Position is in
// seconds (see TempoMap).
class Transport
{
public:
    bool isPlaying() const noexcept { return playing.load (std::memory_order_acquire); }
    double getPositionSeconds() const noexcept { return position.load (std::memory_order_acquire); }
    double getPlayStartSeconds() const noexcept { return playStart.load (std::memory_order_acquire); }
    unsigned getSeekGeneration() const noexcept { return seekGeneration.load (std::memory_order_acquire); }

    void play (double endSeconds) noexcept;
    void pause() noexcept;
    void stop() noexcept;
    void seek (double seconds) noexcept;
    void goToStart() noexcept { seek (0.0); }
    void goToEnd (double endSeconds) noexcept { seek (endSeconds); }

    void advance (double seconds, double endSeconds) noexcept;

private:
    std::atomic<bool> playing { false };
    std::atomic<double> position { 0.0 };
    std::atomic<double> playStart { 0.0 };
    std::atomic<unsigned> seekGeneration { 0 };
};

// Tick of the start of the bar containing `tick`, or of the previous bar when
// `tick` is exactly on a bar line. Bad meter values yield 0.
double previousBarTick (double tick, int ticksPerQuarter, int numerator, int denominator);

} // namespace lotro
```

`Transport.cpp`:

```cpp
#include "UI/Playback/Transport.h"

#include <algorithm>
#include <cmath>

namespace lotro
{

void Transport::play (double endSeconds) noexcept
{
    if (! (endSeconds > 0.0))
        return;
    if (position.load() >= endSeconds)
        position.store (0.0);
    playStart.store (position.load());
    playing.store (true, std::memory_order_release);
}

void Transport::pause() noexcept
{
    playing.store (false, std::memory_order_release);
}

void Transport::stop() noexcept
{
    playing.store (false, std::memory_order_release);
    position.store (playStart.load());
    seekGeneration.fetch_add (1);
}

void Transport::seek (double seconds) noexcept
{
    position.store (std::max (0.0, seconds), std::memory_order_release);
    seekGeneration.fetch_add (1);
}

void Transport::advance (double seconds, double endSeconds) noexcept
{
    const double next = position.load() + seconds;
    if (next >= endSeconds)
    {
        position.store (endSeconds, std::memory_order_release);
        playing.store (false, std::memory_order_release);
    }
    else
    {
        position.store (next, std::memory_order_release);
    }
}

double previousBarTick (double tick, int ticksPerQuarter, int numerator, int denominator)
{
    if (ticksPerQuarter <= 0 || numerator <= 0 || denominator <= 0)
        return 0.0;
    const double barTicks = (double) ticksPerQuarter * 4.0 * (double) numerator / (double) denominator;
    const double bars = std::ceil (tick / barTicks - 1e-9);
    return std::max (0.0, (bars - 1.0) * barTicks);
}

} // namespace lotro
```

Add `Transport.cpp` to `forge_ui` sources. Note: "Rewind = step back one bar" is **unconfirmed with the user**; it lives only in `previousBarTick` + `PlaybackController::rewindOneBar` (Task 10), so changing it later is a one-function edit.

- [ ] **Step 4: Run to verify it passes** — `ctest --test-dir build -R "Transport|previousBarTick" --output-on-failure` → PASS (6).

- [ ] **Step 5: Commit** — `feat(playback): add the shared Transport and previousBarTick` (full trailer).

---

### Task 6: `HandOff<T>` (lock-free snapshot/synth publication)

**Files:**
- Create: `Source/UI/Playback/HandOff.h`, `Tests/HandOff_tests.cpp`
- Modify: `Tests/CMakeLists.txt`

**Interfaces:**
- Produces:

```cpp
template <typename T> class HandOff
{
public:
    void publish (std::shared_ptr<T> next);   // message thread
    void collectRetired();                    // message thread (also called by publish)
    T* acquire (bool& swapped) noexcept;      // audio thread: newest published object; swapped=true on change
    size_t liveCount() const;                 // message thread: objects still owned (tests)
};
```

- [ ] **Step 1: Write the failing tests** — `Tests/HandOff_tests.cpp`:

```cpp
#include "UI/Playback/HandOff.h"

#include <catch2/catch_test_macros.hpp>

#include <memory>

using namespace lotro;

namespace { struct Thing { int value; }; }

TEST_CASE ("HandOff: nothing published acquires null", "[playback][handoff]")
{
    HandOff<Thing> h;
    bool swapped = true;
    CHECK (h.acquire (swapped) == nullptr);
    CHECK (! swapped);
}

TEST_CASE ("HandOff: a published object is acquired once, then stays current", "[playback][handoff]")
{
    HandOff<Thing> h;
    h.publish (std::make_shared<Thing> (Thing { 1 }));
    bool swapped = false;
    auto* a = h.acquire (swapped);
    REQUIRE (a != nullptr);
    CHECK (a->value == 1);
    CHECK (swapped);
    auto* b = h.acquire (swapped);
    CHECK (b == a);
    CHECK (! swapped);
}

TEST_CASE ("HandOff: the replaced object is retired and freed by the message thread, never the audio thread", "[playback][handoff]")
{
    HandOff<Thing> h;
    std::weak_ptr<Thing> first;
    {
        auto p = std::make_shared<Thing> (Thing { 1 });
        first = p;
        h.publish (std::move (p));
    }
    bool swapped = false;
    h.acquire (swapped);
    h.publish (std::make_shared<Thing> (Thing { 2 }));
    CHECK (! first.expired());                 // audio thread still on #1
    CHECK (h.acquire (swapped)->value == 2);   // swaps, pushes #1 to the retire queue
    CHECK (! first.expired());                 // not freed on the audio side
    h.collectRetired();
    CHECK (first.expired());                   // freed on the message thread
}

TEST_CASE ("HandOff: an object published twice before the audio thread looks is dropped unseen", "[playback][handoff]")
{
    HandOff<Thing> h;
    std::weak_ptr<Thing> skipped;
    {
        auto p = std::make_shared<Thing> (Thing { 1 });
        skipped = p;
        h.publish (std::move (p));
    }
    h.publish (std::make_shared<Thing> (Thing { 2 }));
    CHECK (skipped.expired());
    bool swapped = false;
    CHECK (h.acquire (swapped)->value == 2);
}
```

- [ ] **Step 2: Run to verify it fails** — add `HandOff_tests.cpp`; build → FAIL.

- [ ] **Step 3: Implement** — `HandOff.h`:

```cpp
#pragma once

#include <juce_core/juce_core.h>

#include <array>
#include <atomic>
#include <map>
#include <memory>

namespace lotro
{

// Publishes immutable objects (PlaybackSnapshot, tsf instances) from the
// message thread to the audio thread without locks or audio-thread frees.
//
//  * publish(): the message thread owns every object in `owned`, and puts the
//    new raw pointer into the single `pending` slot. If an earlier pending
//    object was never picked up, exchange() hands it back and it is freed
//    here (the audio thread never saw it).
//  * acquire(): the audio thread atomically takes `pending`, makes it current,
//    and pushes the previous current pointer into a fixed-size retire FIFO.
//  * collectRetired(): the message thread pops the FIFO and drops ownership.
//
// If the FIFO is full the audio thread simply leaves `pending` in place until
// the message thread drains it.
template <typename T>
class HandOff
{
public:
    void publish (std::shared_ptr<T> next)
    {
        T* raw = next.get();
        owned.emplace (raw, std::move (next));
        if (T* unclaimed = pending.exchange (raw, std::memory_order_acq_rel))
            owned.erase (unclaimed);
        collectRetired();
    }

    void collectRetired()
    {
        int s1 = 0, n1 = 0, s2 = 0, n2 = 0;
        fifo.prepareToRead (fifo.getNumReady(), s1, n1, s2, n2);
        for (int i = 0; i < n1; ++i) owned.erase (retired[(size_t) (s1 + i)]);
        for (int i = 0; i < n2; ++i) owned.erase (retired[(size_t) (s2 + i)]);
        fifo.finishedRead (n1 + n2);
    }

    T* acquire (bool& swapped) noexcept
    {
        swapped = false;
        if (pending.load (std::memory_order_acquire) != nullptr && (current == nullptr || fifo.getFreeSpace() > 0))
        {
            if (T* next = pending.exchange (nullptr, std::memory_order_acq_rel))
            {
                if (current != nullptr)
                {
                    int s1 = 0, n1 = 0, s2 = 0, n2 = 0;
                    fifo.prepareToWrite (1, s1, n1, s2, n2);
                    retired[(size_t) s1] = current;
                    fifo.finishedWrite (1);
                }
                current = next;
                swapped = true;
            }
        }
        return current;
    }

    size_t liveCount() const { return owned.size(); }

private:
    std::atomic<T*> pending { nullptr };
    T* current = nullptr;                          // audio thread only
    juce::AbstractFifo fifo { 16 };
    std::array<T*, 16> retired {};
    std::map<T*, std::shared_ptr<T>> owned;        // message thread only
};

} // namespace lotro
```

- [ ] **Step 4: Run to verify it passes** — `ctest --test-dir build -R "HandOff" --output-on-failure` → PASS (4).

- [ ] **Step 5: Commit** — `feat(playback): add the lock-free HandOff for audio-thread object publication` (full trailer).

---

### Task 7: `EventSink` and `PlaybackEngine` core (timing, pause/stop, auto-stop)

**Files:**
- Create: `Source/UI/Playback/EventSink.h`, `Source/UI/Playback/PlaybackEngine.h/.cpp`, `Tests/PlaybackEngine_tests.cpp`
- Modify: `CMakeLists.txt`, `Tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `Transport`, `HandOff`, `PlaybackSnapshot`, `PlaybackEvent`.
- Produces:

```cpp
class EventSink
{
public:
    virtual ~EventSink() = default;
    virtual void prepare (double sampleRate, int maxBlockSize) = 0;
    virtual bool beginBlock() noexcept = 0;                    // true if the sink's own state was replaced (engine must re-chase)
    virtual void handle (const PlaybackEvent&) noexcept = 0;
    virtual void releaseChannel (int virtualChannel) noexcept = 0;   // note-off with release tails
    virtual void releaseAll() noexcept = 0;
    virtual void render (float* left, float* right, int numFrames) noexcept = 0;   // overwrites
};
class PlaybackEngine
{
public:
    PlaybackEngine (Transport&, EventSink&);
    void publishSnapshot (std::shared_ptr<PlaybackSnapshot>);   // message thread
    void collectGarbage();                                       // message thread
    void prepare (double sampleRate, int maxBlockSize);
    void renderBlock (juce::AudioBuffer<float>&, int startSample, int numSamples) noexcept;   // audio thread / tests
};
```
- Test-only: `RecordingSink` defined in `Tests/PlaybackTestSupport.h` (added in this task).

- [ ] **Step 1: Add `RecordingSink` to `Tests/PlaybackTestSupport.h`** (append, plus `#include "UI/Playback/EventSink.h"`):

```cpp
struct RecordingSink : EventSink
{
    struct Record { long frame; PlaybackEvent event; };
    std::vector<Record> records;
    std::vector<int> releasedChannels;
    long framesRendered = 0;
    int releaseAllCount = 0;
    bool replaced = false;      // set true to make the next beginBlock() report a replaced sink

    void prepare (double, int) override {}
    bool beginBlock() noexcept override { const bool r = replaced; replaced = false; return r; }
    void handle (const PlaybackEvent& e) noexcept override { records.push_back ({ framesRendered, e }); }
    void releaseChannel (int c) noexcept override { releasedChannels.push_back (c); }
    void releaseAll() noexcept override { ++releaseAllCount; }
    void render (float*, float*, int n) noexcept override { framesRendered += n; }

    int count (PlaybackEventKind k) const
    {
        int n = 0;
        for (const auto& r : records) if (r.event.kind == k) ++n;
        return n;
    }
};
```

- [ ] **Step 2: Write the failing engine tests** — `Tests/PlaybackEngine_tests.cpp`:

```cpp
#include "PlaybackTestSupport.h"
#include "UI/Playback/PlaybackEngine.h"
#include "UI/Playback/PlaybackSnapshot.h"
#include "UI/Playback/Transport.h"

#include <catch2/catch_test_macros.hpp>

#include <juce_audio_basics/juce_audio_basics.h>

using namespace lotro;
using namespace lotro::playbacktest;

namespace
{
    constexpr double sr = 48000.0;

    struct Rig
    {
        Transport transport;
        RecordingSink sink;
        PlaybackEngine engine { transport, sink };
        juce::AudioBuffer<float> buffer { 2, 512 };
        std::shared_ptr<PlaybackSnapshot> snapshot;

        explicit Rig (SongDocument& doc)
        {
            engine.prepare (sr, 512);
            snapshot = buildSnapshot (doc);
            engine.publishSnapshot (snapshot);
        }

        void render (int blocks = 1) { for (int i = 0; i < blocks; ++i) engine.renderBlock (buffer, 0, 512); }
    };
}

TEST_CASE ("PlaybackEngine: not playing renders only (silence/tails) and fires no events", "[playback][engine]")
{
    SongDocument doc;
    addNote (addTrack (doc), 60, 0, 480);
    Rig rig (doc);
    rig.render (4);
    CHECK (rig.sink.records.empty());
    CHECK (rig.sink.framesRendered == 4 * 512);
}

TEST_CASE ("PlaybackEngine: a note at tick 480 (0.5 s at 120 BPM) fires at the predicted frame", "[playback][engine]")
{
    SongDocument doc;
    addNote (addTrack (doc), 60, 480, 240);
    Rig rig (doc);
    rig.transport.play (rig.snapshot->endSeconds());
    rig.render (60);   // 60 * 512 = 30720 frames > 24000

    const PlaybackEvent* on = nullptr;
    long onFrame = -1;
    for (const auto& r : rig.sink.records)
        if (r.event.kind == PlaybackEventKind::NoteOn) { on = &r.event; onFrame = r.frame; }
    REQUIRE (on != nullptr);
    CHECK (onFrame == 24000);   // 0.5 s * 48000
}

TEST_CASE ("PlaybackEngine: a mid-song tempo change moves later events", "[playback][engine]")
{
    SongDocument doc;
    addTempo (doc, 0, 120.0);
    addTempo (doc, 480, 60.0);
    addNote (addTrack (doc), 60, 960, 240);      // 1.5 s
    Rig rig (doc);
    rig.transport.play (rig.snapshot->endSeconds());
    rig.render (200);

    long onFrame = -1;
    for (const auto& r : rig.sink.records)
        if (r.event.kind == PlaybackEventKind::NoteOn) onFrame = r.frame;
    CHECK (onFrame == 72000);   // 1.5 s * 48000
}

TEST_CASE ("PlaybackEngine: playback auto-stops at the end of the song", "[playback][engine]")
{
    SongDocument doc;
    addNote (addTrack (doc), 60, 0, 480);        // ends at 0.5 s
    Rig rig (doc);
    rig.transport.play (rig.snapshot->endSeconds());
    rig.render (80);
    CHECK (! rig.transport.isPlaying());
    CHECK (rig.transport.getPositionSeconds() == rig.snapshot->endSeconds());
    CHECK (rig.sink.count (PlaybackEventKind::NoteOff) == 1);
}

TEST_CASE ("PlaybackEngine: pausing releases voices and silences event delivery", "[playback][engine]")
{
    SongDocument doc;
    addNote (addTrack (doc), 60, 0, 4800);
    Rig rig (doc);
    rig.transport.play (rig.snapshot->endSeconds());
    rig.render (2);
    const int before = rig.sink.releaseAllCount;
    rig.transport.pause();
    rig.render (1);
    CHECK (rig.sink.releaseAllCount == before + 1);
    const auto n = rig.sink.records.size();
    rig.render (5);
    CHECK (rig.sink.records.size() == n);
}

TEST_CASE ("PlaybackEngine: Stop returns the playhead to the play-start and releases", "[playback][engine]")
{
    SongDocument doc;
    addNote (addTrack (doc), 60, 0, 4800);
    Rig rig (doc);
    rig.transport.play (rig.snapshot->endSeconds());
    rig.render (10);
    rig.transport.stop();
    rig.render (1);
    CHECK (rig.transport.getPositionSeconds() == 0.0);
    CHECK (rig.sink.releaseAllCount >= 1);
}

TEST_CASE ("PlaybackEngine: an empty song never plays", "[playback][engine]")
{
    SongDocument doc;
    Rig rig (doc);
    rig.transport.play (rig.snapshot->endSeconds());
    rig.render (4);
    CHECK (! rig.transport.isPlaying());
    CHECK (rig.sink.records.empty());
}

TEST_CASE ("PlaybackEngine: with no snapshot published it renders silence and does not crash", "[playback][engine]")
{
    Transport transport;
    RecordingSink sink;
    PlaybackEngine engine (transport, sink);
    juce::AudioBuffer<float> buffer (2, 256);
    engine.prepare (sr, 256);
    transport.play (10.0);
    engine.renderBlock (buffer, 0, 256);
    CHECK (buffer.getMagnitude (0, 256) == 0.0f);
}
```

- [ ] **Step 3: Run to verify they fail** — add `PlaybackEngine_tests.cpp`, `PlaybackEngine.cpp` to `forge_tests`; build → FAIL.

- [ ] **Step 4: Implement** — `EventSink.h`: as specified in **Interfaces** (with `#pragma once`, `#include "UI/Playback/PlaybackSnapshot.h"`). `PlaybackEngine.h`:

```cpp
#pragma once

#include "UI/Playback/EventSink.h"
#include "UI/Playback/HandOff.h"
#include "UI/Playback/PlaybackSnapshot.h"
#include "UI/Playback/Transport.h"

#include <juce_audio_basics/juce_audio_basics.h>

#include <memory>

namespace lotro
{

// Device-free playback core. renderBlock() is what the audio callback (and the
// tests) call. It never allocates, locks, frees or throws.
class PlaybackEngine
{
public:
    PlaybackEngine (Transport& transportIn, EventSink& sinkIn) : transport (transportIn), sink (sinkIn) {}

    void publishSnapshot (std::shared_ptr<PlaybackSnapshot> snapshot) { snapshots.publish (std::move (snapshot)); }
    void collectGarbage() { snapshots.collectRetired(); }

    void prepare (double sampleRateIn, int maxBlockSize);
    void renderBlock (juce::AudioBuffer<float>& buffer, int startSample, int numSamples) noexcept;

private:
    void chase (const PlaybackSnapshot& snapshot, double positionSeconds) noexcept;
    void releaseNewlyMutedTracks (const PlaybackSnapshot& snapshot) noexcept;

    Transport& transport;
    EventSink& sink;
    HandOff<PlaybackSnapshot> snapshots;
    double sampleRate = 44100.0;
    size_t nextEvent = 0;
    bool wasPlaying = false;
    unsigned seenSeekGeneration = 0;
};

} // namespace lotro
```

`PlaybackEngine.cpp` (chase/seek/swap/mute are implemented here too but only tested in Task 8; implement them now so the file is written once, but **do not claim them done until Task 8's tests pass**):

```cpp
#include "UI/Playback/PlaybackEngine.h"

#include <cmath>

namespace lotro
{

void PlaybackEngine::prepare (double sampleRateIn, int maxBlockSize)
{
    sampleRate = sampleRateIn > 0.0 ? sampleRateIn : 44100.0;
    sink.prepare (sampleRate, maxBlockSize);
}

void PlaybackEngine::chase (const PlaybackSnapshot& snapshot, double positionSeconds) noexcept
{
    for (const auto& e : snapshot.events())
    {
        if (e.seconds >= positionSeconds)
            break;
        if (e.kind == PlaybackEventKind::Program || e.kind == PlaybackEventKind::Control
            || e.kind == PlaybackEventKind::PitchBend)
            sink.handle (e);
    }
}

void PlaybackEngine::releaseNewlyMutedTracks (const PlaybackSnapshot& snapshot) noexcept
{
    for (int t = 0; t < snapshot.numTracks(); ++t)
    {
        const bool audible = snapshot.isAudible (t);
        if (! audible && snapshot.appliedAudible (t))
            for (int vch : snapshot.channelsOfTrack (t))
                sink.releaseChannel (vch);
        snapshot.setAppliedAudible (t, audible);
    }
}

void PlaybackEngine::renderBlock (juce::AudioBuffer<float>& buffer, int startSample, int numSamples) noexcept
{
    buffer.clear (startSample, numSamples);
    if (buffer.getNumChannels() == 0 || numSamples <= 0)
        return;

    float* left = buffer.getWritePointer (0, startSample);
    float* right = buffer.getNumChannels() > 1 ? buffer.getWritePointer (1, startSample) : left;

    bool swapped = false;
    PlaybackSnapshot* snapshot = snapshots.acquire (swapped);
    const bool sinkReplaced = sink.beginBlock();
    swapped = swapped || sinkReplaced;

    const bool playing = snapshot != nullptr && transport.isPlaying();
    const unsigned seekGeneration = transport.getSeekGeneration();
    const bool seeked = seekGeneration != seenSeekGeneration;
    seenSeekGeneration = seekGeneration;

    if (swapped || seeked || (! playing && wasPlaying))
        sink.releaseAll();
    if (snapshot != nullptr)
        releaseNewlyMutedTracks (*snapshot);

    if (! playing)
    {
        wasPlaying = false;
        sink.render (left, right, numSamples);   // lets release tails ring out
        return;
    }

    const double position = transport.getPositionSeconds();
    if (swapped || seeked || ! wasPlaying)
    {
        chase (*snapshot, position);
        nextEvent = snapshot->firstEventAtOrAfter (position);
    }
    wasPlaying = true;

    const double blockSeconds = (double) numSamples / sampleRate;
    const auto& events = snapshot->events();
    int rendered = 0;
    while (nextEvent < events.size() && events[nextEvent].seconds < position + blockSeconds)
    {
        const auto& e = events[nextEvent];
        const int offset = juce::jlimit (rendered, numSamples, (int) std::llround ((e.seconds - position) * sampleRate));
        if (offset > rendered)
        {
            sink.render (left + rendered, right + rendered, offset - rendered);
            rendered = offset;
        }
        if (e.kind != PlaybackEventKind::NoteOn || snapshot->isAudible (e.trackIndex))
            sink.handle (e);
        ++nextEvent;
    }
    if (rendered < numSamples)
        sink.render (left + rendered, right + rendered, numSamples - rendered);

    transport.advance (blockSeconds, snapshot->endSeconds());
}

} // namespace lotro
```

Add `PlaybackEngine.cpp` to `forge_ui` sources.

- [ ] **Step 5: Run to verify it passes** — `ctest --test-dir build -R "PlaybackEngine" --output-on-failure` → PASS (7).

- [ ] **Step 6: Commit** — `feat(playback): add EventSink and the device-free PlaybackEngine` (full trailer).

---

### Task 8: Engine chase, seek, snapshot swap and mute release

**Files:**
- Modify: `Tests/PlaybackEngine_tests.cpp`, `Source/UI/Playback/PlaybackEngine.cpp` (only if a test exposes a defect)

**Interfaces:** Consumes Task 7's engine and `RecordingSink`. Produces no new API; this task pins the behaviours from the spec's *Chase* and *Data flow* sections.

- [ ] **Step 1: Append the failing/pinning tests** to `Tests/PlaybackEngine_tests.cpp`:

```cpp
TEST_CASE ("PlaybackEngine: starting mid-song replays the program and controllers that came before", "[playback][engine][chase]")
{
    SongDocument doc;
    auto t = addTrack (doc, "T", 1);
    addEvent (t, 0, { 0xC0, 24 });           // program 24 at tick 0
    addEvent (t, 100, { 0xB0, 7, 50 });      // volume 50
    addEvent (t, 200, { 0xE0, 0x00, 0x60 }); // bend
    addNote (t, 60, 4800, 480);              // note at 5 s
    Rig rig (doc);
    rig.transport.seek (2.0);                // play from 2 s: all three controllers are in the past
    rig.transport.play (rig.snapshot->endSeconds());
    rig.render (1);

    CHECK (rig.sink.count (PlaybackEventKind::Program) >= 2);   // setup + the real program-24 change
    bool sawProgram24 = false, sawVolume = false, sawBend = false;
    for (const auto& r : rig.sink.records)
    {
        sawProgram24 |= r.event.kind == PlaybackEventKind::Program && r.event.data1 == 24;
        sawVolume |= r.event.kind == PlaybackEventKind::Control && r.event.data1 == 7 && r.event.data2 == 50;
        sawBend |= r.event.kind == PlaybackEventKind::PitchBend && r.event.data1 == 12288;
    }
    CHECK (sawProgram24);
    CHECK (sawVolume);
    CHECK (sawBend);
    CHECK (rig.sink.count (PlaybackEventKind::NoteOn) == 0);   // chase never retriggers notes
}

TEST_CASE ("PlaybackEngine: notes already sounding at the playhead are not retriggered", "[playback][engine][chase]")
{
    SongDocument doc;
    addNote (addTrack (doc), 60, 0, 9600);   // 0..10 s
    Rig rig (doc);
    rig.transport.seek (3.0);
    rig.transport.play (rig.snapshot->endSeconds());
    rig.render (2);
    CHECK (rig.sink.count (PlaybackEventKind::NoteOn) == 0);
}

TEST_CASE ("PlaybackEngine: a seek while playing releases voices and re-chases", "[playback][engine][chase]")
{
    SongDocument doc;
    auto t = addTrack (doc);
    addEvent (t, 0, { 0xC0, 9 });
    addNote (t, 60, 0, 9600);
    Rig rig (doc);
    rig.transport.play (rig.snapshot->endSeconds());
    rig.render (2);
    const int releases = rig.sink.releaseAllCount;
    rig.sink.records.clear();
    rig.transport.seek (4.0);
    rig.render (1);
    CHECK (rig.sink.releaseAllCount == releases + 1);
    bool reappliedProgram = false;
    for (const auto& r : rig.sink.records)
        reappliedProgram |= r.event.kind == PlaybackEventKind::Program && r.event.data1 == 9;
    CHECK (reappliedProgram);
}

TEST_CASE ("PlaybackEngine: a snapshot swap mid-play releases voices, re-chases and keeps the playhead", "[playback][engine][swap]")
{
    SongDocument doc;
    auto t = addTrack (doc);
    addEvent (t, 0, { 0xC0, 9 });
    addNote (t, 60, 0, 9600);
    Rig rig (doc);
    rig.transport.play (rig.snapshot->endSeconds());
    rig.render (20);
    const double before = rig.transport.getPositionSeconds();
    const int releases = rig.sink.releaseAllCount;
    rig.sink.records.clear();

    addNote (t, 64, 9600, 480);                      // an edit
    rig.engine.publishSnapshot (buildSnapshot (doc));
    rig.render (1);

    CHECK (rig.sink.releaseAllCount == releases + 1);
    CHECK (rig.transport.getPositionSeconds() > before);   // kept going, not reset
    bool reappliedProgram = false;
    for (const auto& r : rig.sink.records)
        reappliedProgram |= r.event.kind == PlaybackEventKind::Program && r.event.data1 == 9;
    CHECK (reappliedProgram);
}

TEST_CASE ("PlaybackEngine: a replaced sink (new SoundFont) triggers the same release and re-chase", "[playback][engine][swap]")
{
    SongDocument doc;
    auto t = addTrack (doc);
    addEvent (t, 0, { 0xC0, 9 });
    addNote (t, 60, 0, 9600);
    Rig rig (doc);
    rig.transport.play (rig.snapshot->endSeconds());
    rig.render (2);
    rig.sink.records.clear();
    const int releases = rig.sink.releaseAllCount;
    rig.sink.replaced = true;
    rig.render (1);
    CHECK (rig.sink.releaseAllCount == releases + 1);
    CHECK (rig.sink.count (PlaybackEventKind::Program) >= 1);
}

TEST_CASE ("PlaybackEngine: muted tracks send no NoteOn but still send NoteOff", "[playback][engine][mute]")
{
    SongDocument doc;
    auto t = addTrack (doc);
    addNote (t, 60, 480, 480);
    Rig rig (doc);
    rig.snapshot->setAudible (1, false);
    rig.transport.play (rig.snapshot->endSeconds());
    rig.render (80);
    CHECK (rig.sink.count (PlaybackEventKind::NoteOn) == 0);
    CHECK (rig.sink.count (PlaybackEventKind::NoteOff) == 1);
}

TEST_CASE ("PlaybackEngine: muting a track mid-play releases exactly that track's channels", "[playback][engine][mute]")
{
    SongDocument doc;
    auto a = addTrack (doc, "A", 1);
    auto b = addTrack (doc, "B", 2);
    addNote (a, 60, 0, 9600, 100, 1);
    addNote (b, 64, 0, 9600, 100, 2);
    Rig rig (doc);
    rig.transport.play (rig.snapshot->endSeconds());
    rig.render (2);
    CHECK (rig.sink.releasedChannels.empty());

    rig.snapshot->setAudible (1, false);   // mute A
    rig.render (1);
    REQUIRE (rig.sink.releasedChannels.size() == 1);
    CHECK (rig.sink.releasedChannels[0] == rig.snapshot->channelsOfTrack (1)[0]);
    rig.render (1);
    CHECK (rig.sink.releasedChannels.size() == 1);   // released once, not every block
}

TEST_CASE ("PlaybackEngine: starting from the end restarts from the beginning", "[playback][engine]")
{
    SongDocument doc;
    addNote (addTrack (doc), 60, 0, 480);
    Rig rig (doc);
    rig.transport.goToEnd (rig.snapshot->endSeconds());
    rig.transport.play (rig.snapshot->endSeconds());
    rig.render (1);
    CHECK (rig.sink.count (PlaybackEventKind::NoteOn) == 1);
}
```

- [ ] **Step 2: Run** — `ctest --test-dir build -R "PlaybackEngine" --output-on-failure`. Expected: all PASS if Task 7's implementation is right; any FAIL is a real defect — fix `PlaybackEngine.cpp` (the likely suspect is the `swapped` ordering or the `seeked` flag consumed before `! playing`), re-run until PASS.

- [ ] **Step 3: Commit** — `test(playback): pin chase, seek, swap and mute behaviour of the engine` (full trailer; include a `fix(playback):` commit instead if the implementation needed changes).

---

### Task 9: `SynthVoice` (TinySoundFont wrapper)

**Files:**
- Create: `Source/UI/Playback/SynthVoice.h/.cpp`, `Tests/SynthVoice_tests.cpp`
- Modify: `CMakeLists.txt`, `Tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `EventSink`, `HandOff`, `PlaybackError`, `kMaxVirtualChannels`.
- Produces:

```cpp
class SynthVoice : public EventSink
{
public:
    SynthVoice();
    ~SynthVoice() override;
    void loadSoundFont (const juce::File& file);                 // message thread; throws PlaybackError
    void loadSoundFontFromMemory (const void* data, size_t size);// message thread; throws PlaybackError(SoundFontInvalid)
    bool hasSoundFont() const noexcept;
    void collectGarbage();                                       // message thread
    // EventSink: prepare / beginBlock / handle / releaseChannel / releaseAll / render
};
```
Constants: `kMaxVoices = 192`.

- [ ] **Step 1: Write the failing tests** — `Tests/SynthVoice_tests.cpp`:

```cpp
#include "UI/Playback/PlaybackError.h"
#include "UI/Playback/SynthVoice.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <vector>

using namespace lotro;

namespace
{
    juce::File localSoundFont()
    {
        return juce::File (__FILE__).getParentDirectory().getParentDirectory()
                   .getChildFile ("resources/soundfonts/TimGM6mb.sf2");
    }

    PlaybackEvent ev (PlaybackEventKind k, int vch, int d1, int d2)
    {
        PlaybackEvent e;
        e.kind = k; e.virtualChannel = vch; e.data1 = d1; e.data2 = d2;
        return e;
    }
}

TEST_CASE ("SynthVoice: a missing SoundFont file throws SoundFontMissing", "[playback][synth]")
{
    SynthVoice synth;
    try
    {
        synth.loadSoundFont (juce::File ("/nonexistent/none.sf2"));
        FAIL ("expected PlaybackError");
    }
    catch (const PlaybackError& e)
    {
        CHECK (e.kind() == PlaybackErrorKind::SoundFontMissing);
    }
    CHECK (! synth.hasSoundFont());
}

TEST_CASE ("SynthVoice: garbage bytes throw SoundFontInvalid", "[playback][synth]")
{
    SynthVoice synth;
    const char junk[] = "this is not a soundfont";
    try
    {
        synth.loadSoundFontFromMemory (junk, sizeof (junk));
        FAIL ("expected PlaybackError");
    }
    catch (const PlaybackError& e)
    {
        CHECK (e.kind() == PlaybackErrorKind::SoundFontInvalid);
    }
    CHECK (! synth.hasSoundFont());
}

TEST_CASE ("SynthVoice: with no SoundFont, render outputs silence and events are harmless", "[playback][synth]")
{
    SynthVoice synth;
    synth.prepare (48000.0, 256);
    synth.beginBlock();
    synth.handle (ev (PlaybackEventKind::NoteOn, 0, 60, 100));
    std::vector<float> l (256, 1.0f), r (256, 1.0f);
    synth.render (l.data(), r.data(), 256);
    for (float s : l) CHECK (s == 0.0f);
}

TEST_CASE ("SynthVoice: a real SoundFont sounds a note and falls silent after release (smoke)", "[playback][synth]")
{
    const auto font = localSoundFont();
    if (! font.existsAsFile())
    {
        WARN ("skipped: " << font.getFullPathName().toStdString() << " not present (the GPL-2 SoundFont is local-only)");
        return;
    }

    SynthVoice synth;
    synth.prepare (48000.0, 512);
    synth.loadSoundFont (font);
    REQUIRE (synth.hasSoundFont());

    synth.beginBlock();
    synth.handle (ev (PlaybackEventKind::Program, 0, 0, 0));
    synth.handle (ev (PlaybackEventKind::NoteOn, 0, 60, 110));
    std::vector<float> l (512), r (512);
    float peak = 0.0f;
    for (int i = 0; i < 10; ++i)
    {
        synth.render (l.data(), r.data(), 512);
        for (float s : l) peak = std::max (peak, std::fabs (s));
    }
    CHECK (peak > 0.001f);

    synth.releaseAll();
    for (int i = 0; i < 400; ++i)           // let the release tail finish
        synth.render (l.data(), r.data(), 512);
    float tail = 0.0f;
    for (float s : l) tail = std::max (tail, std::fabs (s));
    CHECK (tail < 0.001f);
}

TEST_CASE ("SynthVoice: loading a SoundFont mid-stream makes beginBlock report the replacement", "[playback][synth]")
{
    const auto font = localSoundFont();
    if (! font.existsAsFile()) { WARN ("skipped: local SoundFont not present"); return; }

    SynthVoice synth;
    synth.prepare (48000.0, 512);
    CHECK (! synth.beginBlock());
    synth.loadSoundFont (font);
    CHECK (synth.beginBlock());
    CHECK (! synth.beginBlock());
}
```

- [ ] **Step 2: Run to verify it fails** — add test + `SynthVoice.cpp` to `forge_tests`; build → FAIL (missing header).

- [ ] **Step 3: Implement** — `SynthVoice.h`:

```cpp
#pragma once

#include "UI/Playback/EventSink.h"
#include "UI/Playback/HandOff.h"

#include <juce_core/juce_core.h>

#include <vector>

struct tsf;   // TinySoundFont

namespace lotro
{

// EventSink backed by TinySoundFont. SoundFont instances are built and fully
// initialised on the message thread (voice limit set, all virtual channels
// allocated) and handed to the audio thread through a HandOff, so the audio
// thread never allocates or frees.
class SynthVoice : public EventSink
{
public:
    static constexpr int kMaxVoices = 192;

    SynthVoice();
    ~SynthVoice() override;

    void loadSoundFont (const juce::File& file);
    void loadSoundFontFromMemory (const void* data, size_t size);
    bool hasSoundFont() const noexcept { return fontLoaded; }
    void collectGarbage() { instances.collectRetired(); }

    void prepare (double sampleRate, int maxBlockSize) override;
    bool beginBlock() noexcept override;
    void handle (const PlaybackEvent& event) noexcept override;
    void releaseChannel (int virtualChannel) noexcept override;
    void releaseAll() noexcept override;
    void render (float* left, float* right, int numFrames) noexcept override;

private:
    struct Instance
    {
        tsf* synth = nullptr;
        ~Instance();
    };

    std::shared_ptr<Instance> build() const;   // throws PlaybackError

    HandOff<Instance> instances;
    Instance* current = nullptr;                // audio thread only
    juce::MemoryBlock fontBytes;                // message thread; kept to rebuild on a sample-rate change
    std::vector<float> scratch;                 // interleaved stereo, sized in prepare()
    double sampleRate = 44100.0;
    int maxBlock = 512;
    bool fontLoaded = false;
};

} // namespace lotro
```

`SynthVoice.cpp`:

```cpp
#include "UI/Playback/SynthVoice.h"

#include "UI/Playback/PlaybackError.h"
#include "UI/Playback/PlaybackSnapshot.h"

#define TSF_IMPLEMENTATION   // the one translation unit that compiles TinySoundFont
#include <tsf.h>

#include <algorithm>

namespace lotro
{

SynthVoice::Instance::~Instance()
{
    if (synth != nullptr)
        tsf_close (synth);
}

SynthVoice::SynthVoice() = default;
SynthVoice::~SynthVoice() = default;

std::shared_ptr<SynthVoice::Instance> SynthVoice::build() const
{
    auto instance = std::make_shared<Instance>();
    instance->synth = tsf_load_memory (fontBytes.getData(), (int) fontBytes.getSize());
    if (instance->synth == nullptr)
        throw PlaybackError (PlaybackErrorKind::SoundFontInvalid, "That file is not a usable SoundFont (.sf2).");

    tsf_set_output (instance->synth, TSF_STEREO_INTERLEAVED, (int) sampleRate, 0.0f);
    tsf_set_max_voices (instance->synth, kMaxVoices);
    // Touching the last channel allocates every channel up front, so the audio
    // thread never grows the channel array.
    tsf_channel_set_volume (instance->synth, kMaxVirtualChannels - 1, 1.0f);
    return instance;
}

void SynthVoice::loadSoundFontFromMemory (const void* data, size_t size)
{
    const auto previousBytes = fontBytes;
    fontBytes = juce::MemoryBlock (data, size);
    try
    {
        instances.publish (build());
        fontLoaded = true;
    }
    catch (...)
    {
        fontBytes = previousBytes;   // keep whatever was working before
        throw;
    }
}

void SynthVoice::loadSoundFont (const juce::File& file)
{
    if (! file.existsAsFile())
        throw PlaybackError (PlaybackErrorKind::SoundFontMissing,
                             "The SoundFont file was not found: " + file.getFullPathName().toStdString());
    juce::MemoryBlock bytes;
    if (! file.loadFileAsData (bytes))
        throw PlaybackError (PlaybackErrorKind::SoundFontMissing,
                             "The SoundFont file could not be read: " + file.getFullPathName().toStdString());
    loadSoundFontFromMemory (bytes.getData(), bytes.getSize());
}

void SynthVoice::prepare (double sampleRateIn, int maxBlockSize)
{
    const bool rateChanged = sampleRateIn != sampleRate;
    sampleRate = sampleRateIn > 0.0 ? sampleRateIn : 44100.0;
    maxBlock = std::max (1, maxBlockSize);
    scratch.assign ((size_t) maxBlock * 2, 0.0f);
    if (rateChanged && fontLoaded)
        instances.publish (build());
}

bool SynthVoice::beginBlock() noexcept
{
    bool swapped = false;
    current = instances.acquire (swapped);
    return swapped;
}

void SynthVoice::handle (const PlaybackEvent& e) noexcept
{
    if (current == nullptr || current->synth == nullptr
        || e.virtualChannel < 0 || e.virtualChannel >= kMaxVirtualChannels)
        return;

    tsf* f = current->synth;
    const int ch = e.virtualChannel;
    switch (e.kind)
    {
        case PlaybackEventKind::NoteOn:    tsf_channel_note_on (f, ch, e.data1, (float) e.data2 / 127.0f); break;
        case PlaybackEventKind::NoteOff:   tsf_channel_note_off (f, ch, e.data1); break;
        case PlaybackEventKind::Program:
            if (e.data2 != 0) tsf_channel_set_bank_preset (f, ch, 128, e.data1);   // drum kit
            else              tsf_channel_set_presetnumber (f, ch, e.data1, 0);
            break;
        case PlaybackEventKind::Control:   tsf_channel_midi_control (f, ch, e.data1, e.data2); break;
        case PlaybackEventKind::PitchBend: tsf_channel_set_pitchwheel (f, ch, e.data1); break;
    }
}

void SynthVoice::releaseChannel (int virtualChannel) noexcept
{
    if (current != nullptr && current->synth != nullptr && virtualChannel >= 0 && virtualChannel < kMaxVirtualChannels)
        tsf_channel_note_off_all (current->synth, virtualChannel);
}

void SynthVoice::releaseAll() noexcept
{
    if (current != nullptr && current->synth != nullptr)
        tsf_note_off_all (current->synth);
}

void SynthVoice::render (float* left, float* right, int numFrames) noexcept
{
    if (current == nullptr || current->synth == nullptr)
    {
        std::fill (left, left + numFrames, 0.0f);
        if (right != left)
            std::fill (right, right + numFrames, 0.0f);
        return;
    }

    int done = 0;
    while (done < numFrames)
    {
        const int n = std::min (maxBlock, numFrames - done);
        tsf_render_float (current->synth, scratch.data(), n, 0);
        for (int i = 0; i < n; ++i)
        {
            left[done + i] = scratch[(size_t) i * 2];
            if (right != left)
                right[done + i] = scratch[(size_t) i * 2 + 1];
        }
        done += n;
    }
}

} // namespace lotro
```

If `-Wall -Wextra` warnings from `tsf.h` appear even with the SYSTEM include dir, fix by moving the `#include <tsf.h>` between `#pragma GCC diagnostic push/ignored/pop` guards (guarded by `#if defined(__GNUC__) || defined(__clang__)`), do not edit `tsf.h`.

Add `SynthVoice.cpp` to `forge_ui` sources.

- [ ] **Step 4: Run to verify it passes** — `cmake --build build --target forge_tests && ctest --test-dir build -R "SynthVoice" --output-on-failure` → PASS (5; the smoke tests run for real because the SoundFont was placed in Task 1; delete `resources/soundfonts/TimGM6mb.sf2` temporarily to confirm they WARN and pass, then restore it).

- [ ] **Step 5: Commit** — `feat(playback): add the TinySoundFont-backed SynthVoice` (full trailer).

---

### Task 10: `PlaybackController` (message-thread orchestration)

**Files:**
- Create: `Source/UI/Playback/PlaybackController.h/.cpp`, `Tests/PlaybackController_tests.cpp`
- Modify: `CMakeLists.txt`, `Tests/CMakeLists.txt`

**Interfaces:**
- Consumes: everything above; `SongDocument` (`getTree()`, `getSourceMidiNode()`, `getTempoMapNode()`, `getMeterMapNode()`), `SongIDs::{name,colorArgb,numerator,denominator}`.
- Produces:

```cpp
class PlaybackController : private juce::ValueTree::Listener, private juce::AsyncUpdater, private juce::Timer
{
public:
    class Listener
    {
    public:
        virtual ~Listener() = default;
        virtual void playbackPositionChanged() {}
        virtual void playbackStateChanged() {}
        virtual void muteSoloChanged() {}
    };
    PlaybackController (SongDocument&, EventSink&);
    ~PlaybackController() override;
    PlaybackEngine& engine() noexcept;
    void addListener (Listener*); void removeListener (Listener*);
    std::function<bool()> onBeforePlay;          // return false to veto Play (e.g. no SoundFont / device)
    // transport
    void play(); void pause(); void stop(); void togglePlayPause();
    void goToStart(); void goToEnd(); void rewindOneBar();
    void seekToTick (double tick);
    bool isPlaying() const; double getPositionSeconds() const; double getPositionTicks() const;
    double ticksForSeconds... (not needed)
    // mute / solo
    void setMuted (juce::int64, bool); void setSoloed (juce::int64, bool);
    bool isMuted (juce::int64) const; bool isSoloed (juce::int64) const; bool isSilencedBySolo (juce::int64) const;
    // document lifecycle
    void documentReplaced();        // New/Open/Close: stop, playhead 0, clear mute/solo, rebuild now
    void flushRebuild();            // run any pending rebuild immediately (documentReplaced + tests)
    std::shared_ptr<PlaybackSnapshot> currentSnapshot() const;
};
```
Timer: 30 Hz while constructed; notifies `playbackPositionChanged()` when the position or playing flag changed since last tick; calls `engine.collectGarbage()`.

- [ ] **Step 1: Write the failing tests** — `Tests/PlaybackController_tests.cpp`:

```cpp
#include "PlaybackTestSupport.h"
#include "UI/Playback/PlaybackController.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_events/juce_events.h>

using namespace lotro;
using namespace lotro::playbacktest;
using Catch::Approx;

namespace
{
    struct Rig
    {
        juce::ScopedJuceInitialiser_GUI juceInit;
        SongDocument doc;
        RecordingSink sink;
        PlaybackController controller { doc, sink };
        juce::AudioBuffer<float> buffer { 2, 512 };

        Rig() { controller.engine().prepare (48000.0, 512); }
        void render (int blocks = 1) { for (int i = 0; i < blocks; ++i) controller.engine().renderBlock (buffer, 0, 512); }
    };
}

TEST_CASE ("PlaybackController: play on an empty song is a no-op", "[playback][controller]")
{
    Rig r;
    r.controller.flushRebuild();
    r.controller.play();
    CHECK (! r.controller.isPlaying());
}

TEST_CASE ("PlaybackController: notes added to the document are heard after a rebuild", "[playback][controller]")
{
    Rig r;
    addNote (addTrack (r.doc), 60, 0, 480);
    r.controller.flushRebuild();
    r.controller.play();
    r.render();
    CHECK (r.sink.count (PlaybackEventKind::NoteOn) == 1);
}

TEST_CASE ("PlaybackController: many tree changes coalesce into one rebuild", "[playback][controller]")
{
    Rig r;
    auto t = addTrack (r.doc);
    r.controller.flushRebuild();
    const auto before = r.controller.currentSnapshot();
    for (int i = 0; i < 100; ++i)
        addNote (t, 60 + (i % 12), i * 10, 5);
    CHECK (r.controller.currentSnapshot() == before);   // nothing rebuilt synchronously
    r.controller.flushRebuild();
    CHECK (r.controller.currentSnapshot() != before);
}

TEST_CASE ("PlaybackController: renaming or recolouring a track does not rebuild (held notes keep ringing)", "[playback][controller]")
{
    Rig r;
    auto t = addTrack (r.doc);
    addNote (t, 60, 0, 480);
    r.controller.flushRebuild();
    const auto before = r.controller.currentSnapshot();
    t.setProperty (SongIDs::name, "Renamed", nullptr);
    t.setProperty (SongIDs::colorArgb, 0xff000000, nullptr);
    r.controller.flushRebuild();
    CHECK (r.controller.currentSnapshot() == before);
}

TEST_CASE ("PlaybackController: changes outside the source MIDI (parts/assignments) do not rebuild", "[playback][controller]")
{
    Rig r;
    auto t = addTrack (r.doc);
    addNote (t, 60, 0, 480);
    r.controller.flushRebuild();
    const auto before = r.controller.currentSnapshot();
    r.doc.addPart ("Lute", "Part 1");
    r.controller.flushRebuild();
    CHECK (r.controller.currentSnapshot() == before);
}

TEST_CASE ("PlaybackController: a mid-play edit swaps the snapshot and keeps playing from the same place", "[playback][controller]")
{
    Rig r;
    auto t = addTrack (r.doc);
    addNote (t, 60, 0, 9600);
    r.controller.flushRebuild();
    r.controller.play();
    r.render (20);
    const double before = r.controller.getPositionSeconds();
    addNote (t, 64, 9600, 480);
    r.controller.flushRebuild();
    r.render (1);
    CHECK (r.controller.isPlaying());
    CHECK (r.controller.getPositionSeconds() > before);
}

TEST_CASE ("PlaybackController: mute and solo govern what is audible and survive a rebuild", "[playback][controller]")
{
    Rig r;
    auto a = addTrack (r.doc, "A", 1);
    auto b = addTrack (r.doc, "B", 2);
    const auto idA = (juce::int64) a.getProperty (SongIDs::trackId);
    const auto idB = (juce::int64) b.getProperty (SongIDs::trackId);
    addNote (a, 60, 0, 480, 100, 1);
    addNote (b, 64, 0, 480, 100, 2);
    r.controller.setMuted (idA, true);
    addNote (b, 65, 480, 480, 100, 2);   // forces a rebuild
    r.controller.flushRebuild();
    r.controller.play();
    r.render (60);

    int onFromA = 0, onFromB = 0;
    const auto snap = r.controller.currentSnapshot();
    for (const auto& rec : r.sink.records)
        if (rec.event.kind == PlaybackEventKind::NoteOn)
            (snap->trackIds()[(size_t) rec.event.trackIndex] == idA ? onFromA : onFromB)++;
    CHECK (onFromA == 0);
    CHECK (onFromB == 2);
    CHECK (r.controller.isSilencedBySolo (idB) == false);
}

TEST_CASE ("PlaybackController: mute state is kept for a track that is removed and restored by undo", "[playback][controller]")
{
    Rig r;
    auto t = addTrack (r.doc);
    const auto id = (juce::int64) t.getProperty (SongIDs::trackId);
    r.controller.setMuted (id, true);
    r.doc.removeTrack (id);
    r.controller.flushRebuild();
    r.doc.undo();
    r.controller.flushRebuild();
    CHECK (r.controller.isMuted (id));
}

TEST_CASE ("PlaybackController: documentReplaced stops playback, zeroes the playhead and clears mute/solo", "[playback][controller]")
{
    Rig r;
    auto t = addTrack (r.doc);
    addNote (t, 60, 0, 9600);
    r.controller.flushRebuild();
    r.controller.setSoloed ((juce::int64) t.getProperty (SongIDs::trackId), true);
    r.controller.play();
    r.render (10);

    r.doc.resetToEmpty();
    r.controller.documentReplaced();
    CHECK (! r.controller.isPlaying());
    CHECK (r.controller.getPositionSeconds() == 0.0);
    CHECK (! r.controller.isSoloed ((juce::int64) t.getProperty (SongIDs::trackId)));
    r.render (2);
    CHECK (r.sink.releaseAllCount >= 1);
}

TEST_CASE ("PlaybackController: seekToTick converts through the tempo map", "[playback][controller]")
{
    Rig r;
    addTempo (r.doc, 0, 60.0);            // 1 s per quarter note
    addNote (addTrack (r.doc), 60, 0, 4800);
    r.controller.flushRebuild();
    r.controller.seekToTick (960.0);      // two quarter notes at 480 PPQ
    CHECK (r.controller.getPositionSeconds() == Approx (2.0));
    CHECK (r.controller.getPositionTicks() == Approx (960.0));
}

TEST_CASE ("PlaybackController: rewindOneBar steps back to the previous bar line", "[playback][controller]")
{
    Rig r;
    addNote (addTrack (r.doc), 60, 0, 19200);
    r.controller.flushRebuild();
    r.controller.seekToTick (2500.0);     // 4/4 at 480 PPQ = 1920 ticks per bar
    r.controller.rewindOneBar();
    CHECK (r.controller.getPositionTicks() == Approx (1920.0));
}

TEST_CASE ("PlaybackController: onBeforePlay can veto Play", "[playback][controller]")
{
    Rig r;
    addNote (addTrack (r.doc), 60, 0, 480);
    r.controller.flushRebuild();
    r.controller.onBeforePlay = [] { return false; };
    r.controller.play();
    CHECK (! r.controller.isPlaying());
}

TEST_CASE ("PlaybackController: listeners hear mute/solo changes and state changes", "[playback][controller]")
{
    Rig r;
    struct L : PlaybackController::Listener
    {
        int mute = 0, state = 0;
        void muteSoloChanged() override { ++mute; }
        void playbackStateChanged() override { ++state; }
    } listener;
    r.controller.addListener (&listener);
    addNote (addTrack (r.doc), 60, 0, 480);
    r.controller.flushRebuild();
    r.controller.setMuted (1, true);
    r.controller.play();
    CHECK (listener.mute == 1);
    CHECK (listener.state >= 1);
    r.controller.removeListener (&listener);
}
```

- [ ] **Step 2: Run to verify it fails** — add test + `PlaybackController.cpp`; build → FAIL.

- [ ] **Step 3: Implement** — `PlaybackController.h`:

```cpp
#pragma once

#include "UI/Playback/EventSink.h"
#include "UI/Playback/MuteSoloState.h"
#include "UI/Playback/PlaybackEngine.h"
#include "UI/Playback/PlaybackSnapshot.h"
#include "UI/Playback/Transport.h"
#include "UI/SongDocument.h"

#include <juce_events/juce_events.h>

#include <functional>
#include <memory>

namespace lotro
{

// Message-thread owner of playback: the Transport, mute/solo, the engine, and
// the snapshot lifecycle. Every UI component talks to this, never to the audio
// side directly.
class PlaybackController : private juce::ValueTree::Listener,
                           private juce::AsyncUpdater,
                           private juce::Timer
{
public:
    class Listener
    {
    public:
        virtual ~Listener() = default;
        virtual void playbackPositionChanged() {}
        virtual void playbackStateChanged() {}
        virtual void muteSoloChanged() {}
    };

    PlaybackController (SongDocument& document, EventSink& sink);
    ~PlaybackController() override;

    PlaybackEngine& engine() noexcept { return playbackEngine; }

    void addListener (Listener* l) { listeners.add (l); }
    void removeListener (Listener* l) { listeners.remove (l); }

    // Return false to veto Play (MainWindow shows why).
    std::function<bool()> onBeforePlay;

    void play();
    void pause();
    void stop();
    void togglePlayPause() { if (isPlaying()) pause(); else play(); }
    void goToStart();
    void goToEnd();
    void rewindOneBar();
    void seekToTick (double tick);

    bool isPlaying() const { return transport.isPlaying(); }
    double getPositionSeconds() const { return transport.getPositionSeconds(); }
    double getPositionTicks() const;

    void setMuted (juce::int64 trackId, bool muted);
    void setSoloed (juce::int64 trackId, bool soloed);
    bool isMuted (juce::int64 trackId) const { return muteSolo.isMuted (trackId); }
    bool isSoloed (juce::int64 trackId) const { return muteSolo.isSoloed (trackId); }
    bool isSilencedBySolo (juce::int64 trackId) const { return muteSolo.isSilencedBySolo (trackId); }

    void documentReplaced();
    void flushRebuild();
    std::shared_ptr<PlaybackSnapshot> currentSnapshot() const { return snapshot; }

private:
    bool isPlaybackRelevant (const juce::ValueTree& tree) const;
    void rebuild();
    void applyMuteSolo();
    void handleAsyncUpdate() override;
    void timerCallback() override;

    void valueTreePropertyChanged (juce::ValueTree& tree, const juce::Identifier& property) override;
    void valueTreeChildAdded (juce::ValueTree& parent, juce::ValueTree&) override { if (isPlaybackRelevant (parent)) triggerAsyncUpdate(); }
    void valueTreeChildRemoved (juce::ValueTree& parent, juce::ValueTree&, int) override { if (isPlaybackRelevant (parent)) triggerAsyncUpdate(); }
    void valueTreeChildOrderChanged (juce::ValueTree& parent, int, int) override { if (isPlaybackRelevant (parent)) triggerAsyncUpdate(); }
    void valueTreeParentChanged (juce::ValueTree&) override {}

    SongDocument& doc;
    // Persistent handles: ValueTree::addListener registers on the HANDLE, so
    // these must outlive the registration (see juce-valuetree-conventions).
    juce::ValueTree rootNode;
    juce::ValueTree sourceMidiNode;
    juce::ValueTree tempoMapNode;

    Transport transport;
    MuteSoloState muteSolo;
    PlaybackEngine playbackEngine;
    std::shared_ptr<PlaybackSnapshot> snapshot;
    juce::ListenerList<Listener> listeners;

    double lastReportedPosition = -1.0;
    bool lastReportedPlaying = false;
};

} // namespace lotro
```

`PlaybackController.cpp`:

```cpp
#include "UI/Playback/PlaybackController.h"

namespace lotro
{

PlaybackController::PlaybackController (SongDocument& document, EventSink& sink)
    : doc (document),
      rootNode (document.getTree()),
      sourceMidiNode (document.getSourceMidiNode()),
      tempoMapNode (document.getTempoMapNode()),
      playbackEngine (transport, sink)
{
    rootNode.addListener (this);
    rebuild();
    startTimerHz (30);
}

PlaybackController::~PlaybackController()
{
    stopTimer();
    cancelPendingUpdate();
    rootNode.removeListener (this);
}

bool PlaybackController::isPlaybackRelevant (const juce::ValueTree& tree) const
{
    return tree == sourceMidiNode || tree.isAChildOf (sourceMidiNode)
        || tree == tempoMapNode || tree.isAChildOf (tempoMapNode);
}

void PlaybackController::valueTreePropertyChanged (juce::ValueTree& tree, const juce::Identifier& property)
{
    // Cosmetic track properties never change what is heard; rebuilding would cut held notes.
    if (property == SongIDs::name || property == SongIDs::colorArgb)
        return;
    if (isPlaybackRelevant (tree))
        triggerAsyncUpdate();
}

void PlaybackController::handleAsyncUpdate() { rebuild(); }

void PlaybackController::flushRebuild()
{
    cancelPendingUpdate();
    rebuild();
}

void PlaybackController::rebuild()
{
    snapshot = buildSnapshot (doc);
    muteSolo.apply (*snapshot);
    playbackEngine.publishSnapshot (snapshot);
}

void PlaybackController::applyMuteSolo()
{
    if (snapshot != nullptr)
        muteSolo.apply (*snapshot);
}

double PlaybackController::getPositionTicks() const
{
    return snapshot != nullptr ? snapshot->tempo().secondsToTicks (transport.getPositionSeconds()) : 0.0;
}

void PlaybackController::play()
{
    if (snapshot == nullptr || ! (snapshot->endSeconds() > 0.0))
        return;
    if (onBeforePlay && ! onBeforePlay())
        return;
    transport.play (snapshot->endSeconds());
    listeners.call ([] (Listener& l) { l.playbackStateChanged(); });
}

void PlaybackController::pause()
{
    transport.pause();
    listeners.call ([] (Listener& l) { l.playbackStateChanged(); });
}

void PlaybackController::stop()
{
    transport.stop();
    listeners.call ([] (Listener& l) { l.playbackStateChanged(); l.playbackPositionChanged(); });
}

void PlaybackController::goToStart()
{
    transport.goToStart();
    listeners.call ([] (Listener& l) { l.playbackPositionChanged(); });
}

void PlaybackController::goToEnd()
{
    if (snapshot != nullptr)
        transport.goToEnd (snapshot->endSeconds());
    listeners.call ([] (Listener& l) { l.playbackPositionChanged(); });
}

void PlaybackController::rewindOneBar()
{
    if (snapshot == nullptr)
        return;
    int numerator = 4, denominator = 4;
    const auto meter = doc.getMeterMapNode().getChild (0);   // first entry only: one-meter-timeline convention
    if (meter.isValid())
    {
        numerator = (int) meter.getProperty (SongIDs::numerator, 4);
        denominator = (int) meter.getProperty (SongIDs::denominator, 4);
    }
    seekToTick (previousBarTick (getPositionTicks(), snapshot->tempo().getTicksPerQuarter(), numerator, denominator));
}

void PlaybackController::seekToTick (double tick)
{
    if (snapshot == nullptr)
        return;
    transport.seek (snapshot->tempo().ticksToSeconds (tick));
    listeners.call ([] (Listener& l) { l.playbackPositionChanged(); });
}

void PlaybackController::setMuted (juce::int64 trackId, bool muted)
{
    muteSolo.setMuted (trackId, muted);
    applyMuteSolo();
    listeners.call ([] (Listener& l) { l.muteSoloChanged(); });
}

void PlaybackController::setSoloed (juce::int64 trackId, bool soloed)
{
    muteSolo.setSoloed (trackId, soloed);
    applyMuteSolo();
    listeners.call ([] (Listener& l) { l.muteSoloChanged(); });
}

void PlaybackController::documentReplaced()
{
    transport.stop();
    transport.goToStart();
    muteSolo.clear();
    flushRebuild();
    listeners.call ([] (Listener& l) { l.playbackStateChanged(); l.playbackPositionChanged(); l.muteSoloChanged(); });
}

void PlaybackController::timerCallback()
{
    playbackEngine.collectGarbage();
    const double position = transport.getPositionSeconds();
    const bool playing = transport.isPlaying();
    if (playing != lastReportedPlaying)
    {
        lastReportedPlaying = playing;
        listeners.call ([] (Listener& l) { l.playbackStateChanged(); });
    }
    if (position != lastReportedPosition)
    {
        lastReportedPosition = position;
        listeners.call ([] (Listener& l) { l.playbackPositionChanged(); });
    }
}

} // namespace lotro
```

Add `PlaybackController.cpp` to `forge_ui` sources. (`SynthVoice::collectGarbage()` is also called from this timer by `MainWindow` in Task 12, since the controller only knows `EventSink`.)

- [ ] **Step 4: Run to verify it passes** — `ctest --test-dir build -R "PlaybackController" --output-on-failure` → PASS (13).

- [ ] **Step 5: Commit** — `feat(playback): add PlaybackController (transport, mute/solo, coalesced snapshot rebuilds)` (full trailer).

---

### Task 11: `TransportStrip` component

**Files:**
- Create: `Source/UI/Playback/TransportStrip.h/.cpp`, `Tests/TransportStrip_tests.cpp`
- Modify: `CMakeLists.txt`, `Tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `PlaybackController` (`goToStart`, `rewindOneBar`, `togglePlayPause`, `stop`, `goToEnd`, `isPlaying`, `Listener`).
- Produces: `class TransportStrip : public juce::Component, private PlaybackController::Listener { explicit TransportStrip (PlaybackController&); ~TransportStrip() override; void resized() override; static constexpr int height = 28; juce::TextButton& playButtonForTesting(); ... }`. Buttons never take keyboard focus (so Space is not consumed as a button click).

- [ ] **Step 1: Write the failing test** — `Tests/TransportStrip_tests.cpp`:

```cpp
#include "PlaybackTestSupport.h"
#include "UI/Playback/PlaybackController.h"
#include "UI/Playback/TransportStrip.h"

#include <catch2/catch_test_macros.hpp>

using namespace lotro;
using namespace lotro::playbacktest;

TEST_CASE ("TransportStrip: buttons never take keyboard focus, so Space cannot re-click them", "[playback][strip]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SongDocument doc;
    RecordingSink sink;
    PlaybackController controller (doc, sink);
    TransportStrip strip (controller);
    strip.setSize (300, TransportStrip::height);
    for (auto* b : strip.buttonsForTesting())
        CHECK (! b->getWantsKeyboardFocus());
}

TEST_CASE ("TransportStrip: the play button toggles playback and its label follows the state", "[playback][strip]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SongDocument doc;
    RecordingSink sink;
    PlaybackController controller (doc, sink);
    addNote (addTrack (doc), 60, 0, 4800);
    controller.flushRebuild();
    TransportStrip strip (controller);

    CHECK (strip.playButtonForTesting().getButtonText() == "Play");
    strip.playButtonForTesting().triggerClick();
    CHECK (controller.isPlaying());
    CHECK (strip.playButtonForTesting().getButtonText() == "Pause");
    strip.playButtonForTesting().triggerClick();
    CHECK (! controller.isPlaying());
    CHECK (strip.playButtonForTesting().getButtonText() == "Play");
}

TEST_CASE ("TransportStrip: stop and go-to buttons drive the controller", "[playback][strip]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SongDocument doc;
    RecordingSink sink;
    PlaybackController controller (doc, sink);
    addNote (addTrack (doc), 60, 0, 4800);
    controller.flushRebuild();
    TransportStrip strip (controller);

    strip.buttonsForTesting()[4]->triggerClick();           // go to end
    CHECK (controller.getPositionSeconds() > 0.0);
    strip.buttonsForTesting()[0]->triggerClick();           // go to start
    CHECK (controller.getPositionSeconds() == 0.0);
}
```

- [ ] **Step 2: Run to verify it fails** — add test + `TransportStrip.cpp`; build → FAIL.

- [ ] **Step 3: Implement** — `TransportStrip.h`:

```cpp
#pragma once

#include "UI/Playback/PlaybackController.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <vector>

namespace lotro
{

// Go to start | Rewind | Play/Pause | Stop | Go to end, bound to one PlaybackController.
class TransportStrip : public juce::Component, private PlaybackController::Listener
{
public:
    static constexpr int height = 28;

    explicit TransportStrip (PlaybackController& controllerIn);
    ~TransportStrip() override;

    void resized() override;
    void paint (juce::Graphics& g) override;

    juce::TextButton& playButtonForTesting() { return playButton; }
    std::vector<juce::TextButton*> buttonsForTesting() { return { &startButton, &rewindButton, &playButton, &stopButton, &endButton }; }

private:
    void playbackStateChanged() override;
    void refreshPlayLabel();

    PlaybackController& controller;
    juce::TextButton startButton { "|<" }, rewindButton { "<<" }, playButton { "Play" },
                     stopButton { "Stop" }, endButton { ">|" };
};

} // namespace lotro
```

`TransportStrip.cpp`:

```cpp
#include "UI/Playback/TransportStrip.h"

#include "UI/SongsmithColours.h"

namespace lotro
{

TransportStrip::TransportStrip (PlaybackController& controllerIn) : controller (controllerIn)
{
    for (auto* b : buttonsForTesting())
    {
        b->setWantsKeyboardFocus (false);
        addAndMakeVisible (*b);
    }
    startButton.setTooltip ("Go to beginning");
    rewindButton.setTooltip ("Rewind one bar");
    playButton.setTooltip ("Play / Pause (Space)");
    stopButton.setTooltip ("Stop");
    endButton.setTooltip ("Go to end");

    startButton.onClick  = [this] { controller.goToStart(); };
    rewindButton.onClick = [this] { controller.rewindOneBar(); };
    playButton.onClick   = [this] { controller.togglePlayPause(); refreshPlayLabel(); };
    stopButton.onClick   = [this] { controller.stop(); refreshPlayLabel(); };
    endButton.onClick    = [this] { controller.goToEnd(); };

    controller.addListener (this);
    refreshPlayLabel();
}

TransportStrip::~TransportStrip()
{
    controller.removeListener (this);
}

void TransportStrip::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (SongsmithColours::background));
}

void TransportStrip::resized()
{
    auto area = getLocalBounds().reduced (4, 3);
    const int w = 56;
    for (auto* b : buttonsForTesting())
    {
        b->setBounds (area.removeFromLeft (w));
        area.removeFromLeft (4);
    }
}

void TransportStrip::playbackStateChanged() { refreshPlayLabel(); }

void TransportStrip::refreshPlayLabel()
{
    playButton.setButtonText (controller.isPlaying() ? "Pause" : "Play");
}

} // namespace lotro
```

Add `TransportStrip.cpp` to `forge_ui` sources.

- [ ] **Step 4: Run to verify it passes** — `ctest --test-dir build -R "TransportStrip" --output-on-failure` → PASS (3).

- [ ] **Step 5: Commit** — `feat(playback): add the TransportStrip component` (full trailer).

---

### Task 12: Audio device, SoundFont setting and main-window wiring

**Files:**
- Create: `Source/UI/Playback/AudioOutput.h/.cpp` (**forge_ui only — not in `Tests/CMakeLists.txt`**)
- Modify: `Source/UI/MainWindow.h`, `Source/UI/MainWindow.cpp`, `Source/UI/SongsmithMainComponent.h/.cpp`, `CMakeLists.txt`

**Interfaces:**
- Consumes: `PlaybackController`, `SynthVoice`, `TransportStrip`, `PlaybackError`.
- Produces: `class AudioOutput { AudioOutput (PlaybackEngine&); ~AudioOutput(); }` (throws `PlaybackError(AudioDeviceUnavailable)` from the constructor). `MainWindow` gains members `SynthVoice synth; PlaybackController playback; std::unique_ptr<AudioOutput> audioOutput;`, a `SoundFont…` command (`SongSoundFont`) in the **Song** menu, Space = Play/Pause, and the transport strip above the Songsmith view. `SongsmithMainComponent (SongDocument&, PlaybackController* = nullptr)` stores the pointer for Tasks 13–16.

This task is build-verified plus manual test (no automated coverage: `MainWindow.cpp`/`UiMain.cpp` are not compiled into `forge_tests`, as with the Song-file wiring).

- [ ] **Step 1: `AudioOutput`** — `AudioOutput.h`:

```cpp
#pragma once

#include "UI/Playback/PlaybackEngine.h"

#include <juce_audio_devices/juce_audio_devices.h>

namespace lotro
{

// Opens the system default output device and pulls PlaybackEngine::renderBlock
// from its callback. Compiled into forge_ui only.
class AudioOutput
{
public:
    explicit AudioOutput (PlaybackEngine& engine);   // throws PlaybackError (AudioDeviceUnavailable)
    ~AudioOutput();

private:
    class EngineSource;
    juce::AudioDeviceManager deviceManager;
    std::unique_ptr<EngineSource> source;
    juce::AudioSourcePlayer player;
};

} // namespace lotro
```

`AudioOutput.cpp`:

```cpp
#include "UI/Playback/AudioOutput.h"

#include "UI/Playback/PlaybackError.h"

namespace lotro
{

class AudioOutput::EngineSource : public juce::AudioSource
{
public:
    explicit EngineSource (PlaybackEngine& e) : engine (e) {}
    void prepareToPlay (int samplesPerBlockExpected, double sampleRate) override { engine.prepare (sampleRate, samplesPerBlockExpected); }
    void releaseResources() override {}
    void getNextAudioBlock (const juce::AudioSourceChannelInfo& info) override
    {
        engine.renderBlock (*info.buffer, info.startSample, info.numSamples);
    }

private:
    PlaybackEngine& engine;
};

AudioOutput::AudioOutput (PlaybackEngine& engine) : source (std::make_unique<EngineSource> (engine))
{
    const auto error = deviceManager.initialiseWithDefaultDevices (0, 2);
    if (error.isNotEmpty() || deviceManager.getCurrentAudioDevice() == nullptr)
        throw PlaybackError (PlaybackErrorKind::AudioDeviceUnavailable,
                             "No audio output device is available" + (error.isNotEmpty() ? ": " + error.toStdString() : std::string()));
    player.setSource (source.get());
    deviceManager.addAudioCallback (&player);
}

AudioOutput::~AudioOutput()
{
    deviceManager.removeAudioCallback (&player);
    player.setSource (nullptr);
}

} // namespace lotro
```

Add `Source/UI/Playback/AudioOutput.cpp` to `forge_ui` `target_sources` **only**; also add `Source/UI/Playback/TransportStrip.cpp`, `PlaybackController.cpp`, etc. if any are still missing from the `forge_ui` list (each earlier task already added its own).

- [ ] **Step 2: `SongsmithMainComponent` accepts the controller.** In `SongsmithMainComponent.h` add `#include "Playback/PlaybackController.h"`, change the constructor to `explicit SongsmithMainComponent (SongDocument& document, PlaybackController* playbackIn = nullptr);`, and add private member `PlaybackController* playback = nullptr;` (declare it **before** `trackList` so it is initialised first). In `SongsmithMainComponent.cpp:83` update the definition and initialiser list: `: doc (document), playback (playbackIn), ...` (keep the existing initialisers in their existing order after it; only add `playback`). Tasks 13–16 use it. Run `cmake --build build --target forge_tests` — existing `SongsmithMainComponent_tests.cpp` still compile (default `nullptr`).

- [ ] **Step 3: MainWindow wiring.** In `MainWindow.h`:
  - includes: `"Playback/AudioOutput.h"`, `"Playback/PlaybackController.h"`, `"Playback/SynthVoice.h"`;
  - add enum value `SongSoundFont` to `CommandId` (before `EditGridSizeBase`);
  - members, declared **after `session` and before `body`** (Body needs the controller at construction): `SynthVoice synth; PlaybackController playback { songDocument, synth };` then, declared **after both** so it is destroyed first (the audio callback must stop before the engine and synth go away): `std::unique_ptr<AudioOutput> audioOutput;`;
  - methods: `void chooseSoundFont(); void loadStartupSoundFont(); bool ensurePlaybackReady();`.

  In `MainWindow.cpp`:
  - `Body` takes the controller: `Body (SongDocument& doc, PlaybackController& playback) : songsmith (doc, &playback), strip (playback)`; members `TransportStrip strip;` added as a child (`addAndMakeVisible (strip)`); `resized()` becomes
    ```cpp
    void resized() override
    {
        auto area = getLocalBounds();
        strip.setBounds (area.removeFromTop (TransportStrip::height));
        songsmith.setBounds (area);
        exportPanel.setBounds (area);
    }
    ```
    and the `MainWindow` constructor passes `playback` where it constructs `Body`.
  - Constructor, after `body` is built and `settings` is created: `playback.onBeforePlay = [this] { return ensurePlaybackReady(); }; loadStartupSoundFont();`.
  - `loadStartupSoundFont()`:
    ```cpp
    void MainWindow::loadStartupSoundFont()
    {
        const juce::File configured (settings->getValue ("soundFontPath"));
        const auto bundled = juce::File::getSpecialLocation (juce::File::currentExecutableFile).getSiblingFile ("TimGM6mb.sf2");
        for (const auto& candidate : { configured, bundled })
        {
            if (candidate == juce::File() || ! candidate.existsAsFile())
                continue;
            try { synth.loadSoundFont (candidate); return; }
            catch (const PlaybackError&) { /* fall through to the next candidate */ }
        }
        // No SoundFont: the app still starts; Play explains (ensurePlaybackReady).
    }
    ```
  - `ensurePlaybackReady()`:
    ```cpp
    bool MainWindow::ensurePlaybackReady()
    {
        if (! synth.hasSoundFont())
        {
            showError ("No SoundFont", "Choose a SoundFont (.sf2) via Song > SoundFont... to enable playback.");
            return false;
        }
        if (audioOutput == nullptr)
        {
            try { audioOutput = std::make_unique<AudioOutput> (playback.engine()); }
            catch (const PlaybackError& e) { showError ("Audio unavailable", e.what()); return false; }
        }
        return true;
    }
    ```
  - `chooseSoundFont()`: `fileChooser = std::make_unique<juce::FileChooser> ("Choose a SoundFont", juce::File(), "*.sf2"); fileChooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [safe = juce::Component::SafePointer<MainWindow> (this)] (const juce::FileChooser& fc) { if (safe == nullptr) return; const auto file = fc.getResult(); if (file == juce::File()) return; try { safe->synth.loadSoundFont (file); safe->settings->setValue ("soundFontPath", file.getFullPathName()); safe->settings->saveIfNeeded(); } catch (const PlaybackError& e) { showError ("SoundFont", e.what()); } });` (a failed load leaves the previous SoundFont active because `SynthVoice::loadSoundFontFromMemory` restores state).
  - Menu: add `menu.addItem (SongSoundFont, "SoundFont...");` to the Song menu in `getMenuForIndex`, and `case SongSoundFont: chooseSoundFont(); break;` in `menuItemSelected`.
  - `keyPressed`: before the existing handling add `if (key == juce::KeyPress::spaceKey) { playback.togglePlayPause(); return true; }`.
  - `afterDocumentReplaced()` (existing, from the Song-file work): append `playback.documentReplaced();`.
  - A 30 Hz tick must also free retired synth instances: add to `MainWindow` a small `struct SynthGc : juce::Timer { SynthVoice& s; void timerCallback() override { s.collectGarbage(); } }` member started at 2 Hz (`startTimerHz (2)`) — declared after `synth`.

- [ ] **Step 4: Build and run the suite** — `cmake --build build` (both `forge_ui` and `forge_tests` must compile and link) then `ctest --test-dir build --output-on-failure` → all tests pass (433 baseline + the playback tests).

- [ ] **Step 5: Windows cross-check** — `./build-windows.sh forge_ui` → builds. Do **not** launch the GUI. (Deploy for the user's manual test is a final-step action: `./build-windows.sh forge_ui && cp build-windows/forge_ui_artefacts/Release/song-smith.exe /mnt/c/Apps/SongSmith/ && cp resources/soundfonts/TimGM6mb.sf2 /mnt/c/Apps/SongSmith/` — only when the user asks to test.)

- [ ] **Step 6: Commit** — `feat(playback): wire the audio device, SoundFont setting and transport into the main window` (full trailer).

---

### Task 13: Mute/solo buttons on track rows

**Files:**
- Modify: `Source/UI/TrackRowComponent.h/.cpp`, `Source/UI/TrackListComponent.h/.cpp`, `Tests/TrackRowComponent_tests.cpp`, `Tests/TrackListComponent_tests.cpp`

**Interfaces:**
- Consumes: `PlaybackController` (`setMuted`, `setSoloed`, `isMuted`, `isSoloed`, `isSilencedBySolo`, `Listener::muteSoloChanged`).
- Produces: `TrackRowComponent::onMuteToggled`, `onSoloToggled` (`std::function<void (juce::int64 trackId, bool newState)>`), `void setMuteSolo (bool muted, bool soloed, bool silencedBySolo)`, `juce::TextButton& muteButtonForTesting()`, `soloButtonForTesting()`, `static constexpr int muteSoloWidth = 40`. `TrackListComponent::setPlayback (PlaybackController*)`.

- [ ] **Step 1: Write the failing tests.** Append to `Tests/TrackRowComponent_tests.cpp`:

```cpp
TEST_CASE ("TrackRowComponent: M and S buttons fire callbacks with the trackId and toggled state", "[track-row][mutesolo]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SongDocument doc;
    auto track = doc.addTrack ("Track A", 0xFFAABBCC, 0, 0);
    const auto trackId = (juce::int64) track.getProperty (SongIDs::trackId);
    TimelineViewState viewState;
    TrackRowComponent row (track, 1, viewState);

    juce::int64 mutedId = -1, soloedId = -1;
    bool mutedState = false, soloedState = false;
    row.onMuteToggled = [&] (juce::int64 id, bool s) { mutedId = id; mutedState = s; };
    row.onSoloToggled = [&] (juce::int64 id, bool s) { soloedId = id; soloedState = s; };

    row.muteButtonForTesting().triggerClick();
    row.soloButtonForTesting().triggerClick();
    CHECK (mutedId == trackId);
    CHECK (mutedState);
    CHECK (soloedId == trackId);
    CHECK (soloedState);
}

TEST_CASE ("TrackRowComponent: the conductor row has no mute/solo buttons", "[track-row][mutesolo]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SongDocument doc;
    TimelineViewState viewState;
    TrackRowComponent row (doc.getConductorTrack(), 0, viewState);
    CHECK (! row.muteButtonForTesting().isVisible());
    CHECK (! row.soloButtonForTesting().isVisible());
}

TEST_CASE ("TrackRowComponent: setMuteSolo reflects state on the buttons", "[track-row][mutesolo]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SongDocument doc;
    auto track = doc.addTrack ("Track A", 0xFFAABBCC, 0, 0);
    TimelineViewState viewState;
    TrackRowComponent row (track, 1, viewState);
    row.setMuteSolo (true, false, false);
    CHECK (row.muteButtonForTesting().getToggleState());
    CHECK (! row.soloButtonForTesting().getToggleState());
    row.setMuteSolo (false, true, false);
    CHECK (! row.muteButtonForTesting().getToggleState());
    CHECK (row.soloButtonForTesting().getToggleState());
}
```

Append to `Tests/TrackListComponent_tests.cpp` (use that file's existing doc/rebuild helpers — `TrackListComponentTestAccess::rebuild` — for a list with one assignable track):

```cpp
TEST_CASE ("TrackListComponent: M/S clicks drive the PlaybackController and rows reflect it after a rebuild", "[track-list][mutesolo]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SongDocument doc;
    auto track = doc.addTrack ("Track A", 0xFFAABBCC, 0, 0);
    SongDocument::getNotesNode (track).addChild (juce::ValueTree (SongIDs::NOTE), -1, nullptr);
    const auto trackId = (juce::int64) track.getProperty (SongIDs::trackId);

    lotro::playbacktest::RecordingSink sink;
    PlaybackController controller (doc, sink);
    TrackListComponent list (doc);
    list.setPlayback (&controller);
    list.setSize (600, 200);
    TrackListComponentTestAccess::rebuild (list);

    controller.setMuted (trackId, true);
    TrackListComponentTestAccess::rebuild (list);
    CHECK (TrackListComponentTestAccess::rowFor (list, trackId)->muteButtonForTesting().getToggleState());

    TrackListComponentTestAccess::rowFor (list, trackId)->soloButtonForTesting().triggerClick();
    CHECK (controller.isSoloed (trackId));
}
```

(If `TrackListComponentTestAccess` has no `rowFor`, add `static TrackRowComponent* rowFor (TrackListComponent&, juce::int64 trackId)` to it next to `rebuild`/`selectTrack`, iterating `list.content.rows`; also `#include "PlaybackTestSupport.h"` in that test file.)

- [ ] **Step 2: Run to verify they fail** — `cmake --build build --target forge_tests` → FAIL (members missing).

- [ ] **Step 3: Implement.**
  - `TrackRowComponent.h`: add `#include <juce_gui_basics/...>` (already), public: `std::function<void (juce::int64, bool)> onMuteToggled, onSoloToggled; void setMuteSolo (bool muted, bool soloed, bool silencedBySolo); juce::TextButton& muteButtonForTesting() { return muteButton; } juce::TextButton& soloButtonForTesting() { return soloButton; } static constexpr int muteSoloWidth = 40;` private: `juce::TextButton muteButton { "M" }, soloButton { "S" }; bool silencedBySolo = false;`.
  - `TrackRowComponent.cpp` constructor: after `addAndMakeVisible (notePreview);` add
    ```cpp
    const bool isConductor = (bool) track.getProperty (SongIDs::isConductor, false);
    for (auto* b : { &muteButton, &soloButton })
    {
        b->setClickingTogglesState (true);
        b->setWantsKeyboardFocus (false);
        b->setVisible (! isConductor);
        addChildComponent (*b);
    }
    muteButton.setColour (juce::TextButton::buttonOnColourId, juce::Colours::orangered);
    soloButton.setColour (juce::TextButton::buttonOnColourId, juce::Colours::gold);
    muteButton.setTooltip ("Mute");
    soloButton.setTooltip ("Solo");
    muteButton.onClick = [this] { if (onMuteToggled) onMuteToggled (getTrackId(), muteButton.getToggleState()); };
    soloButton.onClick = [this] { if (onSoloToggled) onSoloToggled (getTrackId(), soloButton.getToggleState()); };
    ```
    `resized()`: before trimming the info column, place the buttons at the right end of it:
    ```cpp
    auto info = getLocalBounds().withWidth (juce::jmin (trackInfoWidth, getWidth())).withTrimmedBottom (dividerThickness);
    auto buttons = info.removeFromRight (muteSoloWidth).reduced (1, 8);
    muteButton.setBounds (buttons.removeFromLeft (buttons.getWidth() / 2));
    soloButton.setBounds (buttons);
    ```
    `setMuteSolo`: `muteButton.setToggleState (muted, juce::dontSendNotification); soloButton.setToggleState (soloed, juce::dontSendNotification); silencedBySolo = silenced; setAlpha ((muted || silenced) ? 0.5f : 1.0f);`. In `paint()`, find where the name/info text rectangle is derived from `trackInfoWidth` and subtract `muteSoloWidth` from its width so text never runs under the buttons (read the existing `paint()`; it draws into the left `trackInfoWidth` column).
  - `TrackListComponent.h`: `#include "Playback/PlaybackController.h"`; make the class also inherit `private PlaybackController::Listener`; add `void setPlayback (PlaybackController* controller);` and private `PlaybackController* playback = nullptr; void muteSoloChanged() override;`.
  - `TrackListComponent.cpp`: `setPlayback` removes `this` from the old controller's listeners, stores the new one, adds `this`, then `rebuild()`. In `rebuild()` where each row is created and its callbacks wired (next to `onGhostToggled`), add:
    ```cpp
    row->onMuteToggled = [this] (juce::int64 id, bool s) { if (playback != nullptr) playback->setMuted (id, s); };
    row->onSoloToggled = [this] (juce::int64 id, bool s) { if (playback != nullptr) playback->setSoloed (id, s); };
    if (playback != nullptr)
        row->setMuteSolo (playback->isMuted (id), playback->isSoloed (id), playback->isSilencedBySolo (id));
    ```
    where `id = row->getTrackId()`. `muteSoloChanged()`: loop `content.rows` and call `setMuteSolo (...)` for each (no rebuild). Destructor: `if (playback != nullptr) playback->removeListener (this);`.
  - `SongsmithMainComponent` constructor body: `trackList.setPlayback (playback);` (null-safe: `setPlayback (nullptr)` is a no-op).

- [ ] **Step 4: Run to verify they pass** — `ctest --test-dir build -R "track-row|track-list|TrackRow|TrackList" --output-on-failure` → PASS, plus the full suite stays green.

- [ ] **Step 5: Commit** — `feat(playback): add mute/solo buttons to track rows` (full trailer).

---

### Task 14: `TimelineRuler` and `PlayheadOverlay` components

**Files:**
- Create: `Source/UI/Playback/TimelineRuler.h/.cpp`, `Source/UI/Playback/PlayheadOverlay.h/.cpp`, `Tests/PlayheadOverlay_tests.cpp`
- Modify: `CMakeLists.txt`, `Tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `PlaybackController` (`getPositionTicks`, `Listener::playbackPositionChanged`).
- Produces:

```cpp
class TimelineRuler : public juce::Component
{
public:
    static constexpr int height = 14;
    explicit TimelineRuler (std::function<double (int x)> tickForXIn);
    std::function<void (double tick)> onSeek;     // click or drag
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
};
class PlayheadOverlay : public juce::Component, private PlaybackController::Listener
{
public:
    PlayheadOverlay (PlaybackController&, std::function<int (double tick)> tickToXIn);
    ~PlayheadOverlay() override;
    void paint (juce::Graphics&) override;       // 2 px line; nothing when x is outside the bounds
    int currentX() const;                         // for tests/followers
private:
    void playbackPositionChanged() override;      // repaints only the old and new line strips
};
```
`PlayheadOverlay` calls `setInterceptsMouseClicks (false, false)`.

- [ ] **Step 1: Write the failing tests** — `Tests/PlayheadOverlay_tests.cpp`:

```cpp
#include "PlaybackTestSupport.h"
#include "UI/Playback/PlaybackController.h"
#include "UI/Playback/PlayheadOverlay.h"
#include "UI/Playback/TimelineRuler.h"

#include <catch2/catch_test_macros.hpp>

using namespace lotro;
using namespace lotro::playbacktest;

namespace
{
    juce::MouseEvent mouseAt (juce::Component& c, int x)
    {
        return juce::MouseEvent (juce::Desktop::getInstance().getMainMouseSource(),
                                 juce::Point<float> ((float) x, 3.0f), juce::ModifierKeys(),
                                 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, &c, &c, juce::Time::getCurrentTime(),
                                 juce::Point<float> ((float) x, 3.0f), juce::Time::getCurrentTime(), 1, false);
    }
}

TEST_CASE ("PlayheadOverlay: ignores mouse clicks so it never blocks editing", "[playback][overlay]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SongDocument doc;
    RecordingSink sink;
    PlaybackController controller (doc, sink);
    PlayheadOverlay overlay (controller, [] (double tick) { return (int) (tick * 0.1); });
    overlay.setSize (500, 100);
    CHECK (! overlay.getInterceptsMouseClicks (true, true));
    CHECK (! overlay.getInterceptsMouseClicks (false, false));
}

TEST_CASE ("PlayheadOverlay: x follows the controller's position through the supplied tick mapping", "[playback][overlay]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SongDocument doc;
    RecordingSink sink;
    PlaybackController controller (doc, sink);
    addNote (addTrack (doc), 60, 0, 9600);
    controller.flushRebuild();
    PlayheadOverlay overlay (controller, [] (double tick) { return (int) (tick * 0.1); });
    overlay.setSize (500, 100);

    controller.seekToTick (1000.0);
    CHECK (overlay.currentX() == 100);
    controller.seekToTick (0.0);
    CHECK (overlay.currentX() == 0);
}

TEST_CASE ("TimelineRuler: click and drag report the tick under the pointer, clamped at zero", "[playback][ruler]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    TimelineRuler ruler ([] (int x) { return (double) x * 10.0; });
    ruler.setSize (500, TimelineRuler::height);
    double lastTick = -1.0;
    ruler.onSeek = [&] (double t) { lastTick = t; };

    ruler.mouseDown (mouseAt (ruler, 25));
    CHECK (lastTick == 250.0);
    ruler.mouseDrag (mouseAt (ruler, 40));
    CHECK (lastTick == 400.0);
    ruler.mouseDrag (mouseAt (ruler, -30));
    CHECK (lastTick == 0.0);
}
```

- [ ] **Step 2: Run to verify it fails** — add test + both `.cpp` files; build → FAIL.

- [ ] **Step 3: Implement** — `TimelineRuler.h/.cpp`:

```cpp
// TimelineRuler.h
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

namespace lotro
{

// A thin click/drag strip above a note canvas: pressing it moves the playhead.
class TimelineRuler : public juce::Component
{
public:
    static constexpr int height = 14;

    explicit TimelineRuler (std::function<double (int x)> tickForXIn) : tickForX (std::move (tickForXIn))
    {
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
        setTooltip ("Click or drag to move the playhead");
    }

    std::function<void (double tick)> onSeek;

    void paint (juce::Graphics& g) override;
    void mouseDown (const juce::MouseEvent& e) override { seekTo (e.x); }
    void mouseDrag (const juce::MouseEvent& e) override { seekTo (e.x); }

private:
    void seekTo (int x)
    {
        if (onSeek && tickForX)
            onSeek (juce::jmax (0.0, tickForX (x)));
    }

    std::function<double (int)> tickForX;
};

} // namespace lotro
```
```cpp
// TimelineRuler.cpp
#include "UI/Playback/TimelineRuler.h"

#include "UI/SongsmithColours.h"

namespace lotro
{

void TimelineRuler::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (SongsmithColours::background).brighter (0.1f));
    g.setColour (juce::Colours::black.withAlpha (0.5f));
    g.drawHorizontalLine (getHeight() - 1, 0.0f, (float) getWidth());
}

} // namespace lotro
```

`PlayheadOverlay.h/.cpp`:

```cpp
// PlayheadOverlay.h
#pragma once

#include "UI/Playback/PlaybackController.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

namespace lotro
{

// A mouse-transparent vertical line at the shared playhead. The owner supplies
// the tick -> x mapping in this component's own coordinates, because each
// canvas has independent zoom/scroll.
class PlayheadOverlay : public juce::Component, private PlaybackController::Listener
{
public:
    PlayheadOverlay (PlaybackController& controllerIn, std::function<int (double tick)> tickToXIn);
    ~PlayheadOverlay() override;

    void paint (juce::Graphics& g) override;
    int currentX() const { return tickToX ? tickToX (controller.getPositionTicks()) : 0; }

private:
    void playbackPositionChanged() override;

    PlaybackController& controller;
    std::function<int (double)> tickToX;
    int lastX = -1;
};

} // namespace lotro
```
```cpp
// PlayheadOverlay.cpp
#include "UI/Playback/PlayheadOverlay.h"

namespace lotro
{

PlayheadOverlay::PlayheadOverlay (PlaybackController& controllerIn, std::function<int (double)> tickToXIn)
    : controller (controllerIn), tickToX (std::move (tickToXIn))
{
    setInterceptsMouseClicks (false, false);
    controller.addListener (this);
}

PlayheadOverlay::~PlayheadOverlay()
{
    controller.removeListener (this);
}

void PlayheadOverlay::paint (juce::Graphics& g)
{
    const int x = currentX();
    lastX = x;
    if (x < 0 || x >= getWidth())
        return;
    g.setColour (juce::Colours::white.withAlpha (0.9f));
    g.fillRect (x, 0, 2, getHeight());
}

void PlayheadOverlay::playbackPositionChanged()
{
    const int x = currentX();
    if (x == lastX)
        return;
    repaint (juce::jmax (0, lastX - 1), 0, 4, getHeight());   // erase the old line
    repaint (juce::jmax (0, x - 1), 0, 4, getHeight());       // draw the new one
    lastX = x;
}

} // namespace lotro
```

Add both `.cpp` files to `forge_ui` sources.

- [ ] **Step 4: Run to verify it passes** — `ctest --test-dir build -R "overlay|ruler|PlayheadOverlay|TimelineRuler" --output-on-failure` → PASS (3).

- [ ] **Step 5: Commit** — `feat(playback): add TimelineRuler and the mouse-transparent PlayheadOverlay` (full trailer).

---

### Task 15: Playhead, ruler and follow on the main track canvas

**Files:**
- Modify: `Source/UI/TrackListComponent.h/.cpp`, `Tests/TrackListComponent_tests.cpp`

**Interfaces:**
- Consumes: `PlayheadOverlay`, `TimelineRuler`, `TimelineViewState` (`xForTick (int)`, `tickForX (int)`, `getScrollOffsetTicks()`, `setScrollOffsetTicks (double)`), `TrackListComponent::{notePreviewOriginX, previewWidth, syncHorizontalBar, documentEndTick}`, `PlaybackController`.
- Produces: `TrackListComponent` owns a `TimelineRuler` row (height `TimelineRuler::height`) above the viewport and a `PlayheadOverlay` over the viewport's note-preview strip, both created only when `setPlayback (non-null)` is called; `TrackListComponent::followPlayhead()` (called from `playbackPositionChanged`, test-reachable).

- [ ] **Step 1: Write the failing tests** — append to `Tests/TrackListComponent_tests.cpp`:

```cpp
TEST_CASE ("TrackListComponent: clicking the ruler moves the shared playhead", "[track-list][playhead]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SongDocument doc;
    auto track = doc.addTrack ("A", 0xFFAABBCC, 0, 0);
    lotro::playbacktest::addNote (track, 60, 0, 9600);
    lotro::playbacktest::RecordingSink sink;
    PlaybackController controller (doc, sink);
    controller.flushRebuild();
    TrackListComponent list (doc);
    list.setPlayback (&controller);
    list.setSize (800, 300);
    list.fitTimelineToDocument();

    list.rulerForTesting()->onSeek (960.0);
    CHECK (controller.getPositionTicks() == Catch::Approx (960.0));
}

TEST_CASE ("TrackListComponent: while playing, the view page-flips to keep the playhead visible", "[track-list][playhead]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SongDocument doc;
    auto track = doc.addTrack ("A", 0xFFAABBCC, 0, 0);
    lotro::playbacktest::addNote (track, 60, 0, 96000);
    lotro::playbacktest::RecordingSink sink;
    PlaybackController controller (doc, sink);
    controller.flushRebuild();
    TrackListComponent list (doc);
    list.setPlayback (&controller);
    list.setSize (800, 300);
    list.fitTimelineToDocument();
    TrackListComponentTestAccess::zoomIn (list);        // so the song is wider than the view

    controller.seekToTick (50000.0);                    // far beyond the visible span
    list.followPlayheadForTesting (/*playing*/ true);
    CHECK (TrackListComponentTestAccess::scrollOffsetTicks (list) > 0.0);
}
```

(Add to `TrackListComponentTestAccess`: `static double scrollOffsetTicks (TrackListComponent& l) { return l.timelineView.getScrollOffsetTicks(); }` and `static void zoomIn (TrackListComponent& l) { l.timelineView.zoomBy (4.0, l.notePreviewOriginX()); l.timelineFitted = false; l.syncHorizontalBar(); }`; add `#include "UI/Playback/TimelineRuler.h"` where needed.)

- [ ] **Step 2: Run to verify it fails** — build → FAIL (`rulerForTesting` missing).

- [ ] **Step 3: Implement.** In `TrackListComponent.h` add `#include "Playback/PlayheadOverlay.h"`, `#include "Playback/TimelineRuler.h"`; public: `TimelineRuler* rulerForTesting() noexcept { return ruler.get(); } void followPlayheadForTesting (bool playing) { followPlayhead (playing); }`; private: `std::unique_ptr<TimelineRuler> ruler; std::unique_ptr<PlayheadOverlay> overlay; void followPlayhead (bool playing); void playbackPositionChanged() override { followPlayhead (playback != nullptr && playback->isPlaying()); }`. In `TrackListComponent.cpp`:
  - `setPlayback`, after storing the pointer: if non-null and `ruler == nullptr`, create them:
    ```cpp
    ruler = std::make_unique<TimelineRuler> ([this] (int x) { return timelineView.tickForX (x - notePreviewOriginX()); });
    ruler->onSeek = [this] (double tick) { if (playback != nullptr) playback->seekToTick (tick); };
    addAndMakeVisible (*ruler);
    overlay = std::make_unique<PlayheadOverlay> (*playback, [this] (double tick) { return timelineView.xForTick ((int) tick); });
    addAndMakeVisible (*overlay);   // added after the viewport, so it draws on top
    resized();
    ```
    (when `controller` is null: `ruler.reset(); overlay.reset();`).
  - `resized()`: where it lays out the viewport and `horizontalBar`, first `if (ruler != nullptr) ruler->setBounds (area.removeFromTop (TimelineRuler::height));` on the working rectangle, and after the viewport is placed: `if (overlay != nullptr) overlay->setBounds (viewport.getBounds().withTrimmedLeft (notePreviewOriginX()).withWidth (previewWidth()));` (the overlay's local x is the preview-local frame that `timelineView.xForTick` uses). Keep `overlay->toFront (false)` after any rebuild that re-adds rows.
  - `followPlayhead (bool playing)`: 
    ```cpp
    void TrackListComponent::followPlayhead (bool playing)
    {
        if (! playing || playback == nullptr)
            return;
        const double tick = playback->getPositionTicks();
        const int x = timelineView.xForTick ((int) tick);
        if (x >= 0 && x < previewWidth())
            return;
        timelineView.setScrollOffsetTicks (tick);   // page-flip: the playhead becomes the left edge
        timelineFitted = false;                      // do not let a resize refit fight the follow
        syncHorizontalBar();
        repaint();
    }
    ```
    (`setScrollOffsetTicks` already clamps per `TimelineViewState`; `syncHorizontalBar` re-clamps to the song length.)

- [ ] **Step 4: Run to verify it passes** — `ctest --test-dir build -R "track-list|TrackList" --output-on-failure` → PASS, full suite green.

- [ ] **Step 5: Commit** — `feat(playback): playhead, seek ruler and follow on the main track canvas` (full trailer).

---

### Task 16: Playhead, transport strip, ruler and follow in the MIDI editor window

**Files:**
- Modify: `Source/UI/PianoRollComponent.h/.cpp`, `Source/UI/TrackEditorWindow.h/.cpp`, `Source/UI/SongsmithMainComponent.cpp`, `Tests/PianoRollComponent_tests.cpp`, `Tests/TrackEditorWindow_tests.cpp`

**Interfaces:**
- Consumes: `PlayheadOverlay`, `TimelineRuler`, `TransportStrip`, `PlaybackController`, `PianoRollGeometry::{xForTick, tickForX}`, the roll's `viewport`, `gutter`, `geometry`.
- Produces: `PianoRollComponent::setPlayback (PlaybackController*)` (Source role only; no-op for Preview) creating a `PlayheadOverlay` positioned over the viewport but **beneath** the gutter; `int PianoRollComponent::xForTickInComponent (double tick) const` and `double PianoRollComponent::tickForXInComponent (int x) const` (component-local, scroll-aware). `TrackEditorWindow::setPlayback (PlaybackController*)` replaces the window's content with a container holding `TransportStrip` (top), `TimelineRuler`, then the roll; Space toggles playback in the window.

- [ ] **Step 1: Write the failing tests.** Append to `Tests/PianoRollComponent_tests.cpp` (use that file's existing helper for a Source-role roll with a note source; if it has none, build one from `SourceTrackNoteSource` over a track as `TrackEditorWindow::setTrack` does):

```cpp
TEST_CASE ("PianoRollComponent: tick<->x mapping is component-local and scroll-aware", "[piano-roll][playhead]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SongDocument doc;
    auto track = doc.addTrack ("A", 0xFFAABBCC, 0, 0);
    lotro::playbacktest::addNote (track, 60, 0, 9600);
    SourceTrackNoteSource source (track);
    PianoRollComponent roll (PianoRollComponent::Role::Source, &doc);
    roll.setSize (800, 400);
    roll.setNoteSource (&source, 480, doc.getMeterMapNode());

    const int x0 = roll.xForTickInComponent (0.0);
    CHECK (x0 >= 0);                                              // right of the gutter, never inside it
    CHECK (roll.tickForXInComponent (roll.xForTickInComponent (480.0)) == Catch::Approx (480.0).margin (1.0));
}

TEST_CASE ("PianoRollComponent: the playhead overlay is Source-role only and sits under the gutter", "[piano-roll][playhead]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SongDocument doc;
    lotro::playbacktest::RecordingSink sink;
    PlaybackController controller (doc, sink);
    PianoRollComponent preview (PianoRollComponent::Role::Preview);
    preview.setPlayback (&controller);
    CHECK (! preview.hasPlayheadForTesting());

    PianoRollComponent source (PianoRollComponent::Role::Source, &doc);
    source.setPlayback (&controller);
    CHECK (source.hasPlayheadForTesting());
}
```

Append to `Tests/TrackEditorWindow_tests.cpp`:

```cpp
TEST_CASE ("TrackEditorWindow: with playback set, the window hosts the transport strip and ruler above the roll", "[track-editor][playhead]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SongDocument doc;
    lotro::playbacktest::RecordingSink sink;
    PlaybackController controller (doc, sink);
    TrackEditorWindow window (doc, nullptr);
    window.setPlayback (&controller);
    CHECK (window.hasTransportStripForTesting());
    CHECK (window.hasRulerForTesting());
}
```

(Add `#include "PlaybackTestSupport.h"` to both test files; note `TrackEditorWindow_tests.cpp` has a known Wine hang at its centring test — do not touch that test.)

- [ ] **Step 2: Run to verify they fail** — build → FAIL.

- [ ] **Step 3: Implement `PianoRollComponent` support.** In `PianoRollComponent.h` add `#include "Playback/PlayheadOverlay.h"`; public:
  ```cpp
  void setPlayback (PlaybackController* controller);   // Source role only
  int xForTickInComponent (double tick) const;
  double tickForXInComponent (int x) const;
  bool hasPlayheadForTesting() const noexcept { return playhead != nullptr; }
  ```
  private: `std::unique_ptr<PlayheadOverlay> playhead; PlaybackController* playback = nullptr;`. In `PianoRollComponent.cpp`:
  ```cpp
  int PianoRollComponent::xForTickInComponent (double tick) const
  {
      return geometry.xForTick ((int) tick) - viewport.getViewPositionX();
  }

  double PianoRollComponent::tickForXInComponent (int x) const
  {
      return (double) geometry.tickForX (x + viewport.getViewPositionX());
  }

  void PianoRollComponent::setPlayback (PlaybackController* controller)
  {
      if (role != Role::Source)
          return;
      playback = controller;
      playhead.reset();
      if (controller == nullptr)
          return;
      playhead = std::make_unique<PlayheadOverlay> (*controller, [this] (double tick) { return xForTickInComponent (tick); });
      addAndMakeVisible (*playhead);
      playhead->toBack();             // below the pinned gutter, above the viewport
      viewport.toBack();              // keep the viewport behind the overlay
      layoutPlayhead();
  }
  ```
  `geometry.xForTick` includes the gutter offset (`x == getKeyboardGutterWidth()` at tick 0), so no extra offset is needed. Add private `void layoutPlayhead()` setting `playhead->setBounds (viewport.getBounds().withTrimmedBottom (viewport.getHorizontalScrollBar().isVisible() ? viewport.getScrollBarThickness() : 0))`, and call it from `resized()` and at the end of `layoutGutter()` (which already runs on every content resize). The overlay's z-order: children are added viewport, gutter in the existing constructor; after `toBack()` calls the order must be `viewport` < `playhead` < `gutter` — implement by calling `gutter.toFront (false)` as the last statement of `setPlayback`.
  Follow in the editor: `PianoRollComponent` implements `PlaybackController::Listener` privately (add `private PlaybackController::Listener` base; register in `setPlayback`, deregister in the destructor and when `controller == nullptr`); `playbackPositionChanged()`: if `playback->isPlaying()` and `xForTickInComponent (tick)` is outside `[geometry.getKeyboardGutterWidth(), viewport.getWidth())`, call `viewport.setViewPosition (juce::jmax (0, geometry.xForTick ((int) tick) - geometry.getKeyboardGutterWidth()), viewport.getViewPositionY()); timelineFitted = false;`.

- [ ] **Step 4: Implement `TrackEditorWindow` container.** In `TrackEditorWindow.h` add `#include "Playback/PlaybackController.h"`, `"Playback/TimelineRuler.h"`, `"Playback/TransportStrip.h"`; public `void setPlayback (PlaybackController*); bool hasTransportStripForTesting() const noexcept; bool hasRulerForTesting() const noexcept; bool keyPressed (const juce::KeyPress&) override;`; private nested class
  ```cpp
  class Content : public juce::Component
  {
  public:
      Content (PianoRollComponent& rollIn, PlaybackController& controller);
      void resized() override;   // strip (TransportStrip::height) / ruler (TimelineRuler::height) / roll
      TransportStrip strip;
      TimelineRuler ruler;
      PianoRollComponent& roll;
  };
  std::unique_ptr<Content> content;
  PlaybackController* playback = nullptr;
  ```
  `Content` ctor: `strip (controller)`, `ruler ([&rollIn] (int x) { return rollIn.tickForXInComponent (x); })`, `ruler.onSeek = [&controller] (double t) { controller.seekToTick (t); }`, `addAndMakeVisible` strip, ruler, roll. `setPlayback (controller)`: `roll.setPlayback (controller); content = std::make_unique<Content> (roll, *controller); setContentNonOwned (content.get(), true);` (the destructor must `setContentNonOwned (nullptr, false)` before `content` is destroyed — update the existing destructor; declare `content` **after** `roll`). `keyPressed`: `if (key == juce::KeyPress::spaceKey && playback != nullptr) { playback->togglePlayPause(); return true; } return juce::DocumentWindow::keyPressed (key);`. In `SongsmithMainComponent.cpp`, where the window is created (`trackDoubleClicked`), after construction call `trackEditorWindow->setPlayback (playback);` (null-safe: only call when `playback != nullptr`).

- [ ] **Step 5: Run to verify it passes** — `ctest --test-dir build -R "piano-roll|PianoRoll|track-editor|TrackEditorWindow" --output-on-failure` (skip the Wine-hanging centring test under Wine only), then the full suite → green. Build `forge_ui` and run `./build-windows.sh forge_ui` → builds; do not launch the GUI.

- [ ] **Step 6: Commit** — `feat(playback): shared playhead, transport strip and seek ruler in the MIDI editor window` (full trailer).

---

### Task 17: Documentation, licensing guardrails and deploy line

**Files:**
- Modify: `CLAUDE.md`, `docs/ARCHITECTURE.md`, `docs/UI_GUIDE.md`, `docs/TESTING.md`, `docs/songsmith-ui-map.html`, the spec's status line

**Interfaces:** none (docs only; no code). Sweep for stale statements first: `grep -rn -i "playback\|not started\|Project 3" CLAUDE.md docs/*.md docs/superpowers/specs/2026-10-03-songsmith-playback-design.md`.

- [ ] **Step 1: `CLAUDE.md`** — update the Status line to mention Playback (TinySoundFont; transport; mute/solo); in **Build / test commands** extend the deploy line to
  `./build-windows.sh forge_ui && cp build-windows/forge_ui_artefacts/Release/song-smith.exe /mnt/c/Apps/SongSmith/ && cp resources/soundfonts/TimGM6mb.sf2 /mnt/c/Apps/SongSmith/` (the SoundFont is local-only; first-time setup is in `docs/BUILD.md`); under **Licensing guardrails** add: "`TimGM6mb.sf2` is GPL-2 and is never committed or shipped by CI — it is a git-ignored local file under `resources/soundfonts/` copied next to the exe by a CMake post-build step. TinySoundFont (MIT) is vendored at `Source/ThirdParty/tinysoundfont/`."; add the new spec/plan to the Docs map.

- [ ] **Step 2: `docs/ARCHITECTURE.md`** — new section "9.14 Playback": the unit list (snapshot, transport, mute/solo, hand-off, engine, synth voice, controller, audio output), the data-flow paragraph (coalesced rebuild → snapshot swap → release + chase), the seconds time-domain rationale, the virtual-channel rule, and the threading rules (what the audio thread may not do). `docs/UI_GUIDE.md` — add the transport strip, M/S buttons, ruler/playhead, Song ▸ SoundFont…, Space. `docs/TESTING.md` — add the new test prefixes (`PlaybackError`, `TempoMap`, `buildSnapshot`, `MuteSoloState`, `Transport`, `HandOff`, `PlaybackEngine`, `SynthVoice`, `PlaybackController`, `TransportStrip`, `PlayheadOverlay`/`TimelineRuler`) and state that the SynthVoice smoke test skips when the local SoundFont is absent (always on CI). `docs/songsmith-ui-map.html` — add the new regions as clickable `#N` entries in the same style as the existing ones.

- [ ] **Step 3: Spec status** — change the spec's `Status:` line to `Implemented (2026-10-03). Project 3 of 3.` and mark the open items resolved (Rewind = step back one bar per `previousBarTick`, Stop returns to play-start — unless the user changed them; tsf pinned at `853a0a1`; local SoundFont path `resources/soundfonts/TimGM6mb.sf2`).

- [ ] **Step 4: Verify** — `ctest --test-dir build --output-on-failure` (full suite green; report the new total), `./build-windows.sh forge_ui` builds, `git status --short` shows no `resources/` and no `.sf2`.

- [ ] **Step 5: Commit** — `docs: describe Songsmith playback, the SoundFont licensing rule and the deploy step` (full trailer).

---

## Self-Review

**1. Spec coverage**
- Intent/success (play from playhead, mute/solo everywhere, shared playhead, edits mid-play, mid-song start): Tasks 7–8, 10, 12–16.
- Decisions: TinySoundFont (T1, T9); source-only (T16 adds the overlay to Source-role rolls only; T12 leaves the preview canvas untouched); one transport (T5, T10); Play from playhead + end exception (T5, T8); mute/solo semantics + session-only + kept across removal (T4, T10); SoundFont not in repo/CI (T1, T12, T17); approach A (T7+); no device picker/metronome/loops (never added).
- Architecture: `PlaybackSnapshot` seconds/tempo defaults/CC forwarding/conductor ignored/virtual channels/drum bank/audible flags (T2–T3); `Transport` semantics (T5); `MuteSoloState` (T4); `SynthVoice` single `TSF_IMPLEMENTATION` TU + pre-init (T9); `PlaybackEngine` + `EventSink` (T7); `AudioOutput` forge_ui-only (T12); chase (T8); hand-off scheme (T6, T9); data flow coalescing + cosmetic-change filter + release-on-swap + New/Open reset (T10).
- UI: transport strip + Space + non-focus buttons (T11, T12, T16); overlay/ruler/follow on both canvases incl. gutter and Viewport details (T14–T16); M/S buttons (T13); SoundFont… menu + `MainWindow::settings` (T12).
- Errors: `PlaybackError` kinds, no-SoundFont state, device error (T1, T9, T12).
- Testing: device-free engine with recording sink; chase/timing/mute/swap/virtual-channel/drum/coalescing tests (T3, T7, T8, T10); smoke test skipping without sf2 (T9).
- Dependencies/licensing/packaging (T1, T12, T17).
- Gap noted and accepted: `MainWindow`/`AudioOutput`/menu/Space wiring is not automatable (same status as the Song-file wiring); verified by build + the user's manual test.

**2. Placeholder scan:** none. Where a step edits existing code (the `paint()` text width in Task 13, the test helpers reused in Tasks 13 and 16), it names the exact existing member to read and the exact change to make.

**3. Type consistency:** `PlaybackEvent`/`PlaybackEventKind`/`VirtualChannel`/`kMaxVirtualChannels` (T3) used unchanged in T7, T9, T10; `EventSink` methods (`prepare`, `beginBlock`, `handle`, `releaseChannel`, `releaseAll`, `render`) are identical in T7, `RecordingSink`, and `SynthVoice` (T9); `Transport` API (T5) matches its use in the engine (T7) and controller (T10); `PlaybackController` API (T10) matches T11–T16 call sites (`getPositionTicks`, `seekToTick`, `setMuted/Soloed`, `isSilencedBySolo`, `Listener::{playbackPositionChanged,playbackStateChanged,muteSoloChanged}`); `TimelineRuler::height`, `TransportStrip::height` used consistently.

**4. Review Focus:** items 1–6 are each pinned by a named test (empty song: T5 + T7 + T10; channel-10 drums: T3; Open/New while playing: T10; degenerate/bad data: T2 + T3; >64 tracks and >256 pairs: T3 + T4; cosmetic changes not cutting notes: T10).

**Known assumptions to confirm during execution**
- `Rewind` = step back one bar and `Stop` returns to the play-start (both still unconfirmed with the user) are isolated in `previousBarTick` / `Transport::stop`.
- "SoundFont…" is placed in the **Song** menu (the spec says only "a menu item").
- Task 12 starts the audio device lazily on first Play (so a machine with no audio device can still open and edit Songs).
