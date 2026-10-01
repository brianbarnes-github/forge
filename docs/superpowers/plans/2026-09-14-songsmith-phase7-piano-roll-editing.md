# Songsmith Phase 7: Piano Roll Editing Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking. Also load the project skills `implementing-a-songsmith-phase`, `juce-valuetree-conventions`, and `forge-engine-ui-boundary` before starting any task — they cover conventions this plan assumes throughout.

**Goal:** Add mouse-driven note create/move/resize/delete and a selection-scoped quantize action to the *source* piano roll, all undoable through `SongDocument`'s real `UndoManager` one transaction per gesture, plus Ctrl+Z/Ctrl+Y keyboard wiring.

**Architecture:** A new `SourceRollEditor` class (pure `juce_data_structures`/`juce_events` logic, zero painting) owns selection state and gesture handling over a `MIDI_TRACK` ValueTree; `PianoRollComponent` owns one of these only when constructed in `Role::Source` with a `SongDocument*`, and forwards its `Canvas`'s mouse/key events to it through one delegation point. `SongsmithMainComponent` adds a grid-size combo box and a Quantize button that drive the same editor.

**Tech Stack:** C++17, JUCE (`juce_data_structures`, `juce_gui_basics`), Catch2.

**Spec:** `docs/superpowers/specs/2026-09-14-songsmith-phase7-piano-roll-editing-design.md` (reviewed and approved by the project owner 2026-09-14). Also see the master plan's Phase 7 entry: `/home/brian/.claude/plans/lets-talk-about-how-optimized-marble.md`.

## Global Constraints

- **One gesture = one undo transaction.** A drag/resize opens its transaction on mouse-down and lets every intermediate mouse-move batch into it (`newTransaction=false`); delete and quantize open one transaction and batch every affected note into it — never one transaction per note. (`juce-valuetree-conventions`)
- **Every mutation goes through `SongDocument`'s real `UndoManager`**, never `nullptr` — this phase adds no bulk/non-undoable path (that exception stays limited to MIDI import, untouched here).
- **`NOTE` property names stay exactly as already defined** (`pitch`, `startTick`, `durationTicks`, `velocity`, `isDrum`, `sourceTrackIndex`, `sourceEventIndex`) — no new note-property names are introduced.
- **No `forge_core`/`Config`/`ConfigSource` field or `Constraints/*` pass for quantize or any other edit** — every mutation in this plan lands on `SongDocument`'s `NOTE` nodes, before `SongModelBridge` ever runs. (`forge-engine-ui-boundary`)
- **The preview roll is untouched and stays read-only** — `SourceRollEditor` is only ever constructed for `PianoRollComponent::Role::Source`.
- Conventional-commit prefixes (`feat:`, `fix:`, `refactor:`) per `CLAUDE.md`; one commit per task below, never bundling two tasks.
- Full suite (`ctest --test-dir build --output-on-failure`) must stay green after every task. Baseline at plan-writing time: **223/223 passing**.

## Scope decisions made while planning (not fully pinned down by the spec)

These are concrete calls needed to make the spec implementable; flagged here so review can push back explicitly rather than discovering them buried in code:

1. **Resize is scoped to the single note whose edge is grabbed, even inside a multi-selection.** The spec's Goals sentence naming "an arbitrary set of notes" for selection explicitly lists quantize/move/delete, not resize — group-resize (what happens to 5 notes' individual end-ticks when one is trimmed) is undesigned and out of scope for Phase 7.
2. **Move drags every currently-selected note together** (the spec's Goals sentence does list move). Clicking a note that is *not* already selected replaces the selection with just that note first (so an ordinary single-note drag only ever touches one note); clicking a note that *is* already part of a multi-selection preserves the multi-selection and drags all of them.
3. **The right-click "Delete" context menu is deferred**, not built this phase. Delete/Backspace already satisfies the spec's Delete gesture in full; a `juce::PopupMenu` is a modal, effectively unit-untestable UI affordance duplicating the same action, and the spec frames it as "as well" (secondary). Flagged for a follow-up, not silently dropped.
4. **A quantized note's minimum duration is one grid unit**, not zero — the spec doesn't set a floor, but a zero-length note is nonsensical and this is the smallest reasonable one.
5. **The coordinate-math "prerequisite" fix targets an achievable guarantee, not literally universal round-trip.** The spec's own confirmed example (`ticksPerQuarter=480, pixelsPerQuarterNote=479, tick=240` round-trips to `241`) is, by a pigeonhole argument (480 possible tick values per quarter note, only 479 available pixel columns at that zoom), **mathematically impossible to fix for that exact tick under any rounding rule** without changing `tickForX`'s own convention (which the spec says stays "the single source of truth," i.e. unchanged). Task 1 below makes the two functions share one derivation (closing any gratuitous inconsistency) and proves exact round-trip holds whenever `pixelsPerQuarterNote >= ticksPerQuarter` (at least one pixel per tick — the practically relevant regime), documenting the residual sub-pixel aliasing below that ratio as an inherent display-resolution limit, not a bug. `SourceRollEditor`'s own drag-delta math never depends on the broken direction anyway — deltas are always a difference of two `tickForX` calls, never a round-trip through `xForTick`.

## File Structure

| File | Responsibility |
|---|---|
| `Source/UI/PianoRollGeometry.h/.cpp` | *(modified)* shared `pixelsPerTick()` derivation for `xForTick`/`tickForX`. |
| `Source/UI/SongDocument.h/.cpp` | *(modified)* generic `addChild`/`removeChild` mutation methods. |
| `Source/UI/SourceRollEditor.h/.cpp` | *(new)* all gesture logic: hit-testing, selection, create/move/resize/delete, quantize, keyboard undo/redo. Zero JUCE painting. |
| `Source/UI/PianoRollComponent.h/.cpp` | *(modified)* owns a `SourceRollEditor` for `Role::Source`, forwards `Canvas` mouse/key events to it, paints selection highlight + rubber-band rect. |
| `Source/UI/SongsmithColours.h` | *(modified)* one new `selectionHighlight` constant. |
| `Source/UI/GridSize.h` | *(new)* header-only `GridSize` enum + `gridSizeToTicks()`, used only by `SongsmithMainComponent`'s toolbar. |
| `Source/UI/SongsmithMainComponent.h/.cpp` | *(modified)* grid-size combo box + Quantize button in the upper region's header row; wires `trackSelected` to the roll's editable-track state. |
| `CMakeLists.txt` (root) | *(modified)* add `Source/UI/SourceRollEditor.cpp` to `forge_ui`. |
| `Tests/CMakeLists.txt` | *(modified)* add `SourceRollEditor_tests.cpp`, `GridSize_tests.cpp`, and `SourceRollEditor.cpp` to `forge_tests`. |
| `Tests/PianoRollGeometry_tests.cpp`, `Tests/SongDocument_tests.cpp`, `Tests/SourceRollEditor_tests.cpp` *(new)*, `Tests/PianoRollComponent_tests.cpp`, `Tests/GridSize_tests.cpp` *(new)* | Test files, one per task below. |

---

### Task 1: Coordinate math — shared `pixelsPerTick()` derivation

**Files:**
- Modify: `Source/UI/PianoRollGeometry.h`
- Modify: `Source/UI/PianoRollGeometry.cpp`
- Test: `Tests/PianoRollGeometry_tests.cpp`

**Interfaces:**
- Consumes: nothing new.
- Produces: `PianoRollGeometry::xForTick`/`tickForX` unchanged public signatures; behavior guarantee later tasks rely on: exact round trip when `pixelsPerQuarterNote >= ticksPerQuarter`.

- [ ] **Step 1: Write the failing/characterizing tests**

Append to `Tests/PianoRollGeometry_tests.cpp`:

```cpp
TEST_CASE ("PianoRollGeometry: tick round-trips exactly through xForTick/tickForX when there is at least one pixel per tick", "[piano-roll]")
{
    PianoRollGeometry geometry;
    geometry.setTicksPerQuarter (480);
    geometry.setPixelsPerQuarterNote (960.0); // 2 px/tick -- an exact integer ratio
    geometry.setContentOriginTick (0.0);

    for (int tick : { 0, 1, 100, 240, 479, 480, 1000 })
        CHECK (geometry.tickForX (geometry.xForTick (tick)) == tick);
}

TEST_CASE ("PianoRollGeometry: below 1 pixel per tick, tick round-trip drift is bounded to at most 1 tick, not unbounded", "[piano-roll]")
{
    // The documented, inherent aliasing case: 480 possible tick values per
    // quarter note, only 479 pixel columns to place them in -- some tick
    // must land on a neighbour's pixel (pigeonhole), so this only asserts
    // the bound stays tight. Exact equality here is provably impossible
    // without changing tickForX's own rounding convention -- see the
    // comment on xForTick in PianoRollGeometry.cpp.
    PianoRollGeometry geometry;
    geometry.setTicksPerQuarter (480);
    geometry.setPixelsPerQuarterNote (479.0);
    geometry.setContentOriginTick (0.0);

    for (int tick : { 0, 120, 240, 360, 479 })
    {
        const int diff = geometry.tickForX (geometry.xForTick (tick)) - tick;
        CHECK (diff >= -1);
        CHECK (diff <= 1);
    }
}
```

- [ ] **Step 2: Run the new tests**

Run: `cmake --build build --target forge_tests && ./build/Tests/forge_tests "[piano-roll]"`
Expected: both new cases already PASS on the current code (this is a characterization/refactor task, not a bug fix for these specific values — see Scope decision 5 above). Confirm no other `[piano-roll]` test regresses.

- [ ] **Step 3: Refactor `PianoRollGeometry` to derive both directions from one shared ratio**

In `Source/UI/PianoRollGeometry.h`, add to the `private:` section (near the other private fields):

```cpp
    // Pixels per tick, shared by xForTick/tickForX so both are derived from
    // one computation instead of two independently-structured formulas.
    double pixelsPerTick() const noexcept { return pixelsPerQuarterNote / (double) ticksPerQuarter; }
```

In `Source/UI/PianoRollGeometry.cpp`, replace the bodies of `xForTick`/`tickForX`:

```cpp
int PianoRollGeometry::xForTick (int tick) const noexcept
{
    // The exact algebraic left-inverse of tickForX below: same shared
    // pixelsPerTick() ratio, same rounding rule (std::lround), applied to
    // the inverse expression instead of an independently-structured one.
    // This makes tick -> x -> tick exact whenever pixelsPerQuarterNote >=
    // ticksPerQuarter (at least one pixel per tick -- see
    // PianoRollGeometry_tests.cpp's round-trip test at that ratio). Below
    // that ratio there are more possible tick values per quarter note than
    // pixel columns to hold them, so a handful of ticks necessarily alias
    // onto a neighbour's pixel (confirmed by hand at
    // ticksPerQuarter=480/pixelsPerQuarterNote=479: 480 tick values, only
    // 479 pixel columns) -- an inherent display-resolution limit of
    // zooming below 1:1, not fixable by any choice of rounding rule, and
    // not something Phase 7's drag/resize math depends on (deltas are
    // computed as a difference of two tickForX calls, never by
    // round-tripping through xForTick).
    const double ticksFromOrigin = (double) tick - contentOriginTick;
    return keyboardGutterWidth + (int) std::lround (ticksFromOrigin * pixelsPerTick());
}

int PianoRollGeometry::tickForX (int x) const noexcept
{
    // Source of truth for pixel -> tick -- see xForTick's comment above.
    const double ticksFromOrigin = (double) (x - keyboardGutterWidth) / pixelsPerTick();
    return (int) std::lround (contentOriginTick + ticksFromOrigin);
}
```

- [ ] **Step 4: Run the full existing `PianoRollGeometry` suite plus the new tests**

Run: `cmake --build build --target forge_tests && ./build/Tests/forge_tests "[piano-roll]"`
Expected: all pass, including the pre-existing round-trip tests in `Tests/PianoRollGeometry_tests.cpp`.

- [ ] **Step 5: Commit**

```bash
git add Source/UI/PianoRollGeometry.h Source/UI/PianoRollGeometry.cpp Tests/PianoRollGeometry_tests.cpp
git commit -m "refactor(songsmith): derive PianoRollGeometry's tick<->pixel conversion from one shared ratio"
```

---

### Task 2: `SongDocument` generic `addChild`/`removeChild`

**Files:**
- Modify: `Source/UI/SongDocument.h`
- Modify: `Source/UI/SongDocument.cpp`
- Test: `Tests/SongDocument_tests.cpp`

**Interfaces:**
- Produces: `void SongDocument::addChild (juce::ValueTree parent, juce::ValueTree child, bool newTransaction = true)`, `void SongDocument::removeChild (juce::ValueTree parent, juce::ValueTree child, bool newTransaction = true)` — later tasks' `SourceRollEditor` calls these for NOTE create/delete, mirroring `setProperty`'s existing `newTransaction` convention exactly.

- [ ] **Step 1: Write the failing tests**

Append to `Tests/SongDocument_tests.cpp`:

```cpp
TEST_CASE ("SongDocument: addChild/removeChild default to opening their own transaction", "[songdocument]")
{
    SongDocument doc;
    auto track = doc.addTrack ("Track A", 0xFF0000, 0, 1);

    juce::ValueTree note (SongIDs::NOTE);
    note.setProperty (SongIDs::pitch, 60, nullptr);

    doc.addChild (track, note);
    REQUIRE (track.getNumChildren() == 1);

    doc.removeChild (track, note);
    REQUIRE (track.getNumChildren() == 0);

    doc.undo(); // undoes removeChild only (its own transaction)
    CHECK (track.getNumChildren() == 1);

    doc.undo(); // undoes addChild (its own, separate, earlier transaction)
    CHECK (track.getNumChildren() == 0);
}

TEST_CASE ("SongDocument: addChild/removeChild's newTransaction=false batches into the caller's already-open transaction", "[songdocument]")
{
    SongDocument doc;
    auto track = doc.addTrack ("Track A", 0xFF0000, 0, 1);

    juce::ValueTree noteA (SongIDs::NOTE);
    noteA.setProperty (SongIDs::pitch, 60, nullptr);
    juce::ValueTree noteB (SongIDs::NOTE);
    noteB.setProperty (SongIDs::pitch, 64, nullptr);

    doc.getUndoManager().beginNewTransaction();
    doc.addChild (track, noteA, false);
    doc.addChild (track, noteB, false);
    REQUIRE (track.getNumChildren() == 2);

    doc.undo();
    CHECK (track.getNumChildren() == 0); // one undo() reverts BOTH adds

    doc.redo();
    REQUIRE (track.getNumChildren() == 2);

    doc.getUndoManager().beginNewTransaction();
    doc.removeChild (track, track.getChild (0), false);
    doc.removeChild (track, track.getChild (0), false);
    CHECK (track.getNumChildren() == 0);

    doc.undo();
    CHECK (track.getNumChildren() == 2); // one undo() restores both removed children
}
```

- [ ] **Step 2: Run tests to verify they fail**

Run: `cmake --build build --target forge_tests`
Expected: build FAILS — `addChild`/`removeChild` are not members of `SongDocument` yet.

- [ ] **Step 3: Implement**

In `Source/UI/SongDocument.h`, add to the "Mutations" section (right after `setProperty`'s declaration):

```cpp
    // Generic undoable child insertion -- e.g. adding a NOTE under a
    // MIDI_TRACK from SourceRollEditor's create gesture. newTransaction
    // mirrors setProperty's parameter of the same name/meaning.
    void addChild (juce::ValueTree parent, juce::ValueTree child, bool newTransaction = true);

    // Generic undoable child removal -- e.g. removing a NOTE from a
    // MIDI_TRACK from SourceRollEditor's delete gesture.
    void removeChild (juce::ValueTree parent, juce::ValueTree child, bool newTransaction = true);
```

In `Source/UI/SongDocument.cpp`, add right after `setProperty`'s definition:

```cpp
void SongDocument::addChild (juce::ValueTree parent, juce::ValueTree child, bool newTransaction)
{
    if (newTransaction)
        undoManager.beginNewTransaction();

    parent.addChild (child, -1, &undoManager);
}

void SongDocument::removeChild (juce::ValueTree parent, juce::ValueTree child, bool newTransaction)
{
    if (newTransaction)
        undoManager.beginNewTransaction();

    parent.removeChild (child, &undoManager);
}
```

- [ ] **Step 4: Run tests to verify they pass**

Run: `cmake --build build --target forge_tests && ./build/Tests/forge_tests "[songdocument]"`
Expected: PASS, all `[songdocument]` cases.

- [ ] **Step 5: Full suite + commit**

Run: `ctest --test-dir build --output-on-failure` (expect 225/225 — 223 baseline + the 2 new cases)

```bash
git add Source/UI/SongDocument.h Source/UI/SongDocument.cpp Tests/SongDocument_tests.cpp
git commit -m "feat(songsmith): add SongDocument::addChild/removeChild generic mutations"
```

---

### Task 3: `SourceRollEditor` — hit-testing, selection, move/resize

This is the highest-risk task in this plan (new gesture state machine, no existing analog in this codebase). It creates `SourceRollEditor` and implements everything except create/delete/quantize/keyboard (Tasks 4-6).

**Files:**
- Create: `Source/UI/SourceRollEditor.h`
- Create: `Source/UI/SourceRollEditor.cpp`
- Create: `Tests/SourceRollEditor_tests.cpp`
- Modify: `CMakeLists.txt` (root) — add `Source/UI/SourceRollEditor.cpp` to `forge_ui`'s sources, right after the `Source/UI/PianoRollComponent.cpp` line.
- Modify: `Tests/CMakeLists.txt` — add `SourceRollEditor_tests.cpp` to the test-file list (after `PianoRollComponent_tests.cpp`) and `${CMAKE_SOURCE_DIR}/Source/UI/SourceRollEditor.cpp` to the `forge_tests` source list (after the `PianoRollComponent.cpp` line).

**Interfaces:**
- Consumes: `SongDocument` (`getUndoManager()`, `undo()`, `redo()`, `setProperty()`, `addChild()`/`removeChild()` from Task 2), `PianoRollGeometry` (`noteBounds()`, `tickForX()`, `pitchForY()`, `getTicksPerQuarter()`), `SongIDs::{NOTE,pitch,startTick,durationTicks}`.
- Produces (final class surface used by Tasks 4-8):

```cpp
class SourceRollEditor
{
public:
    explicit SourceRollEditor (SongDocument& document);

    void setTrack (juce::ValueTree trackNodeIn);
    juce::ValueTree getTrackNode() const noexcept { return track; }
    void setGeometry (const PianoRollGeometry& geometryIn) noexcept { geometry = geometryIn; }
    void setGridTicks (int ticks) noexcept { currentGridTicks = ticks; }

    bool isSelected (const juce::ValueTree& note) const;
    int getNumSelected() const noexcept { return (int) selection.size(); }
    juce::Rectangle<int> getRubberBandRect() const noexcept { return rubberBandRect; }

    bool mouseDown (juce::Point<int> pos, juce::ModifierKeys mods, bool isDoubleClick);
    bool mouseDrag (juce::Point<int> pos);
    bool mouseUp (juce::Point<int> pos);
    bool keyPressed (const juce::KeyPress& key);           // Task 6
    bool deleteSelection();                                 // Task 4
    bool quantizeSelection();                                // Task 5
};
```

This task implements everything above except `keyPressed`/`deleteSelection`/`quantizeSelection` (stubbed to `return false;`), and `mouseDown`'s double-click branch (stubbed to `return false;`).

- [ ] **Step 1: Create the full header**

Write `Source/UI/SourceRollEditor.h`:

```cpp
#pragma once

#include "PianoRollGeometry.h"
#include "SongDocument.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <vector>

// Mouse/keyboard-gesture-driven note editing for the *source* piano roll
// only (PianoRollComponent owns one of these when constructed with
// Role::Source and a SongDocument). Operates directly on a MIDI_TRACK
// ValueTree's NOTE children through SongDocument's mutation API -- one undo
// transaction per gesture, per juce-valuetree-conventions. Depends only on
// SongDocument and PianoRollGeometry (juce_data_structures/juce_core, no
// painting), so it is unit-tested directly against a hand-built ValueTree
// in Tests/SourceRollEditor_tests.cpp with zero JUCE painting involved.
//
// Hit-testing never round-trips a click through tickForX/pitchForY back to
// a tick/pitch and then back to a rect -- it compares the click point
// directly against each note's already-computed pixel rect
// (PianoRollGeometry::noteBounds), exactly like painting does. Only
// placing a *new* value (create/move/resize) converts a pixel to a tick,
// via tickForX, the geometry's single source of truth for that direction.
namespace lotro
{

class SourceRollEditor
{
public:
    explicit SourceRollEditor (SongDocument& document);

    // Repoints the editor at a different MIDI_TRACK node (or an invalid
    // ValueTree for "no track selected/editable"). Clears selection and
    // cancels any drag in progress -- selection is scoped to one track.
    void setTrack (juce::ValueTree trackNodeIn);
    juce::ValueTree getTrackNode() const noexcept { return track; }

    // Kept in sync with the owning PianoRollComponent's own geometry by the
    // caller, every time that geometry changes (setNoteSource, zoom) --
    // this class never mutates it, only reads it for hit-testing/pixel<->
    // tick conversion.
    void setGeometry (const PianoRollGeometry& geometryIn) noexcept { geometry = geometryIn; }

    // Ticks spanned by the toolbar's current grid-size selection; 0 means
    // "off". Drives both quantizeSelection()'s snap size and create's
    // default duration fallback (a quarter note, via geometry's
    // ticksPerQuarter, when this is 0). Owned/pushed by
    // SongsmithMainComponent from its grid-size combo box -- this class has
    // no notion of the combo's enum, only the resulting tick count.
    void setGridTicks (int ticks) noexcept { currentGridTicks = ticks; }

    bool isSelected (const juce::ValueTree& note) const;
    int getNumSelected() const noexcept { return (int) selection.size(); }

    // In-progress rubber-band rect, in the same canvas-pixel space as
    // PianoRollGeometry::noteBounds; empty when no rubber-band drag is
    // active. Read by PianoRollComponent's paint to draw it.
    juce::Rectangle<int> getRubberBandRect() const noexcept { return rubberBandRect; }

    // Gesture entry points, called by PianoRollComponent's Canvas from its
    // own mouseDown/mouseDrag/mouseUp overrides with canvas-local pixel
    // coordinates. Each returns true if the caller should repaint (and
    // rebuild the canvas's content size, in case a note's extent changed).
    bool mouseDown (juce::Point<int> pos, juce::ModifierKeys mods, bool isDoubleClick);
    bool mouseDrag (juce::Point<int> pos);
    bool mouseUp (juce::Point<int> pos);

    // Delete/Backspace deletes the current selection; Ctrl+Z/Ctrl+Y (or
    // Ctrl+Shift+Z) undo/redo the whole document. Returns true (repaint
    // hint) if the key was handled, false otherwise.
    bool keyPressed (const juce::KeyPress& key);

    // Removes every currently selected note, one undo transaction
    // regardless of selection size. No-op (false) if nothing is selected.
    // Public so both keyPressed and (a future) right-click context menu
    // (see the plan's scope decisions) can trigger it directly.
    bool deleteSelection();

    // Grid-snaps every selected note's startTick and durationTicks to
    // currentGridTicks, one undo transaction regardless of selection size.
    // No-op (false) if the selection is empty or currentGridTicks <= 0
    // ("off" -- there is no implicit grid to snap to).
    bool quantizeSelection();

private:
    enum class DragMode { None, Move, ResizeLeft, ResizeRight, RubberBand };

    struct DragOriginal
    {
        juce::ValueTree note;
        int startTick;
        int pitch;
        int durationTicks;
    };

    juce::ValueTree hitTestNote (juce::Point<int> pos) const;
    int hitTestEdgeZone (const juce::ValueTree& note, juce::Point<int> pos) const;
    void selectOnly (const juce::ValueTree& note);
    void toggleSelection (const juce::ValueTree& note);
    void pruneSelection();
    void updateRubberBandSelection();
    void createNoteAt (juce::Point<int> pos); // Task 4

    SongDocument& doc;
    juce::ValueTree track;
    PianoRollGeometry geometry;
    int currentGridTicks = 0;

    std::vector<juce::ValueTree> selection;
    std::vector<juce::ValueTree> baseSelectionForRubberBand;

    DragMode dragMode = DragMode::None;
    juce::Point<int> dragStartPos;
    juce::Rectangle<int> rubberBandRect;
    std::vector<DragOriginal> dragOriginals; // Move: every selected note. Resize: just the primary note.

    static constexpr int edgeThresholdPixels = 6;
};

} // namespace lotro
```

- [ ] **Step 2: Register the new files in both CMakeLists**

In `CMakeLists.txt` (root), in `forge_ui`'s source list, add a line `Source/UI/SourceRollEditor.cpp` right after `Source/UI/PianoRollComponent.cpp`.

In `Tests/CMakeLists.txt`, add `SourceRollEditor_tests.cpp` to the test-file list right after `PianoRollComponent_tests.cpp`, and add `${CMAKE_SOURCE_DIR}/Source/UI/SourceRollEditor.cpp` to the `forge_tests` source list right after the `PianoRollComponent.cpp` line.

- [ ] **Step 3: Minimal `.cpp` so the build links**

Write `Source/UI/SourceRollEditor.cpp`:

```cpp
#include "SourceRollEditor.h"

#include <algorithm>

namespace lotro
{

namespace
{
    PianoRollNote toPianoRollNote (const juce::ValueTree& noteNode)
    {
        PianoRollNote note;
        note.pitch         = (int) noteNode.getProperty (SongIDs::pitch);
        note.startTick     = (int) noteNode.getProperty (SongIDs::startTick);
        note.durationTicks = (int) noteNode.getProperty (SongIDs::durationTicks);
        return note;
    }

    juce::Rectangle<int> toRect (const PianoRollNoteBounds& b)
    {
        return { b.x, b.y, b.width, b.height };
    }
}

SourceRollEditor::SourceRollEditor (SongDocument& document) : doc (document) {}

void SourceRollEditor::setTrack (juce::ValueTree trackNodeIn)
{
    track = trackNodeIn;
    selection.clear();
    baseSelectionForRubberBand.clear();
    dragOriginals.clear();
    dragMode = DragMode::None;
    rubberBandRect = {};
}

bool SourceRollEditor::isSelected (const juce::ValueTree& note) const
{
    return std::find (selection.begin(), selection.end(), note) != selection.end();
}

void SourceRollEditor::pruneSelection()
{
    selection.erase (std::remove_if (selection.begin(), selection.end(),
                                      [this] (const juce::ValueTree& n) { return n.getParent() != track; }),
                      selection.end());
}

void SourceRollEditor::selectOnly (const juce::ValueTree& note)
{
    selection.clear();
    if (note.isValid())
        selection.push_back (note);
}

void SourceRollEditor::toggleSelection (const juce::ValueTree& note)
{
    auto it = std::find (selection.begin(), selection.end(), note);
    if (it != selection.end())
        selection.erase (it);
    else
        selection.push_back (note);
}

juce::ValueTree SourceRollEditor::hitTestNote (juce::Point<int> pos) const
{
    if (! track.isValid())
        return {};

    // Back-to-front: a later child paints on top, so it should win the hit
    // test for overlapping notes, matching what's visually on top.
    for (int i = track.getNumChildren(); --i >= 0; )
    {
        auto noteNode = track.getChild (i);
        if (toRect (geometry.noteBounds (toPianoRollNote (noteNode))).contains (pos))
            return noteNode;
    }
    return {};
}

int SourceRollEditor::hitTestEdgeZone (const juce::ValueTree& note, juce::Point<int> pos) const
{
    auto bounds = toRect (geometry.noteBounds (toPianoRollNote (note)));
    if (pos.x <= bounds.getX() + edgeThresholdPixels)
        return -1;
    if (pos.x >= bounds.getRight() - edgeThresholdPixels)
        return 1;
    return 0;
}

void SourceRollEditor::updateRubberBandSelection()
{
    selection = baseSelectionForRubberBand;
    if (! track.isValid())
        return;

    for (int i = 0; i < track.getNumChildren(); ++i)
    {
        auto noteNode = track.getChild (i);
        if (toRect (geometry.noteBounds (toPianoRollNote (noteNode))).intersects (rubberBandRect)
            && ! isSelected (noteNode))
            selection.push_back (noteNode);
    }
}

bool SourceRollEditor::mouseDown (juce::Point<int> pos, juce::ModifierKeys mods, bool isDoubleClick)
{
    pruneSelection();

    if (isDoubleClick)
        return false; // create implemented in Task 4

    auto note = hitTestNote (pos);

    if (! note.isValid())
    {
        baseSelectionForRubberBand = (mods.isShiftDown() || mods.isCtrlDown() || mods.isCommandDown())
                                          ? selection
                                          : std::vector<juce::ValueTree>();
        selection = baseSelectionForRubberBand;
        dragMode = DragMode::RubberBand;
        dragStartPos = pos;
        rubberBandRect = { pos.x, pos.y, 0, 0 };
        return true;
    }

    if (mods.isShiftDown() || mods.isCtrlDown() || mods.isCommandDown())
    {
        toggleSelection (note);
        dragMode = DragMode::None;
        return true;
    }

    if (! isSelected (note))
        selectOnly (note);

    const int edge = hitTestEdgeZone (note, pos);
    dragMode = edge < 0 ? DragMode::ResizeLeft : edge > 0 ? DragMode::ResizeRight : DragMode::Move;
    dragStartPos = pos;

    dragOriginals.clear();
    if (dragMode == DragMode::Move)
    {
        for (auto& n : selection)
            dragOriginals.push_back ({ n, (int) n.getProperty (SongIDs::startTick),
                                        (int) n.getProperty (SongIDs::pitch),
                                        (int) n.getProperty (SongIDs::durationTicks) });
    }
    else
    {
        dragOriginals.push_back ({ note, (int) note.getProperty (SongIDs::startTick),
                                    (int) note.getProperty (SongIDs::pitch),
                                    (int) note.getProperty (SongIDs::durationTicks) });
    }

    doc.getUndoManager().beginNewTransaction();
    return true;
}

bool SourceRollEditor::mouseDrag (juce::Point<int> pos)
{
    if (dragMode == DragMode::RubberBand)
    {
        rubberBandRect = juce::Rectangle<int> (dragStartPos, pos);
        updateRubberBandSelection();
        return true;
    }

    if (dragMode == DragMode::None || dragOriginals.empty())
        return false;

    const int deltaTick = geometry.tickForX (pos.x) - geometry.tickForX (dragStartPos.x);

    if (dragMode == DragMode::Move)
    {
        const int deltaPitch = geometry.pitchForY (pos.y) - geometry.pitchForY (dragStartPos.y);
        for (auto& orig : dragOriginals)
        {
            const int newStart = std::max (0, orig.startTick + deltaTick);
            const int newPitch = juce::jlimit (0, 127, orig.pitch + deltaPitch);
            if ((int) orig.note.getProperty (SongIDs::startTick) != newStart)
                doc.setProperty (orig.note, SongIDs::startTick, newStart, false);
            if ((int) orig.note.getProperty (SongIDs::pitch) != newPitch)
                doc.setProperty (orig.note, SongIDs::pitch, newPitch, false);
        }
        return true;
    }

    // Resize: dragOriginals holds exactly one entry (the primary note --
    // resize is not group-scoped, see the plan's scope decisions).
    auto& orig = dragOriginals.front();
    if (dragMode == DragMode::ResizeRight)
    {
        const int newDuration = std::max (1, orig.durationTicks + deltaTick);
        if ((int) orig.note.getProperty (SongIDs::durationTicks) != newDuration)
            doc.setProperty (orig.note, SongIDs::durationTicks, newDuration, false);
    }
    else // ResizeLeft: end tick (startTick + durationTicks) stays fixed.
    {
        const int endTick = orig.startTick + orig.durationTicks;
        const int newStart = juce::jlimit (0, endTick - 1, orig.startTick + deltaTick);
        const int newDuration = endTick - newStart;
        if ((int) orig.note.getProperty (SongIDs::startTick) != newStart)
            doc.setProperty (orig.note, SongIDs::startTick, newStart, false);
        if ((int) orig.note.getProperty (SongIDs::durationTicks) != newDuration)
            doc.setProperty (orig.note, SongIDs::durationTicks, newDuration, false);
    }
    return true;
}

bool SourceRollEditor::mouseUp (juce::Point<int>)
{
    const bool wasActive = dragMode != DragMode::None;
    dragMode = DragMode::None;
    dragOriginals.clear();
    rubberBandRect = {};
    return wasActive;
}

bool SourceRollEditor::keyPressed (const juce::KeyPress&) { return false; }     // Task 6
bool SourceRollEditor::deleteSelection() { return false; }                      // Task 4
bool SourceRollEditor::quantizeSelection() { return false; }                    // Task 5
void SourceRollEditor::createNoteAt (juce::Point<int>) {}                       // Task 4

} // namespace lotro
```

- [ ] **Step 4: Write the failing selection tests**

Write `Tests/SourceRollEditor_tests.cpp`:

```cpp
// Verifies SourceRollEditor's mouse-gesture-driven note editing against a
// hand-built ValueTree: hit-testing, click/shift-click/rubber-band
// selection, and move/resize drags, plus their undo-transaction
// boundaries. No JUCE painting involved.

#include "UI/SourceRollEditor.h"

#include <catch2/catch_test_macros.hpp>

using namespace lotro;

namespace
{
    constexpr int ticksPerQuarter = 480;
    constexpr int viewportWidth = 800;
    constexpr int viewportHeight = 300;

    juce::ValueTree makeNote (int pitch, int startTick, int durationTicks)
    {
        juce::ValueTree note (SongIDs::NOTE);
        note.setProperty (SongIDs::pitch, pitch, nullptr);
        note.setProperty (SongIDs::startTick, startTick, nullptr);
        note.setProperty (SongIDs::durationTicks, durationTicks, nullptr);
        note.setProperty (SongIDs::sourceTrackIndex, -1, nullptr);
        note.setProperty (SongIDs::sourceEventIndex, -1, nullptr);
        return note;
    }

    // A track with two one-quarter-note notes: pitch 60 at tick 0, pitch 64
    // immediately following at tick 480.
    struct Fixture
    {
        SongDocument doc;
        juce::ValueTree track = doc.addTrack ("Track A", 0xFF0000, 0, 1);
        juce::ValueTree noteA = makeNote (60, 0, ticksPerQuarter);
        juce::ValueTree noteB = makeNote (64, ticksPerQuarter, ticksPerQuarter);
        PianoRollGeometry geometry = PianoRollGeometry::fitToContent ({ 0, ticksPerQuarter * 2 }, { 60, 65 },
                                                                        ticksPerQuarter, viewportWidth, viewportHeight);
        SourceRollEditor editor { doc };

        Fixture()
        {
            track.addChild (noteA, -1, nullptr);
            track.addChild (noteB, -1, nullptr);
            editor.setTrack (track);
            editor.setGeometry (geometry);
        }

        static PianoRollNote toNote (const juce::ValueTree& n)
        {
            PianoRollNote pn;
            pn.pitch = (int) n.getProperty (SongIDs::pitch);
            pn.startTick = (int) n.getProperty (SongIDs::startTick);
            pn.durationTicks = (int) n.getProperty (SongIDs::durationTicks);
            return pn;
        }

        juce::Point<int> centreOf (const juce::ValueTree& note) const
        {
            auto b = geometry.noteBounds (toNote (note));
            return { b.x + b.width / 2, b.y + b.height / 2 };
        }
    };
}

TEST_CASE ("SourceRollEditor: a plain click on a note selects only that note", "[source-roll-editor]")
{
    Fixture f;
    CHECK (f.editor.mouseDown (f.centreOf (f.noteA), {}, false));
    CHECK (f.editor.isSelected (f.noteA));
    CHECK_FALSE (f.editor.isSelected (f.noteB));
    CHECK (f.editor.getNumSelected() == 1);
}

TEST_CASE ("SourceRollEditor: clicking empty space clears the selection", "[source-roll-editor]")
{
    Fixture f;
    f.editor.mouseDown (f.centreOf (f.noteA), {}, false);
    REQUIRE (f.editor.isSelected (f.noteA));

    f.editor.mouseDown ({ f.geometry.getKeyboardGutterWidth() + 2, f.geometry.yForPitch (70) }, {}, false);
    CHECK_FALSE (f.editor.isSelected (f.noteA));
    CHECK (f.editor.getNumSelected() == 0);
}

TEST_CASE ("SourceRollEditor: shift-click toggles a note into and out of the selection without starting a drag", "[source-roll-editor]")
{
    Fixture f;
    juce::ModifierKeys shift (juce::ModifierKeys::shiftModifier);

    f.editor.mouseDown (f.centreOf (f.noteA), {}, false);
    f.editor.mouseDown (f.centreOf (f.noteB), shift, false);
    CHECK (f.editor.isSelected (f.noteA));
    CHECK (f.editor.isSelected (f.noteB));
    CHECK (f.editor.getNumSelected() == 2);

    f.editor.mouseDown (f.centreOf (f.noteA), shift, false);
    CHECK_FALSE (f.editor.isSelected (f.noteA));
    CHECK (f.editor.isSelected (f.noteB));

    const auto before = (int) f.noteB.getProperty (SongIDs::startTick);
    f.editor.mouseDrag (f.centreOf (f.noteB).translated (100, 0));
    CHECK ((int) f.noteB.getProperty (SongIDs::startTick) == before); // shift-click never starts a drag
}

TEST_CASE ("SourceRollEditor: dragging on empty canvas rubber-bands every note it intersects", "[source-roll-editor]")
{
    Fixture f;
    const juce::Point<int> start (f.geometry.getKeyboardGutterWidth() + 2, f.geometry.yForPitch (70));
    const juce::Point<int> end (f.geometry.xForTick (ticksPerQuarter * 2), f.geometry.yForPitch (55));

    REQUIRE (f.editor.mouseDown (start, {}, false));
    f.editor.mouseDrag (end);

    CHECK (f.editor.isSelected (f.noteA));
    CHECK (f.editor.isSelected (f.noteB));

    f.editor.mouseUp (end);
    CHECK (f.editor.getRubberBandRect().isEmpty());
}
```

- [ ] **Step 5: Run to verify pass**

Run: `cmake --build build --target forge_tests && ./build/Tests/forge_tests "[source-roll-editor]"`
Expected: all 4 PASS (selection/hit-testing/rubber-band logic is already fully implemented from Step 3).

- [ ] **Step 6: Write the failing move/resize tests**

Append to `Tests/SourceRollEditor_tests.cpp`:

```cpp
TEST_CASE ("SourceRollEditor: dragging a selected note's body moves startTick and pitch together, one undo transaction", "[source-roll-editor]")
{
    Fixture f;
    const auto start = f.centreOf (f.noteA);
    const auto end = start.translated (f.geometry.xForTick (ticksPerQuarter) - f.geometry.xForTick (0),
                                        f.geometry.yForPitch (58) - f.geometry.yForPitch (60));

    REQUIRE (f.editor.mouseDown (start, {}, false));
    f.editor.mouseDrag (end);
    f.editor.mouseUp (end);

    CHECK ((int) f.noteA.getProperty (SongIDs::startTick) == ticksPerQuarter);
    CHECK ((int) f.noteA.getProperty (SongIDs::pitch) == 58);
    CHECK ((int) f.noteA.getProperty (SongIDs::durationTicks) == ticksPerQuarter); // unchanged

    REQUIRE (f.doc.canUndo());
    f.doc.undo();
    CHECK ((int) f.noteA.getProperty (SongIDs::startTick) == 0);
    CHECK ((int) f.noteA.getProperty (SongIDs::pitch) == 60);
}

TEST_CASE ("SourceRollEditor: dragging one note in a multi-selection moves every selected note by the same delta, one undo transaction", "[source-roll-editor]")
{
    Fixture f;
    juce::ModifierKeys shift (juce::ModifierKeys::shiftModifier);
    f.editor.mouseDown (f.centreOf (f.noteA), {}, false);
    f.editor.mouseDown (f.centreOf (f.noteB), shift, false);
    REQUIRE (f.editor.getNumSelected() == 2);

    const auto start = f.centreOf (f.noteA);
    const int deltaTickPixels = f.geometry.xForTick (ticksPerQuarter) - f.geometry.xForTick (0);
    const auto end = start.translated (deltaTickPixels, 0);

    REQUIRE (f.editor.mouseDown (start, {}, false)); // re-click an already-selected note keeps the multi-selection
    f.editor.mouseDrag (end);
    f.editor.mouseUp (end);

    CHECK ((int) f.noteA.getProperty (SongIDs::startTick) == ticksPerQuarter);
    CHECK ((int) f.noteB.getProperty (SongIDs::startTick) == ticksPerQuarter * 2);

    f.doc.undo();
    CHECK ((int) f.noteA.getProperty (SongIDs::startTick) == 0);
    CHECK ((int) f.noteB.getProperty (SongIDs::startTick) == ticksPerQuarter); // one undo restores BOTH
}

TEST_CASE ("SourceRollEditor: dragging a note's right edge resizes durationTicks only", "[source-roll-editor]")
{
    Fixture f;
    auto bounds = f.geometry.noteBounds (Fixture::toNote (f.noteA));
    const juce::Point<int> edgeStart (bounds.x + bounds.width - 1, bounds.y + bounds.height / 2);
    const int extraTicks = ticksPerQuarter / 2;
    const auto end = edgeStart.translated (f.geometry.xForTick (extraTicks) - f.geometry.xForTick (0), 0);

    REQUIRE (f.editor.mouseDown (edgeStart, {}, false));
    f.editor.mouseDrag (end);
    f.editor.mouseUp (end);

    CHECK ((int) f.noteA.getProperty (SongIDs::startTick) == 0); // unchanged
    CHECK ((int) f.noteA.getProperty (SongIDs::durationTicks) == ticksPerQuarter + extraTicks);
}

TEST_CASE ("SourceRollEditor: dragging a note's left edge changes startTick and durationTicks together, keeping the end tick fixed", "[source-roll-editor]")
{
    Fixture f; // noteB starts at tick 480, duration 480 (end tick 960)
    auto bounds = f.geometry.noteBounds (Fixture::toNote (f.noteB));
    const juce::Point<int> edgeStart (bounds.x + 1, bounds.y + bounds.height / 2);
    const int shrinkTicks = ticksPerQuarter / 4;
    const auto end = edgeStart.translated (f.geometry.xForTick (shrinkTicks) - f.geometry.xForTick (0), 0);

    REQUIRE (f.editor.mouseDown (edgeStart, {}, false));
    f.editor.mouseDrag (end);
    f.editor.mouseUp (end);

    const int expectedStart = ticksPerQuarter + shrinkTicks;
    const int expectedDuration = ticksPerQuarter - shrinkTicks;
    CHECK ((int) f.noteB.getProperty (SongIDs::startTick) == expectedStart);
    CHECK ((int) f.noteB.getProperty (SongIDs::durationTicks) == expectedDuration);
    CHECK (expectedStart + expectedDuration == ticksPerQuarter * 2); // end tick unchanged
}
```

- [ ] **Step 7: Run to verify pass**

Run: `cmake --build build --target forge_tests && ./build/Tests/forge_tests "[source-roll-editor]"`
Expected: all 8 cases PASS.

- [ ] **Step 8: Full suite + commit**

Run: `ctest --test-dir build --output-on-failure` (expect 233/233 — 225 + 8 new).

```bash
git add Source/UI/SourceRollEditor.h Source/UI/SourceRollEditor.cpp Tests/SourceRollEditor_tests.cpp CMakeLists.txt Tests/CMakeLists.txt
git commit -m "feat(songsmith): add SourceRollEditor hit-testing, selection, and move/resize gestures"
```

---

### Task 4: Create and delete gestures

**Files:**
- Modify: `Source/UI/SourceRollEditor.h` (move `deleteSelection` — already public from Task 3 — add nothing new to the header besides what's already declared; `createNoteAt` is already declared private from Task 3).
- Modify: `Source/UI/SourceRollEditor.cpp`
- Modify: `Tests/SourceRollEditor_tests.cpp`

**Interfaces:**
- Consumes: `SongDocument::addChild`/`removeChild` (Task 2).
- Produces: real `createNoteAt`/`deleteSelection` bodies; `mouseDown`'s double-click branch now creates a note.

- [ ] **Step 1: Write the failing tests**

Append to `Tests/SourceRollEditor_tests.cpp`:

```cpp
TEST_CASE ("SourceRollEditor: double-clicking an empty cell creates a note there, one undo transaction", "[source-roll-editor]")
{
    Fixture f;
    const juce::Point<int> emptyCell (f.geometry.xForTick (ticksPerQuarter * 3), f.geometry.yForPitch (72));

    REQUIRE (f.track.getNumChildren() == 2);
    CHECK (f.editor.mouseDown (emptyCell, {}, true));
    REQUIRE (f.track.getNumChildren() == 3);

    auto created = f.track.getChild (2);
    CHECK ((int) created.getProperty (SongIDs::pitch) == 72);
    CHECK ((int) created.getProperty (SongIDs::startTick) == f.geometry.tickForX (emptyCell.x));
    CHECK ((int) created.getProperty (SongIDs::durationTicks) == ticksPerQuarter); // grid off -> quarter note fallback

    REQUIRE (f.doc.canUndo());
    f.doc.undo();
    CHECK (f.track.getNumChildren() == 2);
}

TEST_CASE ("SourceRollEditor: create uses the current grid size for the new note's duration when grid is on", "[source-roll-editor]")
{
    Fixture f;
    f.editor.setGridTicks (ticksPerQuarter / 4);

    const juce::Point<int> emptyCell (f.geometry.xForTick (ticksPerQuarter * 3), f.geometry.yForPitch (72));
    f.editor.mouseDown (emptyCell, {}, true);

    auto created = f.track.getChild (2);
    CHECK ((int) created.getProperty (SongIDs::durationTicks) == ticksPerQuarter / 4);
}

TEST_CASE ("SourceRollEditor: double-clicking an existing note is a no-op", "[source-roll-editor]")
{
    Fixture f;
    CHECK_FALSE (f.editor.mouseDown (f.centreOf (f.noteA), {}, true));
    CHECK (f.track.getNumChildren() == 2);
}

TEST_CASE ("SourceRollEditor: deleteSelection removes every selected note in one undo transaction", "[source-roll-editor]")
{
    Fixture f;
    juce::ModifierKeys shift (juce::ModifierKeys::shiftModifier);
    f.editor.mouseDown (f.centreOf (f.noteA), {}, false);
    f.editor.mouseDown (f.centreOf (f.noteB), shift, false);
    REQUIRE (f.editor.getNumSelected() == 2);

    CHECK (f.editor.deleteSelection());
    CHECK (f.track.getNumChildren() == 0);
    CHECK (f.editor.getNumSelected() == 0);

    f.doc.undo();
    CHECK (f.track.getNumChildren() == 2); // one undo restores both
}

TEST_CASE ("SourceRollEditor: deleteSelection with nothing selected is a no-op", "[source-roll-editor]")
{
    Fixture f;
    CHECK_FALSE (f.editor.deleteSelection());
    CHECK (f.track.getNumChildren() == 2);
}
```

- [ ] **Step 2: Run to verify failure**

Run: `cmake --build build --target forge_tests && ./build/Tests/forge_tests "[source-roll-editor]"`
Expected: the 5 new cases FAIL (current stubs always return false / do nothing).

- [ ] **Step 3: Implement**

In `Source/UI/SourceRollEditor.cpp`, replace the stub bodies:

```cpp
void SourceRollEditor::createNoteAt (juce::Point<int> pos)
{
    if (! track.isValid())
        return;

    juce::ValueTree note (SongIDs::NOTE);
    note.setProperty (SongIDs::pitch, geometry.pitchForY (pos.y), nullptr);
    note.setProperty (SongIDs::startTick, std::max (0, geometry.tickForX (pos.x)), nullptr);
    note.setProperty (SongIDs::durationTicks,
                       currentGridTicks > 0 ? currentGridTicks : geometry.getTicksPerQuarter(), nullptr);
    note.setProperty (SongIDs::velocity, 100, nullptr);
    note.setProperty (SongIDs::isDrum, false, nullptr);
    note.setProperty (SongIDs::sourceTrackIndex, -1, nullptr);
    note.setProperty (SongIDs::sourceEventIndex, -1, nullptr);

    doc.addChild (track, note);
    selectOnly (note);
}

bool SourceRollEditor::deleteSelection()
{
    pruneSelection();
    if (selection.empty())
        return false;

    doc.getUndoManager().beginNewTransaction();
    for (auto& note : selection)
        doc.removeChild (track, note, false);
    selection.clear();
    return true;
}
```

And update `mouseDown`'s double-click branch:

```cpp
    if (isDoubleClick)
    {
        if (hitTestNote (pos).isValid())
            return false;

        createNoteAt (pos);
        return true;
    }
```

- [ ] **Step 4: Run to verify pass**

Run: `cmake --build build --target forge_tests && ./build/Tests/forge_tests "[source-roll-editor]"`
Expected: all 13 cases PASS.

- [ ] **Step 5: Full suite + commit**

Run: `ctest --test-dir build --output-on-failure` (expect 238/238).

```bash
git add Source/UI/SourceRollEditor.cpp Tests/SourceRollEditor_tests.cpp
git commit -m "feat(songsmith): add SourceRollEditor create and delete gestures"
```

---

### Task 5: Quantize

**Files:**
- Modify: `Source/UI/SourceRollEditor.cpp`
- Modify: `Tests/SourceRollEditor_tests.cpp`

**Interfaces:**
- Consumes: `SongDocument::setProperty` (existing), `currentGridTicks` (Task 3's `setGridTicks`).
- Produces: real `quantizeSelection` body.

- [ ] **Step 1: Write the failing tests**

Append to `Tests/SourceRollEditor_tests.cpp`:

```cpp
TEST_CASE ("SourceRollEditor: quantizeSelection snaps startTick and durationTicks of every selected note to the grid, one undo transaction", "[source-roll-editor]")
{
    Fixture f;
    // Nudge noteA off-grid so quantize has something to actually snap.
    f.noteA.setProperty (SongIDs::startTick, 10, nullptr);
    f.noteA.setProperty (SongIDs::durationTicks, 470, nullptr);

    f.editor.setGridTicks (ticksPerQuarter / 4); // 120 ticks
    f.editor.mouseDown (f.centreOf (f.noteA), {}, false);
    juce::ModifierKeys shift (juce::ModifierKeys::shiftModifier);
    f.editor.mouseDown (f.centreOf (f.noteB), shift, false);

    CHECK (f.editor.quantizeSelection());

    CHECK ((int) f.noteA.getProperty (SongIDs::startTick) == 0);       // round(10/120)*120 = 0
    CHECK ((int) f.noteA.getProperty (SongIDs::durationTicks) == 480); // round(470/120)*120 = 480
    CHECK ((int) f.noteB.getProperty (SongIDs::startTick) == 480);     // already on-grid, value preserved
    CHECK ((int) f.noteB.getProperty (SongIDs::durationTicks) == 480);

    REQUIRE (f.doc.canUndo());
    f.doc.undo();
    CHECK ((int) f.noteA.getProperty (SongIDs::startTick) == 10); // one undo restores BOTH notes
}

TEST_CASE ("SourceRollEditor: quantizeSelection is a no-op with an empty selection or the grid off", "[source-roll-editor]")
{
    Fixture f;
    CHECK_FALSE (f.editor.quantizeSelection()); // nothing selected

    f.editor.mouseDown (f.centreOf (f.noteA), {}, false);
    CHECK_FALSE (f.editor.quantizeSelection()); // grid still off (currentGridTicks == 0)
}
```

- [ ] **Step 2: Run to verify failure**

Run: `cmake --build build --target forge_tests && ./build/Tests/forge_tests "[source-roll-editor]"`
Expected: the first case FAILS (stub always returns false, notes stay unchanged); the second already trivially passes since the stub always returns false — leave it in as a lock-in regression guard once Step 3 lands.

- [ ] **Step 3: Implement**

In `Source/UI/SourceRollEditor.cpp`, replace the `quantizeSelection` stub:

```cpp
bool SourceRollEditor::quantizeSelection()
{
    pruneSelection();
    if (selection.empty() || currentGridTicks <= 0)
        return false;

    doc.getUndoManager().beginNewTransaction();
    for (auto& note : selection)
    {
        const int origStart = (int) note.getProperty (SongIDs::startTick);
        const int origDuration = (int) note.getProperty (SongIDs::durationTicks);

        const int snappedStart = juce::jmax (0, (int) std::lround ((double) origStart / currentGridTicks) * currentGridTicks);
        // At least one grid unit -- the spec sets no floor, but a
        // zero-length note after quantize is nonsensical.
        const int snappedDuration = juce::jmax (currentGridTicks,
                                                 (int) std::lround ((double) origDuration / currentGridTicks) * currentGridTicks);

        if (origStart != snappedStart)
            doc.setProperty (note, SongIDs::startTick, snappedStart, false);
        if (origDuration != snappedDuration)
            doc.setProperty (note, SongIDs::durationTicks, snappedDuration, false);
    }
    return true;
}
```

Add `#include <cmath>` to the top of `Source/UI/SourceRollEditor.cpp` if not already present (for `std::lround`).

- [ ] **Step 4: Run to verify pass**

Run: `cmake --build build --target forge_tests && ./build/Tests/forge_tests "[source-roll-editor]"`
Expected: all 15 cases PASS.

- [ ] **Step 5: Full suite + commit**

Run: `ctest --test-dir build --output-on-failure` (expect 240/240).

```bash
git add Source/UI/SourceRollEditor.cpp Tests/SourceRollEditor_tests.cpp
git commit -m "feat(songsmith): add SourceRollEditor quantize action"
```

---

### Task 6: Keyboard — Delete/Backspace, Ctrl+Z/Ctrl+Y

**Files:**
- Modify: `Source/UI/SourceRollEditor.cpp`
- Modify: `Tests/SourceRollEditor_tests.cpp`

**Interfaces:**
- Consumes: `SongDocument::undo()`/`redo()` (existing), `deleteSelection()` (Task 4).
- Produces: real `keyPressed` body.

- [ ] **Step 1: Write the failing tests**

Append to `Tests/SourceRollEditor_tests.cpp`:

```cpp
TEST_CASE ("SourceRollEditor: the delete key removes the selection; backspace does too", "[source-roll-editor]")
{
    Fixture f;
    f.editor.mouseDown (f.centreOf (f.noteA), {}, false);
    CHECK (f.editor.keyPressed (juce::KeyPress (juce::KeyPress::deleteKey)));
    CHECK (f.track.getNumChildren() == 1);

    f.editor.mouseDown (f.centreOf (f.noteB), {}, false);
    CHECK (f.editor.keyPressed (juce::KeyPress (juce::KeyPress::backspaceKey)));
    CHECK (f.track.getNumChildren() == 0);
}

TEST_CASE ("SourceRollEditor: the delete key with nothing selected is not handled", "[source-roll-editor]")
{
    Fixture f;
    CHECK_FALSE (f.editor.keyPressed (juce::KeyPress (juce::KeyPress::deleteKey)));
}

TEST_CASE ("SourceRollEditor: Ctrl+Z undoes and Ctrl+Y redoes the last mutation", "[source-roll-editor]")
{
    Fixture f;
    f.editor.mouseDown (f.centreOf (f.noteA), {}, false);
    REQUIRE (f.editor.keyPressed (juce::KeyPress (juce::KeyPress::deleteKey)));
    REQUIRE (f.track.getNumChildren() == 1);

    const auto ctrlZ = juce::KeyPress ('z', juce::ModifierKeys (juce::ModifierKeys::ctrlModifier), 0);
    CHECK (f.editor.keyPressed (ctrlZ));
    CHECK (f.track.getNumChildren() == 2);

    const auto ctrlY = juce::KeyPress ('y', juce::ModifierKeys (juce::ModifierKeys::ctrlModifier), 0);
    CHECK (f.editor.keyPressed (ctrlY));
    CHECK (f.track.getNumChildren() == 1);
}

TEST_CASE ("SourceRollEditor: Ctrl+Shift+Z also redoes", "[source-roll-editor]")
{
    Fixture f;
    f.editor.mouseDown (f.centreOf (f.noteA), {}, false);
    f.editor.keyPressed (juce::KeyPress (juce::KeyPress::deleteKey));
    f.editor.keyPressed (juce::KeyPress ('z', juce::ModifierKeys (juce::ModifierKeys::ctrlModifier), 0));
    REQUIRE (f.track.getNumChildren() == 2);

    const auto ctrlShiftZ = juce::KeyPress ('z', juce::ModifierKeys (juce::ModifierKeys::ctrlModifier
                                                                       | juce::ModifierKeys::shiftModifier), 0);
    CHECK (f.editor.keyPressed (ctrlShiftZ));
    CHECK (f.track.getNumChildren() == 1);
}
```

- [ ] **Step 2: Run to verify failure**

Run: `cmake --build build --target forge_tests && ./build/Tests/forge_tests "[source-roll-editor]"`
Expected: the 4 new cases FAIL (stub always returns false).

- [ ] **Step 3: Implement**

In `Source/UI/SourceRollEditor.cpp`, replace the `keyPressed` stub:

```cpp
bool SourceRollEditor::keyPressed (const juce::KeyPress& key)
{
    if (key == juce::KeyPress (juce::KeyPress::deleteKey) || key == juce::KeyPress (juce::KeyPress::backspaceKey))
        return deleteSelection();

    const bool isCtrlOrCmd = key.getModifiers().isCtrlDown() || key.getModifiers().isCommandDown();
    if (! isCtrlOrCmd)
        return false;

    const bool isShift = key.getModifiers().isShiftDown();

    if (key.getTextCharacter() == 'z' && ! isShift)
    {
        doc.undo();
        pruneSelection();
        return true;
    }
    if ((key.getTextCharacter() == 'z' && isShift) || key.getTextCharacter() == 'y')
    {
        doc.redo();
        pruneSelection();
        return true;
    }
    return false;
}
```

- [ ] **Step 4: Run to verify pass**

Run: `cmake --build build --target forge_tests && ./build/Tests/forge_tests "[source-roll-editor]"`
Expected: all 19 cases PASS.

- [ ] **Step 5: Full suite + commit**

Run: `ctest --test-dir build --output-on-failure` (expect 244/244).

```bash
git add Source/UI/SourceRollEditor.cpp Tests/SourceRollEditor_tests.cpp
git commit -m "feat(songsmith): wire SourceRollEditor keyboard delete/undo/redo"
```

---

### Task 7: `PianoRollComponent` integration

**Files:**
- Modify: `Source/UI/PianoRollComponent.h`
- Modify: `Source/UI/PianoRollComponent.cpp`
- Modify: `Source/UI/SongsmithColours.h`
- Modify: `Tests/PianoRollComponent_tests.cpp`

**Interfaces:**
- Consumes: `SourceRollEditor` (Tasks 3-6).
- Produces:

```cpp
explicit PianoRollComponent (Role roleIn = Role::Source, SongDocument* editableDocument = nullptr);
void setEditableTrack (juce::ValueTree trackNode);
void setGridTicks (int ticks);
bool quantizeSelection();
```

Only constructed with an editor when `roleIn == Role::Source && editableDocument != nullptr`; the existing `previewRoll { PianoRollComponent::Role::Preview }` call site is unaffected (second parameter defaults to `nullptr`).

- [ ] **Step 1: Add the new colour constant**

In `Source/UI/SongsmithColours.h`, add after `accentAmber`:

```cpp
    // Phase 7 -- source-role selection highlight (roll editing).
    constexpr juce::uint32 selectionHighlight = 0xFFFFFFFF;
```

- [ ] **Step 2: Modify `PianoRollComponent.h`**

Add `#include "SourceRollEditor.h"` after the existing `#include "PianoRollNoteSource.h"`.

Change the constructor declaration:

```cpp
    explicit PianoRollComponent (Role roleIn = Role::Source, SongDocument* editableDocument = nullptr);
```

Add to the public section, after `setPreviewRangeBand`:

```cpp
    // Source role only (no-op otherwise, or if this roll has no editable
    // document): repoints the roll's SourceRollEditor at a different
    // MIDI_TRACK node. Call this alongside setNoteSource whenever the
    // selected track changes.
    void setEditableTrack (juce::ValueTree trackNode);

    // Source role only: pushes the toolbar's current grid-size selection
    // into the roll's SourceRollEditor.
    void setGridTicks (int ticks);

    // Source role only: grid-snaps the current selection. Returns true if
    // anything changed (mirrors SourceRollEditor::quantizeSelection).
    bool quantizeSelection();
```

Add to the `Canvas` inner class (after the existing `mouseWheelMove` declaration):

```cpp
        void mouseDown (const juce::MouseEvent& e) override;
        void mouseDoubleClick (const juce::MouseEvent& e) override;
        void mouseDrag (const juce::MouseEvent& e) override;
        void mouseUp (const juce::MouseEvent& e) override;
        bool keyPressed (const juce::KeyPress& key) override;
```

Add to the `private:` section, after `drawNotes`'s declaration:

```cpp
    bool handleEditorMouseDown (juce::Point<int> pos, juce::ModifierKeys mods, bool isDoubleClick);
    bool handleEditorMouseDrag (juce::Point<int> pos);
    bool handleEditorMouseUp (juce::Point<int> pos);
    bool handleEditorKeyPressed (const juce::KeyPress& key);
    void afterEditorGesture (bool changed);
```

Add the new member near `noteSource`:

```cpp
    std::unique_ptr<SourceRollEditor> sourceEditor; // Role::Source with an editable document only.
```

- [ ] **Step 3: Modify `PianoRollComponent.cpp`**

Constructor:

```cpp
PianoRollComponent::PianoRollComponent (Role roleIn, SongDocument* editableDocument) : role (roleIn)
{
    if (role == Role::Source && editableDocument != nullptr)
        sourceEditor = std::make_unique<SourceRollEditor> (*editableDocument);

    viewport.setViewedComponent (&canvas, false);
    viewport.setScrollBarsShown (true, true);
    addAndMakeVisible (viewport);
    addAndMakeVisible (gutter);
}
```

`Canvas` constructor gains keyboard focus:

```cpp
        explicit Canvas (PianoRollComponent& ownerIn) : owner (ownerIn) { setWantsKeyboardFocus (true); }
```

At the end of `setNoteSource` and `zoom` (right before their final `canvas.repaint();`), add:

```cpp
    if (sourceEditor != nullptr)
        sourceEditor->setGeometry (geometry);
```

New methods (place after `zoom`):

```cpp
void PianoRollComponent::setEditableTrack (juce::ValueTree trackNode)
{
    if (sourceEditor != nullptr)
        sourceEditor->setTrack (trackNode);
}

void PianoRollComponent::setGridTicks (int ticks)
{
    if (sourceEditor != nullptr)
        sourceEditor->setGridTicks (ticks);
}

bool PianoRollComponent::quantizeSelection()
{
    if (sourceEditor == nullptr)
        return false;
    const bool changed = sourceEditor->quantizeSelection();
    afterEditorGesture (changed);
    return changed;
}

void PianoRollComponent::afterEditorGesture (bool changed)
{
    if (! changed)
        return;
    rebuildContentSize();
    canvas.repaint();
}

bool PianoRollComponent::handleEditorMouseDown (juce::Point<int> pos, juce::ModifierKeys mods, bool isDoubleClick)
{
    if (sourceEditor == nullptr)
        return false;
    const bool changed = sourceEditor->mouseDown (pos, mods, isDoubleClick);
    afterEditorGesture (changed);
    return changed;
}

bool PianoRollComponent::handleEditorMouseDrag (juce::Point<int> pos)
{
    if (sourceEditor == nullptr)
        return false;
    const bool changed = sourceEditor->mouseDrag (pos);
    afterEditorGesture (changed);
    return changed;
}

bool PianoRollComponent::handleEditorMouseUp (juce::Point<int> pos)
{
    if (sourceEditor == nullptr)
        return false;
    const bool changed = sourceEditor->mouseUp (pos);
    afterEditorGesture (changed);
    return changed;
}

bool PianoRollComponent::handleEditorKeyPressed (const juce::KeyPress& key)
{
    if (sourceEditor == nullptr)
        return false;
    const bool changed = sourceEditor->keyPressed (key);
    afterEditorGesture (changed);
    return changed;
}

void PianoRollComponent::Canvas::mouseDown (const juce::MouseEvent& e)
{
    grabKeyboardFocus();
    owner.handleEditorMouseDown (e.getPosition(), e.mods, false);
}

void PianoRollComponent::Canvas::mouseDoubleClick (const juce::MouseEvent& e)
{
    owner.handleEditorMouseDown (e.getPosition(), e.mods, true);
}

void PianoRollComponent::Canvas::mouseDrag (const juce::MouseEvent& e)
{
    owner.handleEditorMouseDrag (e.getPosition());
}

void PianoRollComponent::Canvas::mouseUp (const juce::MouseEvent& e)
{
    owner.handleEditorMouseUp (e.getPosition());
}

bool PianoRollComponent::Canvas::keyPressed (const juce::KeyPress& key)
{
    return owner.handleEditorKeyPressed (key);
}
```

In `drawNotes`, inside the per-note loop, right after the existing `if (role == Role::Preview) { ... }` block (before its closing brace's matching `for` continues), add:

```cpp
        if (role == Role::Source && sourceEditor != nullptr
            && sourceEditor->isSelected (sourceEditor->getTrackNode().getChild (i)))
        {
            g.setColour (juce::Colour (SongsmithColours::selectionHighlight));
            g.drawRect (rect, 2);
        }
```

At the end of `drawNotes` (after the `for` loop, before the function's closing brace), add:

```cpp
    if (role == Role::Source && sourceEditor != nullptr)
    {
        const auto bandRect = sourceEditor->getRubberBandRect();
        if (! bandRect.isEmpty())
        {
            g.setColour (juce::Colour (SongsmithColours::selectionHighlight).withAlpha (0.15f));
            g.fillRect (bandRect);
            g.setColour (juce::Colour (SongsmithColours::selectionHighlight).withAlpha (0.6f));
            g.drawRect (bandRect, 1);
        }
    }
```

- [ ] **Step 4: Build to confirm it compiles**

Run: `cmake --build build --target forge_tests`
Expected: builds clean (no tests added yet for the new wiring — that's Step 5).

- [ ] **Step 5: Write the failing integration tests**

Append to `Tests/PianoRollComponent_tests.cpp` (add `#include "UI/SongDocument.h"` and `#include "UI/SourceTrackNoteSource.h"` to its includes, and extend the existing friend-access struct):

```cpp
namespace lotro
{
    struct PianoRollComponentTestAccess
    {
        static void paintCanvas (const PianoRollComponent& c, juce::Graphics& g, juce::Rectangle<int> clip)
        {
            c.paintCanvas (g, clip);
        }

        static bool mouseDown (PianoRollComponent& c, juce::Point<int> pos, juce::ModifierKeys mods, bool dbl)
        {
            return c.handleEditorMouseDown (pos, mods, dbl);
        }
        static bool mouseDrag (PianoRollComponent& c, juce::Point<int> pos) { return c.handleEditorMouseDrag (pos); }
        static bool mouseUp (PianoRollComponent& c, juce::Point<int> pos) { return c.handleEditorMouseUp (pos); }
    };
}
```

(This replaces the existing smaller `PianoRollComponentTestAccess` struct — keep `paintCanvas` and add the three new statics to the same struct.)

```cpp
TEST_CASE ("PianoRollComponent: mouse gestures reach SourceRollEditor and mutate the real SongDocument end to end", "[piano-roll]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    SongDocument doc;
    auto track = doc.addTrack ("Track A", 0xFF0000, 0, 1);
    juce::ValueTree note (SongIDs::NOTE);
    note.setProperty (SongIDs::pitch, 60, nullptr);
    note.setProperty (SongIDs::startTick, 0, nullptr);
    note.setProperty (SongIDs::durationTicks, ticksPerQuarter, nullptr);
    track.addChild (note, -1, nullptr);

    SourceTrackNoteSource source (track);

    PianoRollComponent roll (PianoRollComponent::Role::Source, &doc);
    roll.setBounds (0, 0, viewportWidth, viewportHeight);
    roll.setNoteSource (&source, ticksPerQuarter, {});
    roll.setEditableTrack (track);

    const auto geometry = PianoRollGeometry::fitToContent (source.getTickRange(), source.getPitchRange(),
                                                             ticksPerQuarter, viewportWidth, viewportHeight);
    const auto bounds = geometry.noteBounds (source.getNote (0));
    const juce::Point<int> clickPos (bounds.x + bounds.width / 2, bounds.y + bounds.height / 2);
    const juce::Point<int> dragPos = clickPos.translated (40, 0);

    using Access = PianoRollComponentTestAccess;
    REQUIRE (Access::mouseDown (roll, clickPos, {}, false));
    REQUIRE (Access::mouseDrag (roll, dragPos));
    Access::mouseUp (roll, dragPos);

    juce::MessageManager::getInstance()->runDispatchLoopUntil (50);

    const int expectedDeltaTick = geometry.tickForX (dragPos.x) - geometry.tickForX (clickPos.x);
    CHECK ((int) track.getChild (0).getProperty (SongIDs::startTick) == expectedDeltaTick);
    CHECK (doc.canUndo());

    doc.undo();
    CHECK ((int) track.getChild (0).getProperty (SongIDs::startTick) == 0);
}

TEST_CASE ("PianoRollComponent: a selected source-role note paints with the selection highlight border", "[piano-roll]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    SongDocument doc;
    auto track = doc.addTrack ("Track A", 0xFF0000, 0, 1);
    juce::ValueTree note (SongIDs::NOTE);
    note.setProperty (SongIDs::pitch, 60, nullptr);
    note.setProperty (SongIDs::startTick, 0, nullptr);
    note.setProperty (SongIDs::durationTicks, ticksPerQuarter, nullptr);
    track.addChild (note, -1, nullptr);

    SourceTrackNoteSource source (track);

    PianoRollComponent roll (PianoRollComponent::Role::Source, &doc);
    roll.setBounds (0, 0, viewportWidth, viewportHeight);
    roll.setNoteSource (&source, ticksPerQuarter, {});
    roll.setEditableTrack (track);

    const auto geometry = PianoRollGeometry::fitToContent (source.getTickRange(), source.getPitchRange(),
                                                             ticksPerQuarter, viewportWidth, viewportHeight);
    const auto bounds = geometry.noteBounds (source.getNote (0));
    const juce::Point<int> centre (bounds.x + bounds.width / 2, bounds.y + bounds.height / 2);

    using Access = PianoRollComponentTestAccess;
    Access::mouseDown (roll, centre, {}, false);
    Access::mouseUp (roll, centre);

    juce::Image image (juce::Image::ARGB, viewportWidth, viewportHeight, true);
    juce::Graphics g (image);
    Access::paintCanvas (roll, g, { 0, 0, viewportWidth, viewportHeight });

    const auto topBorderPixel = image.getPixelAt (bounds.x + bounds.width / 2, bounds.y);
    CHECK (topBorderPixel == juce::Colour (SongsmithColours::selectionHighlight));
}
```

- [ ] **Step 6: Run to verify pass**

Run: `cmake --build build --target forge_tests && ./build/Tests/forge_tests "[piano-roll]"`
Expected: all cases (pre-existing + 2 new) PASS.

- [ ] **Step 7: Full suite + commit**

Run: `ctest --test-dir build --output-on-failure` (expect 246/246).

```bash
git add Source/UI/PianoRollComponent.h Source/UI/PianoRollComponent.cpp Source/UI/SongsmithColours.h Tests/PianoRollComponent_tests.cpp
git commit -m "feat(songsmith): wire PianoRollComponent's source role to SourceRollEditor"
```

---

### Task 8: `SongsmithMainComponent` wiring — grid combo, Quantize button, `GridSize`

**Files:**
- Create: `Source/UI/GridSize.h`
- Create: `Tests/GridSize_tests.cpp`
- Modify: `Source/UI/SongsmithMainComponent.h`
- Modify: `Source/UI/SongsmithMainComponent.cpp`
- Modify: `Tests/CMakeLists.txt` — add `GridSize_tests.cpp` to the test-file list.

**Interfaces:**
- Consumes: `PianoRollComponent::setEditableTrack`/`setGridTicks`/`quantizeSelection` (Task 7).
- Produces: `enum class GridSize`, `constexpr int gridSizeToTicks (GridSize, int ticksPerQuarter)` — used only inside `SongsmithMainComponent.cpp`.

- [ ] **Step 1: Write the failing `GridSize` test**

Write `Tests/GridSize_tests.cpp`:

```cpp
// Verifies GridSize's tick conversion -- the toolbar grid-size selector's
// only piece of logic, pure and header-only.

#include "UI/GridSize.h"

#include <catch2/catch_test_macros.hpp>

using namespace lotro;

TEST_CASE ("GridSize: gridSizeToTicks maps each grid size to the right tick span, Off to zero", "[gridsize]")
{
    constexpr int ticksPerQuarter = 480;

    CHECK (gridSizeToTicks (GridSize::Off, ticksPerQuarter) == 0);
    CHECK (gridSizeToTicks (GridSize::Quarter, ticksPerQuarter) == 480);
    CHECK (gridSizeToTicks (GridSize::Eighth, ticksPerQuarter) == 240);
    CHECK (gridSizeToTicks (GridSize::Sixteenth, ticksPerQuarter) == 120);
}
```

- [ ] **Step 2: Add to `Tests/CMakeLists.txt` and run to verify failure**

Add `GridSize_tests.cpp` to the test-file list (after `SourceRollEditor_tests.cpp`).

Run: `cmake --build build --target forge_tests`
Expected: build FAILS — `Source/UI/GridSize.h` doesn't exist yet.

- [ ] **Step 3: Implement `GridSize.h`**

Write `Source/UI/GridSize.h`:

```cpp
#pragma once

// The Songsmith piano-roll toolbar's grid-size selector: transient UI
// state (never persisted in SongDocument, per the plan) driving both
// SourceRollEditor::quantizeSelection's snap size and create's default
// note duration. Header-only, no JUCE dependency -- pure enum + arithmetic.
namespace lotro
{

enum class GridSize
{
    Off,
    Quarter,
    Eighth,
    Sixteenth
};

// Returns 0 for GridSize::Off ("no grid"), else the number of ticks
// spanned by that note value at the given ticksPerQuarter.
constexpr int gridSizeToTicks (GridSize size, int ticksPerQuarter) noexcept
{
    switch (size)
    {
        case GridSize::Quarter:   return ticksPerQuarter;
        case GridSize::Eighth:    return ticksPerQuarter / 2;
        case GridSize::Sixteenth: return ticksPerQuarter / 4;
        case GridSize::Off:
        default:                 return 0;
    }
}

} // namespace lotro
```

- [ ] **Step 4: Run to verify pass**

Run: `cmake --build build --target forge_tests && ./build/Tests/forge_tests "[gridsize]"`
Expected: PASS.

- [ ] **Step 5: Wire `SongsmithMainComponent`**

In `Source/UI/SongsmithMainComponent.h`:

Add member declarations right after `trackList`:

```cpp
    juce::ComboBox            gridSizeCombo;
    juce::TextButton          quantizeButton { "Quantize" };
```

Change `sourceRoll`'s declaration comment is unaffected; its type stays `PianoRollComponent sourceRoll;` (construction now happens via the mem-initializer-list, see below).

Change `UpperRegion`'s constructor signature and add two reference members:

```cpp
    class UpperRegion : public juce::Component
    {
    public:
        UpperRegion (juce::Label& headerIn, juce::ComboBox& gridComboIn, juce::TextButton& quantizeBtnIn,
                     TrackListComponent& trackListIn, PianoRollComponent& rollIn);
        void resized() override;

    private:
        juce::Label& header;
        juce::ComboBox& gridCombo;
        juce::TextButton& quantizeBtn;
        TrackListComponent& trackList;
        PianoRollComponent& roll;
    };
```

Add a new private method declaration, near `trackSelected`:

```cpp
    void updateGridTicks();
```

In `Source/UI/SongsmithMainComponent.cpp`:

Add `#include "GridSize.h"` after the existing includes.

Replace `UpperRegion`'s constructor/`resized()`:

```cpp
SongsmithMainComponent::UpperRegion::UpperRegion (juce::Label& headerIn, juce::ComboBox& gridComboIn,
                                                   juce::TextButton& quantizeBtnIn, TrackListComponent& trackListIn,
                                                   PianoRollComponent& rollIn)
    : header (headerIn), gridCombo (gridComboIn), quantizeBtn (quantizeBtnIn), trackList (trackListIn), roll (rollIn)
{
    addAndMakeVisible (header);
    addAndMakeVisible (gridCombo);
    addAndMakeVisible (quantizeBtn);
    addAndMakeVisible (trackList);
    addAndMakeVisible (roll);
}

void SongsmithMainComponent::UpperRegion::resized()
{
    auto area = getLocalBounds();
    auto headerRow = area.removeFromTop (sourceHeaderHeight);
    quantizeBtn.setBounds (headerRow.removeFromRight (90));
    gridCombo.setBounds (headerRow.removeFromRight (90));
    header.setBounds (headerRow);
    trackList.setBounds (area.removeFromLeft (trackListWidth));
    roll.setBounds (area);
}
```

In the constructor, change `sourceRoll`'s (implicit) construction to explicit, and `upperRegion`'s argument list:

```cpp
SongsmithMainComponent::SongsmithMainComponent (SongDocument& document)
    : doc (document),
      trackList (document),
      sourceRoll (PianoRollComponent::Role::Source, &doc),
      partStrip (document),
      upperRegion (sourceHeader, gridSizeCombo, quantizeButton, trackList, sourceRoll),
      previewRegion (previewHeader, previewAssignedPanel, previewRoll),
      lowerRegion (partStrip, previewRegion, diagnostics),
      splitter (SplitterComponent::Orientation::topBottom)
{
    sourceHeader.setText (...); // unchanged
    ...
    gridSizeCombo.addItem ("Off", 1);
    gridSizeCombo.addItem ("1/4", 2);
    gridSizeCombo.addItem ("1/8", 3);
    gridSizeCombo.addItem ("1/16", 4);
    gridSizeCombo.setSelectedId (1, juce::dontSendNotification);
    gridSizeCombo.onChange = [this] { updateGridTicks(); };
    updateGridTicks();

    quantizeButton.onClick = [this] { sourceRoll.quantizeSelection(); };

    trackList.onTrackSelected = [this] (juce::int64 trackId) { trackSelected (trackId); };
    partStrip.onPartSelected = [this] (juce::int64 partId) { selectPartForPreview (partId); };

    addAndMakeVisible (upperRegion);
    addAndMakeVisible (lowerRegion);

    splitter.setComponents (&upperRegion, &lowerRegion);
    addAndMakeVisible (splitter);
}
```

(Keep the existing `sourceHeader.setText(...)`/`previewHeader.setText(...)` block exactly as-is; only the mem-initializer-list, the new combo/button setup, and the `trackList.onTrackSelected`/`addAndMakeVisible` ordering relative to the new lines change.)

Add `updateGridTicks`:

```cpp
void SongsmithMainComponent::updateGridTicks()
{
    const int ticksPerQuarter = (int) doc.getSourceMidiNode().getProperty (SongIDs::ticksPerQuarter, 480);
    const auto size = static_cast<GridSize> (gridSizeCombo.getSelectedId() - 1);
    sourceRoll.setGridTicks (gridSizeToTicks (size, ticksPerQuarter));
}
```

Update `trackSelected` to keep the editor's track in sync:

```cpp
void SongsmithMainComponent::trackSelected (juce::int64 trackId)
{
    auto trackNode = doc.findTrackById (trackId);
    if (! trackNode.isValid())
    {
        currentNoteSource.reset();
        sourceRoll.setNoteSource (nullptr, 480, {});
        sourceRoll.setEditableTrack ({});
        return;
    }

    currentNoteSource = std::make_unique<SourceTrackNoteSource> (trackNode);
    const int ticksPerQuarter = (int) doc.getSourceMidiNode().getProperty (SongIDs::ticksPerQuarter, 480);
    sourceRoll.setNoteSource (currentNoteSource.get(), ticksPerQuarter, doc.getMeterMapNode());
    sourceRoll.setEditableTrack (trackNode);
}
```

- [ ] **Step 6: Build**

Run: `cmake --build build --target forge_tests && cmake --build build --target forge_ui`
Expected: both build clean. There is no new automated test for this step specifically (pure UI wiring/layout with no independently testable logic beyond `GridSize`, already covered in Step 1-4) — per `implementing-a-songsmith-phase`, say so explicitly rather than inventing a test around it.

- [ ] **Step 7: Full suite + commit**

Run: `ctest --test-dir build --output-on-failure` (expect 247/247 — 246 + 1 `GridSize` test).

```bash
git add Source/UI/GridSize.h Tests/GridSize_tests.cpp Source/UI/SongsmithMainComponent.h Source/UI/SongsmithMainComponent.cpp Tests/CMakeLists.txt
git commit -m "feat(songsmith): add grid-size toolbar and Quantize button to SongsmithMainComponent"
```

---

## Manual verification (after Task 8)

Per the plan's Phase 4-8 verification recipe and `docs/superpowers/specs/2026-09-14-songsmith-phase7-piano-roll-editing-design.md`'s Testing section:

- `./run-ui.sh` — import a MIDI file, select a track, and exercise create (double-click empty cell) / move (drag a note) / resize (drag an edge) / delete (Delete key and multi-select) / quantize (pick a grid size, click Quantize) / undo / redo (Ctrl+Z / Ctrl+Y). Confirm the preview roll updates after edits and stays read-only throughout.
- Per this project's standing policy (`docs/superpowers/specs/...` Testing section, and the master plan's Phase 4-8 verification note): a Linux `run-ui.sh` pass does **not** count as this project's real verification — a Windows CI `.exe` pass is still required before this phase is considered actually verified. Do not report this phase as fully verified on a Linux-only pass.
- The right-click "Delete" context menu named in the spec is explicitly deferred (Scope decision 3 above) — not part of this phase's manual verification checklist.

## Self-review notes

- **Spec coverage:** every Goal in the design spec maps to a task above (create/move/resize/delete: Tasks 3-4, 7; multi-select: Task 3; quantize: Task 5; undo/redo keyboard: Task 6; coordinate-math prerequisite: Task 1). Every Non-goal is respected by omission (no `forge_core` change, no preview-roll change, no snap-while-drag, no velocity/split/merge/copy-paste, no full keyboard pass, no cursor-anchored zoom). The two explicitly-named "Open items carried into implementation" (edge-threshold pixel value, KeyListener vs. ApplicationCommandTarget) are resolved as concrete decisions (6px constant; a plain `Component::keyPressed` override, since no `ApplicationCommandManager` exists anywhere in this codebase to hook into).
- **Placeholder scan:** no task leaves a "TODO"/"add later" comment as its final state; every stub introduced in Task 3 (`keyPressed`/`deleteSelection`/`quantizeSelection`/`createNoteAt` returning `false`/doing nothing) is filled in by name in Tasks 4-6, each with its own failing-test step.
- **Type consistency:** `SourceRollEditor`'s public surface (`setTrack`, `setGeometry`, `setGridTicks`, `isSelected`, `getNumSelected`, `getRubberBandRect`, `mouseDown`/`mouseDrag`/`mouseUp`, `keyPressed`, `deleteSelection`, `quantizeSelection`, `getTrackNode`) is declared once in Task 3 and never renamed; `PianoRollComponent`'s new methods (`setEditableTrack`, `setGridTicks`, `quantizeSelection`) match the names `SongsmithMainComponent` calls in Task 8 exactly; `GridSize`/`gridSizeToTicks` names match between Task 8's test and implementation.
