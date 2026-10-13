# Songsmith Mixer Panel Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A dockable Mixer panel (per-track pan knob, mute, solo, vertical volume fader, number and name, plus a Master strip) that is a second view of per-track playback state, built behind a `MixerTarget` seam so LOTRO-part playback can plug in later.

**Architecture:** Per-track `playbackPan` (saved like `playbackVolume`, an override of the file's CC10 while set) and a session-only master level are added to the playback engine (`Source/UI/Playback/`, `Source/Core/` untouched). `MixerPanelComponent` depends only on the pure `MixerTarget` interface; `SourceMidiTarget` is its one implementation (tree + `PlaybackController`); `ActivePlaybackTarget` returns it today. The panel is registered as the fourth dock panel `mixer`.

**Tech Stack:** C++17, JUCE (ValueTree, Slider, Viewport), TinySoundFont (`tsf_channel_set_pan`), Catch2, CMake/Ninja. No new dependencies.

**Spec:** `docs/superpowers/specs/2026-10-12-songsmith-mixer-design.md`

## Global Constraints

- `Source/Core/` is not touched (see skill `forge-engine-ui-boundary`); everything lives under `Source/UI/`.
- Pan and master are audition aids: nothing reaches MIDI or ABC export.
- `playbackPan`: int -100..+100; absent = follow the file; removed rather than written when unset; no format-version bump; `validateLoaded` message `"a track pan is out of range."` (`InvalidStructure`).
- Master: int 0-100, session-only on `PlaybackController`, 100 = unity, never boosts.
- Pan and volume changes never schedule a snapshot rebuild (it would cut held notes).
- Pan edits are undoable, one undo step per drag (the `startsGesture` convention of the head's volume slider).
- Custom error types, no `any`/untyped escapes; add types everywhere; functional style where practical.
- No new third-party dependencies.
- Commit messages: conventional commits, end with `Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>`.
- Never push without explicit approval; never launch the GUI; run ctest serially (`ctest --test-dir build -j1`); a failed `cmake --build` followed by `ctest` runs the STALE binary, so grep the build output for `error` first.
- New `.cpp` files must be added to BOTH `CMakeLists.txt` (forge_ui) and `Tests/CMakeLists.txt` (forge_tests source list; test files go in its test list); new `.h` files need no entry.
- `-Wmissing-field-initializers` is on: give every field of `PanelDef` / `PanelState` explicitly.
- `allCommandIds` is a `std::array<MenuCommandId, N>`: bump N in `MenuModel.h` and the `size() == N` check in `MenuModel_tests.cpp` together.

## Review Focus

- A file with its own CC10 events plus a mixer pan override stays overridden after a seek and after Stop/Play (Task 2 tests).
- Clearing a pan override mid-song restores the file's last CC10 before the playhead, not centre (Task 2).
- A track with several virtual channels (or a drum track) has every channel panned (Task 2).
- New / Open replaces the document in place with different tracks, and a Song with zero or only-conductor tracks: the mixer shows the right strips (or only Master) without dangling pointers (Tasks 3 and 5).
- One fader drag is one undo step, and a head slider move updates the fader without echoing back (Tasks 3 and 5).

---

## File Structure

| File | Responsibility |
|---|---|
| `Source/UI/SongDocument.{h,cpp}` | `SongIDs::playbackPan`, `trackPlaybackPan`, `validateLoaded` rule |
| `Source/UI/Playback/PlaybackSnapshot.{h,cpp}` | per-track pan override atomic, built from the tree |
| `Source/UI/Playback/EventSink.h`, `SynthVoice.{h,cpp}`, `Tests/PlaybackTestSupport.h` | `setChannelPan` |
| `Source/UI/Playback/PlaybackEngine.{h,cpp}` | master gain, pan override apply / CC10 drop / restore |
| `Source/UI/Playback/PlaybackController.{h,cpp}` | `applyTrackPan`, master level, listener hook |
| `Source/UI/Mixer/MixerTarget.h` | pure interface + `MixerChannel` |
| `Source/UI/Mixer/SourceMidiTarget.{h,cpp}` | the source-MIDI implementation |
| `Source/UI/Mixer/ActivePlaybackTarget.h` | holder returning the active target |
| `Source/UI/MuteSoloIconButton.{h,cpp}` | the head's mute/solo icon button, extracted and shared |
| `Source/UI/Mixer/MixerStripComponent.{h,cpp}`, `MixerPanelComponent.{h,cpp}` | strips, Master strip, scrolling panel |
| `Source/UI/Dock/DockLayoutState.cpp`, `MenuModel.h`, `SongsmithMainComponent.{h,cpp}`, `MainWindow.cpp` | `mixer` panel registration, View toggle |

---

### Task 1: `playbackPan` property and validation

**Files:**
- Modify: `Source/UI/SongDocument.h` (declare `playbackPan`, `trackPlaybackPan`)
- Modify: `Source/UI/SongDocument.cpp` (identifier near line 45, accessor near line 92, validation near line 371)
- Test: `Tests/SongDocument_tests.cpp` (find the existing `playbackVolume` cases with `grep -n playbackVolume Tests/*.cpp` and add alongside)

**Interfaces:**
- Produces: `SongIDs::playbackPan` (`MIDI_TRACK` property, int -100..100); `std::optional<int> trackPlaybackPan (const juce::ValueTree& track)` (nullopt when absent or not an int; clamped to -100..100 otherwise).

- [ ] **Step 1: Write the failing tests** (mirror the file's existing `playbackVolume` tests for how a document, a track and a save/load round trip are built):

```cpp
TEST_CASE ("trackPlaybackPan: absent is nullopt, present is clamped", "[songdocument][pan]")
{
    juce::ValueTree track (SongIDs::MIDI_TRACK);
    CHECK (! trackPlaybackPan (track).has_value());
    track.setProperty (SongIDs::playbackPan, -40, nullptr);
    CHECK (trackPlaybackPan (track) == std::optional<int> (-40));
    track.setProperty (SongIDs::playbackPan, 500, nullptr);
    CHECK (trackPlaybackPan (track) == std::optional<int> (100));
    track.setProperty (SongIDs::playbackPan, "left", nullptr);
    CHECK (! trackPlaybackPan (track).has_value());
}

TEST_CASE ("validateLoaded rejects an out-of-range or non-integer playbackPan", "[songdocument][pan]")
{
    // Build a valid loaded document the same way the playbackVolume validation test does,
    // then set playbackPan on its first track.
    for (const juce::var bad : { juce::var (101), juce::var (-101), juce::var ("x"), juce::var (0.5) })
    {
        // ...construct `loaded` tree with the property = bad ...
        CHECK_THROWS_AS (SongDocument().replaceContents (loaded), SongFileError);
    }
    // ...and a valid -100 / 0 / 100 must load.
}
```

Copy the exact construction helper the existing volume-validation test uses; do not invent a new one.

- [ ] **Step 2:** `cmake --build build 2>&1 | grep -E " error "` then `ctest --test-dir build -R "pan" --output-on-failure`. Expected: compile error / FAIL (`playbackPan` undefined).
- [ ] **Step 3: Implement.** In `SongDocument.cpp` add `const juce::Identifier playbackPan ("playbackPan");` next to `playbackVolume`, and declare it in the `SongIDs` namespace in the header with the comment `// MIDI_TRACK: -100..100 audition pan override; absent = follow the file`. Add:

```cpp
std::optional<int> trackPlaybackPan (const juce::ValueTree& track)
{
    const auto v = track.getProperty (SongIDs::playbackPan);
    if (! v.isInt())
        return std::nullopt;
    return juce::jlimit (-100, 100, (int) v);
}
```

and in `validateLoaded`, after the volume check:

```cpp
        if (track.hasProperty (SongIDs::playbackPan))
        {
            const auto pan = track.getProperty (SongIDs::playbackPan);
            if (! pan.isInt() || (int) pan < -100 || (int) pan > 100)
                return bad ("a track pan is out of range.");
        }
```

Add `#include <optional>` to the header and declare `std::optional<int> trackPlaybackPan (const juce::ValueTree& track);` beside `trackPlaybackVolume`.

- [ ] **Step 4:** rebuild, `ctest --test-dir build -R "pan|SongDocument" --output-on-failure`. Expected: PASS.
- [ ] **Step 5: Commit**

```bash
git add Source/UI/SongDocument.h Source/UI/SongDocument.cpp Tests/SongDocument_tests.cpp
git commit -m "feat(ui): per-track playbackPan property with load validation

Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>"
```

---

### Task 2: Engine — master gain and per-track pan override

**Files:**
- Modify: `Source/UI/Playback/PlaybackSnapshot.{h,cpp}`, `EventSink.h`, `SynthVoice.{h,cpp}`, `PlaybackEngine.{h,cpp}`, `PlaybackController.{h,cpp}`
- Modify: `Tests/PlaybackTestSupport.h` (`RecordingSink`)
- Test: `Tests/PlaybackEngine_tests.cpp`, `Tests/PlaybackController_tests.cpp`, `Tests/PlaybackSnapshot_tests.cpp`

**Interfaces:**
- Consumes: Task 1's `trackPlaybackPan`.
- Produces:
  - `PlaybackSnapshot::panOverride (int trackIndex) const noexcept` returns `std::optional<int>` (-100..100); `void setPanOverride (int trackIndex, std::optional<int>) noexcept`; audio-thread-only `int appliedPan (int trackIndex) const noexcept` / `void setAppliedPan (int trackIndex, int) const noexcept`, using the constant `PlaybackSnapshot::kNoPan = -128` (internal storage sentinel for "no override").
  - `EventSink::setChannelPan (int virtualChannel, int pan) noexcept` (pan -100..100, 0 = centre; `SynthVoice` maps to tsf's 0..1 as `(pan + 100) / 200.0f`).
  - `PlaybackEngine::setMasterPercent (int percent) noexcept` / `int masterPercent() const noexcept`; the rendered block is multiplied by `percent / 100.0f` (linear, unity at 100).
  - `PlaybackController::setMasterPercent (int)`, `int masterPercent() const`, `Listener::masterChanged()` (default no-op), `void applyTrackPan (const juce::ValueTree& track)` (private).
  - `RecordingSink::pans` (`std::vector<struct { size_t recordsBefore; int channel; int pan; }>`).

Engine behaviour (all in `PlaybackEngine.cpp`):
1. `renderBlock` is split: the current body becomes private `renderBlockUnscaled(...)`; the public `renderBlock` calls it, then multiplies both buffer channels by `masterGain` for `numSamples` frames (skip when percent is 100).
2. `chase`: after the `resetChannel` loop and the event replay, for each track with an override, call `sink.setChannelPan (vch, pan)` for every channel of that track. During the replay, skip `Control` events with `data1 == 10` when `snapshot.panOverride (e.trackIndex)` has a value.
3. Live loop: same CC10 skip for `Control` events of overridden tracks.
4. New `applyPanChanges (snapshot, position, playing)` called each block after `releaseNewlyMutedTracks`: for each track compare `panOverride` to `appliedPan`. If now overridden: `setChannelPan` on its channels. If the override was cleared and `playing`: for each channel of the track send the last file CC10 event strictly before `position` (scan `snapshot.events()` up to `position`, last `Control` with `data1 == 10` for that `virtualChannel`) through `sink.handle`; if none, `sink.setChannelPan (vch, 0)`. Then `setAppliedPan`. When not playing, only update `appliedPan`; the next Play's `chase` applies the right state.

- [ ] **Step 1: Write failing tests** in `Tests/PlaybackEngine_tests.cpp` (use the helpers `addTrack`, `addNote`, `RecordingSink` from `PlaybackTestSupport.h` as the neighbouring tests do; read two of them first and copy their setup for building a controller/engine and driving `renderBlock`):

```cpp
TEST_CASE ("engine: an overridden track drops its file CC10 and is panned on every channel", "[playback][engine][pan]")
{
    // Document with one track that has a CC10 Control event (data1 = 10) at tick 0 and notes;
    // set SongIDs::playbackPan = -50 on it; build the snapshot; render a block while playing.
    // CHECK: no Control event with data1 == 10 reached the sink for that track;
    // CHECK: sink.pans contains (channel, -50) for every channel of that track.
}

TEST_CASE ("engine: the override survives a seek", "[playback][engine][pan]")
{
    // Same setup, then seek to a later tick, render: CHECK the last pan recorded for the
    // channel is -50 and no CC10 reached the sink after the seek.
}

TEST_CASE ("engine: clearing the override restores the file's last CC10", "[playback][engine][pan]")
{
    // File has CC10 = 20 at tick 0 and CC10 = 100 at tick 480. Play past tick 480 with an
    // override of -50, then snapshot->setPanOverride (idx, std::nullopt) and render one block.
    // CHECK: a Control event data1 == 10, data2 == 100 was handled for each channel of the track.
}

TEST_CASE ("engine: every virtual channel of a multi-channel track is panned", "[playback][engine][pan]")
{
    // A track whose notes use two MIDI channels (two VirtualChannels): both appear in sink.pans.
}

TEST_CASE ("engine: master percent scales the rendered block and 100 is unity", "[playback][engine][master]")
{
    // Use a sink whose render() fills the buffer with 1.0f (a small local EventSink subclass in the
    // test, or extend RecordingSink with `float fill = 0`). engine.setMasterPercent (50): samples == 0.5f;
    // 100: 1.0f; 0: 0.0f. Cover both the playing and the not-playing path.
}
```

Also in `PlaybackSnapshot_tests.cpp`: `buildSnapshot` initialises `panOverride` from `playbackPan` (absent gives `nullopt`). In `PlaybackController_tests.cpp`: setting `playbackPan` on a track node changes `currentSnapshot()->panOverride(...)` with the snapshot pointer unchanged (no rebuild), and removing the property clears it.

- [ ] **Step 2:** build (grep for `error`), run `ctest --test-dir build -R "pan|master" --output-on-failure`. Expected: compile errors, then FAIL.
- [ ] **Step 3: Implement** as specified above. `PlaybackSnapshot`: add `std::unique_ptr<std::atomic<std::int8_t>[]> pan;` initialised to `kNoPan` in the constructor, `mutable std::vector<std::int8_t> appliedPanValues` initialised to `kNoPan`; `buildSnapshot` loop gets `snapshot->setPanOverride (i, trackPlaybackPan (doc.getTrack (i)));`. `SynthVoice::setChannelPan`:

```cpp
void SynthVoice::setChannelPan (int virtualChannel, int pan) noexcept
{
    if (current == nullptr || current->synth == nullptr || virtualChannel < 0 || virtualChannel >= kMaxVirtualChannels)
        return;
    tsf_channel_set_pan (current->synth, virtualChannel, (float) (juce::jlimit (-100, 100, pan) + 100) / 200.0f);
}
```

`PlaybackController::valueTreePropertyChanged`: `if (property == SongIDs::playbackPan) { applyTrackPan (tree); return; }` with `applyTrackPan` mirroring `applyTrackGain` but calling `snapshot->setPanOverride (index, trackPlaybackPan (track))`. `setMasterPercent` clamps 0-100, stores into `playbackEngine.setMasterPercent`, keeps its own `int masterLevel = 100`, and calls `listeners.call (&Listener::masterChanged)`. Add `masterChanged` to `Listener`.

- [ ] **Step 4:** build and run `ctest --test-dir build -j1 2>&1 | tail -5`. Expected: all pass (existing count + new).
- [ ] **Step 5: Commit**

```bash
git add Source/UI/Playback Tests/PlaybackTestSupport.h Tests/PlaybackEngine_tests.cpp Tests/PlaybackController_tests.cpp Tests/PlaybackSnapshot_tests.cpp
git commit -m "feat(playback): per-track pan override and master gain in the engine

Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>"
```

---

### Task 3: `MixerTarget`, `SourceMidiTarget`, `ActivePlaybackTarget`

**Files:**
- Create: `Source/UI/Mixer/MixerTarget.h`, `Source/UI/Mixer/SourceMidiTarget.{h,cpp}`, `Source/UI/Mixer/ActivePlaybackTarget.h`
- Test: `Tests/SourceMidiTarget_tests.cpp` (add to `Tests/CMakeLists.txt` test list; add `Source/UI/Mixer/SourceMidiTarget.cpp` to the source lists of both CMake files)

**Interfaces:**
- Consumes: Tasks 1-2 (`playbackPan`, `trackPlaybackPan`, `setMasterPercent`, `Listener::masterChanged`).
- Produces (`namespace lotro`):

```cpp
struct MixerChannel
{
    juce::int64        id = -1;           // MIDI_TRACK trackId
    int                number = 0;        // 1-based display number among non-conductor tracks
    juce::String       name;
    juce::uint32       argb = 0;
    int                volume = 100;      // 0..100
    std::optional<int> pan;               // nullopt = follows the source
    bool               muted = false;
    bool               soloed = false;
    bool               silencedBySolo = false;
};

class MixerTarget
{
public:
    class Listener { public: virtual ~Listener() = default; virtual void mixerChanged() = 0; };
    virtual ~MixerTarget() = default;
    virtual std::vector<MixerChannel> channels() const = 0;       // conductor excluded
    virtual void setVolume (juce::int64 id, int percent, bool startsGesture) = 0;
    virtual void setPan (juce::int64 id, std::optional<int> pan, bool startsGesture) = 0;   // nullopt clears
    virtual void setMuted (juce::int64 id, bool muted) = 0;
    virtual void setSoloed (juce::int64 id, bool soloed) = 0;
    virtual int  masterPercent() const = 0;
    virtual void setMasterPercent (int percent) = 0;
    virtual void addListener (Listener*) = 0;
    virtual void removeListener (Listener*) = 0;
};

class SourceMidiTarget : public MixerTarget, private juce::ValueTree::Listener,
                         private PlaybackController::Listener, private juce::AsyncUpdater
{
public:
    SourceMidiTarget (SongDocument&, PlaybackController&);
    ~SourceMidiTarget() override;
    // ... overrides ...
    void flushPendingForTesting() { handleUpdateNowIfNeeded(); }
};

class ActivePlaybackTarget
{
public:
    explicit ActivePlaybackTarget (MixerTarget& initial) : target (&initial) {}
    MixerTarget& get() const noexcept { return *target; }
    // Later: setActive (MixerTarget&) + a change listener; today there is exactly one target.
private:
    MixerTarget* target;
};
```

`SourceMidiTarget` implementation rules: volume 100 removes `playbackVolume` (`doc.removeProperty`), otherwise `doc.setProperty`, both with `startsGesture` (same as `TrackListComponent`'s `onVolumeChanged`); pan nullopt removes `playbackPan`, otherwise sets it; mute/solo/master delegate to the controller. It holds a **persistent** `juce::ValueTree` handle to the document root (`juce::ValueTree rootNode`, per the `juce-valuetree-conventions` skill; `replaceContents` restores in place so it stays valid), registers as the tree listener (any property/child change) and as a `PlaybackController::Listener` (`muteSoloChanged`, `masterChanged`), and coalesces all of these into one `mixerChanged()` through `triggerAsyncUpdate()`. `channels()` skips tracks with `isConductor`, numbers the rest from 1, and reads mute/solo/silenced from the controller.

- [ ] **Step 1: Write failing tests** (`Tests/SourceMidiTarget_tests.cpp`; copy the document/controller/`RecordingSink` setup from `PlaybackController_tests.cpp`):

```cpp
TEST_CASE ("SourceMidiTarget: channels skip the conductor and number the rest", "[mixer][target]") { /* conductor + 2 tracks: 2 channels, numbers 1, 2, names/colours match */ }
TEST_CASE ("SourceMidiTarget: setVolume writes playbackVolume, 100 removes it, one undo step per gesture", "[mixer][target]")
{
    // setVolume (id, 40, true); setVolume (id, 30, false); setVolume (id, 20, false);
    // CHECK trackPlaybackVolume == 20; doc.getUndoManager().undo() once -> back to the pre-gesture value.
    // setVolume (id, 100, true) -> property absent.
}
TEST_CASE ("SourceMidiTarget: setPan writes playbackPan, nullopt removes it", "[mixer][target][pan]") { /* ... */ }
TEST_CASE ("SourceMidiTarget: mute, solo and master go through the controller", "[mixer][target]") { /* isMuted/isSoloed/silencedBySolo reflected in channels(); masterPercent round trips */ }
TEST_CASE ("SourceMidiTarget: a change made anywhere notifies once after coalescing", "[mixer][target]")
{
    // A counting MixerTarget::Listener. Change a track name, volume, and add a track, then
    // flushPendingForTesting(): exactly one mixerChanged.
}
TEST_CASE ("SourceMidiTarget: survives replaceContents with different tracks", "[mixer][target]")
{
    // Save a different Song, doc.replaceContents (loaded), flush: channels() is the new track list
    // and later edits still notify (the root handle was not orphaned).
}
TEST_CASE ("SourceMidiTarget: a Song with no tracks or only a conductor has no channels", "[mixer][target]") { /* ... */ }
```

- [ ] **Step 2:** build, `ctest --test-dir build -R "mixer" --output-on-failure`. Expected: FAIL (missing headers).
- [ ] **Step 3:** implement the three headers/files exactly as the interface block above.
- [ ] **Step 4:** run `ctest --test-dir build -R "mixer" --output-on-failure`. Expected: PASS.
- [ ] **Step 5: Commit**

```bash
git add Source/UI/Mixer Tests/SourceMidiTarget_tests.cpp Tests/CMakeLists.txt CMakeLists.txt
git commit -m "feat(ui): MixerTarget seam with the source-MIDI implementation

Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>"
```

---

### Task 4: Share the mute/solo icon button

**Files:**
- Create: `Source/UI/MuteSoloIconButton.{h,cpp}`
- Modify: `Source/UI/TrackHeadComponent.{h,cpp}` (remove the nested `IconButton` and its anonymous-namespace glyph helpers; use the shared class)
- Test: existing `Tests/TrackHeadComponent_tests.cpp` must stay green; add `Tests/MuteSoloIconButton_tests.cpp` (a render check)

**Interfaces:**
- Produces: `class MuteSoloIconButton : public juce::Button { enum class Kind { Mute, Solo }; MuteSoloIconButton (Kind, juce::Colour onColour); void paintButton (...) override; }` — identical drawing to today's `TrackHeadComponent::IconButton` (off fill `0xFF4A4A4A`, rounded rect, glyph inset 4 px, speaker / headphones, `clickingTogglesState`, no keyboard focus).

- [ ] **Step 1: Write a failing render test:** construct a `Mute` button 24x24, `paintEntireComponent` into an ARGB image, CHECK a pixel inside the body differs between `setToggleState (false)` and `(true)` (on-colour fill), and that the off fill pixel equals `juce::Colour (0xFF4A4A4A)` away from the glyph (e.g. at `(2, 2)` inset corner is inside the rounded rect only if radius allows; pick `(12, 2)`).
- [ ] **Step 2:** build, `ctest --test-dir build -R MuteSoloIconButton`. Expected: FAIL (missing header).
- [ ] **Step 3:** move the glyph helpers and `paintButton` verbatim from `TrackHeadComponent.cpp` (lines ~13-103) into `MuteSoloIconButton.cpp`; in `TrackHeadComponent.h` replace the nested class with `#include "MuteSoloIconButton.h"` and `MuteSoloIconButton muteButton { MuteSoloIconButton::Kind::Mute, juce::Colours::orangered };` (same for solo with `gold`). Add the new `.cpp` to both CMake lists.
- [ ] **Step 4:** `ctest --test-dir build -j1 2>&1 | tail -3`. Expected: everything passes (pure refactor).
- [ ] **Step 5: Commit** `refactor(ui): extract the mute/solo icon button for reuse by the mixer`.

---

### Task 5: `MixerStripComponent` and `MixerPanelComponent`

**Files:**
- Create: `Source/UI/Mixer/MixerStripComponent.{h,cpp}`, `Source/UI/Mixer/MixerPanelComponent.{h,cpp}`
- Test: `Tests/MixerPanelComponent_tests.cpp`
- Modify: both CMake files

**Interfaces:**
- Consumes: Task 3 (`MixerTarget`, `MixerChannel`, `ActivePlaybackTarget`), Task 4 (`MuteSoloIconButton`).
- Produces:

```cpp
class MixerStripComponent : public juce::Component
{
public:
    static constexpr int width = 64;
    explicit MixerStripComponent (MixerTarget&, juce::int64 channelId);   // per-track strip
    void refresh (const MixerChannel&);   // sets controls WITHOUT notification
    // test access
    juce::Slider& faderForTesting();
    juce::Slider& panForTesting();
    juce::Button& muteForTesting();
    juce::Button& soloForTesting();
};

class MixerPanelComponent : public juce::Component, private MixerTarget::Listener
{
public:
    explicit MixerPanelComponent (ActivePlaybackTarget&);
    ~MixerPanelComponent() override;
    int numTrackStripsForTesting() const;
    MixerStripComponent* trackStripForTesting (int index);
    juce::Slider& masterFaderForTesting();
    static constexpr int masterWidth = 72;
    static constexpr int minimumHeight = 150;
};
```

Behaviour:
- Strip layout, top to bottom: pan knob (`juce::Slider::RotaryHorizontalVerticalDrag`, range -100..100, no text box; a `PanKnob` subclass whose `mouseDoubleClick` calls the clear path, i.e. `target.setPan (id, std::nullopt, true)`); a small "MIDI" label under the knob while `pan` is nullopt (tooltip "Pan follows the MIDI file. Move the knob to override; double-click to follow the file again."); mute and solo `MuteSoloIconButton`s side by side; a `LinearVertical` fader 0..100, no text box, `setDoubleClickReturnValue (true, 100.0)`, popup value bubble with `%` suffix as the head's slider does; the number and name label at the bottom, text in the track colour, tooltip full name.
- Callbacks: fader `onDragStart/onDragEnd/onValueChange` use the same `dragging` / `dragHasChanged` gesture tracking as `TrackHeadComponent` (copy that block, `TrackHeadComponent.cpp:124-134`) and call `target.setVolume (id, value, starts)`; pan knob likewise calls `target.setPan (id, value, starts)`; mute/solo toggles call `target.setMuted` / `setSoloed`. `refresh()` sets values with `dontSendNotification`, dims the strip's mute button like the head when `silencedBySolo` (`setAlpha` on the strip's fader area at 0.5 is sufficient; match the head's visual rule by reading `TrackRowComponent::setMuteSolo`).
- Panel: Master strip pinned at the left (fader bound to `masterPercent`/`setMasterPercent`, label "MASTER", no mute/solo, no pan), then a `juce::Viewport` holding the track strips left to right, each `MixerStripComponent::width` wide with a 1 px gutter; horizontal scroll bar on overflow. On `mixerChanged()` it re-reads `channels()`: if the id list differs from the current strips' ids it rebuilds the strips (destroy and recreate, never keep a strip whose id is gone); otherwise it calls `refresh` on each strip. The panel unregisters from the target in its destructor. When there are no channels only the Master strip shows.

- [ ] **Step 1: Write failing tests** (`Tests/MixerPanelComponent_tests.cpp`, real `SongDocument` + `PlaybackController` + `RecordingSink` + `SourceMidiTarget`, plus a `juce::ScopedJuceInitialiser_GUI`; pump with `target.flushPendingForTesting()`):

```cpp
TEST_CASE ("MixerPanel: one strip per non-conductor track, in order", "[mixer][panel]") { /* 3 tracks + conductor -> 3 strips */ }
TEST_CASE ("MixerPanel: moving a fader writes playbackVolume in one undo step", "[mixer][panel]")
{
    // strip.faderForTesting().setValue (40, juce::sendNotificationSync) three times with onDragStart/onDragEnd
    // invoked around them (call the slider's onDragStart/onDragEnd lambdas); undo once -> original volume.
}
TEST_CASE ("MixerPanel: a head-side volume change moves the fader without echoing", "[mixer][panel]")
{
    // doc.setProperty (track, SongIDs::playbackVolume, 25, true); target.flushPendingForTesting();
    // CHECK fader value == 25 and the undo stack gained no extra transaction.
}
TEST_CASE ("MixerPanel: pan knob override and double-click clearing", "[mixer][panel][pan]")
{
    // set pan knob to -60 -> playbackPan == -60, "MIDI" marker hidden;
    // call the knob's mouseDoubleClick -> playbackPan removed, marker shown.
}
TEST_CASE ("MixerPanel: mute and solo stay in step with the controller", "[mixer][panel]")
{
    // click the strip's mute -> controller.isMuted; controller.setSoloed (other, true) -> this strip shows silenced.
}
TEST_CASE ("MixerPanel: strips follow track add, remove and a replaced document", "[mixer][panel]")
{
    // add a track -> 4 strips; remove one -> 3 (no dangling pointer: call a getter on each remaining strip);
    // doc.replaceContents with a different Song -> strips match the new tracks;
    // an empty / conductor-only Song -> 0 track strips, master still present.
}
TEST_CASE ("MixerPanel: the master fader drives the controller", "[mixer][panel][master]") { /* masterFader 60 -> controller.masterPercent() == 60 */ }
```

- [ ] **Step 2:** build, `ctest --test-dir build -R "mixer" --output-on-failure`. Expected: FAIL (missing headers).
- [ ] **Step 3:** implement as described. Draw a 1 px divider line between strips and use `SongsmithColours` for backgrounds/text (read `SongsmithColours.h` for the names already used by `TrackHeadComponent::paint`).
- [ ] **Step 4:** `ctest --test-dir build -R "mixer|MuteSolo|TrackHead" --output-on-failure`. Expected: PASS.
- [ ] **Step 5: Commit** `feat(ui): mixer strips and panel with a pinned master strip`.

---

### Task 6: Register the panel, View toggle and wiring

**Files:**
- Modify: `Source/UI/Dock/DockLayoutState.cpp:100-104` (add the def), `Source/UI/MenuModel.h`, `Source/UI/SongsmithMainComponent.{h,cpp}`, `Source/UI/MainWindow.cpp`
- Test: `Tests/DockLayoutState_tests.cpp`, `Tests/DockHost_tests.cpp` (if it counts panels), `Tests/MenuModel_tests.cpp`, `Tests/SongsmithMainComponent_tests.cpp`

**Interfaces:**
- Consumes: Task 5's `MixerPanelComponent`, Task 3's `SourceMidiTarget` / `ActivePlaybackTarget`.
- Produces: `panelDefs()[3]` = `{ "mixer", "Mixer", Zone::bottom, Mode::docked, false, 0.6, 150, 1 }` (hidden by default; `defaultOrder` 1 puts it last in the bottom zone, below the LOTRO preview, so a saved layout from before it existed gains it there. This refines the spec's "after Parts"); `MenuCommandId::ViewMixerToggle`; `MenuState::mixerVisible`; `SongsmithMainComponent::isMixerVisible()` / `setMixerVisible (bool)`.

- [ ] **Step 1: Write failing tests:**
  - `DockLayoutState_tests`: `panelDefs()` has `mixer`; the default layout has it hidden, docked, bottom zone, order after the preview; a saved layout JSON from before `mixer` existed (three panels) normalises to include it; JSON round trip keeps its visibility and weight.
  - `MenuModel_tests`: `allCommandIds.size() == 34`; the View menu contains a checkable "Mixer" item whose tick follows `MenuState::mixerVisible`.
  - `SongsmithMainComponent_tests` (copy a nearby Parts test): `setMixerVisible (true)` makes the mixer panel visible in the bottom stack and fires `onDockLayoutChanged`; the first-run geometry tests that assumed the bottom zone's contents still pass because the mixer is hidden by default.
- [ ] **Step 2:** build, run `ctest --test-dir build -R "Dock|MenuModel|SongsmithMain" --output-on-failure`. Expected: FAIL.
- [ ] **Step 3: Implement.** In `DockLayoutState.cpp` add the def after `parts`. In `MenuModel.h` add `ViewMixerToggle` after `ViewPartsToggle` in the enum, add it to `allCommandIds` (34), add `bool mixerVisible = false;`, add the View item `item (ViewMixerToggle, "Mixer", {}, true, s.mixerVisible)` after the Parts item, and bump `MenuModel_tests`'s size check. In `SongsmithMainComponent`, own `std::unique_ptr<SourceMidiTarget> mixerTarget; std::unique_ptr<ActivePlaybackTarget> activeTarget; std::unique_ptr<MixerPanelComponent> mixerPanel;` created only when `playback != nullptr` (constructor: after `partStrip` registration, `dockHost.addPanel (dock::panelDefs()[3], juce::Colour (SongsmithColours::text), *mixerPanel);` — when `playback` is null, as in some tests, add nothing and make `isMixerVisible` return false; check how `dockHost.addPanel` and `apply` treat a def with no registered panel and handle it the way the existing code does for the transport-less case, reading `DockHost.cpp` first). Declare members so the panel is destroyed before the target and the target before nothing it depends on (reverse of construction). Add `isMixerVisible` / `setMixerVisible` beside the Parts ones. In `MainWindow.cpp`: `s.mixerVisible = songsmith.isMixerVisible();` (near line 246) and `case ViewMixerToggle: body->getSongsmith().setMixerVisible (! body->getSongsmith().isMixerVisible()); return;` (near line 354).
- [ ] **Step 4:** full run `ctest --test-dir build -j1 2>&1 | tail -5` after a clean `cmake --build build 2>&1 | grep -E " error "`. Expected: all pass.
- [ ] **Step 5: Commit** `feat(ui): the Mixer is a dockable panel with a View menu toggle`.

---

### Task 7: Docs, TODO and spec tweak

**Files:**
- Modify: `docs/ARCHITECTURE.md` (§9.13 add `playbackPan`; §9.14 pan override and master gain; §9.19 mixer panel plus the `MixerTarget` seam), `docs/UI_GUIDE.md`, `docs/TESTING.md` (test count: run `ctest --test-dir build -N | tail -1`), `docs/songsmith-ui-map.html`, `CLAUDE.md` (status line and docs map row for the spec/plan), `docs/TODO.md` (tick item 2; add a line for "make the View menu follow `panelDefs()`" only if not already present), `docs/superpowers/specs/2026-10-12-songsmith-mixer-design.md` (change "`defaultOrder` after Parts" to "`defaultOrder` 1, last in the bottom zone").
- [ ] **Step 1:** make the edits above, describing behaviour as built (read the final code before writing; do not paste the plan's wording).
- [ ] **Step 2:** `ctest --test-dir build -j1 2>&1 | tail -3`; confirm the count in `docs/TESTING.md` matches.
- [ ] **Step 3: Commit** `docs: describe the Mixer panel, pan override and master gain`.

---

## Manual verification (user, on the deployed exe; not automatable)

- Pan sweeps audibly left/right, live, on a track while it plays; double-click returns to the file's own pan.
- Faders feel right to drag; head sliders and mixer faders stay in step; mute/solo agree between the heads and the mixer.
- Master fader lowers the overall level smoothly.
- The Mixer docks, floats, hides and reappears with its layout remembered.

## Self-review notes

- Spec coverage: data (Task 1), engine (2), seam (3), UI (4-5), docking/menu (6), docs (7), testing as listed, out-of-scope items absent.
- `trackPlaybackPan`, `panOverride`, `setChannelPan`, `setMasterPercent`, `masterChanged`, `MixerChannel`, `SourceMidiTarget::flushPendingForTesting` are used with the same names in every task.
- One deliberate refinement of the spec: the `mixer` `defaultOrder` is 1 (below the preview) instead of "after Parts", because Parts and the preview already occupy -1 and 0; Task 7 corrects the spec.
