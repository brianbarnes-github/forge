# Snap to Key Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** MIDI editor Options ▸ Snap to Key ▸ (Enabled / Root ▸ / Type ▸ incl. Chords ▸) that refuses double-click note creation on pitches outside the chosen scale or chord.

**Architecture:** Pure JUCE-free `ScaleSnap` module (root names, scale/chord interval tables, pitch test, song-key → starting choice). `SourceRollEditor` gains a pitch filter predicate consulted only in the double-click create path. `TrackEditorPanel` owns the session-only state (enabled flag + optional user pick; with no pick it reads `SongDocument::getSongKey()` live), builds the menu via `EditorMenuModel.h`, and installs the predicate on its roll.

**Tech Stack:** C++20, JUCE, Catch2, CMake/Ninja.

**Spec:** `docs/superpowers/specs/2026-10-15-songsmith-snap-to-key-design.md`

## Global Constraints

- `Source/UI/` only; no `Source/Core/` change.
- Free functions, lowercase enumerators (house style, like `KeyMode::major`).
- Nothing saved: no `.songsmith` property, no AppSettings key, no Preferences UI.
- A refused double-click: no NOTE, no selection change, no undo transaction; never moved to another pitch.
- Only note creation is filtered; drag, resize, semitone nudge, merge, Move/Copy, quantize, import untouched.
- Root names (UTF-8, high to low): `C♭ B B♭ A♯ A A♭ G♯ G G♭ F♯ F E E♭ D♯ D D♭ C♯ C`.
- Menu labels exactly: `Snap to Key`, `Enabled`, `Root`, `Type`, `Major`, `Natural minor`, `Melodic minor`, `Harmonic minor`, `Major pentatonic`, `Minor pentatonic`, `Major blues`, `Minor blues`, `Chords`, `Major triad`, `Minor triad`, `Major seventh`, `Minor seventh`, `Dominant seventh`, `Diminished triad`, `Diminished seventh`, `Augmented fifth triad`.
- Commits: conventional commits, ending `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`. No push.
- Never launch the GUI.

## Review Focus

1. Undo of Song ▸ Set Key while following → snap follows the restored key (or turns inert when none) — pinned in Task 4.
2. Refused double-click with an existing selection → selection unchanged, `canUndo()` unchanged — pinned in Task 2.
3. Song replaced (`clearTrack`) after a pick → back to following the new song's key, Enabled kept — pinned in Task 4.
4. Enharmonic roots (A♯ vs B♭) → only the picked spelling is ticked, same pitches allowed — pinned in Tasks 1 and 4.
5. Pitch below the root's pitch class (pitch 0 with root B) → modulo stays non-negative — pinned in Task 1.

---

### Task 1: ScaleSnap data and pitch test

**Files:**
- Create: `Source/UI/ScaleSnap.h`, `Source/UI/ScaleSnap.cpp`
- Create: `Tests/ScaleSnap_tests.cpp`
- Modify: `CMakeLists.txt` and `Tests/CMakeLists.txt` (add `Source/UI/ScaleSnap.cpp` after `Source/UI/KeyDetection.cpp`; `ScaleSnap_tests.cpp` after `KeyDetector_tests.cpp`)

**Interfaces:**
- Consumes: `keyAt(int)`, `KeyCandidate::tonic/mode/name()` from `KeyDetector.h`.
- Produces:
  ```cpp
  enum class SnapScale { major, naturalMinor, melodicMinor, harmonicMinor,
                         majorPentatonic, minorPentatonic, majorBlues, minorBlues,
                         majorTriad, minorTriad, majorSeventh, minorSeventh,
                         dominantSeventh, diminishedTriad, diminishedSeventh, augmentedTriad };
  inline constexpr int snapScaleCount = 16;
  inline constexpr int snapFirstChord = 8;          // (int) SnapScale::majorTriad
  inline constexpr int snapRootCount  = 18;
  struct SnapRoot { const char* name; int pitchClass; };
  const std::array<SnapRoot, snapRootCount>& snapRoots();   // the Global Constraints order
  const char* snapScaleName (SnapScale);                    // the menu label
  const std::vector<int>& snapScaleIntervals (SnapScale);   // the spec's table
  struct SnapKey { int rootIndex = 0; SnapScale scale = SnapScale::major; };   // + operator==
  bool isPitchAllowed (const SnapKey&, int pitch);
  SnapKey snapKeyForSongKey (int songKeyIndex);   // root spelled as keyAt(i).name() spells the tonic
  ```

- [ ] **Step 1: Write failing tests** in `Tests/ScaleSnap_tests.cpp`, tag `[scale-snap]`:
  - `snapRoots()` names equal the 18 Global Constraints names in order; pitch classes C♭=11, B=11, B♭=10, A♯=10, A=9, A♭=8, G♯=8, G=7, G♭=6, F♯=6, F=5, E=4, E♭=3, D♯=3, D=2, D♭=1, C♯=1, C=0.
  - `snapScaleIntervals` equals the spec table for every one of the 16 (e.g. `minorBlues == {0,3,5,6,7,10}`, `augmentedTriad == {0,4,8}`); `snapScaleName` matches each label.
  - `isPitchAllowed({rootOf("C"), major}, p)` true for 60 62 64 65 67 69 71 72, false for 61 66 70.
  - Root A♯ and root B♭ with `minorPentatonic` agree on every pitch 0..127.
  - Root B, `major`: pitch 0 (C) false, 11 true, 1 (C♯) true, 127 (G) false — no negative modulo.
  - Chord: root G, `dominantSeventh` allows only pitch classes 7 11 2 5.
  - `snapKeyForSongKey(0)` → root "C", major; `(1)` → "D♭", major; `(13)` → "C♯", naturalMinor; `(22)` → "B♭", naturalMinor; every index 0..23 maps to a root whose name is the first word of `keyAt(i).name()`.

- [ ] **Step 2: Run** `cmake --build build && ctest --test-dir build -R scale-snap --output-on-failure` — expect a build failure (missing header).

- [ ] **Step 3: Implement** `ScaleSnap.{h,cpp}` with the interface above. `isPitchAllowed` uses `((pitch - pc) % 12 + 12) % 12`. `snapKeyForSongKey` matches the text of `keyAt(i).name()` before the first space against `snapRoots()` names.

- [ ] **Step 4: Run** the same command — expect PASS.

- [ ] **Step 5: Commit** `feat(ui): ScaleSnap scale and chord tables for Snap to Key`

### Task 2: Pitch filter on note creation

**Files:**
- Modify: `Source/UI/SourceRollEditor.h/.cpp` (filter member, `mouseDown` double-click branch, `createNoteAt`)
- Modify: `Source/UI/PianoRollComponent.h/.cpp` (forwarding setter)
- Test: `Tests/SourceRollEditor_tests.cpp`

**Interfaces:**
- Produces:
  ```cpp
  // SourceRollEditor: nullptr / empty = every pitch allowed. Consulted only by
  // double-click creation; a refused pitch changes nothing and returns false.
  void setPitchFilter (std::function<bool (int pitch)> allowed);
  // PianoRollComponent: forwards to its SourceRollEditor (no-op without one).
  void setPitchFilter (std::function<bool (int pitch)> allowed);
  ```

- [ ] **Step 1: Write failing tests** (`[source-roll-editor]`, existing `Fixture`):
  - `"SourceRollEditor: a pitch filter refuses a double-click on a disallowed row"` — filter `p != 72`; double-click the empty cell at pitch 72 → `mouseDown` returns false, note count stays 2, `canUndo()` equals its value before.
  - `"...a refused double-click leaves the selection alone"` — select `noteA` first; after the refused double-click, `isSelected(noteA)` and `getNumSelected() == 1`.
  - `"...an allowed pitch still creates under a filter"` — filter `p == 72` → note created at 72.
  - `"...clearing the filter allows every pitch again"` — `setPitchFilter(nullptr)` then double-click at 72 creates.
  - A drag of `noteA` onto a filtered-out pitch still moves it (filter never touches moves).

- [ ] **Step 2: Run** `ctest --test-dir build -R source-roll-editor --output-on-failure` after building — expect compile failure.

- [ ] **Step 3: Implement** — check the filter on `geometry.pitchForY(pos.y)` in the double-click branch before `createNoteAt`; return false when refused.

- [ ] **Step 4: Run** — PASS.

- [ ] **Step 5: Commit** `feat(ui): pitch filter on MIDI editor note creation`

### Task 3: Snap to Key menu model

**Files:**
- Modify: `Source/UI/EditorMenuModel.h`
- Test: `Tests/MenuModel_tests.cpp` (beside the existing `[editor-menu]` cases)

**Interfaces:**
- Consumes: `snapRoots()`, `snapScaleName`, `snapScaleCount`, `snapFirstChord` (Task 1).
- Produces:
  ```cpp
  // appended to EditorMenuCommandId:
  EditorOptionsSnapToKeyEnabled,
  // separate ranges (enum constants, like SongKeyBase):
  EditorSnapRootBase  = 1000,   // + root index 0..17
  EditorSnapScaleBase = 1100,   // + (int) SnapScale 0..15
  constexpr bool isEditorSnapRootCommand (int id) noexcept;
  constexpr bool isEditorSnapScaleCommand (int id) noexcept;
  // EditorMenuState:
  bool snapToKey = false;      // Enabled is ticked
  int snapRootIndex = -1;      // current root, -1 = no key (song has none, nothing picked)
  int snapScaleIndex = -1;     // current (int) SnapScale, -1 with no key
  ```

- [ ] **Step 1: Write failing tests** (`[menu-model][editor-menu]`):
  - Options is `Snap to Grid`, `Snap to Key>` (the `labels()` helper's `>` suffix).
  - Snap to Key children: `Enabled`, `Root>`, `Type>`. Type children: the 8 scale labels then `Chords>`; Chords children: the 8 chord labels.
  - With `snapRootIndex = -1`: Enabled and Type disabled, Root enabled, nothing ticked.
  - With `snapRootIndex = 3` (A♯), `snapScaleIndex = (int) SnapScale::dominantSeventh`, `snapToKey = true`: Enabled enabled and ticked; only Root item 3 ticked (B♭ item 2 not); only `Dominant seventh` ticked inside Chords; ids are `EditorSnapRootBase + i` and `EditorSnapScaleBase + (int) scale`.
  - `isEditorSnapRootCommand(1000)`/`(1017)` true, `(1018)` false; scale `(1100)`/`(1115)` true, `(1116)` false.

- [ ] **Step 2: Run** `ctest --test-dir build -R editor-menu --output-on-failure` — compile failure.

- [ ] **Step 3: Implement** the submenus with `menu_detail::submenu`/`item`, Snap to Key submenu itself always enabled.

- [ ] **Step 4: Run** — PASS (and the existing Options test updated if it pinned Options to one item).

- [ ] **Step 5: Commit** `feat(ui): MIDI editor Options > Snap to Key menu model`

### Task 4: TrackEditorPanel state, dispatch and docs

**Files:**
- Modify: `Source/UI/TrackEditorPanel.h/.cpp`
- Test: `Tests/TrackEditorPanel_tests.cpp`
- Docs: `docs/UI_GUIDE.md` (editor Options menu), `docs/ARCHITECTURE.md` (editor panel / §9.19 menu bar bullet), `docs/TESTING.md` (count + bullet), `CLAUDE.md` status line (append Options → Snap to Key to the MIDI editor menu bar clause)

**Interfaces:**
- Consumes: Tasks 1–3; `SongDocument::getSongKey()` / `setSongKey(...)`.
- Produces (panel, public):
  ```cpp
  // nullopt = no key (song has none and nothing picked). The pick, else snapKeyForSongKey(song key).
  std::optional<SnapKey> currentSnapKey() const;
  bool getSnapToKey() const noexcept;
  ```
  Private state: `bool snapToKey = false; std::optional<SnapKey> pickedSnapKey;`. Constructor installs `roll.setPitchFilter` with a lambda that returns true unless `snapToKey && currentSnapKey()` and then `isPitchAllowed`. Root pick: `pickedSnapKey = { index, current ? current->scale : SnapScale::major }`. Scale pick (only with a current key): `pickedSnapKey = { current->rootIndex, scale }`. `EditorOptionsSnapToKeyEnabled` toggles `snapToKey` only when a current key exists. `clearTrack()` resets `pickedSnapKey`, keeps `snapToKey`.

- [ ] **Step 1: Write failing tests** (`[track-editor][snap-to-key]`), adding to `TrackEditorPanelTestAccess` a `doubleClickRow(w, pitch)` that calls `w.roll.handleEditorMouseDown(pos, {}, true)` at an empty tick (48000) of that row; doc with a track whose song key is set via `doc.setSongKey(0)` (C major):
  - Enabled off by default: double-click at 61 creates a note.
  - After `invokeMenuCommand(EditorOptionsSnapToKeyEnabled)`: 61 refused (note count unchanged), 62 created.
  - Following: `doc.setSongKey(21)` (A minor) → `currentSnapKey()` root "A", naturalMinor; `doc.undo()` → back to C major; with no song key (`setSongKey(std::nullopt)`) → `currentSnapKey()` nullopt, 61 created even though Enabled was toggled on, and Enabled is disabled in the menu (assert via `editorMenus` state through the panel, or `currentSnapKey()`).
  - Pick freezes: pick `EditorSnapScaleBase + (int) SnapScale::majorTriad` → only C E G allowed; then `doc.setSongKey(7)` → still C major triad.
  - Root pick with no song key → that root with `major`; picking A♯ (index 3) keeps rootIndex 3, not 2.
  - `clearTrack()` after a pick → following the song key again; `getSnapToKey()` unchanged.

- [ ] **Step 2: Run** `ctest --test-dir build -R snap-to-key --output-on-failure` — compile failure.

- [ ] **Step 3: Implement** the state, `getMenuForIndex` fields (`snapToKey`, `snapRootIndex`, `snapScaleIndex` from `currentSnapKey()`), `invokeMenuCommand` handling, the filter lambda, `clearTrack` reset, and the header comment on `invokeMenuCommand` extended.

- [ ] **Step 4: Run** the full suite `ctest --test-dir build --output-on-failure` — all pass (known flake: "TempoMeterEditorWindow: a point dragged to a new tick stays selected, so Delete removes it" under `-j`; rerun alone).

- [ ] **Step 5: Update docs** listed above; test count in `docs/TESTING.md` from `ctest -N | tail -1`.

- [ ] **Step 6: Commit** `feat(ui): MIDI editor Options > Snap to Key refuses off-key note creation`
