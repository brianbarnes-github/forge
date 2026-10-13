# Songsmith Loop Range Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Select a time range by left-dragging in a track's instrument band; while a range exists, playback loops it (sample-accurate wrap), with edge handles, a shaded overlay and Esc / handle-click to clear.

**Architecture:** The range is session-only transport state held in ticks on `PlaybackController` and published to the engine as a seconds pair through a tiny seqlock cell. `PlaybackEngine::renderBlockUnscaled` splits a block at the loop end, re-chases at the loop start (the existing `chase`) and finishes the block from there, committing the final position with one CAS (`Transport::advanceTo`). UI: `TrackRowComponent` forwards band drags, `TimelineRuler` draws/edits handles, `LoopRangeOverlay` shades the span. `Source/Core/` is untouched.

**Tech Stack:** C++17, JUCE, Catch2, CMake/Ninja. No new dependencies.

**Spec:** `docs/superpowers/specs/2026-10-12-songsmith-loop-range-design.md`

## Global Constraints

- `Source/Core/` is not touched (skill `forge-engine-ui-boundary`); everything lives under `Source/UI/`.
- The range is audition-only: session-only, not in the Song, not undoable, never exported, cleared by `documentReplaced`.
- Range is global time, stored in **ticks** on `PlaybackController`; edges satisfy `startTick < endTick`; minimum length `0.05` seconds (`kMinLoopSeconds`) so a degenerate loop can never thrash the audio thread.
- Range always loops: there is no toggle. No range = playback exactly as today.
- Audio thread: no allocation, no lock, no throw. At most `kMaxWrapsPerBlock = 4` wraps per block.
- Left-drag creates the range only in the **instrument band of a non-conductor row** (`TrackRowComponent::inInstrumentBand`); the right-click instrument menu is unchanged. Conductor row: no range gesture.
- Snap: both edges snap to the current grid ticks; Alt = unsnapped; grid `Off` (0 ticks) = unsnapped. A zero-length or sub-minimum drag creates nothing and leaves an existing range alone.
- Custom error types, no `any`; add types everywhere; functional style where practical. No new third-party dependencies.
- Commit messages: conventional commits, ending with `Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>`.
- Never push without explicit approval; never launch the GUI; run ctest serially (`ctest --test-dir build -j1`). ctest names do not carry Catch tags: use `-R <TestFileBaseName>`-style patterns (e.g. `-R LoopRange`) or `ctest -N` to find names. A failed `cmake --build` followed by `ctest` runs the STALE binary: after every build run `cmake --build build 2>&1 | grep -i error` and expect no output.
- New `.cpp` files must be added to BOTH `CMakeLists.txt` (forge_ui) and `Tests/CMakeLists.txt` (forge_tests source list; test files go in its test list). New `.h` files need no entry.
- `-Wmissing-field-initializers` is on: give every field of aggregates explicitly.
- C++17: no `std::erase_if`.

## Review Focus

- A loop shorter than a block (`kMinLoopSeconds` = 0.05 s is ~2400 frames at 48 kHz, so test with a small block size too) wraps correctly and never more than `kMaxWrapsPerBlock` times (Task 2).
- Playhead at or past the loop end when Play/seek happens never wraps; playhead before the range plays into it and then loops (Task 2).
- A message-thread seek during a wrap block wins (the CAS drop path) and forces a re-chase (Task 2).
- Tempo edits move the loop edges with the music (ticks → seconds republished on snapshot swap), and `documentReplaced` / zero-length Songs never leave a stale range (Task 3).
- Mute/solo/pan/master are unchanged across a wrap; notes held across the wrap are released (Task 2).
- A band drag on the conductor row, a right-click, and a sub-grid drag create nothing; the instrument menu and track selection still work (Task 5).
- Esc must not steal the note editor's gesture-cancel (Task 6).

---

## File Structure

| File | Responsibility |
|---|---|
| `Source/UI/Playback/LoopRange.{h,cpp}` | `LoopRange` (ticks), `normaliseLoopRange`, `snapLoopTick`, `LoopSpan` (seconds), `LoopSpanCell` (seqlock), `nextLoopStep` |
| `Source/UI/Playback/Transport.{h,cpp}` | `advanceTo` (CAS commit to an arbitrary position) |
| `Source/UI/Playback/PlaybackEngine.{h,cpp}` | loop span cell, block splitting + wrap |
| `Source/UI/Playback/PlaybackController.{h,cpp}` | `setLoopRange` / `clearLoopRange` / `getLoopRange`, `Listener::playbackLoopChanged`, play() rule, republish |
| `Source/UI/Playback/PlayheadOverlay.{h,cpp}` | `LoopRangeOverlay` |
| `Source/UI/Playback/TimelineRuler.{h,cpp}` | range shading, edge handles, handle drag / click-to-clear |
| `Source/UI/TrackRowComponent.{h,cpp}` | band left-drag → `onLoopRangeDragged` |
| `Source/UI/TrackListComponent.{h,cpp}`, `PianoRollComponent.{h,cpp}`, `TrackEditorPanel.cpp`, `SongsmithMainComponent.{h,cpp}` | wiring, snap source, overlays, Esc |
| `Tests/LoopRange_tests.cpp`, `Tests/PlaybackEngine_tests.cpp`, `Tests/PlaybackController_tests.cpp`, `Tests/PlayheadOverlay_tests.cpp`, `Tests/TrackRowComponent_tests.cpp`, `Tests/TrackListComponent_tests.cpp` | tests |

---

### Task 1: Pure loop helpers and the seqlock cell

**Files:**
- Create: `Source/UI/Playback/LoopRange.h`, `Source/UI/Playback/LoopRange.cpp`
- Test: `Tests/LoopRange_tests.cpp`
- Modify: `CMakeLists.txt` (add `Source/UI/Playback/LoopRange.cpp` next to `Playback/Transport.cpp`), `Tests/CMakeLists.txt` (source list: same `.cpp`; test list: `LoopRange_tests.cpp`)

**Interfaces:**
- Produces (`namespace lotro`):

```cpp
struct LoopRange { double startTick = 0.0; double endTick = 0.0; };   // startTick < endTick

constexpr double kMinLoopSeconds = 0.05;
constexpr int    kMaxWrapsPerBlock = 4;

// Orders the edges, clamps to [0, endTickLimit], and rejects a range shorter
// than minTicks (returns nullopt). minTicks > 0.
std::optional<LoopRange> normaliseLoopRange (double a, double b, double endTickLimit, double minTicks);

// Nearest multiple of gridTicks (>= 0). gridTicks <= 0 or unsnapped = the tick unchanged.
double snapLoopTick (double tick, int gridTicks, bool unsnapped) noexcept;

// The range in seconds. start < end, both >= 0.
struct LoopSpan { double startSeconds = 0.0; double endSeconds = 0.0; };

// Single-writer (message thread), single-reader (audio thread) cell. read() never blocks:
// it returns the last consistent value it saw (nullopt = no loop).
class LoopSpanCell
{
public:
    void publish (std::optional<LoopSpan> span) noexcept;   // message thread
    std::optional<LoopSpan> read() noexcept;                // audio thread
private:
    std::atomic<unsigned> version { 0 };
    std::atomic<double> start { 0.0 }, end { 0.0 };
    std::atomic<bool> active { false };
    std::optional<LoopSpan> lastGood;   // audio-thread-only cache
};

struct LoopStep { int samples = 0; bool wraps = false; };

// How many of `remainingSamples` to render before either the block ends or the
// playhead reaches loopEnd (then wraps = true). pos >= loopEnd, or no loop, means
// render everything with no wrap. samples is always >= 1 when remainingSamples >= 1.
LoopStep nextLoopStep (double positionSeconds, int remainingSamples, double sampleRate,
                       std::optional<LoopSpan> loop) noexcept;
```

- [ ] **Step 1: Write the failing tests** (`Tests/LoopRange_tests.cpp`, `#include "UI/Playback/LoopRange.h"`, Catch2, `using namespace lotro`):

```cpp
TEST_CASE ("normaliseLoopRange orders, clamps and rejects short ranges", "[loop]")
{
    auto r = normaliseLoopRange (960.0, 480.0, 4800.0, 24.0);
    REQUIRE (r.has_value());
    CHECK (r->startTick == 480.0);
    CHECK (r->endTick == 960.0);

    r = normaliseLoopRange (-100.0, 99999.0, 4800.0, 24.0);
    REQUIRE (r.has_value());
    CHECK (r->startTick == 0.0);
    CHECK (r->endTick == 4800.0);

    CHECK (! normaliseLoopRange (480.0, 480.0, 4800.0, 24.0).has_value());   // zero length
    CHECK (! normaliseLoopRange (480.0, 490.0, 4800.0, 24.0).has_value());   // shorter than minTicks
    CHECK (! normaliseLoopRange (5000.0, 6000.0, 4800.0, 24.0).has_value()); // both past the end -> empty after clamp
}

TEST_CASE ("snapLoopTick snaps to the nearest grid line unless unsnapped or grid off", "[loop]")
{
    CHECK (snapLoopTick (130.0, 120, false) == 120.0);
    CHECK (snapLoopTick (181.0, 120, false) == 240.0);
    CHECK (snapLoopTick (130.0, 120, true) == 130.0);   // Alt
    CHECK (snapLoopTick (130.0, 0, false) == 130.0);    // grid Off
}

TEST_CASE ("nextLoopStep splits the block at the loop end", "[loop]")
{
    const LoopSpan loop { 1.0, 2.0 };
    // Not crossing: whole block.
    auto s = nextLoopStep (1.2, 512, 48000.0, loop);
    CHECK (s.samples == 512);
    CHECK (! s.wraps);
    // Crossing: 10 ms (480 samples) before the end.
    s = nextLoopStep (1.99, 1000, 48000.0, loop);
    CHECK (s.samples == 480);
    CHECK (s.wraps);
    // At/after the end never wraps.
    s = nextLoopStep (2.0, 512, 48000.0, loop);
    CHECK (s.samples == 512);
    CHECK (! s.wraps);
    s = nextLoopStep (5.0, 512, 48000.0, loop);
    CHECK (! s.wraps);
    // Before the range: plays toward it, no wrap yet.
    s = nextLoopStep (0.5, 512, 48000.0, loop);
    CHECK (! s.wraps);
    // No loop.
    s = nextLoopStep (1.99, 1000, 48000.0, std::nullopt);
    CHECK (s.samples == 1000);
    CHECK (! s.wraps);
    // Progress is guaranteed: even a position a hair before the end renders >= 1 sample.
    s = nextLoopStep (1.9999999, 512, 48000.0, loop);
    CHECK (s.samples >= 1);
    CHECK (s.wraps);
}

TEST_CASE ("LoopSpanCell round-trips and clears", "[loop]")
{
    LoopSpanCell cell;
    CHECK (! cell.read().has_value());
    cell.publish (LoopSpan { 1.0, 3.0 });
    auto v = cell.read();
    REQUIRE (v.has_value());
    CHECK (v->startSeconds == 1.0);
    CHECK (v->endSeconds == 3.0);
    cell.publish (std::nullopt);
    CHECK (! cell.read().has_value());
}
```

- [ ] **Step 2:** `cmake --build build 2>&1 | grep -i error` (expect compile errors: header missing), then `ctest --test-dir build -R LoopRange --output-on-failure`. Expected: FAIL.
- [ ] **Step 3: Implement** `LoopRange.h/.cpp` as the interface block above. Details:

```cpp
std::optional<LoopRange> normaliseLoopRange (double a, double b, double endTickLimit, double minTicks)
{
    double lo = std::min (a, b), hi = std::max (a, b);
    lo = std::clamp (lo, 0.0, endTickLimit);
    hi = std::clamp (hi, 0.0, endTickLimit);
    if (! (hi - lo >= minTicks))
        return std::nullopt;
    return LoopRange { lo, hi };
}

double snapLoopTick (double tick, int gridTicks, bool unsnapped) noexcept
{
    if (unsnapped || gridTicks <= 0)
        return tick;
    return std::round (tick / (double) gridTicks) * (double) gridTicks;
}

LoopStep nextLoopStep (double pos, int remaining, double sr, std::optional<LoopSpan> loop) noexcept
{
    if (! loop.has_value() || pos >= loop->endSeconds || remaining < 1)
        return { remaining, false };
    const double blockSeconds = (double) remaining / sr;
    if (pos + blockSeconds < loop->endSeconds)
        return { remaining, false };
    const int toEnd = (int) std::ceil ((loop->endSeconds - pos) * sr);
    return { std::clamp (toEnd, 1, remaining), true };
}
```

`LoopSpanCell::publish` (seqlock writer): `version.fetch_add(1, release)` (now odd) → store `active`, `start`, `end` (relaxed) → `version.fetch_add(1, release)` (even). `read()` (audio thread): up to 3 attempts: `v1 = version.load(acquire)`; if odd retry; read `active`, `start`, `end`; `v2 = version.load(acquire)`; if `v1 == v2` then set `lastGood` (nullopt when inactive) and return it; after 3 failed attempts return `lastGood`.

- [ ] **Step 4:** build (grep for errors), `ctest --test-dir build -R LoopRange --output-on-failure`. Expected: PASS.
- [ ] **Step 5: Commit**

```bash
git add Source/UI/Playback/LoopRange.h Source/UI/Playback/LoopRange.cpp Tests/LoopRange_tests.cpp CMakeLists.txt Tests/CMakeLists.txt
git commit -m "feat(playback): pure loop-range helpers and a lock-free loop span cell

Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>"
```

---

### Task 2: Engine — sample-accurate wrap

**Files:**
- Modify: `Source/UI/Playback/Transport.{h,cpp}`, `Source/UI/Playback/PlaybackEngine.{h,cpp}`
- Test: `Tests/PlaybackEngine_tests.cpp`, `Tests/TransportTests` — find the existing Transport test file with `ls Tests | grep -i transport` (add `advanceTo` cases there, or into `PlaybackEngine_tests.cpp` if none exists)

**Interfaces:**
- Consumes: Task 1 (`LoopSpan`, `LoopSpanCell`, `nextLoopStep`, `kMaxWrapsPerBlock`).
- Produces:
  - `bool Transport::advanceTo (double fromSeconds, double toSeconds, double endSeconds) noexcept` — CAS from `fromSeconds` to `min(toSeconds, endSeconds)`; clears `playing` when the result reaches `endSeconds`; returns false (touching nothing) when the position no longer equals `fromSeconds`. `advance(from, seconds, end)` becomes `advanceTo(from, from + seconds, end)`.
  - `void PlaybackEngine::setLoopSpan (std::optional<LoopSpan> span) noexcept` (any thread; publishes to the cell).

Engine behaviour (`renderBlockUnscaled`, after the existing chase / pan setup, replacing the single event loop and the final advance):

1. Read `const auto loop = loopCell.read();` once per block.
2. Render the block as segments: `double segPos = position; int done = 0; int wraps = 0;` and loop while `done < numSamples`:
   - `auto step = nextLoopStep (segPos, numSamples - done, sampleRate, wraps < kMaxWrapsPerBlock ? loop : std::nullopt);`
   - `renderSegment (*snapshot, left + done, right + done, step.samples, segPos);` — this is the existing `while (nextEvent < events.size() && events[nextEvent].seconds < ...)` body, with `position`/`blockSeconds` replaced by `segPos`/`step.samples / sampleRate`, rendering `sink.render` for the gaps and the tail of the segment (it already does both).
   - `done += step.samples;` and if `step.wraps`: `sink.releaseAll(); chase (*snapshot, loop->startSeconds); nextEvent = snapshot->firstEventAtOrAfter (loop->startSeconds); segPos = loop->startSeconds; ++wraps;` else `segPos += step.samples / sampleRate`.
3. Commit: `const bool advanced = transport.advanceTo (position, segPos, endSeconds);` `expectedPosition = advanced ? std::min (segPos, endSeconds) : -1.0;` (so a message-thread seek during the block, which makes the CAS fail, forces a re-chase next block exactly as today).
4. The end-of-Song rule is unchanged (`advanceTo` clears `playing` at `endSeconds`). A loop whose `endSeconds` exceeds the Song's end simply never wraps (the Song ends first); `setLoopRange` in Task 3 already clamps to the Song.

- [ ] **Step 1: Write failing tests** in `Tests/PlaybackEngine_tests.cpp` using the existing `Rig` (sr and block size 512 are the file's constants; for the short-loop test construct a second rig with a smaller block, or call `rig.engine.renderBlock (rig.buffer, 0, 64)` directly):

```cpp
TEST_CASE ("PlaybackEngine loop: a block crossing the loop end wraps sample-accurately", "[playback][engine][loop]")
{
    SongDocument doc;
    auto t = addTrack (doc);
    addEvent (t, 0, { 0xC0, 9 });
    addNote (t, 60, 0, 9600);            // long note: song is ~10 s at 120 BPM
    addNote (t, 64, 480, 240);           // note at 0.5 s (480 ticks)
    Rig rig (doc);
    // Loop 0.4 s .. 0.6 s  (ticks 384 .. 576 at 120 BPM / 480 PPQ: 1 tick = 1/960 s)
    rig.engine.setLoopSpan (LoopSpan { 0.4, 0.6 });
    rig.transport.seek (0.4);
    rig.transport.play (rig.snapshot->endSeconds());
    rig.render (60);                      // 60 * 512 frames = 0.64 s of audio at 48 kHz
    // The playhead never leaves the loop and the note at 0.5 s fires more than once.
    CHECK (rig.transport.getPositionSeconds() >= 0.4);
    CHECK (rig.transport.getPositionSeconds() < 0.6);
    int noteOns = 0;
    for (const auto& r : rig.sink.records)
        if (r.event.kind == PlaybackEventKind::NoteOn && r.event.data1 == 64) ++noteOns;
    CHECK (noteOns >= 2);
    CHECK (rig.sink.releaseAllCount >= 2);   // at least one wrap chase released voices
}
```

(Adjust `sr`/tick-time constants to the file's own `sr` and tempo; read the neighbouring tests first.) Also:

```cpp
TEST_CASE ("PlaybackEngine loop: a playhead at or past the loop end never wraps", "[playback][engine][loop]")
{   // loop 0.4..0.6, seek to 0.7, play, render: position keeps increasing past 0.7, releaseAllCount unchanged after the first chase }

TEST_CASE ("PlaybackEngine loop: a playhead before the range plays into it and then loops", "[playback][engine][loop]")
{   // loop 0.4..0.6, start at 0.0, render 0.8 s worth: the position ends inside [0.4, 0.6) }

TEST_CASE ("PlaybackEngine loop: a loop shorter than a block wraps at most kMaxWrapsPerBlock times and stays in range", "[playback][engine][loop]")
{   // loop 0.40..0.45 (2400 frames at 48k) rendered with 9600-frame blocks via renderBlock(buffer,0,9600) on a 2x9600 buffer:
    // after one block the position is inside [0.40, 0.45) and records show no more than kMaxWrapsPerBlock+1 chases (count resets) }

TEST_CASE ("PlaybackEngine loop: clearing the loop plays on past the old end", "[playback][engine][loop]")
{   // setLoopSpan(nullopt) mid-play: the position passes 0.6 }

TEST_CASE ("PlaybackEngine loop: a message-thread seek during the wrap block wins and re-chases", "[playback][engine][loop]")
{   // After rendering to a wrap, seek(0.55): next render starts from 0.55 (position > 0.55, releaseAll count +1) }

TEST_CASE ("Transport::advanceTo commits only from the expected position and stops at the end", "[playback][transport]")
{   // advanceTo(0.0, 0.5, 2.0) true -> position 0.5; advanceTo(0.0, 0.7, 2.0) false (position unchanged 0.5);
    // advanceTo(0.5, 3.0, 2.0) true -> position 2.0 and playing false (after play) }
```

Write these out fully (no `//` placeholders remain in the committed tests): copy a neighbouring test's setup, use `rig.transport.getPositionSeconds()`, `rig.sink.records`, `rig.sink.releaseAllCount`.

- [ ] **Step 2:** build (grep for errors → expect compile errors for `setLoopSpan` / `advanceTo`), `ctest --test-dir build -R "PlaybackEngine|Transport" --output-on-failure`. Expected: FAIL.
- [ ] **Step 3: Implement** `Transport::advanceTo`, rewrite `advance` as a one-line wrapper, add `LoopSpanCell loopCell;` and `setLoopSpan` to the engine, extract `renderSegment` (private, audio thread, takes `const PlaybackSnapshot&`, `float* left`, `float* right`, `int samples`, `double segPos`) from the existing event loop, and restructure `renderBlockUnscaled` per the behaviour list. Keep `applyPanChanges`, `releaseNewlyMutedTracks` and the initial chase exactly where they are.
- [ ] **Step 4:** build and `ctest --test-dir build -j1 2>&1 | tail -5`. Expected: all pass (existing count + new).
- [ ] **Step 5: Commit** `feat(playback): sample-accurate loop wrap in the engine` (with trailer).

---

### Task 3: `PlaybackController` loop range

**Files:**
- Modify: `Source/UI/Playback/PlaybackController.{h,cpp}`
- Test: `Tests/PlaybackController_tests.cpp`

**Interfaces:**
- Consumes: Task 1 (`LoopRange`, `normaliseLoopRange`, `kMinLoopSeconds`), Task 2 (`PlaybackEngine::setLoopSpan`).
- Produces: `void setLoopRange (double startTick, double endTick)` (normalises; ignored when `snapshot == nullptr` or `endSeconds() <= 0`; a result shorter than `kMinLoopSeconds` converted to ticks, or fully outside the Song, leaves the current range unchanged); `void clearLoopRange()`; `std::optional<LoopRange> getLoopRange() const`; `Listener::playbackLoopChanged()` (default no-op; fires once per actual change); `play()` rule below; `documentReplaced` clears the range.

Behaviour:
- `setLoopRange`: `flushRebuild()`; convert the minimum length with the snapshot tempo map around `startTick` (`minTicks = secondsToTicks (ticksToSeconds (lo) + kMinLoopSeconds) - lo`, computed after ordering the pair); clamp the end to `secondsToTicks (snapshot->endSeconds())`; call `normaliseLoopRange (a, b, endLimit, minTicks)`; if it returns a range different from the stored one, store, `publishLoopSpan()`, and call `playbackLoopChanged`.
- `publishLoopSpan()` (private): `playbackEngine.setLoopSpan (range ? LoopSpan { ticksToSeconds (start), ticksToSeconds (end) } : nullopt)` using the current snapshot's tempo; called from `setLoopRange`, `clearLoopRange`, `rebuild()` (so a tempo edit or snapshot swap re-derives the seconds), and `documentReplaced`.
- `play()`: when a loop range exists: if the playhead is inside `[loopStartSeconds, loopEndSeconds)` do not seek (resume / continue from the playhead); otherwise `transport.seek (loopStartSeconds)`. The start marker is ignored while a range exists (the marker value is untouched). With no range, `play()` is unchanged.
- `rebuild()` clamping: if the rebuilt Song is shorter than the stored range's end, publish `end = min(end, endSeconds)`; if that makes the span shorter than `kMinLoopSeconds`, publish `nullopt` but keep the stored ticks (the range comes back if the Song grows again).

- [ ] **Step 1: Write failing tests** in `Tests/PlaybackController_tests.cpp` (copy the file's existing `Rig` pattern at line ~16 and its marker tests for how a controller + doc + `RecordingSink` is built):

```cpp
TEST_CASE ("PlaybackController: setLoopRange stores an ordered range and notifies once", "[playback][controller][loop]") { /* 960,480 -> {480,960}; Counter listener playbackLoopChanged == 1; setting the same range again -> still 1 */ }
TEST_CASE ("PlaybackController: setLoopRange is ignored with nothing to play and when too short", "[playback][controller][loop]") { /* empty doc -> nullopt; 480..485 -> nullopt (shorter than kMinLoopSeconds); an existing range is kept when a too-short one is offered */ }
TEST_CASE ("PlaybackController: the range is clamped to the end of the Song", "[playback][controller][loop]") { /* end past the Song -> end tick = Song end */ }
TEST_CASE ("PlaybackController: play() starts at the range start unless the playhead is inside it", "[playback][controller][loop]") { /* range 480..960 ticks; position 0 -> play seeks to 0.5 s; position inside -> unchanged; a marker set elsewhere is ignored then applies again after clearLoopRange() */ }
TEST_CASE ("PlaybackController: a tempo edit moves the loop edges with the music", "[playback][controller][loop]") { /* range in ticks stays; the span published to the engine (observe via playing a short render and checking the wrap position, or add a test-only accessor engine.loopSpanForTesting() returning loopCell.read()) changes seconds after a tempo change */ }
TEST_CASE ("PlaybackController: documentReplaced clears the range", "[playback][controller][loop]") { /* ... plus playbackLoopChanged fired */ }
```

Write each body out in full against the file's helpers; add `std::optional<LoopSpan> PlaybackEngine::loopSpanForTesting() { return loopCell.read(); }` (documented test accessor) if you need to observe the published span.

- [ ] **Step 2:** build (grep errors) and `ctest --test-dir build -R PlaybackController --output-on-failure`. Expected: FAIL.
- [ ] **Step 3: Implement** as described; add `std::optional<LoopRange> loopRange;` beside `markerTick`.
- [ ] **Step 4:** build and `ctest --test-dir build -j1 2>&1 | tail -5`. Expected: all pass.
- [ ] **Step 5: Commit** `feat(playback): controller-held loop range with tempo-following span` (with trailer).

---

### Task 4: `LoopRangeOverlay` and ruler handles

**Files:**
- Modify: `Source/UI/Playback/PlayheadOverlay.{h,cpp}` (add `LoopRangeOverlay` beside `MarkerOverlay`), `Source/UI/Playback/TimelineRuler.{h,cpp}`
- Test: `Tests/PlayheadOverlay_tests.cpp` (use its `mouseAt` helper), plus ruler cases in the same file (it already tests `TimelineRuler`)

**Interfaces:**
- Consumes: Task 3 (`getLoopRange`, `playbackLoopChanged`).
- Produces:
  - `class LoopRangeOverlay : public juce::Component, private PlaybackController::Listener` with `LoopRangeOverlay (PlaybackController&, std::function<int (double tick)> tickToX)`; fully mouse-transparent (`setInterceptsMouseClicks (false, false)`); `paint` fills `[xStart, xEnd)` clipped to the component with `SongsmithColours::accentAmber.withAlpha (0.14f)` and draws 1 px edge lines at alpha 0.6; `std::optional<std::pair<int,int>> currentXs() const` for tests; `playbackLoopChanged()` repaints.
  - `TimelineRuler`: `void setLoopRange (std::function<std::optional<LoopRange>()>)`; callbacks `std::function<void (bool isStart, double tick)> onLoopEdgeDragged;` and `std::function<void()> onClearLoop;`; `std::optional<juce::Rectangle<int>> loopHandleBounds (bool isStart) const` (a 8 x 12 px bracket at `xForTick (edge)` in the top row; start bracket opens right, end bracket opens left); drawing of the shaded span over both rows and the brackets; interaction: a left press on a bracket sets `dragEdge` (start/end) and `pressedOnLoopHandle = true` (so `applyPointer` must not set the marker); a drag then calls `onLoopEdgeDragged (isStart, tickForX (e.x))`; `mouseUp` with total drag distance < 3 px calls `onClearLoop`; the marker triangle keeps priority if the two overlap.

- [ ] **Step 1: Write failing tests:** overlay paints nothing without a range and shades `[x(start), x(end))` with a range (render to an `juce::Image` via `paintEntireComponent`, check a pixel inside differs from one outside, in the style of the existing marker overlay test); overlay is mouse-transparent; ruler `loopHandleBounds` is nullopt without a range and positioned at the edge x with one; a left press on the start bracket + drag 40 px calls `onLoopEdgeDragged (true, tickForX(x))` and does NOT call `onSetMarker`; a press + release without movement calls `onClearLoop`; a press elsewhere still sets the marker; a right press still seeks.
- [ ] **Step 2:** build (grep errors), `ctest --test-dir build -R PlayheadOverlay --output-on-failure`. Expected: FAIL.
- [ ] **Step 3: Implement** both. `LoopRangeOverlay` mirrors `MarkerOverlay` (constructor adds itself as a controller listener, destructor removes it). The ruler reads the range through its new `std::function` exactly like `marker`.
- [ ] **Step 4:** build and `ctest --test-dir build -R "PlayheadOverlay|TimelineRuler" --output-on-failure`. Expected: PASS.
- [ ] **Step 5: Commit** `feat(ui): loop range overlay and ruler edge handles` (with trailer).

---

### Task 5: Band drag in `TrackRowComponent` and the track-list wiring

**Files:**
- Modify: `Source/UI/TrackRowComponent.{h,cpp}`, `Source/UI/TrackListComponent.{h,cpp}`
- Test: `Tests/TrackRowComponent_tests.cpp`, `Tests/TrackListComponent_tests.cpp`

**Interfaces:**
- Consumes: Tasks 1, 3, 4 (`snapLoopTick`, `setLoopRange`, `getLoopRange`, `LoopRangeOverlay`, ruler loop API).
- Produces:
  - `TrackRowComponent`: `std::function<void (double tickAtX, bool finished, bool unsnapped)> onLoopRangeDragged;` plus `std::function<double (int x)> tickForLocalX;` set by the list (row-local x → tick, from the row's `TimelineViewState`; the row already holds `viewState`). Behaviour: `mouseDown` with the left button (not popup) in `inInstrumentBand` on a non-conductor row still calls `onTrackSelected`, and additionally records `bandDragActive = true` and the press tick; `mouseDrag` while `bandDragActive` and `getDistanceFromDragStart() >= 4` calls `onLoopRangeDragged (tick, false, e.mods.isAltDown())` and **returns before** the drag-to-part logic; `mouseUp` ends it with `finished = true`. Conductor row, popup presses and presses outside the band never start it.
  - `TrackListComponent`: `std::function<int()> loopSnapTicks;` (the main component supplies the current grid ticks; unset = 0 = unsnapped); the row callback handler builds the range: anchor = snapped press tick, other edge = snapped current tick (`snapLoopTick (tick, loopSnapTicks ? loopSnapTicks() : 0, unsnapped)`), calls `playback->setLoopRange (anchor, current)` on each drag step (so the shading follows the pointer); the controller ignores a too-short result, which leaves any previous range in place until the drag is long enough. A press that starts inside an existing range (band x between its edges) instead moves the range: remember `rangeLength` and the grab offset, and call `setLoopRange (snapped(newStart), snapped(newStart) + rangeLength)`.
  - The list creates a `LoopRangeOverlay` next to `markerOverlay` (same bounds rule at lines ~105 and ~953), wires the ruler's `setLoopRange` / `onLoopEdgeDragged` (snapped the same way; moving an edge past the other flips the pair via the controller's normalisation) / `onClearLoop` (`playback->clearLoopRange()`), and repaints the overlay and ruler in `refreshOverlay` (line ~126).

- [ ] **Step 1: Write failing tests:**
  - `TrackRowComponent_tests`: a left press in the band + drag 30 px fires `onLoopRangeDragged` and does not start a drag-and-drop; release fires `finished = true`; a press in the notes area, a right-click in the band (menu still builds), the conductor row's band, and a press in the head never fire it; `onTrackSelected` still fires on the band press.
  - `TrackListComponent_tests`: dragging in a track band sets `playback->getLoopRange()` (with a grid of 120 ticks the edges are multiples of 120; with Alt held they are not); a sub-grid drag leaves no range; dragging inside an existing range moves it keeping its length; dragging a ruler handle changes one edge; clicking a handle clears; the overlay exists only while a controller is set.
- [ ] **Step 2:** build (grep errors), `ctest --test-dir build -R "TrackRowComponent|TrackListComponent" --output-on-failure`. Expected: FAIL.
- [ ] **Step 3: Implement** per the interfaces. Keep the row's existing selection and menu behaviour byte-for-byte; the new branch is added after the popup-menu branches in `mouseDown` and at the top of `mouseDrag`.
- [ ] **Step 4:** build and `ctest --test-dir build -j1 2>&1 | tail -5`. Expected: all pass.
- [ ] **Step 5: Commit** `feat(ui): drag a loop range in a track's instrument band` (with trailer).

---

### Task 6: Editor-roll wiring, snap source and Esc

**Files:**
- Modify: `Source/UI/PianoRollComponent.{h,cpp}` (add a `LoopRangeOverlay` beside `markerOverlay`, lines ~83 and ~98), `Source/UI/TrackEditorPanel.cpp` (ruler wiring beside lines ~42-49), `Source/UI/SongsmithMainComponent.{h,cpp}` (supply `trackList.loopSnapTicks`; Esc), `Source/UI/MainWindow.cpp` only if Esc needs a command (see below)
- Test: `Tests/SongsmithMainComponent_tests.cpp`, `Tests/PianoRollComponent_tests.cpp` (or whichever file already has `hasMarkerOverlayForTesting` tests: `grep -rn hasMarkerOverlayForTesting Tests`)

**Interfaces:**
- Consumes: Tasks 4-5.
- Produces: `SongsmithMainComponent::currentLoopSnapTicks() const` — the active editor's grid ticks when `isTrackEditorLoaded()` (`trackEditor->…getGridTicks()`), else `gridSizeToTicks (defaultGridSize, ticksPerQuarter)` with `ticksPerQuarter` read as in `setActiveEditorGridSize` (line ~248); `trackList.loopSnapTicks = [this] { return currentLoopSnapTicks(); };`. The roll's overlay is created/destroyed with the controller exactly as `markerOverlay`; the editor panel's ruler gets `setLoopRange`, `onLoopEdgeDragged` (snapped with `snapLoopTick (tick, roll.getGridTicks(), false)`) and `onClearLoop`.
- Esc: `keyPressed (Esc)` in `SongsmithMainComponent` clears the loop range **only if** no note-editor gesture is in progress and no text editor has focus. Settle the exact check against `SourceRollEditor`'s existing Esc handling (read `grep -n "Escape" Source/UI/*.cpp` first); if the editor already consumes Esc whenever it has an active selection/gesture, the main component only receives it otherwise, which is the desired precedence. State the decision in the commit body.

- [ ] **Step 1: Write failing tests:** `currentLoopSnapTicks()` equals the default grid ticks with no editor and the editor's grid when one is loaded and its grid changed; the roll creates a loop overlay with a controller and none without; the editor ruler handle drag changes an edge; Esc on the main component clears a range (and does nothing when there is none); Esc during an active note-editor gesture does not clear it.
- [ ] **Step 2:** build (grep errors), run the affected suites with `ctest --test-dir build -R "SongsmithMain|PianoRoll|TrackEditor" --output-on-failure`. Expected: FAIL.
- [ ] **Step 3: Implement** as described.
- [ ] **Step 4:** `ctest --test-dir build -j1 2>&1 | tail -5`. Expected: all pass.
- [ ] **Step 5: Commit** `feat(ui): loop range in the editor roll, grid snapping and Esc to clear` (with trailer).

---

### Task 7: Docs

**Files:**
- Modify: `docs/ARCHITECTURE.md` (§9.14: loop range, `LoopSpanCell`, engine wrap, `Transport::advanceTo`, controller rules; §9.10 or wherever the band/row gestures are documented: band left-drag), `docs/UI_GUIDE.md` (new entry for the loop range: gesture, handles, Esc), `docs/TESTING.md` (count from `ctest --test-dir build -N | tail -1`), `docs/songsmith-ui-map.html` (legend row, same style as the Mixer's #34 entry), `CLAUDE.md` (status line + docs-map row for the spec and plan), `docs/TODO.md` (tick item 3), `docs/superpowers/specs/2026-10-12-songsmith-loop-range-design.md` (status "implemented"; fix the Play wording: with a range, Play seeks to the range start only when the playhead is outside it; resume/continue otherwise; resolve the open questions to what was built).
- [ ] **Step 1:** make the edits, describing behaviour as built (read the final code first; do not paste plan wording).
- [ ] **Step 2:** `ctest --test-dir build -j1 2>&1 | tail -3`; confirm the count in `docs/TESTING.md` matches.
- [ ] **Step 3: Commit** `docs: describe the loop range` (with trailer).

---

## Manual verification (user, on the deployed exe; not automatable)

- Left-drag in a track's instrument band draws a shaded range across all tracks; edges snap to the grid, Alt drags freely.
- Play loops the range seamlessly (no click or drift on a steady-tempo song); dragging edge handles in the ruler adjusts it live while playing.
- Clicking a handle or pressing Esc clears the range and playback runs on to the end.
- The right-click instrument menu, track selection and the start marker still behave as before.
- Editing notes or changing tempo while looping does not break the loop.

## Self-review notes

- Spec coverage: create/snap/adjust/clear (Tasks 4-6), always-loops and Play rule (Task 3), wrap and seek cases (Task 2), scope/session-only (Task 3 `documentReplaced`), pure helpers (Task 1), overlay and ruler (Task 4), docs (Task 7). The spec's optional Transport-menu "Clear Loop Range" is deliberately not built (Esc and the handle click suffice; `allCommandIds` stays 34).
- Two deliberate refinements of the spec, fixed in Task 7: Play seeks to the range start only when the playhead is outside the range; the band drag also auto-selects the row as today (no change).
- Names used consistently across tasks: `LoopRange`, `LoopSpan`, `LoopSpanCell`, `nextLoopStep`, `kMinLoopSeconds`, `kMaxWrapsPerBlock`, `Transport::advanceTo`, `PlaybackEngine::setLoopSpan`, `PlaybackController::setLoopRange` / `clearLoopRange` / `getLoopRange`, `Listener::playbackLoopChanged`, `LoopRangeOverlay`, `TimelineRuler::setLoopRange` / `onLoopEdgeDragged` / `onClearLoop`, `TrackRowComponent::onLoopRangeDragged`, `TrackListComponent::loopSnapTicks`.
