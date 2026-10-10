# Songsmith Track Head Redesign Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the 180 px track head with a 200 px two-row head: colour-picker swatch, right-click rename, drawn mute/solo icons, and a saved per-track playback volume slider; drop the note-count line.

**Architecture:** A new `TrackHeadComponent` (child of `TrackRowComponent`, mouse-transparent itself, so the row keeps selection and drag-to-part) owns the swatch, name, icon buttons, slider and the rename editor. It reports edits through callbacks; `TrackListComponent` (which owns the `SongDocument`) performs the undoable writes. Volume is a `MIDI_TRACK` property `playbackVolume`, copied into an atomic per-track gain on `PlaybackSnapshot` and applied by `PlaybackEngine` as a `NoteOn` velocity multiplier. Property changes to `name`, `colorArgb` and `playbackVolume` refresh the head in place instead of rebuilding every row (a rebuild would destroy the slider / picker / editor mid-gesture).

**Tech Stack:** C++20, JUCE (`Slider`, `ColourSelector`, `CallOutBox`, `PopupMenu`, `TextEditor`, `Path`), Catch2, ValueTree/UndoManager. No new dependencies.

**Spec:** `docs/superpowers/specs/2026-10-10-songsmith-track-head-design.md`

## Spec amendments made by this plan

Task 8 writes these into the spec so spec and code agree:

1. **No CC7/CC11 scaling.** Scaling `NoteOn` velocity alone is enough (controllers cannot undo a velocity multiplier, which was the spec's reason for scaling them) and it keeps live slider changes fully effective; scaling CCs would go stale mid-playback. Gain applies to *notes that start after* the change; sounding notes finish at their old level.
2. **Callbacks, not a `SongDocument` handle.** `TrackRowComponent`'s constructor (used by ~40 tests) stays `(ValueTree, int, const TimelineViewState&)`. The head reports edits via callbacks wired in `TrackListComponent`, the same pattern as `onSetInstrumentRequested`.
3. **Row-height risk is smaller than stated.** The info column spans the row *plus* the 16 px instrument band, so at `minRowHeight` 30 each head row is about 22 px; `minRowHeight` does not change.

## Global Constraints

- Head width `trackInfoWidth` = 200 (was 180); the 2 px column divider stays inside it; the note preview still starts at `trackInfoWidth`.
- Property `SongIDs::playbackVolume` (int 0–100); absent reads as 100; not written when 100. Not `volumePercent` (that is the ASSIGNMENT LOTRO offset).
- Core/UI boundary: everything in `Source/UI/`; `Source/Core/` untouched.
- One undo transaction per gesture (`SongDocument::setProperty(..., newTransaction)`); no write when the value is unchanged.
- Conductor row: no swatch picker, no rename, no M/S, no slider.
- Muted / solo-silenced rows stay dimmed (`setAlpha(0.5f)`, done by the row).
- Mute/solo remain session-only; the volume never reaches MIDI/ABC export.
- Conventional commits; commit trailer `Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>`. Never push.
- Build/test: `cmake --build build && ctest --test-dir build -R <name> --output-on-failure` (`forge_tests`). Do not launch the GUI.
- New `.cpp` files must be added to BOTH `CMakeLists.txt` (near `Source/UI/TrackRowComponent.cpp`, line ~151) and `Tests/CMakeLists.txt` (near line 115); new test files to `Tests/CMakeLists.txt` (list near line 71).

## Review Focus

Inputs the spec implies but the task tests do not otherwise cover; each is pinned in the owning task:

1. A `.songsmith` file saved before this feature (no `playbackVolume`) must load and play at full volume (Task 1).
2. A volume of 0 must silence new notes, but a note already sounding must still receive its `NoteOff` (Task 2).
3. Dragging the slider / dragging in the colour picker must not rebuild the row mid-gesture (Task 7).
4. Undo/redo of volume, colour and name must update the visible head (Task 7).
5. A very long or blank track name: long names ellipsize without overlapping the swatch; blank is rejected (Tasks 4, 5).

---

### Task 1: `playbackVolume` property, helper and file validation

**Files:**
- Modify: `Source/UI/SongDocument.h` (MIDI_TRACK identifiers near line 46; add helper declaration)
- Modify: `Source/UI/SongDocument.cpp` (identifier near line 41; `validateLoaded` near line 300)
- Test: `Tests/SongFile_tests.cpp` (or `Tests/SongFileRoundTrip_tests.cpp`, same style as the existing "sections round-trip" cases at line 107)

**Interfaces:**
- Produces: `SongIDs::playbackVolume` (`juce::Identifier`); `int lotro::trackPlaybackVolume (const juce::ValueTree& track)` (declared in `SongDocument.h`, namespace `lotro`) returning the clamped 0–100 value, 100 when absent.

- [ ] **Step 1: Write the failing tests** (append to `Tests/SongFileRoundTrip_tests.cpp`; reuse that file's existing save/load helpers — read lines 1–46 and 107–136 first and mirror them exactly)

```cpp
TEST_CASE ("song file: playbackVolume round-trips, absent means 100", "[song-file][track-head]")
{
    SongDocument doc;
    auto a = doc.addTrack ("A", (int) 0xFFAABBCC, 1, 1);
    auto b = doc.addTrack ("B", (int) 0xFFAABBCC, 2, 1);
    doc.setProperty (a, SongIDs::playbackVolume, 40);
    CHECK (trackPlaybackVolume (a) == 40);
    CHECK (trackPlaybackVolume (b) == 100);        // never set
    // ...save to a temp file and load into `reloaded` exactly as the sections test does...
    CHECK (trackPlaybackVolume (reloaded.getTrack (1)) == 40);
    CHECK (trackPlaybackVolume (reloaded.getTrack (2)) == 100);
}

TEST_CASE ("song file: out-of-range or non-integer playbackVolume is rejected", "[song-file][track-head]")
{
    for (const juce::var bad : { juce::var (101), juce::var (-1), juce::var ("loud") })
    {
        SongDocument doc;
        auto t = doc.addTrack ("A", (int) 0xFFAABBCC, 1, 1);
        t.setProperty (SongIDs::playbackVolume, bad, nullptr);
        const auto error = SongDocument::validateLoaded (doc.getTree());
        REQUIRE (error.has_value());
        CHECK (error->kind() == SongFileErrorKind::InvalidStructure);
    }
}
```

(If `SongFileError` exposes its kind under another accessor, use what `Tests/SongFile_tests.cpp` uses.)

- [ ] **Step 2: Run to verify failure**

Run: `cmake --build build --target forge_tests 2>&1 | tail -20`
Expected: compile error, `playbackVolume` / `trackPlaybackVolume` not declared.

- [ ] **Step 3: Implement**

In `SongDocument.h`, MIDI_TRACK block (after `importBatch`):

```cpp
    extern const juce::Identifier playbackVolume;   // MIDI_TRACK: 0..100 audition gain; absent = 100
```

After the `SongIDs` namespace closes (before `class SongDocument`), declare:

```cpp
// A MIDI_TRACK's saved audition volume, 0..100; 100 when the property is absent.
int trackPlaybackVolume (const juce::ValueTree& track);
```

In `SongDocument.cpp` identifiers block (after `importBatch`): `const juce::Identifier playbackVolume ("playbackVolume");`, and define (inside `namespace lotro`, near the other free helpers or before `validateLoaded`):

```cpp
int trackPlaybackVolume (const juce::ValueTree& track)
{
    const auto v = track.getProperty (SongIDs::playbackVolume);
    return v.isInt() ? juce::jlimit (0, 100, (int) v) : 100;
}
```

In `validateLoaded`, inside the per-track loop right after the `importBatch` check:

```cpp
        if (track.hasProperty (SongIDs::playbackVolume))
        {
            const auto volume = track.getProperty (SongIDs::playbackVolume);
            if (! volume.isInt() || (int) volume < 0 || (int) volume > 100)
                return bad ("a track volume is out of range.");
        }
```

- [ ] **Step 4: Run to verify pass**

Run: `cmake --build build --target forge_tests && ctest --test-dir build -R "track-head|song-file" --output-on-failure`
Expected: PASS (including pre-existing song-file tests, which prove old files still load).

- [ ] **Step 5: Commit**

```bash
git add Source/UI/SongDocument.h Source/UI/SongDocument.cpp Tests/SongFileRoundTrip_tests.cpp
git commit -m "feat(ui): playbackVolume track property with load validation

Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>"
```

---

### Task 2: Snapshot gain, engine velocity scaling, live controller update

**Files:**
- Modify: `Source/UI/Playback/PlaybackSnapshot.h`, `Source/UI/Playback/PlaybackSnapshot.cpp`
- Modify: `Source/UI/Playback/PlaybackEngine.cpp` (event loop near line 112)
- Modify: `Source/UI/Playback/PlaybackController.cpp` (`valueTreePropertyChanged`, `rebuild`)
- Modify: `Source/UI/Playback/PlaybackController.h` (private helper)
- Test: `Tests/PlaybackEngine_tests.cpp`, `Tests/PlaybackController_tests.cpp`, `Tests/PlaybackSnapshot_tests.cpp`

**Interfaces:**
- Consumes: `trackPlaybackVolume (const juce::ValueTree&)` (Task 1).
- Produces: `PlaybackSnapshot::gainPercent (int trackIndex) const noexcept` and `PlaybackSnapshot::setGainPercent (int trackIndex, int percent) noexcept` (percent clamped 0–100; default 100); `int scaleVelocity (int velocity, int gainPercent) noexcept` declared in `PlaybackSnapshot.h` (free function, `namespace lotro`): returns `velocity` at 100, `0` when gain is 0, otherwise `max(1, (velocity*gain + 50)/100)`.

- [ ] **Step 1: Write the failing tests**

`Tests/PlaybackSnapshot_tests.cpp`:

```cpp
TEST_CASE ("scaleVelocity: 100 is identity, 0 silences, otherwise at least 1", "[playback][gain]")
{
    CHECK (scaleVelocity (100, 100) == 100);
    CHECK (scaleVelocity (100, 50) == 50);
    CHECK (scaleVelocity (1, 10) == 1);     // quiet but still sounds
    CHECK (scaleVelocity (100, 0) == 0);
}

TEST_CASE ("buildSnapshot: gain comes from the track's playbackVolume, default 100", "[playback][gain]")
{
    SongDocument doc;
    auto a = addTrack (doc);
    auto b = addTrack (doc);
    a.setProperty (SongIDs::playbackVolume, 30, nullptr);
    addNote (a, 60, 0, 480);
    addNote (b, 62, 0, 480);
    const auto snap = buildSnapshot (doc);
    CHECK (snap->gainPercent (snap->trackIndexForId ((juce::int64) a.getProperty (SongIDs::trackId))) == 30);
    CHECK (snap->gainPercent (snap->trackIndexForId ((juce::int64) b.getProperty (SongIDs::trackId))) == 100);
}
```

`Tests/PlaybackEngine_tests.cpp` (uses its `Rig`):

```cpp
TEST_CASE ("PlaybackEngine: a track at 50% plays NoteOn at half velocity, NoteOff unchanged", "[playback][engine][gain]")
{
    SongDocument doc;
    auto t = addTrack (doc);
    addNote (t, 60, 0, 240, 100);
    Rig rig (doc);
    rig.snapshot->setGainPercent (0, 50);
    rig.transport.play (rig.snapshot->endSeconds());
    rig.render (60);
    bool sawOn = false, sawOff = false;
    for (const auto& r : rig.sink.records)
    {
        if (r.event.kind == PlaybackEventKind::NoteOn)  { sawOn = true;  CHECK (r.event.data2 == 50); }
        if (r.event.kind == PlaybackEventKind::NoteOff) { sawOff = true; CHECK (r.event.data1 == 60); }
    }
    CHECK (sawOn);
    CHECK (sawOff);
}

TEST_CASE ("PlaybackEngine: gain 0 drops NoteOn but a sounding note still gets its NoteOff", "[playback][engine][gain]")
{
    SongDocument doc;
    auto t = addTrack (doc);
    addNote (t, 60, 0, 48000, 100);     // long note, still sounding when gain drops
    addNote (t, 62, 48000, 480, 100);   // starts after the gain drops
    Rig rig (doc);
    rig.transport.play (rig.snapshot->endSeconds());
    rig.render (2);
    rig.snapshot->setGainPercent (0, 0);
    rig.render (400);
    CHECK (rig.sink.count (PlaybackEventKind::NoteOn) == 1);    // only the first
    CHECK (rig.sink.count (PlaybackEventKind::NoteOff) == 2);   // both offs delivered
}
```

(Note ticks: 480 ticks = 0.5 s at the default 120 BPM, so 48000 ticks is long; adjust the second note's start if the first note's off lands before the render loop reaches it — the intent is "gain drops while note 1 sounds, note 2 starts after".)

`Tests/PlaybackController_tests.cpp`:

```cpp
TEST_CASE ("PlaybackController: changing playbackVolume updates the live snapshot without rebuilding", "[playback][controller][gain]")
{
    Rig r;
    auto t = addTrack (r.doc);
    addNote (t, 60, 0, 480);
    r.controller.flushRebuild();
    const auto before = r.controller.currentSnapshot();
    t.setProperty (SongIDs::playbackVolume, 25, nullptr);
    r.controller.flushRebuild();
    CHECK (r.controller.currentSnapshot() == before);
    CHECK (before->gainPercent (0) == 25);
}
```

- [ ] **Step 2: Run to verify failure**

Run: `cmake --build build --target forge_tests 2>&1 | tail`
Expected: compile errors (`gainPercent`, `scaleVelocity` undefined).

- [ ] **Step 3: Implement**

`PlaybackSnapshot.h` — add public members next to `isAudible`:

```cpp
    int gainPercent (int trackIndex) const noexcept { return gain[(size_t) trackIndex].load (std::memory_order_acquire); }
    void setGainPercent (int trackIndex, int percent) noexcept
    {
        gain[(size_t) trackIndex].store ((std::uint8_t) juce::jlimit (0, 100, percent), std::memory_order_release);
    }
```

private member: `std::unique_ptr<std::atomic<std::uint8_t>[]> gain;`. After the class (before `tempoMapFromDocument`) add:

```cpp
// The audition gain applied to a NoteOn velocity: identity at 100, silence at 0,
// otherwise never below 1 so a quiet track still sounds.
int scaleVelocity (int velocity, int gainPercent) noexcept;
```

`PlaybackSnapshot.cpp` — constructor: `gain (new std::atomic<std::uint8_t>[trackIdList.size()])` (declare after `audible` in the init order matching header order) and in the loop `gain[i].store (100, std::memory_order_relaxed);`. Define:

```cpp
int scaleVelocity (int velocity, int gainPercent) noexcept
{
    if (gainPercent >= 100) return velocity;
    if (gainPercent <= 0) return 0;
    return std::max (1, (velocity * gainPercent + 50) / 100);
}
```

At the end of `buildSnapshot`, after the snapshot is constructed and before it is returned, set each track's gain: `snapshot->setGainPercent (i, trackPlaybackVolume (doc.getTrack (i)));` for every track index `i` (read the tail of `buildSnapshot` to place it where the `shared_ptr` exists).

`PlaybackEngine.cpp` — replace the `if (e.kind != NoteOn || audible) sink.handle (e);` block:

```cpp
        if (e.kind != PlaybackEventKind::NoteOn)
            sink.handle (e);
        else if (snapshot->isAudible (e.trackIndex))
        {
            const int scaled = scaleVelocity (e.data2, snapshot->gainPercent (e.trackIndex));
            if (scaled > 0)
            {
                PlaybackEvent heard = e;
                heard.data2 = scaled;
                sink.handle (heard);
            }
        }
```

`PlaybackController.cpp` — in `valueTreePropertyChanged`, before the cosmetic early return add:

```cpp
    if (property == SongIDs::playbackVolume)
    {
        applyTrackGain (tree);
        return;   // audition gain only: a rebuild would cut held notes
    }
```

and implement (declare `void applyTrackGain (const juce::ValueTree& track);` private in the header):

```cpp
void PlaybackController::applyTrackGain (const juce::ValueTree& track)
{
    if (snapshot == nullptr || ! track.hasType (SongIDs::MIDI_TRACK))
        return;
    const int index = snapshot->trackIndexForId ((juce::int64) track.getProperty (SongIDs::trackId, (juce::int64) -1));
    if (index >= 0)
        snapshot->setGainPercent (index, trackPlaybackVolume (track));
}
```

`rebuild()` needs no change: `buildSnapshot` reads the property.

- [ ] **Step 4: Run to verify pass**

Run: `cmake --build build --target forge_tests && ctest --test-dir build -R "playback|gain" --output-on-failure`
Expected: PASS, including all pre-existing playback tests (gain defaults to 100 = identity).

- [ ] **Step 5: Commit**

```bash
git add Source/UI/Playback Tests/PlaybackSnapshot_tests.cpp Tests/PlaybackEngine_tests.cpp Tests/PlaybackController_tests.cpp
git commit -m "feat(ui): per-track playback gain scales NoteOn velocity live

Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>"
```

---

### Task 3: `TrackHeadComponent` — layout, painting, icon buttons, slider

**Files:**
- Create: `Source/UI/TrackHeadComponent.h`, `Source/UI/TrackHeadComponent.cpp`
- Modify: `CMakeLists.txt` (add `Source/UI/TrackHeadComponent.cpp` beside `TrackRowComponent.cpp`), `Tests/CMakeLists.txt` (source beside line 115; add `TrackHeadComponent_tests.cpp` to the test list)
- Test: `Tests/TrackHeadComponent_tests.cpp`

**Interfaces:**
- Consumes: `trackPlaybackVolume` (Task 1), `SongsmithColours` (`text`, `textMuted`, etc.).
- Produces (all in `namespace lotro`):

```cpp
struct TrackHeadLayout
{
    juce::Rectangle<int> index, swatch, name, mute, solo, volume;
};

class TrackHeadComponent : public juce::Component
{
public:
    TrackHeadComponent (juce::ValueTree trackNode, int displayIndex);

    // Pure geometry: bounds = the head's own local bounds.
    static TrackHeadLayout layoutFor (juce::Rectangle<int> bounds, bool conductor);

    void paint (juce::Graphics&) override;
    void resized() override;

    void setMuteSolo (bool muted, bool soloed);
    void refreshFromTrack();                      // re-reads name/colour/volume; repaints; slider set without notification

    juce::Rectangle<int> swatchBounds() const;    // head-local; empty for the conductor

    std::function<void (bool)> onMuteToggled;     // new state
    std::function<void (bool)> onSoloToggled;
    std::function<void (int percent, bool startsGesture)> onVolumeChanged;

    juce::Button& muteButtonForTesting() { return muteButton; }
    juce::Button& soloButtonForTesting() { return soloButton; }
    juce::Slider& volumeSliderForTesting() { return volumeSlider; }
};
```

  Constants: `static constexpr int sidePadding = 8, rightPadding = 6, indexWidth = 16, swatchSize = 14, iconSize = 22, gap = 4;`.

- [ ] **Step 1: Write the failing tests** (`Tests/TrackHeadComponent_tests.cpp`)

```cpp
#include "UI/SongDocument.h"
#include "UI/TrackHeadComponent.h"

#include <catch2/catch_test_macros.hpp>

using namespace lotro;

namespace
{
    void click (juce::Button& b)
    {
        b.triggerClick();
        juce::MessageManager::getInstance()->runDispatchLoopUntil (20);
    }
}

TEST_CASE ("TrackHead layout: two rows, nothing overlaps or leaves the bounds, at min and max heights", "[track-head][layout]")
{
    for (const int h : { 45, 62, 136 })   // minRowHeight(30)+band(16)-divider(1), default, max
    {
        const juce::Rectangle<int> bounds (0, 0, 198, h);
        const auto l = TrackHeadComponent::layoutFor (bounds, false);
        const juce::Rectangle<int> parts[] = { l.index, l.swatch, l.name, l.mute, l.solo, l.volume };
        for (const auto& r : parts)
        {
            CHECK (bounds.contains (r));
            CHECK (! r.isEmpty());
        }
        CHECK (l.mute.getY() >= l.name.getBottom());          // row 2 below row 1
        CHECK (! l.mute.intersects (l.solo));
        CHECK (! l.solo.intersects (l.volume));
        CHECK (! l.swatch.intersects (l.name));
        CHECK (! l.index.intersects (l.swatch));
    }
}

TEST_CASE ("TrackHead layout: conductor has only the name", "[track-head][layout]")
{
    const auto l = TrackHeadComponent::layoutFor ({ 0, 0, 198, 62 }, true);
    CHECK (l.swatch.isEmpty());
    CHECK (l.mute.isEmpty());
    CHECK (l.solo.isEmpty());
    CHECK (l.volume.isEmpty());
    CHECK (! l.name.isEmpty());
}

TEST_CASE ("TrackHead: M and S fire their callbacks with the new state", "[track-head][mutesolo]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SongDocument doc;
    TrackHeadComponent head (doc.addTrack ("A", (int) 0xFFAABBCC, 1, 1), 1);
    head.setBounds (0, 0, 198, 62);
    bool muted = false, soloed = false;
    head.onMuteToggled = [&] (bool s) { muted = s; };
    head.onSoloToggled = [&] (bool s) { soloed = s; };
    click (head.muteButtonForTesting());
    click (head.soloButtonForTesting());
    CHECK (muted);
    CHECK (soloed);
}

TEST_CASE ("TrackHead: setMuteSolo reflects state without firing callbacks", "[track-head][mutesolo]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SongDocument doc;
    TrackHeadComponent head (doc.addTrack ("A", (int) 0xFFAABBCC, 1, 1), 1);
    bool fired = false;
    head.onMuteToggled = [&] (bool) { fired = true; };
    head.setMuteSolo (true, false);
    CHECK (head.muteButtonForTesting().getToggleState());
    CHECK (! head.soloButtonForTesting().getToggleState());
    CHECK (! fired);
}

TEST_CASE ("TrackHead: the slider shows the saved volume and reports user changes only", "[track-head][volume]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SongDocument doc;
    auto track = doc.addTrack ("A", (int) 0xFFAABBCC, 1, 1);
    track.setProperty (SongIDs::playbackVolume, 40, nullptr);
    TrackHeadComponent head (track, 1);
    CHECK ((int) head.volumeSliderForTesting().getValue() == 40);

    int reported = -1;
    head.onVolumeChanged = [&] (int v, bool) { reported = v; };
    track.setProperty (SongIDs::playbackVolume, 70, nullptr);
    head.refreshFromTrack();
    CHECK ((int) head.volumeSliderForTesting().getValue() == 70);
    CHECK (reported == -1);                       // programmatic refresh is silent

    head.volumeSliderForTesting().setValue (20, juce::sendNotificationSync);
    CHECK (reported == 20);
}

TEST_CASE ("TrackHead: double-clicking the slider resets it to 100", "[track-head][volume]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SongDocument doc;
    auto track = doc.addTrack ("A", (int) 0xFFAABBCC, 1, 1);
    track.setProperty (SongIDs::playbackVolume, 40, nullptr);
    TrackHeadComponent head (track, 1);
    int reported = -1;
    head.onVolumeChanged = [&] (int v, bool) { reported = v; };
    CHECK (head.volumeSliderForTesting().getDoubleClickReturnValue() == 100.0);
    head.volumeSliderForTesting().setValue (head.volumeSliderForTesting().getDoubleClickReturnValue(),
                                            juce::sendNotificationSync);
    CHECK (reported == 100);
}

TEST_CASE ("TrackHead: the conductor head has no controls", "[track-head]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SongDocument doc;
    TrackHeadComponent head (doc.getConductorTrack(), 0);
    head.setBounds (0, 0, 198, 62);
    CHECK (! head.muteButtonForTesting().isVisible());
    CHECK (! head.soloButtonForTesting().isVisible());
    CHECK (! head.volumeSliderForTesting().isVisible());
    CHECK (head.swatchBounds().isEmpty());
}
```

- [ ] **Step 2: Run to verify failure**

Run: `cmake --build build --target forge_tests 2>&1 | tail`
Expected: `TrackHeadComponent.h` not found.

- [ ] **Step 3: Implement**

`TrackHeadComponent.h` declares the interface above plus private members: `juce::ValueTree track; int index; IconButton muteButton, soloButton; juce::Slider volumeSlider; bool gestureOpen = false;`, where `IconButton` is a private nested `juce::Button` subclass:

```cpp
    class IconButton : public juce::Button
    {
    public:
        enum class Kind { Mute, Solo };
        IconButton (Kind k, juce::Colour onColour) : juce::Button ({}), kind (k), onColourValue (onColour)
        {
            setClickingTogglesState (true);
            setWantsKeyboardFocus (false);
        }
        void paintButton (juce::Graphics&, bool isOver, bool isDown) override;
    private:
        Kind kind;
        juce::Colour onColourValue;
    };
```

`paintButton` fills a rounded rect (`on` → `onColourValue`, off → `SongsmithColours` button grey, brightened when `isOver`, darkened when `isDown`), then draws the glyph as a `juce::Path` scaled into the inner bounds:
- Mute: speaker (a small rectangle + trapezoid cone) and, when `getToggleState()`, a diagonal slash line across it; when off, two arc "sound waves".
- Solo: a headphone (arc over the top plus two ear-cup rectangles).
Glyph colour: `text` when on, `textMuted` when off. Build paths with `addRectangle`/`addTriangle`/`addArc`/`startNewSubPath`, no external assets.

`layoutFor`: area = bounds; split in half vertically (`top = area.removeFromTop (area.getHeight() / 2)`, `bottom = area`). Row 1 (non-conductor): `top.reduced`-style: `removeFromLeft (sidePadding)`, `index = removeFromLeft (indexWidth)`, `swatch = removeFromLeft (swatchSize + gap).withSizeKeepingCentre (swatchSize, swatchSize)`, `name = remainder.withTrimmedRight (rightPadding)`. Conductor: `index` as above, `name` = rest, swatch/buttons/slider default (empty). Row 2: `bottom.removeFromLeft (sidePadding)`; `mute = removeFromLeft (iconSize).withSizeKeepingCentre (iconSize, jmin (iconSize, bottom.getHeight() - 2))`; skip `gap`; `solo` likewise; skip `gap`; `volume = remainder.withTrimmedRight (rightPadding).reduced (0, 2)`.

Constructor: `MIDI_TRACK` jassert; `setInterceptsMouseClicks (false, true)` (the head itself is transparent so the row keeps selection / drag / double-click; only child controls take clicks); `setOpaque (false)`; configure `volumeSlider` as `Slider::LinearHorizontal`, `NoTextBox`, range `0..100` step `1`, `setDoubleClickReturnValue (true, 100.0)`, tooltip "Playback volume"; `onDragStart = [this] { gestureOpen = false; pendingStart = true; }` approach — implement `startsGesture` as: set `startsGesture = true` in `onDragStart`; `onValueChange` calls `onVolumeChanged ((int) getValue(), std::exchange (startsGesture, false))`; `onDragEnd` sets `startsGesture = true` again. Initial `startsGesture = true` (so keyboard/wheel/double-click changes each open their own step). For a non-conductor, add the three controls visible; for the conductor `setVisible (false)`. `muteButton` on-colour `juce::Colours::orangered`, solo `juce::Colours::gold`, tooltips "Mute"/"Solo"; `onClick` calls the callbacks with `getToggleState()`. Initial slider value = `trackPlaybackVolume (track)` set with `dontSendNotification`.

`resized()` applies `layoutFor (getLocalBounds(), conductor)` to the controls. `paint`: draw `index` (monospace 11 px, muted; skipped when `index == 0`), the swatch square filled with `colorArgb` (non-conductor), and the name (11 px, `text`, or `textMuted` for the conductor) with `juce::Justification::centredLeft` and ellipsis (`g.drawText (..., true)`). `refreshFromTrack()` sets the slider with `dontSendNotification` to `trackPlaybackVolume (track)` and `repaint()`. `setMuteSolo` sets both toggle states with `dontSendNotification`.

Register in both CMake files (see Global Constraints).

- [ ] **Step 4: Run to verify pass**

Run: `cmake --build build --target forge_tests && ctest --test-dir build -R "track-head" --output-on-failure`
Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add Source/UI/TrackHeadComponent.* CMakeLists.txt Tests/CMakeLists.txt Tests/TrackHeadComponent_tests.cpp
git commit -m "feat(ui): TrackHeadComponent with drawn mute/solo icons and volume slider

Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>"
```

---

### Task 4: Integrate the head into `TrackRowComponent` (200 px, no note line)

**Files:**
- Modify: `Source/UI/TrackRowComponent.h`, `Source/UI/TrackRowComponent.cpp`
- Modify: `Tests/TrackRowComponent_tests.cpp` (remove the two `buildSecondLineForTesting` tests at ~lines 245–265; keep the mute/solo tests — they work against `juce::Button&`)

**Interfaces:**
- Consumes: `TrackHeadComponent` (Task 3).
- Produces on `TrackRowComponent`: `trackInfoWidth = 200`; `muteButtonForTesting()` / `soloButtonForTesting()` now return `juce::Button&` (delegating to the head); `TrackHeadComponent& headForTesting()`; new forwarding callbacks (wired later): `std::function<void (juce::int64, int percent, bool startsGesture)> onVolumeChanged;`. Removed: `muteSoloWidth`, `buildSecondLine`, `buildSecondLineForTesting`, the `muteButton`/`soloButton` members and `<limits>`/`pitchName` if unused.

- [ ] **Step 1: Write the failing tests** (append to `Tests/TrackRowComponent_tests.cpp`; delete the two second-line tests)

```cpp
TEST_CASE ("TrackRowComponent: the head is 200 px and the note preview starts after it", "[track-row][head]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SongDocument doc;
    auto track = doc.addTrack ("A", (int) 0xFFAABBCC, 1, 1);
    TimelineViewState view;
    TrackRowComponent row (track, 1, view);
    row.setBounds (0, 0, 600, TrackRowComponent::defaultRowHeight + TrackRowComponent::instrumentBandHeight);
    CHECK (TrackRowComponent::trackInfoWidth == 200);
    CHECK (row.headForTesting().getRight() <= TrackRowComponent::trackInfoWidth);
    CHECK (row.notePreviewForTesting().getX() == TrackRowComponent::trackInfoWidth);
}

TEST_CASE ("TrackRowComponent: volume changes from the head are forwarded with the trackId", "[track-row][head][volume]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SongDocument doc;
    auto track = doc.addTrack ("A", (int) 0xFFAABBCC, 1, 1);
    const auto id = (juce::int64) track.getProperty (SongIDs::trackId);
    TimelineViewState view;
    TrackRowComponent row (track, 1, view);
    juce::int64 gotId = -1; int gotPercent = -1;
    row.onVolumeChanged = [&] (juce::int64 i, int p, bool) { gotId = i; gotPercent = p; };
    row.headForTesting().volumeSliderForTesting().setValue (35, juce::sendNotificationSync);
    CHECK (gotId == id);
    CHECK (gotPercent == 35);
}

TEST_CASE ("TrackRowComponent: a click on the head surface still selects the track", "[track-row][head]")
{
    // Build a MouseEvent as the existing tests in this file do (eventAt helper), click at the
    // head's index area (e.g. {10, 5}) and CHECK onTrackSelected fired with the row's trackId.
}
```

(Fill the third test using the file's existing `eventAt (row, pos, clicks)` helper and the same assertions as the existing selection test; do not leave it as a comment.)

- [ ] **Step 2: Run to verify failure**

Run: `cmake --build build --target forge_tests 2>&1 | tail`
Expected: `headForTesting` undeclared.

- [ ] **Step 3: Implement**

`TrackRowComponent.h`: `#include "TrackHeadComponent.h"`; `trackInfoWidth = 200` (update its comment: index/name/swatch/mute/solo/volume column); delete `muteSoloWidth`, `buildSecondLine*`; member `TrackHeadComponent head;` replacing the two `TextButton`s; accessors:

```cpp
    juce::Button& muteButtonForTesting() { return head.muteButtonForTesting(); }
    juce::Button& soloButtonForTesting() { return head.soloButtonForTesting(); }
    TrackHeadComponent& headForTesting() { return head; }
    std::function<void (juce::int64, int, bool)> onVolumeChanged;
```

`TrackRowComponent.cpp`: construct `head (trackNode, displayIndex)` in the initializer list (declare it before `notePreview` or keep order consistent with the header); `addAndMakeVisible (head)`; wire `head.onMuteToggled = [this] (bool s) { if (onMuteToggled) onMuteToggled (getTrackId(), s); };` and likewise solo and `onVolumeChanged`; delete the old button setup. `resized()`: `head.setBounds (info)` where `info` is the existing left slice minus `columnDividerThickness` on the right (keep the existing `area` / `info` computation, drop the button bounds lines). `setMuteSolo` calls `head.setMuteSolo (muted, soloed)` then `silencedBySolo = silenced; setAlpha (...)` as before. `paint`: delete the first-line / second-line text and swatch drawing and the `textLeft`/`row` computation (the head paints those); keep the divider, selected background, accent bar, column divider and instrument band exactly as they are. Remove `buildSecondLine`, `pitchName`, `<limits>`, `<algorithm>` if now unused.

- [ ] **Step 4: Run to verify pass**

Run: `cmake --build build --target forge_tests && ctest --test-dir build -R "track-row|track-list|track-head" --output-on-failure`
Expected: PASS. If `TrackListComponent` tests assert the old 180 px or old second-line text, update those assertions to `TrackRowComponent::trackInfoWidth` / remove the line check (each such change is a direct consequence of this task's spec'd behaviour change).

- [ ] **Step 5: Commit**

```bash
git add Source/UI/TrackRowComponent.* Tests/TrackRowComponent_tests.cpp Tests/TrackListComponent_tests.cpp
git commit -m "feat(ui): track row hosts the new 200 px head, note line removed

Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>"
```

---

### Task 5: Right-click menu and inline rename

**Files:**
- Modify: `Source/UI/TrackHeadComponent.h/.cpp`, `Source/UI/TrackRowComponent.h/.cpp`
- Test: `Tests/TrackHeadComponent_tests.cpp`, `Tests/TrackRowComponent_tests.cpp`

**Interfaces:**
- Produces on `TrackHeadComponent`: `juce::PopupMenu buildContextMenu() const` (item id 1 = "Rename…", disabled for the conductor); `void contextMenuChosen (int itemId)`; `void beginRename()`; `void commitRename (const juce::String& newName)` (static-free: trims; if empty or unchanged → cancel; else calls `onRenamed`); `std::function<void (const juce::String&)> onRenamed;`; `juce::TextEditor* renameEditorForTesting()` (null when not editing).
- Produces on `TrackRowComponent`: `std::function<void (juce::int64, const juce::String&)> onRenamed;`; `static bool inHeadArea (juce::Point<int> p) noexcept` (x < trackInfoWidth, y ≥ 0, excluding the instrument band — the band is on the canvas side, so this is just `p.x >= 0 && p.x < trackInfoWidth`); `mouseDown` routes a right-click in the head area to `head.showContextMenu()`.

- [ ] **Step 1: Write the failing tests**

```cpp
TEST_CASE ("TrackHead: the context menu offers Rename, disabled on the conductor", "[track-head][rename]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SongDocument doc;
    TrackHeadComponent head (doc.addTrack ("A", (int) 0xFFAABBCC, 1, 1), 1);
    TrackHeadComponent conductor (doc.getConductorTrack(), 0);
    juce::PopupMenu::MenuItemIterator it (head.buildContextMenu());
    REQUIRE (it.next());
    CHECK (it.getItem().text == juce::String::fromUTF8 ("Rename\xe2\x80\xa6"));
    CHECK (it.getItem().isEnabled);
    juce::PopupMenu::MenuItemIterator itC (conductor.buildContextMenu());
    REQUIRE (itC.next());
    CHECK (! itC.getItem().isEnabled);
}

TEST_CASE ("TrackHead: committing a new name fires onRenamed once; blank or unchanged does not", "[track-head][rename]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SongDocument doc;
    TrackHeadComponent head (doc.addTrack ("Lute", (int) 0xFFAABBCC, 1, 1), 1);
    head.setBounds (0, 0, 198, 62);
    std::vector<juce::String> names;
    head.onRenamed = [&] (const juce::String& n) { names.push_back (n); };

    head.beginRename();
    REQUIRE (head.renameEditorForTesting() != nullptr);
    CHECK (head.renameEditorForTesting()->getText() == "Lute");
    head.commitRename ("  Flute ");
    CHECK (names == std::vector<juce::String> { "Flute" });
    CHECK (head.renameEditorForTesting() == nullptr);

    head.beginRename(); head.commitRename ("   ");   CHECK (names.size() == 1);   // blank rejected
    head.beginRename(); head.commitRename ("Lute");  CHECK (names.size() == 1);   // unchanged
}

TEST_CASE ("TrackHead: Escape cancels a rename", "[track-head][rename]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SongDocument doc;
    TrackHeadComponent head (doc.addTrack ("Lute", (int) 0xFFAABBCC, 1, 1), 1);
    head.setBounds (0, 0, 198, 62);
    bool fired = false;
    head.onRenamed = [&] (const juce::String&) { fired = true; };
    head.beginRename();
    head.renameEditorForTesting()->setText ("Other", juce::dontSendNotification);
    head.renameEditorForTesting()->escapePressed();
    CHECK (head.renameEditorForTesting() == nullptr);
    CHECK (! fired);
}
```

Row test: a right-click (`ModifierKeys::rightButtonModifier`) at `{20, 10}` on a normal row must NOT call `onTrackSelected` (it opens the menu); a right-click on the instrument band must still go to the existing instrument menu path (see the existing test near `inInstrumentBand`). Use the file's `eventAt` helper with a modifiers parameter if it has one; otherwise build the event like the existing tests do.

- [ ] **Step 2: Run to verify failure** — Run: `cmake --build build --target forge_tests 2>&1 | tail`. Expected: `buildContextMenu` undeclared.

- [ ] **Step 3: Implement**

`TrackHeadComponent`: members `std::unique_ptr<juce::TextEditor> renameEditor;`.
- `buildContextMenu()`: `juce::PopupMenu m; m.addItem (1, juce::String::fromUTF8 ("Rename\xe2\x80\xa6"), ! isConductor); return m;` — the single place later features add items.
- `showContextMenu()` (public): `SafePointer` + `buildContextMenu().showMenuAsync (juce::PopupMenu::Options(), [safe] (int id) { if (safe != nullptr) safe->contextMenuChosen (id); });`.
- `contextMenuChosen (1)` → `beginRename()`.
- `beginRename()`: no-op for the conductor or if already editing. Create `TextEditor` bounded to `layoutFor(...).name` (via `addAndMakeVisible`, `setBounds`), font 11 px, text = current name, `selectAll()`, `grabKeyboardFocus()`; `onReturnKey = [this] { commitRename (renameEditor->getText()); }`; `onEscapeKey = [this] { endRename(); }`; `onFocusLost = [this] { endRename(); }` (focus loss cancels — spec).
- `commitRename (text)`: `const auto trimmed = text.trim(); const auto old = track.getProperty (SongIDs::name).toString(); endRename(); if (trimmed.isNotEmpty() && trimmed != old && onRenamed) onRenamed (trimmed);`
- `endRename()`: resets `renameEditor` (defer deletion if called from the editor's own callback: use `juce::MessageManager::callAsync` holding a `SafePointer`, or move the pointer to a local and delete after clearing callbacks; the test expects `renameEditorForTesting() == nullptr` immediately after, so move the `unique_ptr` into a local, clear its callbacks, `removeChildComponent`, and let the local die after the callback stack unwinds — if that crashes under the editor's own callback, post deletion with `callAsync` while nulling the member synchronously).

`TrackRowComponent::mouseDown`: before the existing instrument-band block add

```cpp
    if (e.mods.isPopupMenu() && inHeadArea (e.getPosition()) && ! isConductorTrack())
    {
        head.showContextMenu();
        return;
    }
```

Declare/implement `inHeadArea`, add `onRenamed` forwarding (`head.onRenamed = [this] (const juce::String& n) { if (onRenamed) onRenamed (getTrackId(), n); };`). The conductor falls through to the existing select behaviour.

- [ ] **Step 4: Run to verify pass** — `cmake --build build --target forge_tests && ctest --test-dir build -R "track-head|track-row" --output-on-failure`. Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add Source/UI/TrackHeadComponent.* Source/UI/TrackRowComponent.* Tests/TrackHeadComponent_tests.cpp Tests/TrackRowComponent_tests.cpp
git commit -m "feat(ui): right-click Rename on the track head with inline editor

Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>"
```

---

### Task 6: Swatch click opens the colour picker

**Files:**
- Modify: `Source/UI/TrackHeadComponent.h/.cpp`, `Source/UI/TrackRowComponent.h/.cpp`
- Test: `Tests/TrackHeadComponent_tests.cpp`, `Tests/TrackRowComponent_tests.cpp`

**Interfaces:**
- Produces on `TrackHeadComponent`: `void openColourPicker()`; `void colourPicked (juce::Colour c)` (public so tests can drive it: calls `onColourChanged (c.getARGB(), startsGesture)`, with `startsGesture` true for the first call of a picker session and false afterwards); `void colourPickerClosed()` (resets the session flag); `std::function<void (juce::uint32 argb, bool startsGesture)> onColourChanged;`.
- Produces on `TrackRowComponent`: `std::function<void (juce::int64, juce::uint32, bool)> onColourChanged;`; `mouseDown` left-click inside `head.swatchBounds()` (translated to row coords) opens the picker and does not select.

- [ ] **Step 1: Write the failing tests**

```cpp
TEST_CASE ("TrackHead: the first colour change of a picker session starts a gesture, later ones continue it", "[track-head][colour]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SongDocument doc;
    TrackHeadComponent head (doc.addTrack ("A", (int) 0xFFAABBCC, 1, 1), 1);
    std::vector<std::pair<juce::uint32, bool>> got;
    head.onColourChanged = [&] (juce::uint32 argb, bool starts) { got.emplace_back (argb, starts); };
    head.colourPicked (juce::Colour (0xFF112233));
    head.colourPicked (juce::Colour (0xFF445566));
    head.colourPickerClosed();
    head.colourPicked (juce::Colour (0xFF778899));
    REQUIRE (got.size() == 3);
    CHECK (got[0].second);
    CHECK (! got[1].second);
    CHECK (got[2].second);
    CHECK (got[0].first == 0xFF112233u);
}

TEST_CASE ("TrackHead: the conductor swatch is not clickable", "[track-head][colour]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SongDocument doc;
    TrackHeadComponent head (doc.getConductorTrack(), 0);
    head.setBounds (0, 0, 198, 62);
    CHECK (head.swatchBounds().isEmpty());
}
```

Row test: a left-click inside the swatch (`row.headForTesting().swatchBounds().getCentre()` in row coords, head at x 0) must not call `onTrackSelected` and must flag the picker as open (expose `bool colourPickerOpenForTesting() const` on the head, true between `openColourPicker()` and `colourPickerClosed()`); a left-click elsewhere in the head still selects.

- [ ] **Step 2: Run to verify failure** — Expected: `colourPicked` undeclared.

- [ ] **Step 3: Implement**

`openColourPicker()`: if the conductor or already open, return. Build a small `ColourSelector` subclass inside the .cpp:

```cpp
    class PickerContent : public juce::ColourSelector, private juce::ChangeListener
    {
    public:
        PickerContent (juce::Colour initial, std::function<void (juce::Colour)> onPick)
            : juce::ColourSelector (juce::ColourSelector::showColourspace | juce::ColourSelector::showSliders),
              pick (std::move (onPick))
        {
            setCurrentColour (initial, juce::dontSendNotification);
            setSize (240, 260);
            addChangeListener (this);
        }
        ~PickerContent() override { removeChangeListener (this); }
    private:
        void changeListenerCallback (juce::ChangeBroadcaster*) override { pick (getCurrentColour()); }
        std::function<void (juce::Colour)> pick;
    };
```

Show it with `juce::CallOutBox::launchAsynchronously (std::make_unique<PickerContent> (...), getScreenBounds-of-swatch, nullptr)`. Closing detection: `CallOutBox` is deleted on dismissal; make the content's destructor call a `std::function<void()> onClosed` (set to `[safe] { if (safe != nullptr) safe->colourPickerClosed(); }`) — implement by having `PickerContent` hold `onClosed` and invoke it in its destructor. Set `pickerOpen = true` in `openColourPicker`, false in `colourPickerClosed`. `colourPicked (c)`: `onColourChanged (c.getARGB(), std::exchange (pickerFirstChange, false))`; `colourPickerClosed()` resets `pickerFirstChange = true; pickerOpen = false;`. (`pickerFirstChange` initial true.) Initial colour = the track's `colorArgb` as `juce::Colour ((juce::uint32) (int) property)`.

`TrackRowComponent`: forwarding `head.onColourChanged = [this] (juce::uint32 argb, bool s) { if (onColourChanged) onColourChanged (getTrackId(), argb, s); };`. In `mouseDown`, after the right-click head block and before `onTrackSelected`:

```cpp
    if (! e.mods.isPopupMenu() && head.swatchBounds().translated (head.getX(), head.getY()).contains (e.getPosition()))
    {
        head.openColourPicker();
        return;
    }
```

- [ ] **Step 4: Run to verify pass** — `cmake --build build --target forge_tests && ctest --test-dir build -R "track-head|track-row" --output-on-failure`. Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add Source/UI/TrackHeadComponent.* Source/UI/TrackRowComponent.* Tests/TrackHeadComponent_tests.cpp Tests/TrackRowComponent_tests.cpp
git commit -m "feat(ui): click the track swatch to pick a colour

Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>"
```

---

### Task 7: Wire `TrackListComponent` — undoable writes and in-place refresh

**Files:**
- Modify: `Source/UI/TrackListComponent.h` (listener override, helper), `Source/UI/TrackListComponent.cpp` (`rebuild()` wiring near line 164–185)
- Test: `Tests/TrackListComponent_tests.cpp`

**Interfaces:**
- Consumes: row callbacks `onColourChanged (juce::int64, juce::uint32, bool)`, `onRenamed (juce::int64, const juce::String&)`, `onVolumeChanged (juce::int64, int, bool)`; `TrackHeadComponent::refreshFromTrack()`; existing test access helper `Access::rowFor (list, trackId)` (see how `Tests/TrackListComponent_tests.cpp` defines it near line 822).
- Produces: `void TrackListComponent::refreshHead (juce::int64 trackId)` (private).

- [ ] **Step 1: Write the failing tests** (mirror the file's existing fixtures for building a `TrackListComponent` over a `SongDocument`; read lines ~800–830 first)

```cpp
TEST_CASE ("TrackList: a volume drag is one undo step and does not rebuild the row", "[track-list][head][volume]")
{
    // fixture: doc with one track, list built and laid out; row = Access::rowFor (list, id)
    auto* rowBefore = Access::rowFor (list, id);
    auto& slider = rowBefore->headForTesting().volumeSliderForTesting();
    slider.onDragStart();
    slider.setValue (60, juce::sendNotificationSync);
    slider.setValue (30, juce::sendNotificationSync);
    slider.onDragEnd();
    juce::MessageManager::getInstance()->runDispatchLoopUntil (50);     // let any async rebuild run
    CHECK (Access::rowFor (list, id) == rowBefore);                      // same row object: not rebuilt
    CHECK (trackPlaybackVolume (track) == 30);
    doc.getUndoManager().undo();
    CHECK (trackPlaybackVolume (track) == 100);                          // one step undoes the whole drag
    CHECK ((int) Access::rowFor (list, id)->headForTesting().volumeSliderForTesting().getValue() == 100);   // head refreshed
}

TEST_CASE ("TrackList: rename and recolour write the track and undo cleanly", "[track-list][head]")
{
    // fixture as above
    row->onRenamed (id, "Flute");
    CHECK (track.getProperty (SongIDs::name).toString() == "Flute");
    row->onColourChanged (id, 0xFF112233u, true);
    row->onColourChanged (id, 0xFF445566u, false);
    CHECK ((juce::uint32) (int) track.getProperty (SongIDs::colorArgb) == 0xFF445566u);
    doc.getUndoManager().undo();
    CHECK ((juce::uint32) (int) track.getProperty (SongIDs::colorArgb) == 0xFFAABBCCu);   // both picker changes = one step
    doc.getUndoManager().undo();
    CHECK (track.getProperty (SongIDs::name).toString() == "A");
}

TEST_CASE ("TrackList: setting volume to 100 removes the property (default is implicit)", "[track-list][head][volume]")
{
    // set 40 then 100 via row->onVolumeChanged; CHECK ! track.hasProperty (SongIDs::playbackVolume)
}
```

Fill the fixtures with the file's real helpers (do not leave the comments). If `SongDocument` exposes its undo manager under another name, use that; `doc.getUndoManager()` is assumed.

- [ ] **Step 2: Run to verify failure** — Expected: row callbacks not wired / row gets rebuilt (`rowFor` pointer differs).

- [ ] **Step 3: Implement**

In `rebuild()`'s row setup (after `onSoloToggled`):

```cpp
        row->onRenamed = [this] (juce::int64 id, const juce::String& newName)
        {
            if (auto t = doc.findTrackById (id); t.isValid())
                doc.setProperty (t, SongIDs::name, newName);
        };
        row->onColourChanged = [this] (juce::int64 id, juce::uint32 argb, bool startsGesture)
        {
            if (auto t = doc.findTrackById (id); t.isValid())
                doc.setProperty (t, SongIDs::colorArgb, (int) argb, startsGesture);
        };
        row->onVolumeChanged = [this] (juce::int64 id, int percent, bool startsGesture)
        {
            auto t = doc.findTrackById (id);
            if (! t.isValid())
                return;
            if (percent >= 100)
                doc.removeProperty (t, SongIDs::playbackVolume, startsGesture);   // 100 is the implicit default
            else
                doc.setProperty (t, SongIDs::playbackVolume, percent, startsGesture);
        };
```

Replace the one-line `valueTreePropertyChanged` override in the header with a declaration, and implement in the .cpp:

```cpp
void TrackListComponent::valueTreePropertyChanged (juce::ValueTree& tree, const juce::Identifier& property)
{
    // The head edits itself: a rebuild here would destroy the slider / colour picker /
    // rename editor in the middle of the gesture that caused the change.
    if (tree.hasType (SongIDs::MIDI_TRACK)
        && (property == SongIDs::name || property == SongIDs::colorArgb || property == SongIDs::playbackVolume))
    {
        refreshHead ((juce::int64) tree.getProperty (SongIDs::trackId, (juce::int64) -1));
        return;
    }
    triggerAsyncUpdate();
}

void TrackListComponent::refreshHead (juce::int64 trackId)
{
    for (auto* row : content.rows)
        if (row->getTrackId() == trackId)
            row->headForTesting().refreshFromTrack();   // rename to a non-testing accessor `refreshHead()` on the row if preferred
}
```

Add a non-test `void TrackRowComponent::refreshHead() { head.refreshFromTrack(); }` and call that instead of the `ForTesting` accessor. Check `doc.setProperty` early-outs when the value is unchanged (it should, per the juce-valuetree-conventions skill; if not, add `if (current == newValue) return;` guards in the three lambdas). Does anything else depend on a rebuild after a rename (e.g. the part strip chip names)? Those components listen to the tree themselves; confirm by running the whole suite.

- [ ] **Step 4: Run to verify pass** — `cmake --build build && ctest --test-dir build --output-on-failure`. Expected: whole suite PASS.

- [ ] **Step 5: Commit**

```bash
git add Source/UI/TrackListComponent.* Source/UI/TrackRowComponent.* Tests/TrackListComponent_tests.cpp
git commit -m "feat(ui): wire track head edits to undoable document writes

Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>"
```

---

### Task 8: Docs, spec amendments, test count

**Files:**
- Modify: `docs/superpowers/specs/2026-10-10-songsmith-track-head-design.md` (apply the three amendments listed at the top of this plan; set Status to "implemented")
- Modify: `docs/UI_GUIDE.md` (head region: layout, swatch picker, context menu, slider; field → property table gains `playbackVolume`), `docs/songsmith-ui-map.html` (head entry), `docs/ARCHITECTURE.md` (§9.13 `playbackVolume` + validation; §9.14 snapshot gain / velocity scaling / live update; track-list section: `TrackHeadComponent`, in-place refresh), `docs/TESTING.md`, `CLAUDE.md` (status line + test count)

- [ ] **Step 1:** Run `ctest --test-dir build -N | tail -1` for the new total test count.
- [ ] **Step 2:** Update each doc above; in `CLAUDE.md` add the spec/plan row to the Docs map table (same format as the other rows) and update the test count wherever the last commit's `docs: test count 911` convention put it (grep `911`).
- [ ] **Step 3:** Verify: `grep -rn "buildSecondLine\|muteSoloWidth\|180" docs CLAUDE.md | grep -i "track\|info"` shows no stale references to the old head.
- [ ] **Step 4: Commit**

```bash
git add docs CLAUDE.md
git commit -m "docs: track head redesign (UI guide, architecture, spec amendments, test count)

Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>"
```

---

## Self-review notes

- **Spec coverage:** 200 px width (T4); two-row layout, icons, slider (T3); note line removed (T4); right-click menu + rename (T5); swatch colour picker (T6); saved `playbackVolume` + validation + default-100 compatibility (T1); snapshot gain, live update, engine scaling (T2); undo-step semantics, in-place refresh (T7); docs (T8). Channel is out of scope per the spec.
- **Type consistency:** `onVolumeChanged` is `(int, bool)` on the head, `(juce::int64, int, bool)` on the row/list; `onColourChanged` `(juce::uint32, bool)` head / `(juce::int64, juce::uint32, bool)` row; `onRenamed` `(const String&)` head / `(juce::int64, const String&)` row; `trackPlaybackVolume`, `gainPercent`/`setGainPercent`, `scaleVelocity` named identically wherever used; `muteButtonForTesting()` returns `juce::Button&` on both head and row.
- **Known soft spots for the executor:** icon glyph drawing is free-form (no pixel test; judge by the user's manual look at the Windows build); the `endRename` deletion-from-own-callback detail in T5; exact fixture helpers in T7 depend on the existing test file.
