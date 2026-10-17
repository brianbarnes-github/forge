# Songsmith Note Display Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** The MIDI editor's View menu gains Piano Roll Notes ▸ (Rectangle / Diamond / Diamond + Length) and Color Note by ▸ (Track / Velocity / Channel / Pitch); shape changes the editor roll, colour changes the editor roll and the main page's note bars; both saved.

**Architecture:** Two header-only enums (`NoteDisplay.h`), a pure colour function (`NoteColours.{h,cpp}`) and a pure shape footprint (`NoteFootprint.h`) carry all the decisions; the roll, the editor's hit tests, `ThemePaint` and `TrackNotePreview` consume them. Settings flow exactly like View ▸ Piano Notes / MIDI Numbers: `TrackEditorPanel` menu → `SongsmithMainComponent` → `MainWindow` → `AppSettings`, and back at startup through `applyViewSettings`.

**Tech Stack:** C++20, JUCE, Catch2; build `cmake --build build`, tests `build/Tests/forge_tests`.

**Spec:** `docs/superpowers/specs/2026-10-16-songsmith-note-display-design.md`

## Global Constraints

- Everything lives in `Source/UI/`; `Source/Core/` is untouched (`forge-engine-ui-boundary`).
- Display only: no Song/ValueTree writes, no undo transactions, no export/conversion change.
- Defaults: shape Rectangle, colour Track; absent or unrecognised saved values read the defaults.
- Saved keys: `view.noteShape` = `"rect"` / `"diamond"` / `"diamondLength"`; `view.noteColour` = `"track"` / `"velocity"` / `"channel"` / `"pitch"`.
- Menu labels exactly: `Piano Roll Notes` ▸ `Rectangle`, `Diamond`, `Diamond + Length`; `Color Note by` ▸ `Track`, `Velocity`, `Channel`, `Pitch`. Always enabled, current one ticked (no radio flag exists).
- Missing NOTE `velocity` reads 64, missing `channel` reads 1 (playback's defaults, `PlaybackSnapshot.cpp:113-118`); both clamped (velocity 0..127, channel 1..16).
- The LOTRO preview roll (`Role::Preview`) keeps rectangles and its own colours.
- Main page keeps its 2 px bars; only their colour changes.
- New `.cpp` files go in both `CMakeLists.txt` (forge_ui sources, near `Source/UI/KeyDetector.cpp`) and `Tests/CMakeLists.txt` (both the test list and the `${CMAKE_SOURCE_DIR}/Source/UI/...` list).
- Commit messages: conventional commits, ending with `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`.

## Review Focus

1. A note with no `velocity`/`channel` property (older Songs, hand-built trees) must colour as 64 / channel 1, not crash or read 0 — Task 1 tests `noteColourFor` with the defaults; Task 3 tests a NOTE built without them.
2. Out-of-range stored values (channel 0 or 17, velocity 200) clamp, never index out of a table — Task 1.
3. Diamond notes near the left edge: the diamond extends `rowHeight/2` left of the start, so a partial repaint must not cull it — Task 5 tests a clip that covers only the diamond's left half.
4. A very short Diamond + Length note (line ends inside the diamond) has no resize handle; the press moves it — Task 4.
5. Main-page rows are rebuilt on every NOTE change (`TrackListComponent::valueTreePropertyChanged` → async rebuild); a rebuilt row must keep the colour mode — Task 6 tests a rebuild after a mode set.

---

### Task 1: Enums and note colours

**Files:**
- Create: `Source/UI/NoteDisplay.h`, `Source/UI/NoteColours.h`, `Source/UI/NoteColours.cpp`
- Test: `Tests/NoteColours_tests.cpp` (add to `Tests/CMakeLists.txt`)

**Interfaces:**
- Produces (`NoteDisplay.h`, no JUCE, styled like `KeyboardLabels.h`):
  `enum class NoteShape { rectangle, diamond, diamondLength };`
  `enum class NoteColourMode { track, velocity, channel, pitch };`
- Produces (`NoteColours.h`, no JUCE): `std::uint32_t noteColourFor (NoteColourMode mode, int pitch, int velocity, int channel, std::uint32_t trackArgb);` returning opaque `0xFFRRGGBB` (Track returns `trackArgb` unchanged).

- [ ] **Step 1: Write the failing tests** (`[note-colours]`)

```cpp
TEST_CASE ("noteColourFor: Track is the track's colour unchanged", "[note-colours]")
{ CHECK (noteColourFor (NoteColourMode::track, 60, 100, 3, 0xFF123456u) == 0xFF123456u); }

TEST_CASE ("noteColourFor: Velocity runs the heat map, 0 blue to 127 red, stops evenly spaced", "[note-colours]")
{
    using M = NoteColourMode;
    CHECK (noteColourFor (M::velocity, 60, 0,   1, 0) == 0xFF2F5BD8u);
    CHECK (noteColourFor (M::velocity, 60, 127, 1, 0) == 0xFFE52C32u);
    // 127 * 4 / 8 = 63.5 sits on the middle stop; 63 and 64 straddle it.
    CHECK (noteColourFor (M::velocity, 60, 64,  1, 0) != noteColourFor (M::velocity, 60, 63, 1, 0));
    CHECK (noteColourFor (M::velocity, 60, -5,  1, 0) == 0xFF2F5BD8u);   // clamps
    CHECK (noteColourFor (M::velocity, 60, 200, 1, 0) == 0xFFE52C32u);
}

TEST_CASE ("noteColourFor: Channel uses the 16-colour table, 10 grey, out of range clamps", "[note-colours]")
{
    using M = NoteColourMode;
    CHECK (noteColourFor (M::channel, 60, 64, 1,  0) == 0xFFE5484Du);
    CHECK (noteColourFor (M::channel, 60, 64, 10, 0) == 0xFF9A9EA6u);
    CHECK (noteColourFor (M::channel, 60, 64, 16, 0) == 0xFFA0A4E0u);
    CHECK (noteColourFor (M::channel, 60, 64, 0,  0) == 0xFFE5484Du);
    CHECK (noteColourFor (M::channel, 60, 64, 17, 0) == 0xFFA0A4E0u);
}

TEST_CASE ("noteColourFor: Pitch colours by pitch class, sharps halfway, every octave alike", "[note-colours]")
{
    using M = NoteColourMode;
    CHECK (noteColourFor (M::pitch, 60, 64, 1, 0) == 0xFFE5484Du);   // C4
    CHECK (noteColourFor (M::pitch, 0,  64, 1, 0) == 0xFFE5484Du);   // C-1
    CHECK (noteColourFor (M::pitch, 61, 64, 1, 0) == 0xFFEA6944u);   // C#
    CHECK (noteColourFor (M::pitch, 66, 64, 1, 0) == 0xFF4DBD98u);   // F#
    CHECK (noteColourFor (M::pitch, 71, 64, 1, 0) == 0xFFA46BE0u);   // B
}
```

Plus one CHECK per remaining spec value (velocity stops at `127 * k / 8`, every channel 1..16, all 12 pitch classes) as a table-driven loop.

- [ ] **Step 2: Run, verify it fails** — `cmake --build build` fails: `NoteColours.h` not found.

- [ ] **Step 3: Implement** — tables copied from the spec. Velocity: stops `#2f5bd8 #2d8fe0 #2fc0c8 #45c97a #9ccf3e #e6cf35 #f0a030 #ec6a2c #e52c32`; `t = clamp(v,0,127) / 127.0 * 8`, lerp each channel between stops `floor(t)` and `floor(t)+1`, `std::lround`. Channel: `#e5484d #f08a3c #e8cf3a #9ccf3e #45c97a #3fb6c6 #4f9ce8 #4f6ae8 #8a63e0 #9a9ea6 #d65fc8 #e86a96 #b5835a #7fb3a0 #c9b27a #a0a4e0`. Pitch class 0..11: `#e5484d #ea6944 #f08a3c #ecac3b #e8cf3a #5cc46a #4dbd98 #3fb6c6 #4799d7 #4f7ce8 #7973e4 #a46be0` (pitch class `((p % 12) + 12) % 12`).

- [ ] **Step 4: Run** `build/Tests/forge_tests "[note-colours]"` — PASS.

- [ ] **Step 5: Commit** — `feat(ui): note colour modes (track, velocity heat map, channel, pitch class)`

---

### Task 2: Saved settings

**Files:**
- Modify: `Source/UI/AppSettings.h` (after `setKeyboardLabels`, key constants near `keyKeyboardLabels`)
- Test: `Tests/AppSettings_tests.cpp`

**Interfaces:**
- Consumes: Task 1 enums.
- Produces: `NoteShape noteShape() const; bool setNoteShape (NoteShape);` `NoteColourMode noteColourMode() const; bool setNoteColourMode (NoteColourMode);` keys `view.noteShape`, `view.noteColour` with the Global Constraints values.

- [ ] **Step 1: Failing tests** — two cases mirroring `"AppSettings: keyboard labels default to piano notes, round-trip and persist; junk reads piano notes"` (`[app-settings][note-display]`): default reads rectangle / track; every value round-trips and survives reopening the file; `"junk"` reads the default.
- [ ] **Step 2: Run** — compile fails (no `noteShape`).
- [ ] **Step 3: Implement** the four methods like `keyboardLabels()` / `setKeyboardLabels`.
- [ ] **Step 4: Run** `build/Tests/forge_tests "[app-settings]"` — PASS.
- [ ] **Step 5: Commit** — `feat(ui): save the note shape and colour mode`

---

### Task 3: Editor roll colours

**Files:**
- Modify: `Source/UI/PianoRollNoteSource.h` (`PianoRollNote`), `Source/UI/SourceTrackNoteSource.cpp:25-38`, `Source/UI/PianoRollComponent.{h,cpp}` (`drawNotes` at `:1119`)
- Test: `Tests/SourceTrackNoteSource_tests.cpp`, `Tests/PianoRollComponent_tests.cpp`

**Interfaces:**
- Consumes: `noteColourFor`.
- Produces: `PianoRollNote::velocity` (int, default 64) and `PianoRollNote::channel` (int, default 1); `void PianoRollComponent::setNoteColourMode (NoteColourMode)` (repaints the canvas) and `NoteColourMode getNoteColourMode() const noexcept`.

- [ ] **Step 1: Failing tests**
  - `SourceTrackNoteSource: a note carries its velocity and channel, 64 and 1 when absent` (`[note-display]`).
  - `PianoRollComponent: Color Note by recolours Source notes; the Preview roll keeps its colours` (`[piano-roll][note-display]`): a `SourceRollFixture` note with velocity 127 / channel 10 / pitch 61; paint with `Access::paintCanvas`; the pixel at the note's centre is within 16 of `noteColourFor(mode, …)` for each of the four modes (the body has a ±0.15 vertical gradient, so compare at the vertical centre with the `isNear` tolerance used in `ThemePaint_tests.cpp`), and Track gives the track's colour. A `Role::Preview` roll with `setNoteColourMode (NoteColourMode::pitch)` still paints `note.colourArgb`.
- [ ] **Step 2: Run** — compile fails.
- [ ] **Step 3: Implement** — `SourceTrackNoteSource::getNote` fills velocity/channel via `getProperty (…, 64)` / `(…, 1)`; `colourArgb` stays the track colour. In `drawNotes`, `fill = role == Role::Source ? juce::Colour (noteColourFor (noteColourMode, note.pitch, note.velocity, note.channel, note.colourArgb)) : juce::Colour (note.colourArgb)`. Colour is computed at paint time, so any repaint after an edit (pitch drag, merge, undo) shows the current value.
- [ ] **Step 4: Run** `build/Tests/forge_tests "[note-display]"` and `"[piano-roll]"` — PASS.
- [ ] **Step 5: Commit** — `feat(ui): the editor roll colours notes by track, velocity, channel or pitch`

---

### Task 4: Note shape footprint and editor hit tests

**Files:**
- Create: `Source/UI/NoteFootprint.h` (header-only; JUCE `Rectangle` allowed)
- Modify: `Source/UI/SourceRollEditor.{h,cpp}` (`hitTestNote` `:89`, `hitTestEdgeZone` `:106`, `updateRubberBandSelection` `:119`)
- Test: `Tests/SourceRollEditor_tests.cpp`

**Interfaces:**
- Consumes: `NoteShape`, `PianoRollNoteBounds`.
- Produces:
  ```cpp
  enum class NoteEdges { both, rightOnly, none };
  struct NoteFootprint
  {
      juce::Rectangle<int> body;      // rectangle: the note; diamond shapes: the diamond's square
      juce::Rectangle<int> lengthLine; // diamondLength only: 2 px at the row's centre, start x to end x; else empty
      juce::Rectangle<int> hit;       // what clicks and the rubber band test
      NoteEdges edges;
  };
  NoteFootprint noteFootprint (const PianoRollNoteBounds& note, NoteShape shape);
  ```
  Diamond square: side `note.height`, `x = note.x - note.height / 2`, `y = note.y`. `hit`: rectangle → note; diamond → the square; diamondLength → square ∪ note rect. `edges`: rectangle → both; diamond → none; diamondLength → rightOnly.
  `void SourceRollEditor::setNoteShape (NoteShape) noexcept;` (default rectangle).

- [ ] **Step 1: Failing tests** (`[source-roll-editor][note-display]`)
  - `noteFootprint: …` the three shapes' `body`, `lengthLine`, `hit`, `edges` for a `{ x = 100, y = 20, width = 60, height = 14 }` note (square `{ 93, 20, 14, 14 }`, line `{ 100, 26, 60, 2 }`).
  - Diamond: a press at the start x, 5 px left of the note's rect, selects the note; a press 20 px right of the start (inside the old rect) does not; a press-drag at the right end moves, never resizes.
  - Diamond + Length: a press on the line selects; a press-drag at the line's end resizes (duration changes, start fixed); a press-drag near the start moves (no left resize).
  - Diamond + Length, a note whose end x is inside the diamond: a press-drag at the end moves.
  - Rubber band with Diamond: a band covering only the old rect's right half selects nothing; one covering the diamond selects it.
  - Rectangle: existing tests unchanged.
- [ ] **Step 2: Run** — compile fails.
- [ ] **Step 3: Implement** — `hitTestNote` and `updateRubberBandSelection` use `noteFootprint(...).hit`; `hitTestEdgeZone` returns 0 for `none`, keeps today's logic for `both`, and for `rightOnly` returns 1 only when `pos.x >= bounds.getRight() - edgeThresholdPixels` and `bounds.getRight() - edgeThresholdPixels > body.getRight()`.
- [ ] **Step 4: Run** `build/Tests/forge_tests "[source-roll-editor]"` — PASS.
- [ ] **Step 5: Commit** — `feat(ui): diamond note footprints for hit tests, resize and rubber band`

---

### Task 5: Diamond painting in the editor roll

**Files:**
- Modify: `Source/UI/Theme/ThemePaint.{h,cpp}`, `Source/UI/PianoRollComponent.{h,cpp}`
- Test: `Tests/ThemePaint_tests.cpp`, `Tests/PianoRollComponent_tests.cpp`

**Interfaces:**
- Consumes: `noteFootprint`, `SourceRollEditor::setNoteShape`.
- Produces: `void ThemePaint::paintDiamondNote (juce::Graphics&, juce::Rectangle<int> square, juce::Rectangle<int> lengthLine, juce::Colour fill, const NoteStyle& style = {});` `void PianoRollComponent::setNoteShape (NoteShape)` (forwards to the editor, repaints the canvas) and `NoteShape getNoteShape() const noexcept`.

- [ ] **Step 1: Failing tests**
  - `ThemePaint: a diamond note fills its centre and leaves its square's corners clear` (`[theme]`): centre near fill (tolerance 16), the square's four corner pixels still black.
  - `ThemePaint: a diamond with a length line paints the line to its end` and `a selected diamond is outlined in white`.
  - `PianoRollComponent: Diamond draws a diamond at the note start; the Preview roll keeps rectangles` (`[piano-roll][note-display]`): with Diamond, a pixel 20 px right of the start in the row centre is background; with Diamond + Length it is the fill; Rectangle unchanged.
  - `PianoRollComponent: a repaint covering only a diamond's left half still draws it` (Review Focus 3).
- [ ] **Step 2: Run** — compile fails.
- [ ] **Step 3: Implement** — `paintDiamondNote`: line first (flat `fill.withAlpha (0.7f)`), then the diamond as a `juce::Path` with the same top-to-bottom ±0.15 gradient and `fill.brighter (0.4f)` edge as `paintNoteBody`; no glow; when `square` is under `smallNoteSize`, fill the path flat. Selected: 1 px white stroke around the diamond and the line. `drawNotes` (Source role only) uses `noteFootprint` and culls on `footprint.hit.expanded (ThemePaint::noteOverhang)`.
- [ ] **Step 4: Run** `"[theme]"` and `"[piano-roll]"` — PASS.
- [ ] **Step 5: Commit** — `feat(ui): Diamond and Diamond + Length note shapes in the editor roll`

---

### Task 6: Main-page note bar colours

**Files:**
- Modify: `Source/UI/TrackNotePreview.{h,cpp}` (`paint` `:194-211`), `Source/UI/TrackRowComponent.{h,cpp}`, `Source/UI/TrackListComponent.{h,cpp}` (`setHeadWidth` `:96`, row creation `:318`)
- Test: `Tests/TrackNotePreview_tests.cpp`, `Tests/TrackListComponent_tests.cpp`

**Interfaces:**
- Consumes: `noteColourFor`.
- Produces: `TrackNotePreview::setNoteColourMode (NoteColourMode) noexcept` (repaints); `TrackRowComponent::setNoteColourMode (NoteColourMode)`; `TrackListComponent::setNoteColourMode (NoteColourMode)` / `NoteColourMode getNoteColourMode() const noexcept`, applied to every row and to rows built later.

- [ ] **Step 1: Failing tests**
  - `TrackNotePreview: Color Note by colours each bar` (`[track-note-preview][note-display]`): built like the existing `"paints a note as a bar…"` test, with two notes (velocity 0 and 127); Velocity mode → the first bar's pixel is `0xFF2F5BD8`, the second `0xFFE52C32`; Track mode → the track colour.
  - `TrackListComponent: the colour mode reaches every row and survives a rebuild` (Review Focus 5): set Pitch, add a track (forcing the async rebuild — follow how the existing list tests flush it), and the new row's preview reports Pitch (add `NoteColourMode getNoteColourMode() const noexcept` on `TrackNotePreview` for the check).
- [ ] **Step 2: Run** — compile fails.
- [ ] **Step 3: Implement** — `paint` sets the colour per bar with `noteColourFor (mode, pitch, velocity (default 64), channel (default 1), trackArgb)`; the track swatch and selected-row accent bar are untouched.
- [ ] **Step 4: Run** `"[track-note-preview]"` and `"[track-list]"` (the file's tag) — PASS.
- [ ] **Step 5: Commit** — `feat(ui): the main page's note bars follow Color Note by`

---

### Task 7: View menu, wiring, saving, docs

**Files:**
- Modify: `Source/UI/EditorMenuModel.h` (ids after `EditorViewMidiNumbers`, `EditorMenuState`, View menu `:222-226`), `Source/UI/TrackEditorPanel.{h,cpp}` (menu state `:82`, `invokeMenuCommand` `:175`), `Source/UI/SongsmithMainComponent.{h,cpp}` (`:181`, `:286`), `Source/UI/MainWindow.cpp` (`:213`, `applyViewSettings` `:721`)
- Docs: `CLAUDE.md` status line (after the View → Piano Notes / MIDI Numbers clause), `docs/UI_GUIDE.md` (View paragraph), `docs/ARCHITECTURE.md` (TrackEditorPanel bullet's View part and a "Note display." settings bullet after "Keyboard labels."), `docs/TESTING.md` count, add the spec + plan row to CLAUDE.md's docs map
- Test: `Tests/MenuModel_tests.cpp`, `Tests/TrackEditorPanel_tests.cpp`, `Tests/SongsmithMainComponent_tests.cpp`

**Interfaces:**
- Consumes: Tasks 2, 3, 5, 6.
- Produces: ids `EditorViewNoteRectangle`, `EditorViewNoteDiamond`, `EditorViewNoteDiamondLength`, `EditorViewColourTrack`, `EditorViewColourVelocity`, `EditorViewColourChannel`, `EditorViewColourPitch` (appended after `EditorViewMidiNumbers`); `EditorMenuState::noteShape`, `::noteColourMode`; `TrackEditorPanel::setNoteShape` / `getNoteShape` (to the roll), `setNoteColourMode` / `getNoteColourMode` (to the roll), `onNoteShapeChanged`, `onNoteColourModeChanged` (fired only on an actual change, not by the setters); `SongsmithMainComponent::setNoteShape`, `setNoteColourMode` (editor and `trackList`), `onNoteShapeChanged`, `onNoteColourModeChanged`.

- [ ] **Step 1: Failing tests**
  - Extend `"EditorMenuModel: View is Piano Notes and MIDI Numbers…"` → View labels `{ "Piano Notes", "MIDI Numbers", "-", "Piano Roll Notes", "Color Note by" }`; the submenus' labels, ids, enabled with no track, the current one ticked for each state value.
  - `TrackEditorPanel: View > Piano Roll Notes and Color Note by set the roll and report only changes` (`[track-editor][note-display]`); update the panel's View item-count check (2 → 5 including the separator, or whatever the existing helper counts — check how `TrackEditorPanel_tests.cpp` counts the keyboard-labels View items).
  - `SongsmithMainComponent: note shape and colour reach the editor and the track list, and the editor's View items reach the callbacks` mirroring the keyboard-labels test; add `editorNoteShape` / `editorNoteColourMode` accessors to `SongsmithMainComponentTestAccess`.
- [ ] **Step 2: Run** — compile fails.
- [ ] **Step 3: Implement** — follow the `EditorViewPianoNotes` path at every layer; `MainWindow` saves with `setNoteShape` / `setNoteColourMode` and `applyViewSettings` pushes both.
- [ ] **Step 4: Run** the full suite `ctest --test-dir build --output-on-failure` — 100 % pass (known skip `TransportControls: Enter gives keyboard focus away`); put the new count (`ctest --test-dir build -N | tail -1`) in `docs/TESTING.md`.
- [ ] **Step 5: Docs** as listed under Files.
- [ ] **Step 6: Commit** — `feat(ui): View > Piano Roll Notes and Color Note by in the MIDI editor`
