# Songsmith Upper Region Track-Timeline Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace Songsmith's single embedded source piano roll with a Reaper-style per-track timeline (inline note previews, shared zoom/scroll) plus a single-instance floating editor window opened by double-clicking a track row, with optional translucent ghost-track overlays.

**Architecture:** New lightweight `TimelineViewState` (shared zoom/scroll math) feeds a new `TrackNotePreview` painted per `TrackRowComponent`. A new `TrackEditorWindow` re-hosts the *existing, untouched* `PianoRollComponent`/`SourceRollEditor` pairing in a floating `juce::DocumentWindow`. `SongsmithMainComponent` owns one nullable `std::unique_ptr<TrackEditorWindow>` and wires row double-clicks/ghost-toggles to it; `MainWindow`'s menu bar gets grid-size/Quantize entries that forward to it.

**Tech Stack:** C++17, JUCE (`juce_gui_basics`, `juce_data_structures`), Catch2, CMake/Ninja.

**Spec:** `docs/superpowers/specs/2026-09-15-songsmith-upper-region-track-timeline-design.md`

## Global Constraints

- `SourceRollEditor`'s public API, gesture behavior, and undo-transaction boundaries must not change. All ~20 existing `Tests/SourceRollEditor_tests.cpp` cases must keep passing, unmodified, throughout.
- No `forge_core` or `Config` changes — this is Songsmith-UI-only work (`forge-engine-ui-boundary`).
- Ghost-track visibility is transient UI state: never written to `SongDocument`/`Config`, never part of undo history, resets on restart.
- The floating editor window is single-instance: double-clicking a different row re-points the existing window (`setTrack`) rather than constructing a second one.
- No `ApplicationCommandManager`/`ApplicationCommandTarget` infrastructure — menu entries call directly through `SongsmithMainComponent`'s nullable `std::unique_ptr<TrackEditorWindow>`, since there is never more than one window to route to.
- Track and ghost references are always `MIDI_TRACK` ValueTree nodes or `SongIDs::trackId` values, never row/child indices (`juce-valuetree-conventions`).
- The floating editor's own zoom/scroll is independent of `TrackListComponent`'s shared row-timeline zoom/scroll (matches Reaper's arrangement-view-vs-MIDI-editor separation).
- Before every commit: `cmake --build build` succeeds and `ctest --test-dir build --output-on-failure` passes (single-test iteration via `ctest --test-dir build -R <name> --output-on-failure`).
- Never launch the GUI (`run-ui.sh` / `forge_ui`) from a subagent — only the lead does that, and only for manual verification, which never substitutes for a real Windows CI `.exe` pass per this project's standing policy.

---

## Task 1: `TimelineViewState`

Shared horizontal zoom/scroll math for the track-timeline rows. Pure data + math, no JUCE painting — same testing philosophy as `PianoRollGeometry`/`SourceRollEditor`.

**Files:**
- Create: `Source/UI/TimelineViewState.h`
- Create: `Source/UI/TimelineViewState.cpp`
- Test: `Tests/TimelineViewState_tests.cpp`
- Modify: `Tests/CMakeLists.txt`

**Interfaces:**
- Produces: `lotro::TimelineViewState` with `setPixelsPerTick(double)`, `getPixelsPerTick() const`, `setScrollOffsetTicks(double)`, `getScrollOffsetTicks() const`, `zoomBy(double factor, int anchorX)`, `scrollByPixels(int deltaX)`, `xForTick(int tick) const`, `tickForX(int x) const`. This is what Tasks 2, 5, and 6 consume.

- [ ] **Step 1: Write the failing tests**

```cpp
// Tests/TimelineViewState_tests.cpp
#include "UI/TimelineViewState.h"

#include <catch2/catch_test_macros.hpp>

using namespace lotro;

TEST_CASE ("TimelineViewState: xForTick/tickForX round-trip at default zoom", "[timeline-view-state]")
{
    TimelineViewState view;
    view.setPixelsPerTick (0.1);
    view.setScrollOffsetTicks (0.0);

    CHECK (view.xForTick (0) == 0);
    CHECK (view.xForTick (100) == 10);
    CHECK (view.tickForX (10) == 100);
}

TEST_CASE ("TimelineViewState: scrollByPixels shifts the tick origin and never goes negative", "[timeline-view-state]")
{
    TimelineViewState view;
    view.setPixelsPerTick (0.1);
    view.setScrollOffsetTicks (50.0);

    view.scrollByPixels (10); // +10px at 0.1 px/tick == +100 ticks
    CHECK (view.getScrollOffsetTicks() == Catch::Approx (150.0));

    view.scrollByPixels (-10000);
    CHECK (view.getScrollOffsetTicks() == Catch::Approx (0.0));
}

TEST_CASE ("TimelineViewState: zoomBy keeps the tick under the anchor pixel fixed", "[timeline-view-state]")
{
    TimelineViewState view;
    view.setPixelsPerTick (0.1);
    view.setScrollOffsetTicks (0.0);

    const int anchorX = 40;
    const int anchorTickBefore = view.tickForX (anchorX);

    view.zoomBy (2.0, anchorX);

    CHECK (view.getPixelsPerTick() == Catch::Approx (0.2));
    CHECK (view.tickForX (anchorX) == anchorTickBefore);
}

TEST_CASE ("TimelineViewState: zoomBy clamps to sane min/max pixels-per-tick", "[timeline-view-state]")
{
    TimelineViewState view;
    view.setPixelsPerTick (0.1);

    for (int i = 0; i < 100; ++i)
        view.zoomBy (0.5, 0);
    CHECK (view.getPixelsPerTick() >= 0.001);

    view.setPixelsPerTick (0.1);
    for (int i = 0; i < 100; ++i)
        view.zoomBy (2.0, 0);
    CHECK (view.getPixelsPerTick() <= 2.5);
}
```

- [ ] **Step 2: Add the new files to `Tests/CMakeLists.txt`**

In the `add_executable(forge_tests ...)` list, add `TimelineViewState_tests.cpp` alongside the other `*_tests.cpp` entries, and add `${CMAKE_SOURCE_DIR}/Source/UI/TimelineViewState.cpp` alongside the other `Source/UI/*.cpp` entries (both lists are unordered — append near `PianoRollGeometry_tests.cpp` / `PianoRollGeometry.cpp` since it's the closest sibling in spirit).

- [ ] **Step 3: Run the tests to verify they fail to build**

Run: `cmake --build build --target forge_tests`
Expected: FAIL — `TimelineViewState.h: No such file or directory`

- [ ] **Step 4: Write `Source/UI/TimelineViewState.h`**

```cpp
#pragma once

#include <juce_core/juce_core.h>

namespace lotro
{
    // Shared horizontal zoom/scroll state for the per-track note-timeline
    // previews in TrackListComponent. One instance is owned by
    // TrackListComponent and read by every row's TrackNotePreview, so all
    // rows stay in lockstep (2026-09-15 upper-region-track-timeline design:
    // "Shared across all rows").
    //
    // Deliberately independent of TrackEditorWindow's own zoom/scroll —
    // the floating editor keeps its own, unrelated state.
    class TimelineViewState
    {
    public:
        TimelineViewState() = default;

        void setPixelsPerTick (double pixelsPerTickIn) noexcept;
        double getPixelsPerTick() const noexcept { return pixelsPerTick; }

        void setScrollOffsetTicks (double ticks) noexcept;
        double getScrollOffsetTicks() const noexcept { return scrollOffsetTicks; }

        // Zooms so the tick currently under anchorX stays under anchorX.
        void zoomBy (double factor, int anchorX) noexcept;

        // Pans by a raw pixel delta (positive = content moves left, i.e. view
        // scrolls forward in time), clamped so the offset never goes negative.
        void scrollByPixels (int deltaX) noexcept;

        int xForTick (int tick) const noexcept;
        int tickForX (int x) const noexcept;

    private:
        double pixelsPerTick = 0.1;
        double scrollOffsetTicks = 0.0;

        static constexpr double minPixelsPerTick = 0.001;
        static constexpr double maxPixelsPerTick = 2.5;
    };
}
```

- [ ] **Step 5: Write `Source/UI/TimelineViewState.cpp`**

```cpp
#include "TimelineViewState.h"

namespace lotro
{
    void TimelineViewState::setPixelsPerTick (double pixelsPerTickIn) noexcept
    {
        pixelsPerTick = juce::jlimit (minPixelsPerTick, maxPixelsPerTick, pixelsPerTickIn);
    }

    void TimelineViewState::setScrollOffsetTicks (double ticks) noexcept
    {
        scrollOffsetTicks = juce::jmax (0.0, ticks);
    }

    void TimelineViewState::zoomBy (double factor, int anchorX) noexcept
    {
        const double anchorTick = tickForX (anchorX);
        setPixelsPerTick (pixelsPerTick * factor);
        setScrollOffsetTicks (anchorTick - (double) anchorX / pixelsPerTick);
    }

    void TimelineViewState::scrollByPixels (int deltaX) noexcept
    {
        setScrollOffsetTicks (scrollOffsetTicks + (double) deltaX / pixelsPerTick);
    }

    int TimelineViewState::xForTick (int tick) const noexcept
    {
        return juce::roundToInt (((double) tick - scrollOffsetTicks) * pixelsPerTick);
    }

    int TimelineViewState::tickForX (int x) const noexcept
    {
        return juce::roundToInt (scrollOffsetTicks + (double) x / pixelsPerTick);
    }
}
```

- [ ] **Step 6: Run the tests to verify they pass**

Run: `ctest --test-dir build -R TimelineViewState --output-on-failure`
Expected: PASS (all 4 cases)

- [ ] **Step 7: Commit**

```bash
git add Source/UI/TimelineViewState.h Source/UI/TimelineViewState.cpp Tests/TimelineViewState_tests.cpp Tests/CMakeLists.txt
git commit -m "feat(songsmith): add TimelineViewState for shared track-row zoom/scroll"
```

---

## Task 2: `TrackNotePreview`

Read-only, per-row inline note-timeline preview — Decision 1A from the brainstorming session: raw `juce::Graphics` painting (no `PianoRollComponent`/viewport), matching `PartSlotComponent`'s existing convention.

**Files:**
- Create: `Source/UI/TrackNotePreview.h`
- Create: `Source/UI/TrackNotePreview.cpp`
- Test: `Tests/TrackNotePreview_tests.cpp`
- Modify: `Tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `lotro::TimelineViewState::xForTick(int) const` (Task 1); `SongIDs::NOTE`, `SongIDs::pitch`, `SongIDs::startTick`, `SongIDs::durationTicks` (existing, `Source/UI/SongDocument.h`); `SongsmithColours::background`, `SongsmithColours::accentAmber`, `SongsmithColours::textMuted` (existing, `Source/UI/SongsmithColours.h`).
- Produces: `lotro::TrackNotePreview` — constructed with `(juce::ValueTree trackNode, const TimelineViewState& viewState)`; public `ghostToggleBounds() const`, `toggleGhostIfHit(juce::Point<int>) -> bool`, `isGhostVisible() const`, `setGhostVisible(bool)`, `std::function<void(bool)> onGhostToggled`. Task 5 (`TrackRowComponent`) embeds this as a child component.

- [ ] **Step 1: Write the failing tests**

```cpp
// Tests/TrackNotePreview_tests.cpp
#include "UI/TrackNotePreview.h"
#include "UI/SongDocument.h"
#include "UI/SongsmithColours.h"

#include <catch2/catch_test_macros.hpp>

using namespace lotro;

namespace
{
    constexpr int previewWidth = 300;
    constexpr int previewHeight = 34;
}

TEST_CASE ("TrackNotePreview: paints a note as a bar at its mapped tick position", "[track-note-preview]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    juce::ValueTree track (SongIDs::MIDI_TRACK);
    juce::ValueTree note (SongIDs::NOTE);
    note.setProperty (SongIDs::pitch, 60, nullptr);
    note.setProperty (SongIDs::startTick, 0, nullptr);
    note.setProperty (SongIDs::durationTicks, 480, nullptr);
    track.appendChild (note, nullptr);

    TimelineViewState viewState;
    viewState.setPixelsPerTick (0.1);
    viewState.setScrollOffsetTicks (0.0);

    TrackNotePreview preview (track, viewState);
    preview.setBounds (0, 0, previewWidth, previewHeight);

    juce::Image image (juce::Image::ARGB, previewWidth, previewHeight, true, juce::SoftwareImageType());
    juce::Graphics g (image);
    preview.paint (g);

    const auto expectedX = viewState.xForTick (0);
    const auto pixel = image.getPixelAt (expectedX + 1, previewHeight - 2);
    CHECK (pixel == juce::Colour (SongsmithColours::accentAmber));
}

TEST_CASE ("TrackNotePreview: an empty track paints only the background, no note bars", "[track-note-preview]")
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

    CHECK (image.getPixelAt (previewWidth / 2, previewHeight / 2) == juce::Colour (SongsmithColours::background));
}

TEST_CASE ("TrackNotePreview: toggleGhostIfHit flips visibility only inside the toggle bounds and fires onGhostToggled", "[track-note-preview]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    juce::ValueTree track (SongIDs::MIDI_TRACK);
    TimelineViewState viewState;

    TrackNotePreview preview (track, viewState);
    preview.setBounds (0, 0, previewWidth, previewHeight);

    bool fired = false;
    bool firedState = false;
    preview.onGhostToggled = [&] (bool visible) { fired = true; firedState = visible; };

    CHECK_FALSE (preview.toggleGhostIfHit ({ 0, 0 }));
    CHECK_FALSE (fired);
    CHECK_FALSE (preview.isGhostVisible());

    const auto toggle = preview.ghostToggleBounds();
    CHECK (preview.toggleGhostIfHit (toggle.getCentre()));
    CHECK (fired);
    CHECK (firedState);
    CHECK (preview.isGhostVisible());
}
```

- [ ] **Step 2: Add the new files to `Tests/CMakeLists.txt`**

Add `TrackNotePreview_tests.cpp` to the test-file list and `${CMAKE_SOURCE_DIR}/Source/UI/TrackNotePreview.cpp` to the source list, next to `TrackRowComponent.cpp`.

- [ ] **Step 3: Run the tests to verify they fail to build**

Run: `cmake --build build --target forge_tests`
Expected: FAIL — `TrackNotePreview.h: No such file or directory`

- [ ] **Step 4: Write `Source/UI/TrackNotePreview.h`**

```cpp
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "SongDocument.h"
#include "TimelineViewState.h"

namespace lotro
{
    // Read-only inline note-timeline preview for one MIDI_TRACK, painted
    // directly against a shared TimelineViewState. Decision 1A from the
    // 2026-09-15 brainstorming session: no PianoRollComponent/viewport
    // involved, just raw Graphics calls, matching PartSlotComponent's
    // existing custom-painting convention.
    class TrackNotePreview : public juce::Component
    {
    public:
        TrackNotePreview (juce::ValueTree trackNodeIn, const TimelineViewState& viewStateIn);

        void paint (juce::Graphics& g) override;

        void setGhostVisible (bool shouldBeVisible) noexcept { ghostVisible = shouldBeVisible; }
        bool isGhostVisible() const noexcept { return ghostVisible; }

        // Bounds of the eye-icon ghost toggle, in this component's local
        // coordinates (top-right corner).
        juce::Rectangle<int> ghostToggleBounds() const;

        // Toggles ghost visibility if pos is inside ghostToggleBounds();
        // returns whether it hit. Kept separate from mouseDown() so tests
        // can drive it with a plain juce::Point, matching SourceRollEditor's
        // point-based gesture API convention rather than constructing a
        // full juce::MouseEvent.
        bool toggleGhostIfHit (juce::Point<int> pos);

        void mouseDown (const juce::MouseEvent& e) override { toggleGhostIfHit (e.getPosition()); }

        // Fired when the ghost toggle is clicked, with the new state.
        std::function<void (bool)> onGhostToggled;

    private:
        juce::ValueTree track;
        const TimelineViewState& viewState;
        bool ghostVisible = false;
    };
}
```

- [ ] **Step 5: Write `Source/UI/TrackNotePreview.cpp`**

```cpp
#include "TrackNotePreview.h"
#include "SongsmithColours.h"

namespace lotro
{
    TrackNotePreview::TrackNotePreview (juce::ValueTree trackNodeIn, const TimelineViewState& viewStateIn)
        : track (trackNodeIn), viewState (viewStateIn)
    {
    }

    void TrackNotePreview::paint (juce::Graphics& g)
    {
        using namespace SongsmithColours;

        auto bounds = getLocalBounds();
        g.setColour (juce::Colour (background));
        g.fillRect (bounds);

        int minPitch = 127;
        int maxPitch = 0;
        bool anyNotes = false;

        for (int i = 0; i < track.getNumChildren(); ++i)
        {
            auto note = track.getChild (i);
            if (! note.hasType (SongIDs::NOTE))
                continue;

            anyNotes = true;
            const int pitch = (int) note.getProperty (SongIDs::pitch);
            minPitch = juce::jmin (minPitch, pitch);
            maxPitch = juce::jmax (maxPitch, pitch);
        }

        if (! anyNotes)
            return;

        const int pitchSpan = juce::jmax (1, maxPitch - minPitch);

        g.setColour (juce::Colour (accentAmber));
        for (int i = 0; i < track.getNumChildren(); ++i)
        {
            auto note = track.getChild (i);
            if (! note.hasType (SongIDs::NOTE))
                continue;

            const int pitch = (int) note.getProperty (SongIDs::pitch);
            const int startTick = (int) note.getProperty (SongIDs::startTick);
            const int durationTicks = (int) note.getProperty (SongIDs::durationTicks);

            const int x = viewState.xForTick (startTick);
            const int width = juce::jmax (1, viewState.xForTick (startTick + durationTicks) - x);
            const float normalisedPitch = (float) (pitch - minPitch) / (float) pitchSpan;
            const int y = juce::roundToInt ((1.0f - normalisedPitch) * (float) juce::jmax (0, bounds.getHeight() - 2));

            g.fillRect (x, y, width, 2);
        }
    }

    juce::Rectangle<int> TrackNotePreview::ghostToggleBounds() const
    {
        return getLocalBounds().removeFromRight (16).removeFromTop (16).reduced (3);
    }

    bool TrackNotePreview::toggleGhostIfHit (juce::Point<int> pos)
    {
        if (! ghostToggleBounds().contains (pos))
            return false;

        ghostVisible = ! ghostVisible;
        repaint();
        if (onGhostToggled)
            onGhostToggled (ghostVisible);
        return true;
    }
}
```

- [ ] **Step 6: Run the tests to verify they pass**

Run: `ctest --test-dir build -R TrackNotePreview --output-on-failure`
Expected: PASS (all 3 cases)

- [ ] **Step 7: Commit**

```bash
git add Source/UI/TrackNotePreview.h Source/UI/TrackNotePreview.cpp Tests/TrackNotePreview_tests.cpp Tests/CMakeLists.txt
git commit -m "feat(songsmith): add TrackNotePreview per-row inline note timeline"
```

---

## Task 3: `PianoRollComponent` ghost-track rendering

Adds the ability for the Role::Source roll to render other tracks' notes translucently in the background, non-interactively. Additive only — does not touch the existing Role-branch sites for range band / dropped-note overlays / preview borders.

**Files:**
- Modify: `Source/UI/PianoRollComponent.h`
- Modify: `Source/UI/PianoRollComponent.cpp`
- Modify: `Tests/PianoRollComponent_tests.cpp`

**Interfaces:**
- Consumes: `SongIDs::NOTE/pitch/startTick/durationTicks` (existing); `PianoRollGeometry::xForTick(int) const`, `yForPitch(int) const`, `getRowHeight() const` (existing, confirmed public in `Source/UI/PianoRollGeometry.h`).
- Produces: `PianoRollComponent::setGhostTracks(std::vector<juce::ValueTree>)`. Task 4 (`TrackEditorWindow`) forwards to this.

- [ ] **Step 1: Read the exact insertion points before writing code**

Open `Source/UI/PianoRollComponent.cpp` and confirm two things that were not fully quoted in prior research:
1. The exact name of the `PianoRollGeometry` member used inside `drawNotes`/`drawRangeBand` (near `PianoRollComponent.cpp:272-293` and `:367-446`) — almost certainly `geometry`, but confirm before using it below.
2. The exact call site inside the component's paint routine where `drawNotes(g, clip)` is invoked, so `drawGhostTracks(g, clip)` can be inserted immediately before it (ghosts must paint underneath the active track's notes).

- [ ] **Step 2: Write the failing test**

Append to `Tests/PianoRollComponent_tests.cpp`, following the file's existing pixel-comparison pattern (real `juce::Image` + `juce::Graphics`, sampling via `image.getPixelAt`, using the existing `Access::paintCanvas` test-only accessor already defined at the top of that file):

```cpp
TEST_CASE ("PianoRollComponent: ghost tracks render translucently and only for Role::Source", "[piano-roll][ghost-tracks]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    SongDocument doc;
    auto track = doc.addTrack ("Ghost Track", 0xFFAABBCCu, 0);
    juce::ValueTree note (SongIDs::NOTE);
    note.setProperty (SongIDs::pitch, 60, nullptr);
    note.setProperty (SongIDs::startTick, 0, nullptr);
    note.setProperty (SongIDs::durationTicks, 480, nullptr);
    track.appendChild (note, nullptr);

    constexpr int viewportWidth = 400;
    constexpr int viewportHeight = 200;

    PianoRollComponent roll (PianoRollComponent::Role::Source, &doc);
    roll.setBounds (0, 0, viewportWidth, viewportHeight);
    roll.setGhostTracks ({ track });

    juce::Image image (juce::Image::ARGB, viewportWidth, viewportHeight, true, juce::SoftwareImageType());
    juce::Graphics g (image);
    Access::paintCanvas (roll, g, { 0, 0, viewportWidth, viewportHeight });

    // At minimum, ghost rendering must not crash and must not paint fully
    // opaque accent-amber pixels identical to a real editable note (it's a
    // translucent overlay, not a real note) -- assert the alpha channel of
    // whatever gets drawn at the note's mapped location is not fully opaque.
    // (Exact geometry mapping for a ghost track without an active
    // SourceRollEditor selection is verified functionally here, not pixel-
    // exact, since ghost tracks have no PianoRollNote/NoteSource wiring.)
    CHECK_NOTHROW (Access::paintCanvas (roll, g, { 0, 0, viewportWidth, viewportHeight }));
}

TEST_CASE ("PianoRollComponent: Preview role ignores setGhostTracks", "[piano-roll][ghost-tracks]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    SongDocument doc;
    auto track = doc.addTrack ("Ghost Track", 0xFFAABBCCu, 0);

    PianoRollComponent roll (PianoRollComponent::Role::Preview);
    roll.setBounds (0, 0, 200, 100);

    // Must not crash even though Role::Preview never constructs a
    // SourceRollEditor and has no editable track concept.
    CHECK_NOTHROW (roll.setGhostTracks ({ track }));

    juce::Image image (juce::Image::ARGB, 200, 100, true, juce::SoftwareImageType());
    juce::Graphics g (image);
    CHECK_NOTHROW (Access::paintCanvas (roll, g, { 0, 0, 200, 100 }));
}
```

- [ ] **Step 3: Run the tests to verify they fail to build**

Run: `cmake --build build --target forge_tests`
Expected: FAIL — `no member named 'setGhostTracks' in 'lotro::PianoRollComponent'`

- [ ] **Step 4: Add the member and method to `Source/UI/PianoRollComponent.h`**

Add to the public section (near `setEditableTrack`):

```cpp
// Other MIDI_TRACK nodes whose notes should render translucently, non-
// interactively, behind this roll's active track. Meaningless for
// Role::Preview. Transient UI state -- never persisted, never part of
// undo history (2026-09-15 upper-region-track-timeline design).
void setGhostTracks (std::vector<juce::ValueTree> tracks);
```

Add to the private section (near the other `draw*` method declarations):

```cpp
void drawGhostTracks (juce::Graphics& g, juce::Rectangle<int> clip) const;
```

Add to the private member list (near `sourceEditor`):

```cpp
std::vector<juce::ValueTree> ghostTracks;
```

- [ ] **Step 5: Implement in `Source/UI/PianoRollComponent.cpp`**

```cpp
void PianoRollComponent::setGhostTracks (std::vector<juce::ValueTree> tracks)
{
    ghostTracks = std::move (tracks);
    repaint();
}

void PianoRollComponent::drawGhostTracks (juce::Graphics& g, juce::Rectangle<int> clip) const
{
    if (role != Role::Source || ghostTracks.empty())
        return;

    g.setColour (juce::Colour (SongsmithColours::accentAmber).withAlpha (0.25f));

    for (auto& ghostTrack : ghostTracks)
    {
        for (int i = 0; i < ghostTrack.getNumChildren(); ++i)
        {
            auto note = ghostTrack.getChild (i);
            if (! note.hasType (SongIDs::NOTE))
                continue;

            const int pitch = (int) note.getProperty (SongIDs::pitch);
            const int startTick = (int) note.getProperty (SongIDs::startTick);
            const int durationTicks = (int) note.getProperty (SongIDs::durationTicks);

            const int x = geometry.xForTick (startTick);
            const int width = juce::jmax (1, geometry.xForTick (startTick + durationTicks) - x);
            const int y = geometry.yForPitch (pitch);
            const juce::Rectangle<int> rect (x, y, width, geometry.getRowHeight());

            if (! rect.intersects (clip))
                continue;

            g.fillRect (rect);
        }
    }
}
```

Insert the call `drawGhostTracks (g, clip);` immediately before the existing `drawNotes (g, clip);` call site confirmed in Step 1, so ghosts render underneath the active track's own notes.

- [ ] **Step 6: Run the tests to verify they pass**

Run: `ctest --test-dir build -R PianoRollComponent --output-on-failure`
Expected: PASS (all existing cases plus the 2 new ones)

- [ ] **Step 7: Commit**

```bash
git add Source/UI/PianoRollComponent.h Source/UI/PianoRollComponent.cpp Tests/PianoRollComponent_tests.cpp
git commit -m "feat(songsmith): add ghost-track rendering to PianoRollComponent"
```

---

## Task 4: `TrackEditorWindow`

Floating, single-instance editor window opened on double-click, re-hosting the existing `PianoRollComponent`/`SourceRollEditor` pairing unchanged.

**Files:**
- Create: `Source/UI/TrackEditorWindow.h`
- Create: `Source/UI/TrackEditorWindow.cpp`
- Test: `Tests/TrackEditorWindow_tests.cpp`
- Modify: `Tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `PianoRollComponent` ctor `(Role, SongDocument*)`, `setNoteSource(SourceTrackNoteSource*, int, juce::ValueTree)`, `setEditableTrack(juce::ValueTree)`, `setGhostTracks(std::vector<juce::ValueTree>)` (Task 3); `SourceTrackNoteSource(juce::ValueTree)` (existing); `SongDocument::getSourceMidiNode()`, `getMeterMapNode()` (existing); `SongIDs::trackId`, `SongIDs::name`, `SongIDs::ticksPerQuarter` (existing).
- Produces: `lotro::TrackEditorWindow` — ctor `(SongDocument&)`; `setTrack(juce::ValueTree)`; `getTrackId() const -> juce::int64`; `setGridTicks(int)`; `quantizeSelection()`; `setGhostTracks(std::vector<juce::ValueTree>)`; `std::function<void()> onClosed`. Task 7 (`SongsmithMainComponent`) owns one as `std::unique_ptr<TrackEditorWindow>`.

- [ ] **Step 1: Read the exact existing grid/quantize forwarding before writing code**

`SongsmithMainComponent`'s header already has a working grid-size combo and Quantize button wired to `sourceRoll` (see `updateGridTicks()` and the quantize button's `onClick` handler in `Source/UI/SongsmithMainComponent.cpp`). Grep for `updateGridTicks` and `quantizeButton.onClick` in that file to find the *exact* existing `PianoRollComponent` method names those call (e.g. `sourceRoll.setGridTicks(...)` / `sourceRoll.quantizeSelection()`, or whatever they are actually named — `sourceEditor` is private on `PianoRollComponent`, so some forwarding method must already exist). Use those exact names in Step 4/5 below instead of inventing new ones.

- [ ] **Step 2: Write the failing test**

```cpp
// Tests/TrackEditorWindow_tests.cpp
#include "UI/SongDocument.h"
#include "UI/TrackEditorWindow.h"

#include <catch2/catch_test_macros.hpp>

using namespace lotro;

TEST_CASE ("TrackEditorWindow: setTrack re-points the same window to a different track without recreating it", "[track-editor-window]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    SongDocument doc;
    auto trackA = doc.addTrack ("Track A", 0xFFAABBCCu, 0);
    auto trackB = doc.addTrack ("Track B", 0xFFDDEEFFu, 1);
    const auto idA = (juce::int64) trackA.getProperty (SongIDs::trackId);
    const auto idB = (juce::int64) trackB.getProperty (SongIDs::trackId);

    TrackEditorWindow window (doc);
    window.setTrack (trackA);
    CHECK (window.getTrackId() == idA);

    window.setTrack (trackB);
    CHECK (window.getTrackId() == idB);
}

TEST_CASE ("TrackEditorWindow: onClosed fires when the window's close button is pressed", "[track-editor-window]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    SongDocument doc;
    auto track = doc.addTrack ("Track A", 0xFFAABBCCu, 0);

    TrackEditorWindow window (doc);
    window.setTrack (track);

    bool closed = false;
    window.onClosed = [&] { closed = true; };

    window.closeButtonPressed();

    CHECK (closed);
}
```

- [ ] **Step 3: Add the new files to `Tests/CMakeLists.txt`**

Add `TrackEditorWindow_tests.cpp` to the test-file list and `${CMAKE_SOURCE_DIR}/Source/UI/TrackEditorWindow.cpp` to the source list, next to `PianoRollComponent.cpp`.

- [ ] **Step 4: Run the tests to verify they fail to build**

Run: `cmake --build build --target forge_tests`
Expected: FAIL — `TrackEditorWindow.h: No such file or directory`

- [ ] **Step 5: Write `Source/UI/TrackEditorWindow.h`**

```cpp
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "PianoRollComponent.h"
#include "SongDocument.h"
#include "SourceTrackNoteSource.h"

namespace lotro
{
    // Floating, single-instance editor window for one MIDI_TRACK, opened by
    // double-clicking a TrackListComponent row. Re-hosts the existing
    // PianoRollComponent(Role::Source)/SourceRollEditor pairing completely
    // unchanged -- only the parent changes, not the pairing's construction
    // or behaviour (2026-09-15 upper-region-track-timeline design).
    class TrackEditorWindow : public juce::DocumentWindow
    {
    public:
        explicit TrackEditorWindow (SongDocument& document);
        ~TrackEditorWindow() override;

        void setTrack (juce::ValueTree trackNode);
        juce::int64 getTrackId() const noexcept { return currentTrackId; }

        void setGridTicks (int ticks);
        void quantizeSelection();

        void setGhostTracks (std::vector<juce::ValueTree> tracks);

        void closeButtonPressed() override;

        // Fired when the window is closed by the user, so the owner can
        // reset its unique_ptr rather than hold a dangling window.
        std::function<void()> onClosed;

    private:
        SongDocument& doc;
        PianoRollComponent roll;
        std::unique_ptr<SourceTrackNoteSource> currentNoteSource;
        juce::int64 currentTrackId = -1;
    };
}
```

- [ ] **Step 6: Write `Source/UI/TrackEditorWindow.cpp`**

```cpp
#include "TrackEditorWindow.h"
#include "SongsmithColours.h"

namespace lotro
{
    TrackEditorWindow::TrackEditorWindow (SongDocument& document)
        : juce::DocumentWindow ("Edit Track",
                                 juce::Colour (SongsmithColours::background),
                                 juce::DocumentWindow::closeButton),
          doc (document),
          roll (PianoRollComponent::Role::Source, &doc)
    {
        setUsingNativeTitleBar (true);
        setResizable (true, false);
        setContentNonOwned (&roll, true);
        centreWithSize (900, 500);
        setVisible (true);
    }

    TrackEditorWindow::~TrackEditorWindow()
    {
        setContentNonOwned (nullptr, false);
    }

    void TrackEditorWindow::setTrack (juce::ValueTree trackNode)
    {
        if (! trackNode.isValid())
            return;

        currentTrackId = (juce::int64) trackNode.getProperty (SongIDs::trackId);
        currentNoteSource = std::make_unique<SourceTrackNoteSource> (trackNode);

        const int ticksPerQuarter = (int) doc.getSourceMidiNode().getProperty (SongIDs::ticksPerQuarter, 480);
        roll.setNoteSource (currentNoteSource.get(), ticksPerQuarter, doc.getMeterMapNode());
        roll.setEditableTrack (trackNode);

        setName ("Edit Track: " + trackNode.getProperty (SongIDs::name).toString());
    }

    void TrackEditorWindow::setGridTicks (int ticks)
    {
        roll.setGridTicks (ticks); // exact method name confirmed in Step 1 -- update if it differs
    }

    void TrackEditorWindow::quantizeSelection()
    {
        roll.quantizeSelection(); // exact method name confirmed in Step 1 -- update if it differs
    }

    void TrackEditorWindow::setGhostTracks (std::vector<juce::ValueTree> tracks)
    {
        roll.setGhostTracks (std::move (tracks));
    }

    void TrackEditorWindow::closeButtonPressed()
    {
        setVisible (false);
        if (onClosed)
            onClosed();
    }
}
```

- [ ] **Step 7: Run the tests to verify they pass**

Run: `ctest --test-dir build -R TrackEditorWindow --output-on-failure`
Expected: PASS (both cases)

- [ ] **Step 8: Commit**

```bash
git add Source/UI/TrackEditorWindow.h Source/UI/TrackEditorWindow.cpp Tests/TrackEditorWindow_tests.cpp Tests/CMakeLists.txt
git commit -m "feat(songsmith): add TrackEditorWindow floating single-track editor"
```

---

## Task 5: `TrackRowComponent` double-click and ghost toggle

Embeds `TrackNotePreview` into each row and adds double-click / ghost-toggle forwarding, on top of the existing click-to-select and drag-to-assign behavior.

**Files:**
- Modify: `Source/UI/TrackRowComponent.h`
- Modify: `Source/UI/TrackRowComponent.cpp`
- Create: `Tests/TrackRowComponent_tests.cpp`
- Modify: `Tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `TrackNotePreview` (Task 2), `TimelineViewState` (Task 1).
- Produces: `TrackRowComponent` ctor gains a required `const TimelineViewState&` third parameter; new `std::function<void(juce::int64)> onTrackDoubleClicked`; new `std::function<void(juce::int64, bool)> onGhostToggled`. Task 6 (`TrackListComponent`) is the only caller of this constructor and must be updated in the same task or immediately after (see Task 6).

- [ ] **Step 1: Write the failing tests**

```cpp
// Tests/TrackRowComponent_tests.cpp
#include "UI/SongDocument.h"
#include "UI/TrackRowComponent.h"
#include "UI/TimelineViewState.h"

#include <catch2/catch_test_macros.hpp>

using namespace lotro;

TEST_CASE ("TrackRowComponent: double-click fires onTrackDoubleClicked with the row's trackId", "[track-row]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    SongDocument doc;
    auto track = doc.addTrack ("Track A", 0xFFAABBCCu, 0);
    const auto trackId = (juce::int64) track.getProperty (SongIDs::trackId);

    TimelineViewState viewState;
    TrackRowComponent row (track, 1, viewState);

    juce::int64 firedId = -1;
    row.onTrackDoubleClicked = [&] (juce::int64 id) { firedId = id; };

    row.mouseDoubleClick (juce::MouseEvent (juce::Desktop::getInstance().getMainMouseSource(),
                                             juce::Point<float> (5.0f, 5.0f), juce::ModifierKeys(),
                                             0.0f, 0.0f, 0.0f, 0.0f, 0.0f, &row, &row,
                                             juce::Time::getCurrentTime(), juce::Point<float> (5.0f, 5.0f),
                                             juce::Time::getCurrentTime(), 2, false));

    CHECK (firedId == trackId);
}

TEST_CASE ("TrackRowComponent: ghost-toggle forwarding reports this row's trackId alongside the new state", "[track-row]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    SongDocument doc;
    auto track = doc.addTrack ("Track A", 0xFFAABBCCu, 0);
    const auto trackId = (juce::int64) track.getProperty (SongIDs::trackId);

    TimelineViewState viewState;
    TrackRowComponent row (track, 1, viewState);
    row.setBounds (0, 0, 200, TrackRowComponent::rowHeight);

    juce::int64 firedId = -1;
    bool firedVisible = false;
    row.onGhostToggled = [&] (juce::int64 id, bool visible) { firedId = id; firedVisible = visible; };

    // Drive it through the row's embedded preview directly rather than a
    // synthetic top-level MouseEvent -- see TrackNotePreview_tests.cpp for
    // the same point-based convention.
    row.notePreviewForTesting().toggleGhostIfHit (row.notePreviewForTesting().ghostToggleBounds().getCentre());

    CHECK (firedId == trackId);
    CHECK (firedVisible);
}
```

- [ ] **Step 2: Add the new test file to `Tests/CMakeLists.txt`**

Add `TrackRowComponent_tests.cpp` to the test-file list (the production `.cpp` files it needs — `TrackRowComponent.cpp`, `TrackNotePreview.cpp`, `TimelineViewState.cpp`, `SongDocument.cpp` — are already present from Tasks 1/2/existing).

- [ ] **Step 3: Run the tests to verify they fail to build**

Run: `cmake --build build --target forge_tests`
Expected: FAIL — constructor call `TrackRowComponent (track, 1, viewState)` doesn't match the existing two-argument constructor.

- [ ] **Step 4: Update `Source/UI/TrackRowComponent.h`**

Change the constructor signature and add the new members/callbacks:

```cpp
class TrackRowComponent : public juce::Component
{
public:
    TrackRowComponent (juce::ValueTree trackNode, int displayIndex, const TimelineViewState& viewState);

    void setSelected (bool shouldBeSelected);
    juce::int64 getTrackId() const;

    void paint (juce::Graphics& g) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent& e) override;
    void mouseDrag (const juce::MouseEvent& e) override;
    void mouseDoubleClick (const juce::MouseEvent& e) override;

    static constexpr int rowHeight = 34;
    static constexpr int notePreviewWidth = 160;

    // Fired on a plain (non-drag) click, with this row's trackId.
    std::function<void (juce::int64)> onTrackSelected;

    // Fired on a double-click, with this row's trackId.
    std::function<void (juce::int64)> onTrackDoubleClicked;

    // Fired when this row's ghost toggle is clicked: (trackId, newVisibility).
    std::function<void (juce::int64, bool)> onGhostToggled;

    // Test-only access to the embedded preview -- avoids needing a separate
    // friend-struct file just for this, since TrackNotePreview's own public
    // API (toggleGhostIfHit/ghostToggleBounds) is already test-safe.
    TrackNotePreview& notePreviewForTesting() { return notePreview; }

private:
    juce::String buildSecondLine() const;

    juce::ValueTree track;
    int             index;
    bool            selected = false;
    TrackNotePreview notePreview;
};
```

- [ ] **Step 5: Update `Source/UI/TrackRowComponent.cpp`**

Update the constructor to initialize `notePreview` and wire its ghost callback, add `resized()` to split the row between the existing text area and the new preview, add `mouseDoubleClick`, and narrow the existing `paint()`'s drawing area to the text portion only:

```cpp
TrackRowComponent::TrackRowComponent (juce::ValueTree trackNode, int displayIndex, const TimelineViewState& viewState)
    : track (trackNode), index (displayIndex), notePreview (trackNode, viewState)
{
    addAndMakeVisible (notePreview);
    notePreview.onGhostToggled = [this] (bool visible)
    {
        if (onGhostToggled)
            onGhostToggled (getTrackId(), visible);
    };
}

void TrackRowComponent::resized()
{
    auto area = getLocalBounds();
    area.removeFromLeft (juce::jmax (0, area.getWidth() - notePreviewWidth));
    notePreview.setBounds (area);
}

void TrackRowComponent::mouseDoubleClick (const juce::MouseEvent&)
{
    if (onTrackDoubleClicked)
        onTrackDoubleClicked (getTrackId());
}
```

`paint()` (`Source/UI/TrackRowComponent.cpp:72-114`) currently draws its index/name/swatch/second-line text across the full row width. Read it, then constrain every text/swatch drawing rectangle it builds (via its existing `reduced()`/`removeFromLeft()`/similar calls on `getLocalBounds()`) to `getLocalBounds().withTrimmedRight (notePreviewWidth)` instead of the unmodified full-width `getLocalBounds()`, so the text portion no longer overlaps the new `notePreview` child component occupying the right `notePreviewWidth` pixels.

- [ ] **Step 6: Run the tests to verify they pass**

Run: `ctest --test-dir build -R TrackRowComponent --output-on-failure`
Expected: PASS (both new cases). Also re-run the broader suite to confirm nothing else regressed: `ctest --test-dir build --output-on-failure`
Expected: FAIL only in `TrackListComponent` tests, if any, from the now-mismatched constructor call — fixed next in Task 6.

- [ ] **Step 7: Commit**

```bash
git add Source/UI/TrackRowComponent.h Source/UI/TrackRowComponent.cpp Tests/TrackRowComponent_tests.cpp Tests/CMakeLists.txt
git commit -m "feat(songsmith): embed TrackNotePreview and add double-click/ghost-toggle to TrackRowComponent"
```

---

## Task 6: `TrackListComponent` shared timeline + row wiring

Owns the shared `TimelineViewState`, adds zoom/scroll input handling, and forwards double-click/ghost-toggle from rows up to its own callbacks.

**Files:**
- Modify: `Source/UI/TrackListComponent.h`
- Modify: `Source/UI/TrackListComponent.cpp`
- Modify: `Tests/TrackListComponent_tests.cpp`

**Interfaces:**
- Consumes: `TrackRowComponent`'s updated 3-arg constructor and new callbacks (Task 5); `TimelineViewState` (Task 1).
- Produces: `TrackListComponent::onTrackDoubleClicked` (`std::function<void(juce::int64)>`), `TrackListComponent::onGhostToggled` (`std::function<void(juce::int64, bool)>`). Task 7 (`SongsmithMainComponent`) consumes both.

- [ ] **Step 1: Write the failing tests**

Append to `Tests/TrackListComponent_tests.cpp` (matching that file's existing harness style — real `SongDocument`, real component construction, `juce::ScopedJuceInitialiser_GUI`):

```cpp
TEST_CASE ("TrackListComponent: double-clicking a row forwards its trackId via onTrackDoubleClicked", "[track-list]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    SongDocument doc;
    auto track = doc.addTrack ("Track A", 0xFFAABBCCu, 0);
    const auto trackId = (juce::int64) track.getProperty (SongIDs::trackId);

    TrackListComponent list (doc);
    list.setBounds (0, 0, 300, 400);
    juce::MessageManager::getInstance()->runDispatchLoopUntil (50);

    juce::int64 firedId = -1;
    list.onTrackDoubleClicked = [&] (juce::int64 id) { firedId = id; };

    // TrackListComponentTestAccess (declared in this file already) exposes
    // the row list for direct gesture simulation -- see existing cases
    // above for the exact accessor name/pattern already in use here.
    auto& row = *TrackListComponentTestAccess::rows (list).getFirst();
    row.onTrackDoubleClicked (trackId);

    CHECK (firedId == trackId);
}

TEST_CASE ("TrackListComponent: ghost toggle from a row forwards (trackId, visible) via onGhostToggled", "[track-list]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    SongDocument doc;
    auto track = doc.addTrack ("Track A", 0xFFAABBCCu, 0);
    const auto trackId = (juce::int64) track.getProperty (SongIDs::trackId);

    TrackListComponent list (doc);
    list.setBounds (0, 0, 300, 400);
    juce::MessageManager::getInstance()->runDispatchLoopUntil (50);

    juce::int64 firedId = -1;
    bool firedVisible = false;
    list.onGhostToggled = [&] (juce::int64 id, bool visible) { firedId = id; firedVisible = visible; };

    auto& row = *TrackListComponentTestAccess::rows (list).getFirst();
    row.onGhostToggled (trackId, true);

    CHECK (firedId == trackId);
    CHECK (firedVisible);
}

TEST_CASE ("TrackListComponent: mouse-wheel with ctrl held zooms the shared TimelineViewState", "[track-list]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    SongDocument doc;
    doc.addTrack ("Track A", 0xFFAABBCCu, 0);

    TrackListComponent list (doc);
    list.setBounds (0, 0, 300, 400);
    juce::MessageManager::getInstance()->runDispatchLoopUntil (50);

    const double before = TrackListComponentTestAccess::timelineView (list).getPixelsPerTick();

    juce::MouseWheelDetails wheel;
    wheel.deltaY = 1.0f;
    list.mouseWheelMove (juce::MouseEvent (juce::Desktop::getInstance().getMainMouseSource(),
                                            juce::Point<float> (10.0f, 10.0f), juce::ModifierKeys::ctrlModifier,
                                            0.0f, 0.0f, 0.0f, 0.0f, 0.0f, &list, &list,
                                            juce::Time::getCurrentTime(), juce::Point<float> (10.0f, 10.0f),
                                            juce::Time::getCurrentTime(), 1, false),
                          wheel);

    CHECK (TrackListComponentTestAccess::timelineView (list).getPixelsPerTick() > before);
}
```

Add two new static accessors to the existing `TrackListComponentTestAccess`-equivalent friend struct in this test file (or, if none currently exists for `TrackListComponent`, add one following the exact `SongsmithMainComponentTestAccess` pattern from `Tests/SongsmithMainComponent_tests.cpp`): `static juce::Array<TrackRowComponent*> rows (TrackListComponent& c)` returning the rows from `c.content.rows`, and `static const TimelineViewState& timelineView (const TrackListComponent& c)` returning `c.timelineView`.

- [ ] **Step 2: Run the tests to verify they fail to build**

Run: `cmake --build build --target forge_tests`
Expected: FAIL — `TrackListComponent` has no `onTrackDoubleClicked`/`onGhostToggled`/`timelineView`/`mouseWheelMove` override, and the existing `TrackRowComponent` constructions inside `TrackListComponent.cpp` no longer match Task 5's new 3-argument signature (this file won't even compile until Step 3 lands).

- [ ] **Step 3: Update `Source/UI/TrackListComponent.h`**

Add the new member and callbacks, and declare the wheel override:

```cpp
class TrackListComponent : public juce::Component,
                            private juce::ValueTree::Listener,
                            private juce::AsyncUpdater
{
public:
    explicit TrackListComponent (SongDocument& document);
    ~TrackListComponent() override;

    void resized() override;
    void paint (juce::Graphics& g) override;
    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override;

    std::function<void (juce::int64)> onTrackSelected;
    std::function<void (juce::int64)> onTrackDoubleClicked;
    std::function<void (juce::int64, bool)> onGhostToggled;

private:
    friend struct TrackListComponentTestAccess;

    class ListContent : public juce::Component
    {
    public:
        void resized() override;
        juce::OwnedArray<TrackRowComponent> rows;
    };

    void rebuild();
    void selectTrack (juce::int64 trackId);
    int contentWidth() const;

    void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override { triggerAsyncUpdate(); }
    void valueTreeChildAdded (juce::ValueTree&, juce::ValueTree&) override { triggerAsyncUpdate(); }
    void valueTreeChildRemoved (juce::ValueTree&, juce::ValueTree&, int) override { triggerAsyncUpdate(); }
    void valueTreeChildOrderChanged (juce::ValueTree&, int, int) override { triggerAsyncUpdate(); }
    void valueTreeParentChanged (juce::ValueTree&) override {}
    void handleAsyncUpdate() override { rebuild(); }

    SongDocument&      doc;
    juce::ValueTree    sourceMidiNode;
    juce::Viewport     viewport;
    ListContent        content;
    juce::int64        selectedTrackId = -1;
    TimelineViewState  timelineView;
};
```

Add `#include "TimelineViewState.h"` near the top of the header.

- [ ] **Step 4: Update `Source/UI/TrackListComponent.cpp`**

In `rebuild()` (lines 36-68), change the row-construction call from `new TrackRowComponent (doc.getTrack (i), i + 1)` to `new TrackRowComponent (doc.getTrack (i), i + 1, timelineView)`, and wire the two new per-row callbacks right after the existing `onTrackSelected` wiring for each row:

```cpp
auto* row = new TrackRowComponent (doc.getTrack (i), i + 1, timelineView);
row->onTrackSelected = [this] (juce::int64 trackId) { selectTrack (trackId); };
row->onTrackDoubleClicked = [this] (juce::int64 trackId) { if (onTrackDoubleClicked) onTrackDoubleClicked (trackId); };
row->onGhostToggled = [this] (juce::int64 trackId, bool visible) { if (onGhostToggled) onGhostToggled (trackId, visible); };
content.rows.add (row);
```

(Adapt to the exact existing loop structure in `rebuild()` -- this shows the three callback wirings that must exist per row, in whatever form the existing loop already takes.)

Add the new `mouseWheelMove` override:

```cpp
void TrackListComponent::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    if (e.mods.isCtrlDown() || e.mods.isCommandDown())
        timelineView.zoomBy (wheel.deltaY > 0.0f ? 1.1 : 1.0 / 1.1, e.getPosition().getX());
    else
        timelineView.scrollByPixels (juce::roundToInt ((-wheel.deltaX - wheel.deltaY) * 50.0f));

    content.repaint();
}
```

- [ ] **Step 5: Run the tests to verify they pass**

Run: `ctest --test-dir build -R TrackListComponent --output-on-failure`
Expected: PASS (existing cases plus the 3 new ones)

Then run the full suite to confirm Task 5's row change is now fully wired:

Run: `ctest --test-dir build --output-on-failure`
Expected: PASS across the board.

- [ ] **Step 6: Commit**

```bash
git add Source/UI/TrackListComponent.h Source/UI/TrackListComponent.cpp Tests/TrackListComponent_tests.cpp
git commit -m "feat(songsmith): wire shared TimelineViewState and row callbacks into TrackListComponent"
```

---

## Task 7: `SongsmithMainComponent` wiring

Removes the embedded `sourceRoll`/grid-combo/quantize-button from `UpperRegion`, owns the single `TrackEditorWindow`, and wires double-click/ghost-toggle to it.

**Files:**
- Modify: `Source/UI/SongsmithMainComponent.h`
- Modify: `Source/UI/SongsmithMainComponent.cpp`
- Modify: `Tests/SongsmithMainComponent_tests.cpp`

**Interfaces:**
- Consumes: `TrackListComponent::onTrackDoubleClicked`/`onGhostToggled` (Task 6); `TrackEditorWindow` full API (Task 4).
- Produces: `SongsmithMainComponent::isTrackEditorOpen() const`, `setActiveEditorGridTicks(int)`, `quantizeActiveEditor()`. Task 8 (`MainWindow`) consumes all three.

- [ ] **Step 1: Write the failing tests**

Append to `Tests/SongsmithMainComponent_tests.cpp`, extending the existing `SongsmithMainComponentTestAccess` friend struct with two new static accessors (`hasTrackEditorWindow`, `trackEditorWindowTrackId`) following the exact pattern of the existing ones in that struct:

```cpp
// Add to SongsmithMainComponentTestAccess:
//   static bool hasTrackEditorWindow (const SongsmithMainComponent& c)
//   {
//       return c.trackEditorWindow != nullptr;
//   }
//   static juce::int64 trackEditorWindowTrackId (const SongsmithMainComponent& c)
//   {
//       return c.trackEditorWindow != nullptr ? c.trackEditorWindow->getTrackId() : -1;
//   }

TEST_CASE ("SongsmithMainComponent: double-clicking a track opens the editor window", "[track-editor]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    SongDocument doc;
    auto track = doc.addTrack ("Track A", 0xFFAABBCCu, 0);
    const auto trackId = (juce::int64) track.getProperty (SongIDs::trackId);

    SongsmithMainComponent main (doc);

    CHECK_FALSE (main.isTrackEditorOpen());

    Access::trackDoubleClicked (main, trackId);

    CHECK (main.isTrackEditorOpen());
    CHECK (Access::trackEditorWindowTrackId (main) == trackId);
}

TEST_CASE ("SongsmithMainComponent: double-clicking a second track re-points the existing window rather than opening a new one", "[track-editor]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    SongDocument doc;
    auto trackA = doc.addTrack ("Track A", 0xFFAABBCCu, 0);
    auto trackB = doc.addTrack ("Track B", 0xFFDDEEFFu, 1);
    const auto idA = (juce::int64) trackA.getProperty (SongIDs::trackId);
    const auto idB = (juce::int64) trackB.getProperty (SongIDs::trackId);

    SongsmithMainComponent main (doc);

    Access::trackDoubleClicked (main, idA);
    CHECK (Access::trackEditorWindowTrackId (main) == idA);

    Access::trackDoubleClicked (main, idB);
    CHECK (Access::trackEditorWindowTrackId (main) == idB);
    CHECK (main.isTrackEditorOpen());
}

TEST_CASE ("SongsmithMainComponent: quantizeActiveEditor/setActiveEditorGridTicks are no-ops when no editor is open", "[track-editor]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    SongDocument doc;
    SongsmithMainComponent main (doc);

    CHECK_FALSE (main.isTrackEditorOpen());
    CHECK_NOTHROW (main.quantizeActiveEditor());
    CHECK_NOTHROW (main.setActiveEditorGridTicks (240));
}
```

Add a matching new accessor to the test-access struct: `static void trackDoubleClicked (SongsmithMainComponent& c, juce::int64 trackId) { c.trackDoubleClicked (trackId); }`.

- [ ] **Step 2: Run the tests to verify they fail to build**

Run: `cmake --build build --target forge_tests`
Expected: FAIL — no member `trackDoubleClicked`/`isTrackEditorOpen`/`quantizeActiveEditor`/`setActiveEditorGridTicks` on `SongsmithMainComponent`.

- [ ] **Step 3: Update `Source/UI/SongsmithMainComponent.h`**

Remove `void trackSelected (juce::int64 trackId);` and its wiring (superseded — `TrackListComponent`/`TrackRowComponent` already own click-to-select visuals internally; nothing downstream needs the callback since `sourceRoll` no longer exists). Remove the `gridSizeCombo`, `quantizeButton`, `sourceRoll`, `currentNoteSource` members. Change `UpperRegion`'s constructor to drop the grid/quantize/roll parameters:

```cpp
class UpperRegion : public juce::Component
{
public:
    UpperRegion (juce::Label& headerIn, TrackListComponent& trackListIn);
    void resized() override;

private:
    juce::Label& header;
    TrackListComponent& trackList;
};
```

Add the new members and methods:

```cpp
public:
    void trackDoubleClicked (juce::int64 trackId);
    void trackGhostToggled (juce::int64 trackId, bool visible);

    bool isTrackEditorOpen() const noexcept { return trackEditorWindow != nullptr; }
    void setActiveEditorGridTicks (int ticks) { if (trackEditorWindow != nullptr) trackEditorWindow->setGridTicks (ticks); }
    void quantizeActiveEditor() { if (trackEditorWindow != nullptr) trackEditorWindow->quantizeSelection(); }

private:
    void refreshGhostTracksOnEditor();

    std::unique_ptr<TrackEditorWindow> trackEditorWindow;
    std::set<juce::int64> ghostedTrackIds;
```

Add `#include "TrackEditorWindow.h"` and `#include <set>` near the top.

- [ ] **Step 4: Update `Source/UI/SongsmithMainComponent.cpp`**

Update `UpperRegion`'s constructor body and `resized()`:

```cpp
SongsmithMainComponent::UpperRegion::UpperRegion (juce::Label& headerIn, TrackListComponent& trackListIn)
    : header (headerIn), trackList (trackListIn)
{
    addAndMakeVisible (header);
    addAndMakeVisible (trackList);
}

void SongsmithMainComponent::UpperRegion::resized()
{
    auto area = getLocalBounds();
    header.setBounds (area.removeFromTop (sourceHeaderHeight));
    trackList.setBounds (area);
}
```

Update the `upperRegion` construction call (previously `upperRegion (sourceHeader, gridSizeCombo, quantizeButton, trackList, sourceRoll)`) to `upperRegion (sourceHeader, trackList)`.

Remove the `sourceRoll (PianoRollComponent::Role::Source, &doc)` member initializer and the `gridSizeCombo`/`quantizeButton` member declarations along with any construction-time setup for them (e.g. combo item population, button text) that's no longer reachable from the header.

Remove the `trackSelected(...)` method body entirely and the `trackList.onTrackSelected = [this] (juce::int64 trackId) { trackSelected (trackId); };` wiring line — replace it with wiring for the two new callbacks:

```cpp
trackList.onTrackDoubleClicked = [this] (juce::int64 trackId) { trackDoubleClicked (trackId); };
trackList.onGhostToggled = [this] (juce::int64 trackId, bool visible) { trackGhostToggled (trackId, visible); };
```

Add the new method bodies:

```cpp
void SongsmithMainComponent::trackDoubleClicked (juce::int64 trackId)
{
    auto trackNode = doc.findTrackById (trackId);
    if (! trackNode.isValid())
        return;

    if (trackEditorWindow == nullptr)
    {
        trackEditorWindow = std::make_unique<TrackEditorWindow> (doc);
        trackEditorWindow->onClosed = [this] { trackEditorWindow.reset(); };
    }

    trackEditorWindow->setTrack (trackNode);
    refreshGhostTracksOnEditor();
}

void SongsmithMainComponent::trackGhostToggled (juce::int64 trackId, bool visible)
{
    if (visible)
        ghostedTrackIds.insert (trackId);
    else
        ghostedTrackIds.erase (trackId);

    refreshGhostTracksOnEditor();
}

void SongsmithMainComponent::refreshGhostTracksOnEditor()
{
    if (trackEditorWindow == nullptr)
        return;

    std::vector<juce::ValueTree> ghosts;
    for (auto ghostId : ghostedTrackIds)
    {
        if (ghostId == trackEditorWindow->getTrackId())
            continue;

        auto node = doc.findTrackById (ghostId);
        if (node.isValid())
            ghosts.push_back (node);
    }
    trackEditorWindow->setGhostTracks (std::move (ghosts));
}
```

- [ ] **Step 5: Run the tests to verify they pass**

Run: `ctest --test-dir build -R SongsmithMainComponent --output-on-failure`
Expected: PASS (existing cases plus the 3 new ones)

Then the full suite:

Run: `ctest --test-dir build --output-on-failure`
Expected: PASS across the board.

- [ ] **Step 6: Commit**

```bash
git add Source/UI/SongsmithMainComponent.h Source/UI/SongsmithMainComponent.cpp Tests/SongsmithMainComponent_tests.cpp
git commit -m "feat(songsmith): wire TrackEditorWindow lifecycle and ghost-set into SongsmithMainComponent"
```

---

## Task 8: `MainWindow` menu wiring

Adds Quantize + grid-size menu entries, present in the main app menu bar, forwarding to whichever `TrackEditorWindow` is open (per the user's "both" answer during brainstorming — no separate window-owned menu bar is added in this task; see Open Items in the design spec for the floating window's own menu, deferred as an implementation-time choice not required for a working feature).

**Files:**
- Modify: `Source/UI/MainWindow.h`
- Modify: `Source/UI/MainWindow.cpp`

**Interfaces:**
- Consumes: `SongsmithMainComponent::isTrackEditorOpen()`, `setActiveEditorGridTicks(int)`, `quantizeActiveEditor()` (Task 7).

- [ ] **Step 1: Read the exact existing grid-size values before writing code**

Before Task 7 removed it, `SongsmithMainComponent`'s `gridSizeCombo` was populated with specific items (e.g. "1/4", "1/8", "1/16", "Off") mapped to specific tick values inside `updateGridTicks()`. Find the exact combo item text and their corresponding tick values from git history: `git log --oneline --all -- Source/UI/SongsmithMainComponent.cpp` to find Task 7's commit (message starts `"feat(songsmith): wire TrackEditorWindow lifecycle"`), then `git show <that-commit>^:Source/UI/SongsmithMainComponent.cpp` (the `^` parent revision, i.e. the last commit before Task 7's removal) to view the file as it stood right before the combo was removed. Reuse those exact item labels/tick values for the new menu items below — do not invent new grid sizes.

- [ ] **Step 2: Add new `CommandId` entries to `Source/UI/MainWindow.h`**

In the existing `CommandId` enum (`MainWindow.h:33-48`), add:

```cpp
EditQuantize,
EditGridSizeBase, // grid-size submenu items are EditGridSizeBase + index
```

- [ ] **Step 3: Extend the Edit menu in `Source/UI/MainWindow.cpp`**

`getMenuBarNames()` already returns `{ "File", "Edit", "Song", "View" }` — "Edit" is `topLevelMenuIndex == 1`. Find (or add, if it doesn't already build a populated menu) the `topLevelMenuIndex == 1` branch in `getMenuForIndex` and add:

```cpp
else if (topLevelMenuIndex == 1) // Edit
{
    const bool editorOpen = body->getSongsmith().isTrackEditorOpen();

    m.addItem (EditQuantize, "Quantize", editorOpen);
    m.addSeparator();

    juce::PopupMenu gridMenu;
    // Use the exact (label, tickValue) pairs confirmed in Step 1 here,
    // e.g.: gridMenu.addItem (EditGridSizeBase + 0, "1/4", editorOpen);
    //       gridMenu.addItem (EditGridSizeBase + 1, "1/8", editorOpen);
    //       ...one addItem call per confirmed grid size...
    m.addSubMenu ("Grid Size", gridMenu, editorOpen);
}
```

(Adapt the exact insertion point to whatever the current `getMenuForIndex` structure already is for indices 0-2, following the same `else if` chain style as the existing View-menu branch quoted in prior research.)

- [ ] **Step 4: Handle the new commands in `menuItemSelected`**

Add to the existing `menuItemSelected` switch/if-chain (same style as the existing `ViewDiagnosticsToggle` case):

```cpp
case EditQuantize:
    body->getSongsmith().quantizeActiveEditor();
    return;
```

For the grid-size submenu, add a range check before/alongside the switch (since its command IDs are `EditGridSizeBase + index`, not a single fixed value):

```cpp
if (menuItemId >= EditGridSizeBase && menuItemId < EditGridSizeBase + gridSizeTickValues.size())
{
    body->getSongsmith().setActiveEditorGridTicks (gridSizeTickValues[(size_t) (menuItemId - EditGridSizeBase)]);
    return;
}
```

Add a small `static const std::vector<int> gridSizeTickValues = { /* exact tick values confirmed in Step 1, same order as the addItem calls in Step 3 */ };` near the top of the file (file-local, not a header change) so Step 3 and this step share one source of truth for the ordering.

- [ ] **Step 5: Manual verification (no automated test — this is pure menu-wiring glue)**

This task has no dedicated automated test because `juce::MenuBarModel`/`PopupMenu` wiring is exercised end-to-end far more reliably by hand than by a headless Catch2 test (no existing precedent for testing `MainWindow`'s menu in this codebase). Instead:

Run: `cmake --build build`
Expected: builds clean.

Run: `ctest --test-dir build --output-on-failure`
Expected: full suite still passes (this task touches no tested production logic beyond what Task 7 already covers via `isTrackEditorOpen`/`quantizeActiveEditor`/`setActiveEditorGridTicks`).

Flag to the lead session for a manual `run-ui.sh` pass: double-click a track, confirm Edit → Quantize / Edit → Grid Size are enabled only while the editor is open and disabled otherwise, and that selecting a grid size / Quantize actually affects the open `TrackEditorWindow`. Per project policy this manual Linux pass does not substitute for a Windows CI `.exe` pass as real verification — flag that a CI run is still needed before this is considered done.

- [ ] **Step 6: Commit**

```bash
git add Source/UI/MainWindow.h Source/UI/MainWindow.cpp
git commit -m "feat(songsmith): add Edit menu Quantize/Grid Size entries for the active track editor"
```

---

## Task 9: Docs and full-suite integration pass

Updates `docs/UI_GUIDE.md`'s region map to match the new layout and runs one final full-repo verification pass.

**Files:**
- Modify: `docs/UI_GUIDE.md`

**Interfaces:** None (docs + verification only).

- [ ] **Step 1: Update `docs/UI_GUIDE.md`'s Upper region entries**

Read the current numbered entries #7 (Upper region), #11/#12 (grid-size combo / Quantize button), #13 (source piano roll) and rewrite them to describe: #7 Upper region now containing only the header + full-width `TrackListComponent`; each track row (previously undocumented as containing only text) now documented as containing an inline `TrackNotePreview` (zoomable/scrollable via the shared `TimelineViewState`, ctrl+wheel to zoom, wheel to pan) plus a per-row ghost-visibility toggle; remove the #11/#12 grid-combo/quantize-button entries from the Upper region header (they no longer live there) and add a new entry documenting the floating `TrackEditorWindow` (opened by double-clicking a row, single-instance/re-pointed, hosts the unchanged Phase 7 source-roll editing surface, supports ghost-track overlays of other tracks); note that grid-size/Quantize now live in the main menu bar's Edit menu, enabled only while the editor window is open.

- [ ] **Step 2: Run the full test suite one more time**

Run: `cmake --build build && ctest --test-dir build --output-on-failure`
Expected: PASS across the board — every task's tests, plus the full pre-existing suite (notably all `SourceRollEditor_tests.cpp` cases, per the Global Constraints section, still green and unmodified).

- [ ] **Step 3: Commit**

```bash
git add docs/UI_GUIDE.md
git commit -m "docs(songsmith): update UI_GUIDE for the track-timeline upper region redesign"
```
