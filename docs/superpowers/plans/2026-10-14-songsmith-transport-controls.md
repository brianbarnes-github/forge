# Songsmith Transport Controls Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Fill the right area of the `TransportStrip` with a Selection group (loop start / end / length), a time-signature box, a tempo box and a playback-rate knob.

**Architecture:** Pure helpers (`RateMapping`, `TransportText`) are built first and tested alone. The engine gets a rate by scaling its clock (`framesPerSongSecond = sampleRate / rate`, substituted for `sampleRate` in the loop-step and event-offset maths, so rate 1.0 is bit-identical). A new `TransportControls` component binds the boxes to `PlaybackController` (loop range, rate) and `TempoEdit` (meter and tempo at the playhead), and `TransportStrip` hosts it in `rightArea`.

**Tech Stack:** C++17, JUCE, Catch2, CMake/Ninja. No new dependencies.

**Spec:** `docs/superpowers/specs/2026-10-14-songsmith-transport-controls-design.md`

## Global Constraints

- Everything is UI-side: nothing is added to `Source/Core/`.
- Rate range **0.25 – 4.0**, logarithmic knob, **session only** (never in `AppSettings` or the `.songsmith` file); readout is `%.3f` (`1.000`); double-click resets to 1.000.
- Rate 1.0 must run the same arithmetic as today (`sampleRate / 1.0 == sampleRate` exactly) so every existing engine test passes unchanged.
- Selection = the existing loop range (`PlaybackController::setLoopRange`); the length box is in **beats of the meter's denominator**.
- Meter and tempo boxes read and edit **at the playhead** via `setMeter` / `setTempo` (one undo step each); `TempoEditError` is reused, no string errors.
- Position text is `bar.beat.tick`: bar and beat 1-based, tick 0-based within the beat.
- Custom error types, explicit types everywhere, functional style where practical, conventional-commit messages, commit trailer `Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>`. Never push without approval.
- Do not launch the GUI. Do not add dependencies.
- Spec file-name note: the spec calls the text helpers `BarPosition`; this plan names that file `TransportText` because it also holds the length, meter and tempo parsers. `PlaybackController` is not asked to "re-apply the rate after `documentReplaced`" because the engine object is the same one and keeps its rate; the controller only remembers the value.

## Review Focus

1. Changing the rate while playing neither skips nor repeats an event (Task 2 test).
2. A loop end typed beyond the song end, or start/end typed reversed: the boxes must show the ordered, clamped range, never the raw typing (Task 5 tests).
3. The playhead moving (timer refresh) must not overwrite text the user is typing in a meter/tempo/selection box (Task 6 test).
4. Multi-meter songs: a position typed after a meter change converts to the right tick and back (Task 4 test).
5. Undoing a tempo or meter edit refreshes the boxes (Task 6 test).
6. No song loaded (`NoTimeBase`): the meter/tempo/selection boxes are disabled and typing never throws (Task 6 test).

---

## File Structure

| File | Responsibility |
|---|---|
| Create `Source/UI/Playback/RateMapping.{h,cpp}` | Pure rate <-> slider maths, clamp, snap, text format/parse |
| Create `Source/UI/Playback/TransportText.{h,cpp}` | Pure position / beats / meter / tempo text format and parse over `MeterSegment`s |
| Create `Source/UI/Playback/TransportControls.{h,cpp}` | The right-area component |
| Modify `Source/UI/Playback/PlaybackEngine.{h,cpp}` | `setRate`, clock scaling |
| Modify `Source/UI/Playback/PlaybackController.{h,cpp}` | `setRate` / `rate()`, `playbackRateChanged`, `playbackSnapshotChanged` |
| Modify `Source/UI/Playback/TransportStrip.{h,cpp}` | Takes `SongDocument&`, hosts the controls in `rightArea` |
| Modify `Source/UI/SongsmithMainComponent.cpp`, `Source/UI/TrackEditorPanel.{h,cpp}` | Pass the document to `TransportStrip` |
| Modify `CMakeLists.txt`, `Tests/CMakeLists.txt` | New sources and tests |
| Create `Tests/RateMapping_tests.cpp`, `Tests/TransportText_tests.cpp`, `Tests/TransportControls_tests.cpp`; modify `Tests/PlaybackEngine_tests.cpp`, `Tests/PlaybackController_tests.cpp`, `Tests/TransportStrip_tests.cpp` | Tests |

Build and test commands (repo root): `cmake --build build 2>&1 | tail -30` then `ctest --test-dir build -R "<test name substring>" --output-on-failure`. If a new file is not picked up, re-run `cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug` first.

---

### Task 1: RateMapping

**Files:**
- Create: `Source/UI/Playback/RateMapping.h`, `Source/UI/Playback/RateMapping.cpp`, `Tests/RateMapping_tests.cpp`
- Modify: `CMakeLists.txt` (add `Source/UI/Playback/RateMapping.cpp` next to `LoopRange.cpp`), `Tests/CMakeLists.txt` (add `RateMapping_tests.cpp` and `${CMAKE_SOURCE_DIR}/Source/UI/Playback/RateMapping.cpp`, next to the `LoopRange` lines)

**Interfaces:**
- Produces (namespace `lotro`):
  - `constexpr double kMinPlaybackRate = 0.25, kMaxPlaybackRate = 4.0;`
  - `double clampRate (double rate) noexcept;` (NaN -> 1.0)
  - `double rateToSlider (double rate) noexcept;` (log2 of the clamped rate, range -2..2)
  - `double sliderToRate (double slider) noexcept;` (snaps to exactly 1.0 within 0.02 of 0)
  - `std::string formatRate (double rate);` ("%.3f")
  - `std::optional<double> parseRate (const std::string& text);` (whole string a finite number, clamped; nullopt otherwise)

- [ ] **Step 1: Write the failing test** (`Tests/RateMapping_tests.cpp`)

```cpp
#include "UI/Playback/RateMapping.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>

using namespace lotro;
using Catch::Approx;

TEST_CASE ("RateMapping: 1.000 sits at the slider centre and 0.5 / 2.0 are symmetric", "[playback][rate]")
{
    CHECK (rateToSlider (1.0) == Approx (0.0).margin (1e-12));
    CHECK (rateToSlider (2.0) == Approx (1.0));
    CHECK (rateToSlider (0.5) == Approx (-1.0));
    CHECK (rateToSlider (kMinPlaybackRate) == Approx (-2.0));
    CHECK (rateToSlider (kMaxPlaybackRate) == Approx (2.0));
}

TEST_CASE ("RateMapping: the slider snaps to exactly 1.0 near the centre and round-trips elsewhere", "[playback][rate]")
{
    CHECK (sliderToRate (0.0) == 1.0);
    CHECK (sliderToRate (0.015) == 1.0);
    CHECK (sliderToRate (-0.015) == 1.0);
    CHECK (sliderToRate (0.05) != 1.0);
    for (double r : { 0.25, 0.5, 0.8, 1.25, 2.0, 4.0 })
        CHECK (sliderToRate (rateToSlider (r)) == Approx (r));
}

TEST_CASE ("RateMapping: out-of-range and non-finite rates clamp", "[playback][rate]")
{
    CHECK (clampRate (0.0) == kMinPlaybackRate);
    CHECK (clampRate (-3.0) == kMinPlaybackRate);
    CHECK (clampRate (100.0) == kMaxPlaybackRate);
    CHECK (clampRate (std::nan ("")) == 1.0);
    CHECK (sliderToRate (50.0) == kMaxPlaybackRate);
}

TEST_CASE ("RateMapping: text is three decimals and parses back, rejecting junk", "[playback][rate]")
{
    CHECK (formatRate (1.0) == "1.000");
    CHECK (formatRate (0.25) == "0.250");
    CHECK (parseRate ("1.5").value() == Approx (1.5));
    CHECK (parseRate ("  0.75 ").value() == Approx (0.75));
    CHECK (parseRate ("99").value() == kMaxPlaybackRate);
    CHECK (! parseRate ("").has_value());
    CHECK (! parseRate ("fast").has_value());
    CHECK (! parseRate ("1.2x").has_value());
}
```

- [ ] **Step 2: Run to verify it fails** — `cmake --build build 2>&1 | tail -5` fails: `UI/Playback/RateMapping.h: No such file`.

- [ ] **Step 3: Implement**

`Source/UI/Playback/RateMapping.h`:
```cpp
#pragma once

#include <optional>
#include <string>

namespace lotro
{

constexpr double kMinPlaybackRate = 0.25;
constexpr double kMaxPlaybackRate = 4.0;

// NaN -> 1.0; otherwise clamped to [kMinPlaybackRate, kMaxPlaybackRate].
double clampRate (double rate) noexcept;
// The knob works in log2 units (-2 .. 2) so 0.5 and 2.0 sit equally far from 1.0.
double rateToSlider (double rate) noexcept;
// Inverse of rateToSlider; within 0.02 of the centre it returns exactly 1.0.
double sliderToRate (double slider) noexcept;
// "1.000": the readout.
std::string formatRate (double rate);
// The whole (trimmed) text must be a finite number; the result is clamped.
std::optional<double> parseRate (const std::string& text);

} // namespace lotro
```

`Source/UI/Playback/RateMapping.cpp`:
```cpp
#include "UI/Playback/RateMapping.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace lotro
{

namespace
{
    constexpr double kSnapWindow = 0.02;   // slider (log2) units
}

double clampRate (double rate) noexcept
{
    if (std::isnan (rate))
        return 1.0;
    return std::clamp (rate, kMinPlaybackRate, kMaxPlaybackRate);
}

double rateToSlider (double rate) noexcept { return std::log2 (clampRate (rate)); }

double sliderToRate (double slider) noexcept
{
    if (std::abs (slider) < kSnapWindow)
        return 1.0;
    return clampRate (std::exp2 (slider));
}

std::string formatRate (double rate)
{
    char buffer[32];
    std::snprintf (buffer, sizeof buffer, "%.3f", rate);
    return buffer;
}

std::optional<double> parseRate (const std::string& text)
{
    const auto first = text.find_first_not_of (" \t");
    if (first == std::string::npos)
        return std::nullopt;
    const auto last = text.find_last_not_of (" \t");
    const std::string trimmed = text.substr (first, last - first + 1);
    char* end = nullptr;
    const double value = std::strtod (trimmed.c_str(), &end);
    if (end != trimmed.c_str() + trimmed.size() || ! std::isfinite (value))
        return std::nullopt;
    return clampRate (value);
}

} // namespace lotro
```

- [ ] **Step 4: Run to verify it passes** — `cmake --build build 2>&1 | tail -5 && ctest --test-dir build -R "RateMapping" --output-on-failure` → all pass.

- [ ] **Step 5: Commit**
```bash
git add Source/UI/Playback/RateMapping.* Tests/RateMapping_tests.cpp CMakeLists.txt Tests/CMakeLists.txt
git commit -m "feat(playback): RateMapping, the playback rate's log scale, clamp and text

Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>"
```

---

### Task 2: Engine playback rate

**Files:**
- Modify: `Source/UI/Playback/PlaybackEngine.h`, `Source/UI/Playback/PlaybackEngine.cpp`
- Test: `Tests/PlaybackEngine_tests.cpp` (append; it already has `Rig`, `sr = 48000`, `noteOnFrames`, `addLoopSong`, `kLoopStart = 0.4`, `kLoopEnd = 0.6`, `kFrame`)

**Interfaces:**
- Consumes: `kMinPlaybackRate`, `kMaxPlaybackRate`, `clampRate` from `RateMapping.h`.
- Produces: `void PlaybackEngine::setRate (double rate) noexcept` (any thread; stores `clampRate (rate)`), `double PlaybackEngine::playbackRate() const noexcept`.

- [ ] **Step 1: Write the failing tests** (append to `Tests/PlaybackEngine_tests.cpp`)

```cpp
TEST_CASE ("PlaybackEngine rate: defaults to 1.0 and clamps", "[playback][engine][rate]")
{
    SongDocument doc;
    Rig rig (doc);
    CHECK (rig.engine.playbackRate() == 1.0);
    rig.engine.setRate (100.0);
    CHECK (rig.engine.playbackRate() == kMaxPlaybackRate);
    rig.engine.setRate (0.0);
    CHECK (rig.engine.playbackRate() == kMinPlaybackRate);
}

TEST_CASE ("PlaybackEngine rate: at 2.0 a note starts and ends in half the frames", "[playback][engine][rate]")
{
    SongDocument doc;
    addNote (addTrack (doc), 60, 480, 240);   // on at 0.5 s, off at 0.75 s
    Rig rig (doc);
    rig.engine.setRate (2.0);
    rig.transport.play (rig.snapshot->endSeconds());
    rig.render (60);

    CHECK (noteOnFrames (rig.sink, 60) == std::vector<long> { 12000 });
    std::vector<long> offFrames;
    for (const auto& r : rig.sink.records)
        if (r.event.kind == PlaybackEventKind::NoteOff && r.event.data1 == 60)
            offFrames.push_back (r.frame);
    CHECK (offFrames == std::vector<long> { 18000 });
}

TEST_CASE ("PlaybackEngine rate: at 0.5 the same note takes twice the frames", "[playback][engine][rate]")
{
    SongDocument doc;
    addNote (addTrack (doc), 60, 480, 240);
    Rig rig (doc);
    rig.engine.setRate (0.5);
    rig.transport.play (rig.snapshot->endSeconds());
    rig.render (120);   // 61440 frames

    CHECK (noteOnFrames (rig.sink, 60) == std::vector<long> { 48000 });
}

TEST_CASE ("PlaybackEngine rate: the playhead advances rate x real time and the pitch is untouched", "[playback][engine][rate]")
{
    SongDocument doc;
    addNote (addTrack (doc), 72, 0, 4800);
    Rig rig (doc);
    rig.engine.setRate (2.0);
    rig.transport.play (rig.snapshot->endSeconds());
    rig.render (10);   // 5120 frames

    CHECK (rig.transport.getPositionSeconds() == Catch::Approx (2.0 * 5120 / sr).margin (2.0 * kFrame));
    for (const auto& r : rig.sink.records)
        if (r.event.kind == PlaybackEventKind::NoteOn)
            CHECK (r.event.data1 == 72);
}

TEST_CASE ("PlaybackEngine rate: changing the rate while playing neither skips nor repeats an event", "[playback][engine][rate]")
{
    SongDocument doc;
    auto track = addTrack (doc);
    for (int i = 0; i < 8; ++i)
        addNote (track, 60 + i, 480 * i, 240);   // one note every 0.5 s
    Rig rig (doc);
    rig.transport.play (rig.snapshot->endSeconds());
    rig.render (30);
    rig.engine.setRate (2.0);
    rig.render (100);
    rig.engine.setRate (0.5);
    rig.render (400);

    for (int i = 0; i < 8; ++i)
        CHECK (noteOnFrames (rig.sink, 60 + i).size() == 1);
}

TEST_CASE ("PlaybackEngine rate: the loop wraps on the exact frame at 0.5 and 4.0", "[playback][engine][rate][loop]")
{
    for (const double rate : { 0.5, 4.0 })
    {
        SongDocument doc;
        addLoopSong (doc);
        Rig rig (doc);
        rig.engine.setRate (rate);
        rig.engine.setLoopSpan (LoopSpan { kLoopStart, kLoopEnd });
        rig.transport.seek (kLoopStart);
        rig.transport.play (rig.snapshot->endSeconds());
        rig.render (1);
        const int releases = rig.sink.releaseAllCount;
        rig.render (99);   // 51200 frames in total

        const double loopFrames = (kLoopEnd - kLoopStart) / rate * sr;
        CHECK (rig.sink.releaseAllCount == releases + (int) std::floor (51200.0 / loopFrames));
        CHECK (rig.transport.getPositionSeconds() >= kLoopStart);
        CHECK (rig.transport.getPositionSeconds() < kLoopEnd);
    }
}
```
Add `#include "UI/Playback/RateMapping.h"` and `#include <cmath>` at the top of the test file if absent.

- [ ] **Step 2: Run to verify it fails** — build error: `no member named 'setRate'`.

- [ ] **Step 3: Implement**

`PlaybackEngine.h`: add `#include "UI/Playback/RateMapping.h"`, then next to `setMasterPercent`:
```cpp
    // Playback rate: song seconds per real second (1.0 = as written; pitch is
    // never affected). Clamped to kMinPlaybackRate .. kMaxPlaybackRate. Any
    // thread; the audio thread reads it once per block.
    void setRate (double rateIn) noexcept { rate.store (clampRate (rateIn), std::memory_order_release); }
    double playbackRate() const noexcept { return rate.load (std::memory_order_acquire); }
```
Add the member `std::atomic<double> rate { 1.0 };` beside `master`, and change the `renderSegment` declaration to
`void renderSegment (const PlaybackSnapshot& snapshot, float* left, float* right, int samples, double segPos, double eventsBeforeSeconds, double framesPerSongSecond) noexcept;`

`PlaybackEngine.cpp`, in `renderBlockUnscaled`, after `wasPlaying = true;` add
```cpp
    // Frames per second of SONG time: sampleRate at rate 1.0 (sampleRate / 1.0 is
    // exact), fewer when sped up. The loop step and the event offsets use it.
    const double framesPerSongSecond = sampleRate / playbackRate();
```
then in the loop replace `nextLoopStep (segPos, numSamples - done, sampleRate, loop)` with `nextLoopStep (segPos, numSamples - done, framesPerSongSecond, loop)`, `const double segSeconds = (double) step.samples / sampleRate;` with `... / framesPerSongSecond;`, and pass `framesPerSongSecond` as the new last argument of `renderSegment (...)`. In `renderSegment`, add the parameter and replace `* sampleRate` in the offset line with `* framesPerSongSecond`.

- [ ] **Step 4: Run to verify** — `cmake --build build 2>&1 | tail -5 && ctest --test-dir build -R "PlaybackEngine" --output-on-failure` → the new tests and every pre-existing `PlaybackEngine` test pass (the pre-existing ones prove rate 1.0 is unchanged).

- [ ] **Step 5: Commit**
```bash
git add Source/UI/Playback/PlaybackEngine.* Tests/PlaybackEngine_tests.cpp
git commit -m "feat(playback): engine playback rate scales the song clock without changing pitch

Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>"
```

---

### Task 3: Controller rate and listener hooks

**Files:**
- Modify: `Source/UI/Playback/PlaybackController.h`, `Source/UI/Playback/PlaybackController.cpp`
- Test: `Tests/PlaybackController_tests.cpp` (append)

**Interfaces:**
- Consumes: `PlaybackEngine::setRate`, `clampRate`.
- Produces: `void PlaybackController::setRate (double rate)`, `double PlaybackController::rate() const noexcept`; `Listener::playbackRateChanged()` and `Listener::playbackSnapshotChanged()` (both default no-ops). `playbackSnapshotChanged` is called at the end of `rebuild()`.

- [ ] **Step 1: Write the failing tests** (append; check the file's existing includes/helpers such as `RecordingSink`, `addNote`, `addTrack` come from `PlaybackTestSupport.h`)

```cpp
namespace
{
    struct CountingListener : PlaybackController::Listener
    {
        int rateChanges = 0, snapshotChanges = 0;
        void playbackRateChanged() override { ++rateChanges; }
        void playbackSnapshotChanged() override { ++snapshotChanges; }
    };
}

TEST_CASE ("PlaybackController: setRate clamps, reaches the engine and notifies once", "[playback][controller][rate]")
{
    SongDocument doc;
    RecordingSink sink;
    PlaybackController controller (doc, sink);
    CountingListener listener;
    controller.addListener (&listener);

    CHECK (controller.rate() == 1.0);
    controller.setRate (2.0);
    CHECK (controller.rate() == 2.0);
    CHECK (controller.engine().playbackRate() == 2.0);
    CHECK (listener.rateChanges == 1);
    controller.setRate (99.0);
    CHECK (controller.rate() == kMaxPlaybackRate);
    controller.removeListener (&listener);
}

TEST_CASE ("PlaybackController: the rate survives documentReplaced (session setting)", "[playback][controller][rate]")
{
    SongDocument doc;
    RecordingSink sink;
    PlaybackController controller (doc, sink);
    controller.setRate (0.5);
    controller.documentReplaced();
    CHECK (controller.rate() == 0.5);
    CHECK (controller.engine().playbackRate() == 0.5);
}

TEST_CASE ("PlaybackController: a rebuilt snapshot is announced", "[playback][controller]")
{
    SongDocument doc;
    RecordingSink sink;
    PlaybackController controller (doc, sink);
    CountingListener listener;
    controller.addListener (&listener);
    const int before = listener.snapshotChanges;
    addNote (addTrack (doc), 60, 0, 480);
    controller.flushRebuild();
    CHECK (listener.snapshotChanges > before);
    controller.removeListener (&listener);
}
```

- [ ] **Step 2: Run to verify it fails** — compile error (`rate` / `playbackRateChanged` undeclared).

- [ ] **Step 3: Implement**

`PlaybackController.h`: in `Listener` add `virtual void playbackRateChanged() {}` and `virtual void playbackSnapshotChanged() {}`; include `UI/Playback/RateMapping.h`; beside `setMasterPercent` add
```cpp
    // Session playback rate (1.0 = as written), clamped to 0.25 .. 4.0. Not saved anywhere.
    void setRate (double rate);
    double rate() const noexcept { return rateValue; }
```
and the member `double rateValue = 1.0;` next to `masterLevel`.

`PlaybackController.cpp`:
```cpp
void PlaybackController::setRate (double rateIn)
{
    rateValue = clampRate (rateIn);
    playbackEngine.setRate (rateValue);
    listeners.call ([] (Listener& l) { l.playbackRateChanged(); });
}
```
and at the end of `rebuild()` add `listeners.call ([] (Listener& l) { l.playbackSnapshotChanged(); });`.

- [ ] **Step 4: Run to verify** — `cmake --build build 2>&1 | tail -5 && ctest --test-dir build -R "PlaybackController" --output-on-failure` → pass.

- [ ] **Step 5: Commit**
```bash
git add Source/UI/Playback/PlaybackController.* Tests/PlaybackController_tests.cpp
git commit -m "feat(playback): controller holds the session rate and announces rebuilt snapshots

Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>"
```

---

### Task 4: TransportText (positions, beats, meter, tempo)

**Files:**
- Create: `Source/UI/Playback/TransportText.h`, `Source/UI/Playback/TransportText.cpp`, `Tests/TransportText_tests.cpp`
- Modify: `CMakeLists.txt` (add `Source/UI/Playback/TransportText.cpp`), `Tests/CMakeLists.txt` (add `TransportText_tests.cpp`, `${CMAKE_SOURCE_DIR}/Source/UI/Playback/TransportText.cpp`; `MeterSegments.cpp` is already linked for tests — confirm with grep)

**Interfaces:**
- Consumes: `MeterSegment`, `segmentAt`, `meterSegments` from `UI/MeterSegments.h`.
- Produces (namespace `lotro`):
  - `enum class PositionParseError { Empty, BadFormat, BarOutOfRange, BeatOutOfRange, TickOutOfRange };`
  - `struct ParsedPosition { double tick = 0.0; std::optional<PositionParseError> error; };`
  - `std::string formatPosition (const std::vector<MeterSegment>& segments, double tick);` ("bar.beat.tick")
  - `ParsedPosition parsePosition (const std::string& text, const std::vector<MeterSegment>& segments);` (accepts `5`, `5.3`, `5.3.120`)
  - `double beatsToTicks (const std::vector<MeterSegment>& segments, double startTick, double beats);`
  - `double ticksToBeats (const std::vector<MeterSegment>& segments, double startTick, double endTick);`
  - `std::string formatBeats (double beats);` (at most two decimals, trailing zeros dropped)
  - `std::optional<double> parseBeats (const std::string& text);` (finite, > 0)
  - `std::string formatMeterText (int numerator, int denominator);` ("6/8")
  - `std::optional<std::pair<int, int>> parseMeterText (const std::string& text);` (`n/d`, positive ints, no range check)
  - `std::optional<double> parseTempoText (const std::string& text);` (accepts "♩= 93.75", "=120", "120"; finite and > 0)
  - `const char* describe (PositionParseError);`

- [ ] **Step 1: Write the failing tests**

```cpp
#include "UI/MeterSegments.h"
#include "UI/Playback/TransportText.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace lotro;
using Catch::Approx;

namespace
{
    constexpr int ppq = 480;
    std::vector<MeterSegment> fourFour() { return meterSegments ({ { 0, 4, 4 } }, ppq); }
    // 4/4 for 2 bars (ticks 0..3840), then 6/8 (bars 3+; 240-tick beats, 1440-tick bars).
    std::vector<MeterSegment> mixed() { return meterSegments ({ { 0, 4, 4 }, { 3840, 6, 8 } }, ppq); }
}

TEST_CASE ("TransportText: positions format as bar.beat.tick (1-based bar and beat)", "[playback][text]")
{
    const auto s = fourFour();
    CHECK (formatPosition (s, 0) == "1.1.0");
    CHECK (formatPosition (s, 480) == "1.2.0");
    CHECK (formatPosition (s, 1920) == "2.1.0");
    CHECK (formatPosition (s, 1920 + 480 * 2 + 17) == "2.3.17");
}

TEST_CASE ("TransportText: positions parse, with short forms", "[playback][text]")
{
    const auto s = fourFour();
    CHECK (parsePosition ("1.1.0", s).tick == Approx (0.0));
    CHECK (parsePosition ("2.1.0", s).tick == Approx (1920.0));
    CHECK (parsePosition ("2", s).tick == Approx (1920.0));
    CHECK (parsePosition (" 2.3 ", s).tick == Approx (1920.0 + 960.0));
    CHECK (parsePosition ("2.3.17", s).tick == Approx (1920.0 + 960.0 + 17.0));
    CHECK (! parsePosition ("2.3.17", s).error.has_value());
}

TEST_CASE ("TransportText: bad positions report a typed error", "[playback][text]")
{
    const auto s = fourFour();
    CHECK (parsePosition ("", s).error == PositionParseError::Empty);
    CHECK (parsePosition ("a.b", s).error == PositionParseError::BadFormat);
    CHECK (parsePosition ("1.2.3.4", s).error == PositionParseError::BadFormat);
    CHECK (parsePosition ("1.-1.0", s).error == PositionParseError::BadFormat);
    CHECK (parsePosition ("0.1.0", s).error == PositionParseError::BarOutOfRange);
    CHECK (parsePosition ("1.5.0", s).error == PositionParseError::BeatOutOfRange);
    CHECK (parsePosition ("1.1.480", s).error == PositionParseError::TickOutOfRange);
}

TEST_CASE ("TransportText: positions round-trip across a meter change", "[playback][text]")
{
    const auto s = mixed();
    CHECK (formatPosition (s, 3840) == "3.1.0");
    CHECK (formatPosition (s, 3840 + 240 * 2) == "3.3.0");        // 6/8: 240-tick beats
    CHECK (parsePosition ("3.3.0", s).tick == Approx (3840.0 + 480.0));
    CHECK (parsePosition ("4.1.0", s).tick == Approx (3840.0 + 1440.0));
    CHECK (parsePosition ("3.7.0", s).error == PositionParseError::BeatOutOfRange);   // 6/8 has 6 beats
    for (double tick : { 0.0, 480.0, 3839.0, 3840.0, 4321.0, 7000.0 })
        CHECK (parsePosition (formatPosition (s, tick), s).tick == Approx (tick).margin (0.5));
}

TEST_CASE ("TransportText: beats and ticks convert through meter changes", "[playback][text]")
{
    const auto s = mixed();
    CHECK (beatsToTicks (s, 0.0, 4.0) == Approx (1920.0));
    CHECK (ticksToBeats (s, 0.0, 1920.0) == Approx (4.0));
    // Two beats of 4/4 (960 ticks) up to tick 3840 + 3 beats of 6/8 (3 * 240).
    CHECK (beatsToTicks (s, 3840.0 - 960.0, 5.0) == Approx (3840.0 + 720.0));
    CHECK (ticksToBeats (s, 3840.0 - 960.0, 3840.0 + 720.0) == Approx (5.0));
    CHECK (beatsToTicks (s, 100.0, 0.0) == Approx (100.0));
}

TEST_CASE ("TransportText: beat lengths format and parse", "[playback][text]")
{
    CHECK (formatBeats (88.5) == "88.5");
    CHECK (formatBeats (4.0) == "4");
    CHECK (formatBeats (1.0 / 3.0) == "0.33");
    CHECK (parseBeats ("88.5").value() == Approx (88.5));
    CHECK (! parseBeats ("0").has_value());
    CHECK (! parseBeats ("-2").has_value());
    CHECK (! parseBeats ("abc").has_value());
}

TEST_CASE ("TransportText: meter and tempo text", "[playback][text]")
{
    CHECK (formatMeterText (6, 8) == "6/8");
    CHECK (parseMeterText ("6/8").value() == std::make_pair (6, 8));
    CHECK (parseMeterText (" 3 / 4 ").value() == std::make_pair (3, 4));
    CHECK (! parseMeterText ("6").has_value());
    CHECK (! parseMeterText ("a/b").has_value());
    CHECK (! parseMeterText ("0/4").has_value());

    CHECK (parseTempoText ("120").value() == Approx (120.0));
    CHECK (parseTempoText ("=93.75").value() == Approx (93.75));
    CHECK (parseTempoText ("\xe2\x99\xa9= 93.75").value() == Approx (93.75));
    CHECK (! parseTempoText ("").has_value());
    CHECK (! parseTempoText ("fast").has_value());
    CHECK (! parseTempoText ("0").has_value());
}
```
(`MeterChange` is `{tick, numerator, denominator}` per `Core/Song.h`; check the field order and adjust the braces if it differs.)

- [ ] **Step 2: Run to verify it fails** — missing header.

- [ ] **Step 3: Implement**

`TransportText.h`: declarations exactly as in **Interfaces** above, `#pragma once`, includes `UI/MeterSegments.h`, `<optional>`, `<string>`, `<utility>`, `<vector>`.

`TransportText.cpp`:
```cpp
#include "UI/Playback/TransportText.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>

namespace lotro
{

namespace
{
    std::string trim (const std::string& s)
    {
        const auto first = s.find_first_not_of (" \t");
        if (first == std::string::npos)
            return {};
        return s.substr (first, s.find_last_not_of (" \t") - first + 1);
    }

    // Digits only, 1..9 of them.
    std::optional<long> parseCount (const std::string& s)
    {
        if (s.empty() || s.size() > 9 || s.find_first_not_of ("0123456789") != std::string::npos)
            return std::nullopt;
        return std::strtol (s.c_str(), nullptr, 10);
    }

    std::optional<double> parseNumber (const std::string& text)
    {
        const std::string t = trim (text);
        if (t.empty())
            return std::nullopt;
        char* end = nullptr;
        const double v = std::strtod (t.c_str(), &end);
        if (end != t.c_str() + t.size() || ! std::isfinite (v))
            return std::nullopt;
        return v;
    }

    size_t segmentIndexAt (const std::vector<MeterSegment>& segments, double tick)
    {
        size_t i = 0;
        while (i + 1 < segments.size() && segments[i + 1].startTick <= tick)
            ++i;
        return i;
    }
}

const char* describe (PositionParseError e)
{
    switch (e)
    {
        case PositionParseError::Empty:          return "Enter a position such as 5.1.0";
        case PositionParseError::BadFormat:      return "Use bar.beat.tick, such as 5.1.0";
        case PositionParseError::BarOutOfRange:  return "Bars start at 1";
        case PositionParseError::BeatOutOfRange: return "That beat is outside the bar";
        case PositionParseError::TickOutOfRange: return "That tick is outside the beat";
    }
    return "";
}

std::string formatPosition (const std::vector<MeterSegment>& segments, double tick)
{
    if (segments.empty())
        return "1.1.0";
    const double t = std::floor (std::max (0.0, tick) + 0.5);
    const auto& s = segments[segmentIndexAt (segments, t)];
    const double intoSegment = t - s.startTick;
    const double barsIn = std::floor (intoSegment / s.ticksPerBar);
    const double intoBar = intoSegment - barsIn * s.ticksPerBar;
    const double beatsIn = std::floor (intoBar / s.ticksPerBeat);
    const double tickInBeat = std::floor (intoBar - beatsIn * s.ticksPerBeat);
    char buffer[64];
    std::snprintf (buffer, sizeof buffer, "%lld.%d.%d", s.firstBar + (long long) barsIn + 1, (int) beatsIn + 1, (int) tickInBeat);
    return buffer;
}

ParsedPosition parsePosition (const std::string& text, const std::vector<MeterSegment>& segments)
{
    const std::string t = trim (text);
    if (t.empty())
        return { 0.0, PositionParseError::Empty };

    std::vector<std::string> parts;
    size_t from = 0;
    for (;;)
    {
        const auto dot = t.find ('.', from);
        parts.push_back (t.substr (from, dot == std::string::npos ? std::string::npos : dot - from));
        if (dot == std::string::npos)
            break;
        from = dot + 1;
    }
    if (parts.size() > 3 || segments.empty())
        return { 0.0, PositionParseError::BadFormat };

    const auto bar = parseCount (parts[0]);
    const auto beat = parts.size() > 1 ? parseCount (parts[1]) : std::optional<long> (1);
    const auto tickInBeat = parts.size() > 2 ? parseCount (parts[2]) : std::optional<long> (0);
    if (! bar || ! beat || ! tickInBeat)
        return { 0.0, PositionParseError::BadFormat };
    if (*bar < 1)
        return { 0.0, PositionParseError::BarOutOfRange };

    size_t index = 0;
    while (index + 1 < segments.size() && segments[index + 1].firstBar <= *bar - 1)
        ++index;
    const auto& s = segments[index];
    if (*beat < 1 || *beat > s.numerator)
        return { 0.0, PositionParseError::BeatOutOfRange };
    if ((double) *tickInBeat >= s.ticksPerBeat)
        return { 0.0, PositionParseError::TickOutOfRange };

    const double barsIn = (double) (*bar - 1 - s.firstBar);
    return { s.startTick + barsIn * s.ticksPerBar + (double) (*beat - 1) * s.ticksPerBeat + (double) *tickInBeat, std::nullopt };
}

double beatsToTicks (const std::vector<MeterSegment>& segments, double startTick, double beats)
{
    if (segments.empty() || ! (beats > 0.0))
        return startTick;
    double cur = startTick;
    for (size_t i = segmentIndexAt (segments, cur);; ++i)
    {
        const auto& s = segments[i];
        const bool last = i + 1 == segments.size();
        const double available = last ? std::numeric_limits<double>::infinity()
                                      : (segments[i + 1].startTick - cur) / s.ticksPerBeat;
        if (beats <= available)
            return cur + beats * s.ticksPerBeat;
        beats -= available;
        cur = segments[i + 1].startTick;
    }
}

double ticksToBeats (const std::vector<MeterSegment>& segments, double startTick, double endTick)
{
    if (segments.empty() || ! (endTick > startTick))
        return 0.0;
    double beats = 0.0;
    double cur = startTick;
    for (size_t i = segmentIndexAt (segments, cur); cur < endTick; ++i)
    {
        const auto& s = segments[i];
        const double segEnd = i + 1 == segments.size() ? endTick : std::min (endTick, segments[i + 1].startTick);
        beats += (segEnd - cur) / s.ticksPerBeat;
        cur = segEnd;
    }
    return beats;
}

std::string formatBeats (double beats)
{
    char buffer[48];
    std::snprintf (buffer, sizeof buffer, "%.2f", beats);
    std::string s = buffer;
    if (s.find ('.') != std::string::npos)
    {
        while (s.back() == '0')
            s.pop_back();
        if (s.back() == '.')
            s.pop_back();
    }
    return s;
}

std::optional<double> parseBeats (const std::string& text)
{
    const auto v = parseNumber (text);
    return v && *v > 0.0 ? v : std::nullopt;
}

std::string formatMeterText (int numerator, int denominator)
{
    return std::to_string (numerator) + "/" + std::to_string (denominator);
}

std::optional<std::pair<int, int>> parseMeterText (const std::string& text)
{
    const auto slash = text.find ('/');
    if (slash == std::string::npos)
        return std::nullopt;
    const auto n = parseCount (trim (text.substr (0, slash)));
    const auto d = parseCount (trim (text.substr (slash + 1)));
    if (! n || ! d || *n < 1 || *d < 1)
        return std::nullopt;
    return std::make_pair ((int) *n, (int) *d);
}

std::optional<double> parseTempoText (const std::string& text)
{
    std::string t = trim (text);
    const std::string note = "\xe2\x99\xa9";
    if (t.compare (0, note.size(), note) == 0)
        t = trim (t.substr (note.size()));
    if (! t.empty() && t.front() == '=')
        t = trim (t.substr (1));
    const auto v = parseNumber (t);
    return v && *v > 0.0 ? v : std::nullopt;
}

} // namespace lotro
```
Add `#include <algorithm>` for `std::max`/`std::min`.

- [ ] **Step 4: Run to verify** — `cmake --build build 2>&1 | tail -5 && ctest --test-dir build -R "TransportText" --output-on-failure` → pass. If the `mixed()` bar numbers disagree with `meterSegments`' `firstBar` (2 bars of 4/4 before tick 3840 means `firstBar == 2` for the 6/8 segment), fix the test's expectation, not the maths, after checking `Source/UI/MeterSegments.cpp`.

- [ ] **Step 5: Commit**
```bash
git add Source/UI/Playback/TransportText.* Tests/TransportText_tests.cpp CMakeLists.txt Tests/CMakeLists.txt
git commit -m "feat(playback): TransportText, bar.beat.tick, beat length, meter and tempo text

Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>"
```

---

### Task 5: TransportControls — Selection group and rate knob

**Files:**
- Create: `Source/UI/Playback/TransportControls.h`, `Source/UI/Playback/TransportControls.cpp`, `Tests/TransportControls_tests.cpp`
- Modify: `CMakeLists.txt`, `Tests/CMakeLists.txt` (add the .cpp and the test file)

**Interfaces:**
- Consumes: `PlaybackController` (`getLoopRange`, `setLoopRange`, `clearLoopRange`, `currentSnapshot`, `rate`, `setRate`, `addListener`), `SongDocument::getMeterChanges`, `meterSegments`, everything from `TransportText.h` and `RateMapping.h`.
- Produces:
```cpp
class TransportControls : public juce::Component, private PlaybackController::Listener
{
public:
    static constexpr int preferredWidth = 520;
    TransportControls (PlaybackController&, SongDocument&);
    ~TransportControls() override;
    void resized() override;
    void paint (juce::Graphics&) override;

    // Tests drive the boxes directly: setText (..., dontSendNotification) then onReturnKey().
    juce::TextEditor& startBoxForTesting();
    juce::TextEditor& endBoxForTesting();
    juce::TextEditor& lengthBoxForTesting();
    juce::TextEditor& meterBoxForTesting();     // used from Task 6
    juce::TextEditor& tempoBoxForTesting();     // used from Task 6
    juce::Slider& rateSliderForTesting();
    juce::Label& rateReadoutForTesting();
};
```
In this task the meter and tempo boxes exist (created, laid out, empty and disabled) so the layout is final; Task 6 gives them behaviour.

- [ ] **Step 1: Write the failing tests** (`Tests/TransportControls_tests.cpp`)

```cpp
#include "PlaybackTestSupport.h"
#include "UI/Playback/PlaybackController.h"
#include "UI/Playback/RateMapping.h"
#include "UI/Playback/TransportControls.h"
#include "UI/Playback/TransportText.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace lotro;
using namespace lotro::playbacktest;
using Catch::Approx;

namespace
{
    // 4 bars of 4/4 at 120 BPM: ticks 0..7680 (8 s).
    struct Rig
    {
        juce::ScopedJuceInitialiser_GUI juceInit;
        SongDocument doc;
        RecordingSink sink;
        PlaybackController controller { doc, sink };
        std::unique_ptr<TransportControls> controls;

        Rig()
        {
            addTempo (doc, 0, 120.0);
            addNote (addTrack (doc), 60, 0, 7680);
            controller.flushRebuild();
            controls = std::make_unique<TransportControls> (controller, doc);
            controls->setSize (TransportControls::preferredWidth, 28);
        }

        void type (juce::TextEditor& box, const char* text)
        {
            box.setText (text, juce::dontSendNotification);
            box.onReturnKey();
        }
    };
}

TEST_CASE ("TransportControls: with no loop the Selection boxes are empty", "[playback][controls]")
{
    Rig rig;
    CHECK (rig.controls->startBoxForTesting().getText().isEmpty());
    CHECK (rig.controls->endBoxForTesting().getText().isEmpty());
    CHECK (rig.controls->lengthBoxForTesting().getText().isEmpty());
}

TEST_CASE ("TransportControls: typing a start and an end creates the loop range", "[playback][controls]")
{
    Rig rig;
    rig.type (rig.controls->startBoxForTesting(), "2.1.0");
    CHECK (! rig.controller.getLoopRange().has_value());   // held until both edges are valid
    rig.type (rig.controls->endBoxForTesting(), "3.1.0");

    REQUIRE (rig.controller.getLoopRange().has_value());
    CHECK (rig.controller.getLoopRange()->startTick == Approx (1920.0));
    CHECK (rig.controller.getLoopRange()->endTick == Approx (3840.0));
    CHECK (rig.controls->lengthBoxForTesting().getText() == "4");
}

TEST_CASE ("TransportControls: dragging the loop updates the boxes", "[playback][controls]")
{
    Rig rig;
    rig.controller.setLoopRange (960.0, 2880.0);
    CHECK (rig.controls->startBoxForTesting().getText() == "1.3.0");
    CHECK (rig.controls->endBoxForTesting().getText() == "2.4.0");
    CHECK (rig.controls->lengthBoxForTesting().getText() == "4");
    rig.controller.clearLoopRange();
    CHECK (rig.controls->startBoxForTesting().getText().isEmpty());
}

TEST_CASE ("TransportControls: typing a length moves the end", "[playback][controls]")
{
    Rig rig;
    rig.controller.setLoopRange (1920.0, 3840.0);
    rig.type (rig.controls->lengthBoxForTesting(), "8");
    REQUIRE (rig.controller.getLoopRange().has_value());
    CHECK (rig.controller.getLoopRange()->startTick == Approx (1920.0));
    CHECK (rig.controller.getLoopRange()->endTick == Approx (1920.0 + 8 * 480.0));
    CHECK (rig.controls->endBoxForTesting().getText() == "3.1.0");   // 1920 + 3840 = 5760 -> bar 4 beat 1
}

TEST_CASE ("TransportControls: a reversed start/end is shown ordered", "[playback][controls]")
{
    Rig rig;
    rig.controller.setLoopRange (960.0, 1920.0);
    rig.type (rig.controls->startBoxForTesting(), "3.1.0");   // now after the end (2.1.0)
    REQUIRE (rig.controller.getLoopRange().has_value());
    CHECK (rig.controller.getLoopRange()->startTick < rig.controller.getLoopRange()->endTick);
    CHECK (rig.controls->startBoxForTesting().getText() == formatPosition (meterSegments (rig.doc.getMeterChanges(), 480), rig.controller.getLoopRange()->startTick));
}

TEST_CASE ("TransportControls: an end beyond the song is shown clamped", "[playback][controls]")
{
    Rig rig;
    rig.type (rig.controls->startBoxForTesting(), "2.1.0");
    rig.type (rig.controls->endBoxForTesting(), "99.1.0");
    REQUIRE (rig.controller.getLoopRange().has_value());
    const double songEndTick = rig.controller.currentSnapshot()->tempo().secondsToTicks (rig.controller.currentSnapshot()->endSeconds());
    CHECK (rig.controller.getLoopRange()->endTick <= songEndTick + 1e-6);
    CHECK (rig.controls->endBoxForTesting().getText() != "99.1.0");
}

TEST_CASE ("TransportControls: bad Selection text reverts to the current range", "[playback][controls]")
{
    Rig rig;
    rig.controller.setLoopRange (1920.0, 3840.0);
    rig.type (rig.controls->startBoxForTesting(), "nonsense");
    CHECK (rig.controller.getLoopRange()->startTick == Approx (1920.0));
    CHECK (rig.controls->startBoxForTesting().getText() == "2.1.0");
    rig.type (rig.controls->lengthBoxForTesting(), "-3");
    CHECK (rig.controller.getLoopRange()->endTick == Approx (3840.0));
    CHECK (rig.controls->lengthBoxForTesting().getText() == "4");
}

TEST_CASE ("TransportControls: clearing all three boxes clears the loop", "[playback][controls]")
{
    Rig rig;
    rig.controller.setLoopRange (1920.0, 3840.0);
    for (auto* box : { &rig.controls->startBoxForTesting(), &rig.controls->endBoxForTesting(), &rig.controls->lengthBoxForTesting() })
        box->setText ("", juce::dontSendNotification);
    rig.controls->startBoxForTesting().onReturnKey();
    CHECK (! rig.controller.getLoopRange().has_value());
}

TEST_CASE ("TransportControls: the rate knob drives the controller and shows 1.000 by default", "[playback][controls][rate]")
{
    Rig rig;
    CHECK (rig.controls->rateReadoutForTesting().getText() == "1.000");
    CHECK (rig.controls->rateSliderForTesting().getValue() == Approx (0.0));
    CHECK (rig.controls->rateSliderForTesting().isDoubleClickReturnEnabled());
    CHECK (rig.controls->rateSliderForTesting().getDoubleClickReturnValue() == Approx (0.0));

    rig.controls->rateSliderForTesting().setValue (1.0, juce::sendNotificationSync);   // log2 -> 2.0
    CHECK (rig.controller.rate() == Approx (2.0));
    CHECK (rig.controls->rateReadoutForTesting().getText() == "2.000");
}

TEST_CASE ("TransportControls: typing a rate moves the knob, and a controller change moves the readout", "[playback][controls][rate]")
{
    Rig rig;
    rig.controls->rateReadoutForTesting().setText ("0.5", juce::dontSendNotification);
    rig.controls->rateReadoutForTesting().onTextChange();
    CHECK (rig.controller.rate() == Approx (0.5));
    CHECK (rig.controls->rateSliderForTesting().getValue() == Approx (-1.0));

    rig.controller.setRate (4.0);
    CHECK (rig.controls->rateReadoutForTesting().getText() == "4.000");
    CHECK (rig.controls->rateSliderForTesting().getValue() == Approx (2.0));

    rig.controls->rateReadoutForTesting().setText ("junk", juce::dontSendNotification);
    rig.controls->rateReadoutForTesting().onTextChange();
    CHECK (rig.controls->rateReadoutForTesting().getText() == "4.000");   // reverted
}
```

- [ ] **Step 2: Run to verify it fails** — missing header.

- [ ] **Step 3: Implement**

`TransportControls.h`:
```cpp
#pragma once

#include "UI/Playback/PlaybackController.h"
#include "UI/SongDocument.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace lotro
{

// The right area of the transport strip: the Selection group (loop start / end
// / length), the time signature and tempo at the playhead, and the playback-rate
// knob. Spec: docs/superpowers/specs/2026-10-14-songsmith-transport-controls-design.md.
class TransportControls : public juce::Component, private PlaybackController::Listener
{
public:
    static constexpr int preferredWidth = 520;

    TransportControls (PlaybackController& controllerIn, SongDocument& documentIn);
    ~TransportControls() override;

    void resized() override;
    void paint (juce::Graphics& g) override;

    juce::TextEditor& startBoxForTesting()  { return startBox.editor; }
    juce::TextEditor& endBoxForTesting()    { return endBox.editor; }
    juce::TextEditor& lengthBoxForTesting() { return lengthBox.editor; }
    juce::TextEditor& meterBoxForTesting()  { return meterBox.editor; }
    juce::TextEditor& tempoBoxForTesting()  { return tempoBox.editor; }
    juce::Slider& rateSliderForTesting()    { return rateSlider; }
    juce::Label& rateReadoutForTesting()    { return rateReadout; }

private:
    // A text box plus the text last written into it, so a refresh can tell "the
    // user is typing here" (focused and different from `shown`) from "idle".
    struct Box
    {
        juce::TextEditor editor;
        juce::String shown;
        bool isEditing() const { return editor.hasKeyboardFocus (true) && editor.getText() != shown; }
        void show (const juce::String& text);   // writes text and records it
    };

    enum class Edited { start, end, length };

    void playbackLoopChanged() override { refreshSelection(); }
    void playbackRateChanged() override { refreshRate(); }

    void setupBox (Box& box, const juce::String& tooltip, std::function<void()> onCommit);
    void commitSelection (Edited which);
    void refreshSelection();
    void reject (Box& box, const juce::String& reason);
    void refreshRate();
    void applyReadoutText();

    PlaybackController& controller;
    SongDocument& doc;

    juce::Label selectionLabel;
    Box startBox, endBox, lengthBox, meterBox, tempoBox;
    juce::Slider rateSlider { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::NoTextBox };
    juce::Label rateReadout;
};

} // namespace lotro
```

`TransportControls.cpp` — implement as follows (Task 6 adds the meter/tempo behaviour; here those boxes are `setEnabled (false)`):

```cpp
#include "UI/Playback/TransportControls.h"

#include "UI/MeterSegments.h"
#include "UI/Playback/RateMapping.h"
#include "UI/Playback/TransportText.h"
#include "UI/SongsmithColours.h"

#include <cmath>

namespace lotro
{

namespace
{
    constexpr int labelWidth = 62, positionWidth = 64, lengthWidth = 56, meterWidth = 46, tempoWidth = 78,
                  knobSize = 24, readoutWidth = 52, gap = 6, boxHeight = 20;
}

void TransportControls::Box::show (const juce::String& text)
{
    editor.setText (text, juce::dontSendNotification);
    shown = text;
}

TransportControls::TransportControls (PlaybackController& controllerIn, SongDocument& documentIn)
    : controller (controllerIn), doc (documentIn)
{
    selectionLabel.setText ("Selection", juce::dontSendNotification);
    selectionLabel.setColour (juce::Label::textColourId, juce::Colour (SongsmithColours::textMuted));
    addAndMakeVisible (selectionLabel);

    setupBox (startBox,  "Loop start (bar.beat.tick)", [this] { commitSelection (Edited::start); });
    setupBox (endBox,    "Loop end (bar.beat.tick)",   [this] { commitSelection (Edited::end); });
    setupBox (lengthBox, "Loop length in beats",       [this] { commitSelection (Edited::length); });
    setupBox (meterBox,  "Time signature at the playhead", [] {});
    setupBox (tempoBox,  "Tempo at the playhead",          [] {});
    meterBox.editor.setEnabled (false);
    tempoBox.editor.setEnabled (false);

    rateSlider.setRange (-2.0, 2.0, 0.001);
    rateSlider.setValue (rateToSlider (controller.rate()), juce::dontSendNotification);
    rateSlider.setDoubleClickReturnValue (true, 0.0);
    rateSlider.setWantsKeyboardFocus (false);
    rateSlider.setTooltip ("Playback rate (double-click to reset to 1.000)");
    rateSlider.onValueChange = [this] { controller.setRate (sliderToRate (rateSlider.getValue())); };
    addAndMakeVisible (rateSlider);

    rateReadout.setEditable (false, true);   // double-click to type
    rateReadout.setJustificationType (juce::Justification::centredLeft);
    rateReadout.setColour (juce::Label::textColourId, juce::Colour (SongsmithColours::text));
    rateReadout.onTextChange = [this] { applyReadoutText(); };
    addAndMakeVisible (rateReadout);

    controller.addListener (this);
    refreshSelection();
    refreshRate();
}

TransportControls::~TransportControls() { controller.removeListener (this); }

void TransportControls::setupBox (Box& box, const juce::String& tooltip, std::function<void()> onCommit)
{
    box.editor.setMultiLine (false);
    box.editor.setTooltip (tooltip);
    box.editor.setColour (juce::TextEditor::backgroundColourId, juce::Colour (SongsmithColours::panelHeader));
    box.editor.setColour (juce::TextEditor::textColourId, juce::Colour (SongsmithColours::text));
    box.editor.setColour (juce::TextEditor::outlineColourId, juce::Colour (SongsmithColours::border));
    box.editor.onReturnKey = onCommit;
    box.editor.onFocusLost = onCommit;
    addAndMakeVisible (box.editor);
}

void TransportControls::paint (juce::Graphics& g)
{
    // The Selection group's frame: label + three boxes.
    g.setColour (juce::Colour (SongsmithColours::trackDivider));
    g.drawRoundedRectangle (selectionLabel.getBounds().getX() - 3.0f, 1.0f,
                            (float) (lengthBox.editor.getRight() - selectionLabel.getX() + 6), (float) getHeight() - 2.0f, 3.0f, 1.0f);
}

void TransportControls::resized()
{
    auto area = getLocalBounds().reduced (0, (getHeight() - boxHeight) / 2);
    auto place = [&] (juce::Component& c, int width) { c.setBounds (area.removeFromLeft (width)); area.removeFromLeft (gap); };
    area.removeFromLeft (4);
    place (selectionLabel, labelWidth);
    place (startBox.editor, positionWidth);
    place (endBox.editor, positionWidth);
    place (lengthBox.editor, lengthWidth);
    area.removeFromLeft (gap);
    place (meterBox.editor, meterWidth);
    place (tempoBox.editor, tempoWidth);
    rateSlider.setBounds (area.removeFromLeft (knobSize).withSizeKeepingCentre (knobSize, getHeight() - 4));
    area.removeFromLeft (gap);
    rateReadout.setBounds (area.removeFromLeft (readoutWidth));
}

void TransportControls::reject (Box& box, const juce::String& reason)
{
    box.editor.setTooltip (reason);
    box.editor.setColour (juce::TextEditor::outlineColourId, juce::Colour (SongsmithColours::outOfRangeBorder));
    box.editor.repaint();
    juce::Component::SafePointer<juce::TextEditor> safe (&box.editor);
    juce::Timer::callAfterDelay (1500, [safe]
    {
        if (safe != nullptr)
        {
            safe->setColour (juce::TextEditor::outlineColourId, juce::Colour (SongsmithColours::border));
            safe->repaint();
        }
    });
}

void TransportControls::refreshSelection()
{
    const auto snapshot = controller.currentSnapshot();
    const auto range = controller.getLoopRange();
    const bool enabled = snapshot != nullptr;
    for (auto* box : { &startBox, &endBox, &lengthBox })
        box->editor.setEnabled (enabled);
    if (! range.has_value() || ! enabled)
    {
        for (auto* box : { &startBox, &endBox, &lengthBox })
            if (! box->isEditing())
                box->show ({});
        return;
    }
    const auto segments = meterSegments (doc.getMeterChanges(), snapshot->tempo().getTicksPerQuarter());
    if (! startBox.isEditing())  startBox.show (formatPosition (segments, range->startTick));
    if (! endBox.isEditing())    endBox.show (formatPosition (segments, range->endTick));
    if (! lengthBox.isEditing()) lengthBox.show (formatBeats (ticksToBeats (segments, range->startTick, range->endTick)));
}

void TransportControls::commitSelection (Edited which)
{
    const auto snapshot = controller.currentSnapshot();
    if (snapshot == nullptr)
        return;
    const auto segments = meterSegments (doc.getMeterChanges(), snapshot->tempo().getTicksPerQuarter());
    auto text = [] (Box& b) { return b.editor.getText().toStdString(); };

    // Nothing typed anywhere: clear the loop.
    if (startBox.editor.getText().isEmpty() && endBox.editor.getText().isEmpty() && lengthBox.editor.getText().isEmpty())
    {
        controller.clearLoopRange();
        refreshSelection();
        return;
    }

    // Not edited yet (focus passing through a box): nothing to do.
    Box& edited = which == Edited::start ? startBox : which == Edited::end ? endBox : lengthBox;
    if (edited.editor.getText() == edited.shown)
        return;

    std::optional<double> start, end;
    if (startBox.editor.getText().isNotEmpty())
    {
        const auto p = parsePosition (text (startBox), segments);
        if (p.error) { reject (startBox, describe (*p.error)); edited.shown = {}; refreshSelection(); return; }
        start = p.tick;
    }
    if (which == Edited::length)
    {
        const auto beats = parseBeats (text (lengthBox));
        if (! beats || ! start) { reject (lengthBox, "Enter a length in beats, with a start"); lengthBox.shown = {}; refreshSelection(); return; }
        end = beatsToTicks (segments, *start, *beats);
    }
    else if (endBox.editor.getText().isNotEmpty())
    {
        const auto p = parsePosition (text (endBox), segments);
        if (p.error) { reject (endBox, describe (*p.error)); edited.shown = {}; refreshSelection(); return; }
        end = p.tick;
    }

    if (! start || ! end)
        return;   // an edge is still missing: hold what was typed

    if (! controller.setLoopRange (*start, *end))
        reject (edited, "That range is too short or outside the song");
    startBox.shown = startBox.editor.getText();   // idle again, so the refresh below rewrites all three
    endBox.shown = endBox.editor.getText();
    lengthBox.shown = lengthBox.editor.getText();
    refreshSelection();
}

void TransportControls::refreshRate()
{
    rateSlider.setValue (rateToSlider (controller.rate()), juce::dontSendNotification);
    rateReadout.setText (formatRate (controller.rate()), juce::dontSendNotification);
}

void TransportControls::applyReadoutText()
{
    if (const auto r = parseRate (rateReadout.getText().toStdString()))
        controller.setRate (*r);
    refreshRate();   // also reverts junk to the current rate
}

} // namespace lotro
```
Notes for the implementer: when `controller.setRate` is called from `rateSlider.onValueChange`, `playbackRateChanged` → `refreshRate` sets the slider with `dontSendNotification`, so there is no feedback loop, and the readout text updates. `Edited` and the `edited.shown = {}` lines make a rejected box count as "idle" so `refreshSelection` restores the true value.

- [ ] **Step 4: Run to verify** — add the sources/tests to both CMake files, `cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug && cmake --build build 2>&1 | tail -20 && ctest --test-dir build -R "TransportControls" --output-on-failure` → pass. Fix compile details against the real headers (e.g. `Colour` constants available in `SongsmithColours.h`; the colour used for rejection may be any existing constant).

- [ ] **Step 5: Commit**
```bash
git add Source/UI/Playback/TransportControls.* Tests/TransportControls_tests.cpp CMakeLists.txt Tests/CMakeLists.txt
git commit -m "feat(ui): TransportControls with the loop Selection group and the rate knob

Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>"
```

---

### Task 6: TransportControls — meter and tempo at the playhead

**Files:**
- Modify: `Source/UI/Playback/TransportControls.h`, `Source/UI/Playback/TransportControls.cpp`
- Test: `Tests/TransportControls_tests.cpp` (append)

**Interfaces:**
- Consumes: `setMeter`, `setTempo`, `TempoEditError`, `listTempoMeterEvents`, `bpmAt`, `formatExactTempoText` (`UI/TempoEdit.h`); `segmentAt`; `PlaybackController::getPositionTicks`, `seekToTick`, `Listener::playbackPositionChanged` / `playbackSnapshotChanged`.
- Produces: private `refreshTiming()`, `commitMeter()`, `commitTempo()`, `describe (TempoEditError)` (file-local); `playbackPositionChanged` and `playbackSnapshotChanged` overrides.

- [ ] **Step 1: Write the failing tests** (append)

```cpp
TEST_CASE ("TransportControls: the meter and tempo boxes show the values at the playhead", "[playback][controls][timing]")
{
    Rig rig;
    CHECK (rig.controls->meterBoxForTesting().getText() == "4/4");
    CHECK (rig.controls->tempoBoxForTesting().getText().contains ("120"));
    CHECK (rig.controls->meterBoxForTesting().isEnabled());
}

TEST_CASE ("TransportControls: typing a meter sets it at the nearest bar to the playhead, as one undo step", "[playback][controls][timing]")
{
    Rig rig;
    rig.controller.seekToTick (1920.0);   // bar 2
    rig.doc.getUndoManager().clearUndoHistory();
    rig.type (rig.controls->meterBoxForTesting(), "6/8");
    rig.controller.flushRebuild();

    const auto events = listTempoMeterEvents (rig.doc);
    bool found = false;
    for (const auto& e : events)
        if (e.kind == TempoMeterKind::meter && e.tick == 1920 && e.numerator == 6 && e.denominator == 8)
            found = true;
    CHECK (found);
    CHECK (rig.controls->meterBoxForTesting().getText() == "6/8");

    // Undo reverts the box (Review Focus 5).
    rig.doc.getUndoManager().undo();
    rig.controller.flushRebuild();
    CHECK (rig.controls->meterBoxForTesting().getText() == "4/4");
}

TEST_CASE ("TransportControls: typing a tempo sets it at the playhead and undo restores the box", "[playback][controls][timing]")
{
    Rig rig;
    rig.controller.seekToTick (1920.0);
    rig.doc.getUndoManager().clearUndoHistory();
    rig.type (rig.controls->tempoBoxForTesting(), "\xe2\x99\xa9=90");
    rig.controller.flushRebuild();

    CHECK (bpmAt (listTempoMeterEvents (rig.doc), 1920) == Approx (90.0).margin (0.01));
    CHECK (bpmAt (listTempoMeterEvents (rig.doc), 0) == Approx (120.0).margin (0.01));
    CHECK (rig.controls->tempoBoxForTesting().getText().contains ("90"));

    rig.doc.getUndoManager().undo();
    rig.controller.flushRebuild();
    CHECK (rig.controls->tempoBoxForTesting().getText().contains ("120"));
}

TEST_CASE ("TransportControls: the boxes follow the playhead across a tempo change", "[playback][controls][timing]")
{
    Rig rig;
    rig.controller.seekToTick (1920.0);
    rig.type (rig.controls->tempoBoxForTesting(), "60");
    rig.controller.flushRebuild();
    rig.controller.seekToTick (0.0);
    CHECK (rig.controls->tempoBoxForTesting().getText().contains ("120"));
    rig.controller.seekToTick (3000.0);
    CHECK (rig.controls->tempoBoxForTesting().getText().contains ("60"));
}

TEST_CASE ("TransportControls: a rejected meter or tempo reverts and leaves the document alone", "[playback][controls][timing]")
{
    Rig rig;
    rig.doc.getUndoManager().clearUndoHistory();
    rig.type (rig.controls->meterBoxForTesting(), "40/4");   // numerator above 32
    CHECK (rig.controls->meterBoxForTesting().getText() == "4/4");
    rig.type (rig.controls->meterBoxForTesting(), "banana");
    CHECK (rig.controls->meterBoxForTesting().getText() == "4/4");
    rig.type (rig.controls->tempoBoxForTesting(), "5000");   // above maxBpm
    CHECK (rig.controls->tempoBoxForTesting().getText().contains ("120"));
    CHECK (! rig.doc.getUndoManager().canUndo());
}

TEST_CASE ("TransportControls: typing is not overwritten while the playhead moves", "[playback][controls][timing]")
{
    Rig rig;
    rig.controls->addToDesktop (0);   // needs a peer for keyboard focus
    rig.controls->setVisible (true);
    rig.controls->meterBoxForTesting().grabKeyboardFocus();
    rig.controls->meterBoxForTesting().setText ("6", juce::dontSendNotification);   // typed, not committed
    rig.controller.seekToTick (480.0);                                              // refreshTiming runs
    if (rig.controls->meterBoxForTesting().hasKeyboardFocus (true))
        CHECK (rig.controls->meterBoxForTesting().getText() == "6");
    rig.controls->removeFromDesktop();
}

TEST_CASE ("TransportControls: with no song the boxes are disabled and typing does nothing", "[playback][controls][timing]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SongDocument doc;
    RecordingSink sink;
    PlaybackController controller (doc, sink);
    TransportControls controls (controller, doc);
    controls.setSize (TransportControls::preferredWidth, 28);

    CHECK (! controls.meterBoxForTesting().isEnabled());
    CHECK (! controls.tempoBoxForTesting().isEnabled());
    CHECK (! controls.startBoxForTesting().isEnabled());
    controls.meterBoxForTesting().setText ("6/8", juce::dontSendNotification);
    controls.meterBoxForTesting().onReturnKey();   // must not throw
    CHECK (controller.getLoopRange() == std::nullopt);
}
```
(Include `UI/TempoEdit.h` and `<memory>` in the test file.)

- [ ] **Step 2: Run to verify it fails** — the meter/tempo tests fail (boxes are empty and disabled). If the "no song" test's `SongDocument` has a snapshot with no time base, adjust the condition in Step 3 to the right "no time base" test (the snapshot is non-null for an empty document; use `controller.currentSnapshot() == nullptr || ! (controller.currentSnapshot()->endSeconds() > 0.0)` as "nothing to play" — the same test `PlaybackController::play` uses — and apply it to `refreshSelection` as well).

- [ ] **Step 3: Implement**

Header: add overrides `void playbackPositionChanged() override { refreshTiming(); }` and `void playbackSnapshotChanged() override { refreshSelection(); refreshTiming(); }`, and private `void refreshTiming(); void commitMeter(); void commitTempo(); bool hasSong() const;`.

Source additions:
```cpp
namespace
{
    juce::String describe (TempoEditError e)
    {
        switch (e)
        {
            case TempoEditError::NoTimeBase:        return "Open a MIDI file first";
            case TempoEditError::NegativeTick:      return "That position is before the start";
            case TempoEditError::BpmOutOfRange:     return "Tempo must be between " + juce::String (minBpm(), 2) + " and " + juce::String (maxBpm);
            case TempoEditError::MeterOutOfRange:   return "Numerator 1-32; denominator 1, 2, 4, 8, 16 or 32";
            case TempoEditError::TickOccupied:      return "There is already an event at that position";
            case TempoEditError::NoSuchEvent:       return "No such event";
            case TempoEditError::CannotRemoveFirst: return "The first event cannot be removed";
        }
        return {};
    }
}

bool TransportControls::hasSong() const
{
    const auto snapshot = controller.currentSnapshot();
    return snapshot != nullptr && snapshot->endSeconds() > 0.0;
}

void TransportControls::refreshTiming()
{
    const bool enabled = hasSong();
    meterBox.editor.setEnabled (enabled);
    tempoBox.editor.setEnabled (enabled);
    if (! enabled)
    {
        meterBox.show ({});
        tempoBox.show ({});
        return;
    }
    const auto snapshot = controller.currentSnapshot();
    const double tick = controller.getPositionTicks();
    const auto segments = meterSegments (doc.getMeterChanges(), snapshot->tempo().getTicksPerQuarter());
    if (! meterBox.isEditing())
    {
        const auto& s = segmentAt (segments, tick);
        meterBox.show (formatMeterText (s.numerator, s.denominator));
    }
    if (! tempoBox.isEditing())
        tempoBox.show (juce::String::fromUTF8 (formatExactTempoText (bpmAt (listTempoMeterEvents (doc), (int) std::llround (tick))).c_str()));
}

void TransportControls::commitMeter()
{
    if (! hasSong() || meterBox.editor.getText() == meterBox.shown)
        return;
    const auto parsed = parseMeterText (meterBox.editor.getText().toStdString());
    if (! parsed)
    {
        reject (meterBox, "Use n/d, such as 6/8");
    }
    else if (const auto error = setMeter (doc, (int) std::llround (controller.getPositionTicks()), parsed->first, parsed->second))
    {
        reject (meterBox, describe (*error));
    }
    meterBox.shown = meterBox.editor.getText();   // idle: the refresh writes the true value
    refreshTiming();
}

void TransportControls::commitTempo()
{
    if (! hasSong() || tempoBox.editor.getText() == tempoBox.shown)
        return;
    const auto bpm = parseTempoText (tempoBox.editor.getText().toStdString());
    if (! bpm)
    {
        reject (tempoBox, "Enter a tempo, such as 120");
    }
    else if (const auto error = setTempo (doc, (int) std::llround (controller.getPositionTicks()), *bpm))
    {
        reject (tempoBox, describe (*error));
    }
    tempoBox.shown = tempoBox.editor.getText();
    refreshTiming();
}
```
Wire: in the constructor replace the two `[] {}` commit lambdas with `[this] { commitMeter(); }` / `[this] { commitTempo(); }`; remove the two `setEnabled (false)` lines; call `refreshTiming()` after `refreshSelection()` in the constructor. In `refreshSelection` use `hasSong()` for `enabled` (see Step 2 note). `#include "UI/TempoEdit.h"` and `<cmath>`.

After a successful edit the rebuild is asynchronous; `playbackSnapshotChanged` calls `refreshTiming()` once it lands, and the tests call `flushRebuild()` to force it. `meterBox.shown` is set to the typed text so the box is "idle" at that point; `refreshTiming` then rewrites it.

- [ ] **Step 4: Run to verify** — `cmake --build build 2>&1 | tail -20 && ctest --test-dir build -R "TransportControls" --output-on-failure` → all pass. If the undo test shows a stale box, confirm `UndoManager::undo()` triggers the document's tempo-map sync and then `rebuild()` (the controller's ValueTree listener schedules it; `flushRebuild()` runs it). If undo does not notify, call `controller.flushRebuild()` is not enough and `PlaybackController::isPlaybackRelevant` must be checked against the conductor's EVENT nodes.

- [ ] **Step 5: Commit**
```bash
git add Source/UI/Playback/TransportControls.* Tests/TransportControls_tests.cpp
git commit -m "feat(ui): transport meter and tempo boxes edit at the playhead

Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>"
```

---

### Task 7: Host the controls in the TransportStrip

**Files:**
- Modify: `Source/UI/Playback/TransportStrip.h`, `Source/UI/Playback/TransportStrip.cpp`, `Source/UI/SongsmithMainComponent.cpp:101`, `Source/UI/TrackEditorPanel.h` (`Content` ctor), `Source/UI/TrackEditorPanel.cpp:35` and wherever `Content` is constructed
- Test: `Tests/TransportStrip_tests.cpp` (change the six `TransportStrip strip (controller);` lines to `TransportStrip strip (controller, doc);` and append new tests)

**Interfaces:**
- Consumes: `TransportControls (PlaybackController&, SongDocument&)`.
- Produces: `TransportStrip (PlaybackController&, SongDocument&)`; `TransportControls& controlsForTesting()`.

- [ ] **Step 1: Write the failing tests** (append to `Tests/TransportStrip_tests.cpp`, after switching the existing constructions to the new signature)

```cpp
TEST_CASE ("TransportStrip: the right area holds the transport controls and they follow its bounds", "[playback][strip]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SongDocument doc;
    RecordingSink sink;
    PlaybackController controller (doc, sink);
    TransportStrip strip (controller, doc);
    strip.setSize (1400, TransportStrip::height);

    auto& controls = strip.controlsForTesting();
    CHECK (controls.getParentComponent() == &strip.rightAreaForTesting());
    CHECK (controls.getBounds() == strip.rightAreaForTesting().getLocalBounds());
    strip.dragDividerForTesting (1, 1000);
    CHECK (controls.getBounds() == strip.rightAreaForTesting().getLocalBounds());
}

TEST_CASE ("TransportStrip: a narrow right area clips the controls without touching the buttons", "[playback][strip]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SongDocument doc;
    RecordingSink sink;
    PlaybackController controller (doc, sink);
    TransportStrip strip (controller, doc);
    strip.setSize (500, TransportStrip::height);

    CHECK (strip.rightAreaForTesting().getX() >= strip.centreAreaForTesting().getRight());
    CHECK (strip.rightAreaForTesting().getRight() <= strip.getWidth());
}
```

- [ ] **Step 2: Run to verify it fails** — compile error (`TransportStrip` has no two-argument constructor).

- [ ] **Step 3: Implement**

`TransportStrip.h`: `#include "UI/Playback/TransportControls.h"` and `"UI/SongDocument.h"`; constructor `TransportStrip (PlaybackController& controllerIn, SongDocument& documentIn);`; add `TransportControls& controlsForTesting() { return controls; }` and the member `TransportControls controls;` declared after `rightArea` (so it is destroyed first). Update the class comment: the right area now holds the transport controls; the left one is still empty.

`TransportStrip.cpp`: change the constructor signature and initialiser (`controls (controllerIn, documentIn)`, after `rightArea` in declaration order — keep the initialiser list in member order), delete the `// TODO: the left and right areas...` line (replace with `// The left area is an empty placeholder (TODO.md item 4: favourite commands).`), and after `centreArea.onBoundsChanged = ...` add
```cpp
    rightArea.addAndMakeVisible (controls);
    rightArea.onBoundsChanged = [this] { controls.setBounds (rightArea.getLocalBounds()); };
```
Update the construction sites:
- `SongsmithMainComponent.cpp:101`: `std::make_unique<TransportStrip> (*playbackIn, document)`.
- `TrackEditorPanel`: `Content (PianoRollComponent&, PlaybackController&)` gains a `SongDocument&` parameter and initialises `strip (controller, document)`; pass the panel's `doc` where `Content` is created (`grep -n "Content>" Source/UI/TrackEditorPanel.cpp`).
- Grep `rg "TransportStrip (" Tests Source` and `rg "make_unique<TransportStrip>"` for any other site.

- [ ] **Step 4: Run the whole suite** — `cmake --build build 2>&1 | tail -20 && ctest --test-dir build -j1 --output-on-failure 2>&1 | tail -15`. Expected: every test passes (previous 1383 plus the new ones). The layout tests that assert y offsets under the strip must be unchanged because the strip height is unchanged.

- [ ] **Step 5: Commit**
```bash
git add Source Tests
git commit -m "feat(ui): the transport strip's right area hosts the transport controls

Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>"
```

---

### Task 8: Docs

**Files:**
- Modify: `docs/ARCHITECTURE.md` (§9.14 playback: rate; §9.18 or a new subsection: `TransportControls`), `docs/UI_GUIDE.md` (the transport strip entry `#14`: right area contents), `docs/TESTING.md` (new test count and the new files), `docs/TODO.md` (transport right area done; left area still open), `CLAUDE.md` (status line: add "transport controls (loop selection, meter/tempo at the playhead, playback rate)"), `docs/superpowers/specs/2026-10-14-songsmith-transport-controls-design.md` (set status to "Implemented"; note the `TransportText` file name and the unchanged-engine-object remark from the plan's Global Constraints)

- [ ] **Step 1:** Read each target section first (`grep -n "9.14\|9.18" docs/ARCHITECTURE.md`; `grep -n "#14" docs/UI_GUIDE.md`), then edit: describe the clock scaling (`framesPerSongSecond`), the `Listener` additions, `TransportControls` and its refresh rules (focused-and-different box is never overwritten), and the session-only rate.
- [ ] **Step 2:** Get the final test count with `ctest --test-dir build -j1 2>&1 | tail -3` and update `docs/TESTING.md`.
- [ ] **Step 3: Commit**
```bash
git add docs CLAUDE.md
git commit -m "docs: transport controls (selection, meter/tempo at the playhead, playback rate)

Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>"
```

---

## Self-review notes

- **Spec coverage:** layout and groups (Tasks 5-7); loop-range selection incl. length, clear, revert, ordering, clamping (Task 5); meter/tempo at the playhead with undo and typed errors (Task 6); rate knob 0.25-4.0, log scale, snap, double-click reset, readout, session-only (Tasks 1, 3, 5); engine clock scaling with bit-identical rate 1.0 and wrap maths (Task 2); strip hosting and clipping (Task 7); out-of-scope items untouched; both open review points (beat unit, Enter / focus-lost commit) implemented as written in the spec.
- **Type consistency:** `setRate` / `playbackRate` (engine), `setRate` / `rate` (controller), `playbackRateChanged`, `playbackSnapshotChanged`, `formatPosition` / `parsePosition` / `beatsToTicks` / `ticksToBeats` / `formatBeats` / `parseBeats` / `formatMeterText` / `parseMeterText` / `parseTempoText`, `TransportControls (PlaybackController&, SongDocument&)`, and the `...ForTesting` accessors are used with the same names in every task.
- **Known judgement calls for the executor:** `MeterChange` field order and the `firstBar` numbering in the `mixed()` fixture (Task 4); whether `UndoManager::undo()` triggers a snapshot rebuild (Task 6 Step 4 note); the colour constant used for the rejected-box outline (any existing `SongsmithColours` value).
