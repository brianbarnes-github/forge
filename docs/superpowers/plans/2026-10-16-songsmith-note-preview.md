# MIDI Editor Note Preview Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** The MIDI editor's Options ▸ Note Preview ▸ checkboxes (inserting / editing / velocity changes), saved in AppSettings and mirrored on Preferences ▸ Editing, sound the inserted or moved note(s) through the playback synth.

**Architecture:** A new live-note path in `PlaybackEngine`. The message thread pushes a fixed-size `PreviewRequest` into a lock-free FIFO, and the audio thread drains it every block, both playing and stopped. The audio thread chases the note's playback channel state from the current snapshot onto one of six reserved synth channels (256–261), which `SynthVoice::releaseAll` no longer touches. `SourceRollEditor` reports inserted and moved notes through a listener. `TrackEditorPanel` applies the checkbox state and the chord rule, then calls `PlaybackController::previewNotes`.

**Tech Stack:** C++17, JUCE (`juce::AbstractFifo`), TinySoundFont (vendored), Catch2. No new dependencies.

**Spec:** `docs/superpowers/specs/2026-10-16-songsmith-note-preview-design.md`

## Global Constraints

- C++17: no defaulted comparison operators (write `==` by hand).
- No new dependencies.
- Everything lives in `Source/UI/`; nothing in `Source/Core/`.
- Audio thread (`renderBlock` and everything it calls, every `EventSink` method except `prepare`): never allocate, lock, free or throw.
- Previews never change the document: no undo transaction, no dirty flag.
- Menu labels exactly: `Note Preview` (submenu), `Preview notes when inserting`, `Preview notes when editing`, `Preview notes when velocity changes`.
- All three settings default **off** and persist in `AppSettings` under `editing.previewOnInsert`, `editing.previewOnEdit`, `editing.previewOnVelocity` (`"1"` = on; anything else, including absent, = off).
- Chord rule: 1–6 notes, all with the same `startTick`; otherwise no preview.
- Length: the note's own duration (ticks → seconds through the snapshot's tempo map), capped at `kMaxPreviewSeconds = 1.0`, timed in audio frames at the device sample rate (the playback rate knob does not stretch it).
- Previews ignore mute/solo, follow the track's `playbackVolume` gain (`scaleVelocity`) and the Mixer pan override, and pass through the master gain.
- Strict TDD. Single-test runs: `ctest --test-dir build -R <name> --output-on-failure` (`-R` matches test names, not tags).
- Each new `.cpp` goes in **both** `CMakeLists.txt` (forge_ui sources) and `Tests/CMakeLists.txt` (the test target's source list); each new test file in `Tests/CMakeLists.txt`.
- Commit messages: conventional commits, ending with `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`.
- Do not launch the GUI.

## Review Focus

1. **A preview while a drag rebuilds the snapshot.** Every drag step edits the document, so the snapshot swaps and the engine calls `releaseAll()`. The preview must keep sounding. Pinned by Task 2's SynthVoice test and Task 3's "survives a snapshot swap" test.
2. **A new note on a MIDI channel the snapshot has not seen** (a double-click on an empty track) must still sound: program 0, or the drum kit on channel 10. Pinned in Task 3.
3. **Sustain (CC64 on) earlier in the track** must not hold the preview past its cap. The chase skips CC64. Pinned in Task 3.
4. **A drag that crosses rows quickly:** each new row cuts off the previous preview's notes. No pile-up of voices. Pinned in Task 3 ("a new request stops the previous one") and Task 5.
5. **No playback controller** (an editor without `setPlayback`) or no SoundFont: previewing is a silent no-op, no crash. Pinned in Task 5 (panel without playback) and by `SynthVoice` ignoring events with no font (existing test).

---

### Task 1: Preview request types and the chord rule

**Files:**
- Create: `Source/UI/NotePreview.h` (header-only: settings, change kinds and the listener type; includes `juce_data_structures`)
- Create: `Source/UI/Playback/PreviewRequest.h`, `Source/UI/Playback/PreviewRequest.cpp`
- Modify: `CMakeLists.txt` (after `Source/UI/Playback/PlaybackSnapshot.cpp`), `Tests/CMakeLists.txt` (source after `PlaybackSnapshot.cpp`; test file after `PlaybackSnapshot_tests.cpp`)
- Test: `Tests/PreviewRequest_tests.cpp`

**Interfaces:**
- Produces, `Source/UI/NotePreview.h` (namespace `lotro`):
  ```cpp
  enum class NoteChange { inserted, edited, velocityChanged };
  struct NotePreviewSettings { bool onInsert = false; bool onEdit = false; bool onVelocity = false; };
  bool operator== (const NotePreviewSettings&, const NotePreviewSettings&) noexcept;  // inline, hand-written
  bool operator!= (const NotePreviewSettings&, const NotePreviewSettings&) noexcept;
  constexpr bool previewWanted (const NotePreviewSettings& s, NoteChange c) noexcept; // inserted→onInsert, edited→onEdit, velocityChanged→onVelocity
  using NoteChangeListener = std::function<void (NoteChange change, const std::vector<juce::ValueTree>& notes)>;
  ```
- Produces, `Source/UI/Playback/PreviewRequest.h` (includes `PlaybackSnapshot.h` for `kMaxVirtualChannels`):
  ```cpp
  constexpr int kMaxPreviewNotes = 6;
  constexpr int kPreviewChannelBase = kMaxVirtualChannels;              // 256
  constexpr int kSynthChannels = kMaxVirtualChannels + kMaxPreviewNotes; // 262
  constexpr double kMaxPreviewSeconds = 1.0;
  struct PreviewNote { juce::int64 trackId = -1; int midiChannel = 1; int pitch = 60; int velocity = 100; int startTick = 0; int endTick = 0; };
  struct PreviewRequest { std::array<PreviewNote, kMaxPreviewNotes> notes {}; int count = 0; };
  // nullopt for 0 notes, more than kMaxPreviewNotes, or differing startTicks.
  std::optional<PreviewRequest> makePreviewRequest (juce::int64 trackId, const std::vector<juce::ValueTree>& notes);
  ```
  Each `PreviewNote` comes from the NOTE's properties: `channel` clamped 1..16 (default 1), `pitch` clamped 0..127, `velocity` clamped 1..127 (default 64, as `buildSnapshot`), `startTick` (max 0), and `endTick = startTick + max (1, durationTicks)`. Notes keep their input order.

- [ ] **Step 1: Write the failing tests** in `Tests/PreviewRequest_tests.cpp`, tag `[note-preview]`. Build NOTE trees with `playbacktest::addNote` on a `SongDocument` track and read the children back:
  - `"PreviewRequest: one note becomes a one-note request with its channel, pitch, velocity and span"`: note (pitch 64, start 480, duration 240, velocity 90, channel 3), trackId 7. Check `count == 1`, `notes[0]` = `{7, 3, 64, 90, 480, 720}`.
  - `"PreviewRequest: up to six notes starting together are a chord"`: six notes at start 0 → `count == 6`, in input order.
  - `"PreviewRequest: seven notes, staggered starts, or no notes give no request"`: seven notes at the same start → `nullopt`; two notes at starts 0 and 480 → `nullopt`; empty vector → `nullopt`.
  - `"NotePreview: previewWanted follows the matching checkbox"`: for each `NoteChange`, true only when its own flag is on (all three, each flag alone); default `NotePreviewSettings{}` wants none.
- [ ] **Step 2: Run** `cmake --build build && ctest --test-dir build -R "PreviewRequest|NotePreview" --output-on-failure`. Expected: the build fails (missing headers), which counts as RED.
- [ ] **Step 3: Implement** the two headers and `makePreviewRequest` as specified.
- [ ] **Step 4: Run** the same command. Expected: all PASS.
- [ ] **Step 5: Commit** `feat(ui): note preview request and chord rule`.

---

### Task 2: Six reserved preview channels in SynthVoice

**Files:**
- Modify: `Source/UI/Playback/SynthVoice.cpp` (`build`, `handle`, `releaseChannel`, `releaseAll`, `resetChannel`, `setChannelPan`), `Source/UI/Playback/SynthVoice.h` (comment), `Source/UI/Playback/EventSink.h` (comment on `releaseAll`)
- Test: `Tests/SynthVoice_tests.cpp`

**Interfaces:**
- Consumes: `kSynthChannels`, `kPreviewChannelBase` (Task 1).
- Produces (contract change): `EventSink::releaseAll()` releases **playback channels only** (`0 .. kMaxVirtualChannels - 1`). Preview channels `kPreviewChannelBase .. kSynthChannels - 1` are left sounding. Every other `SynthVoice` method accepts channels `0 .. kSynthChannels - 1`.

- [ ] **Step 1: Write the failing tests**, using the local-SoundFont skip pattern already in the file:
  - `"SynthVoice: a preview channel sounds and is left alone by releaseAll"`: Program then NoteOn 64/120 on `kPreviewChannelBase + kMaxPreviewNotes - 1`; render 4 blocks with peak > 0.001. Call `releaseAll()`, render 400 blocks, and the last block's peak is still > 0.001 (a sustaining preset: use program 19, Church Organ, which does not decay). Then `releaseChannel` that channel and render 400 blocks: tail < 0.001.
  - `"SynthVoice: releaseAll still silences the playback channels"`: the existing smoke test already pins this. Leave it as it is.
- [ ] **Step 2: Run** `ctest --test-dir build -R SynthVoice --output-on-failure`. Expected: FAIL (the channel is out of range, so it never sounds). If the SoundFont is absent the test WARN-skips; note that in the report and rely on Task 3's engine tests.
- [ ] **Step 3: Implement.** In `build()`, allocate channels up front by touching `kSynthChannels - 1`. Change the bounds checks to use `kSynthChannels`. Make `releaseAll()` call `tsf_channel_note_off_all` for each channel `0 .. kMaxVirtualChannels - 1` instead of `tsf_note_off_all`.
- [ ] **Step 4: Run** `ctest --test-dir build -R SynthVoice --output-on-failure`. Expected: PASS.
- [ ] **Step 5: Commit** `feat(ui): reserved preview channels in SynthVoice`.

---

### Task 3: The engine's live preview path and `PlaybackController::previewNotes`

**Files:**
- Modify: `Source/UI/Playback/PlaybackEngine.h`, `Source/UI/Playback/PlaybackEngine.cpp`, `Source/UI/Playback/PlaybackController.h` (and `.cpp` if not inline)
- Test: `Tests/PlaybackEngine_tests.cpp`, `Tests/PlaybackController_tests.cpp`

**Interfaces:**
- Consumes: `PreviewRequest`, `PreviewNote`, `kPreviewChannelBase`, `kMaxPreviewNotes`, `kMaxPreviewSeconds` (Task 1).
- Produces:
  ```cpp
  // PlaybackEngine, message thread. False when the queue is full (the request is dropped).
  bool requestPreview (const PreviewRequest& request) noexcept;
  // PlaybackController, message thread: forwards to the engine.
  void previewNotes (const PreviewRequest& request);
  ```
- Private engine members: `static constexpr int kPreviewQueueSize = 8;`, `juce::AbstractFifo previewFifo { kPreviewQueueSize };`, `std::array<PreviewRequest, kPreviewQueueSize> previewQueue;`, and audio-thread-only `std::array<int, kMaxPreviewNotes> previewKey` (−1 = silent) and `std::array<long, kMaxPreviewNotes> previewFramesLeft`. Methods `void servicePreviews (const PlaybackSnapshot* snapshot, int numSamples) noexcept;` and `void startPreviewNote (const PlaybackSnapshot& snapshot, int slot, const PreviewNote& note) noexcept;`.

**Behaviour (the algorithm the tests pin):**
- `renderBlockUnscaled` calls `servicePreviews (snapshot, numSamples)` once per block. The call goes after the `releaseAll` / `releaseNewlyMutedTracks` lines and before the `if (! playing)` early return, so previews work stopped and playing. Preview events land at the block's first frame.
- `servicePreviews`:
  1. Drain the FIFO and keep only the last request.
  2. If there was one: send NoteOff for every sounding slot (`kPreviewChannelBase + slot`, its key) and mark it silent. Then, with a snapshot, `startPreviewNote` for each of its notes (slot = index).
  3. For each sounding slot: if `previewFramesLeft <= 0`, send NoteOff and mark it silent; otherwise subtract `numSamples`.
- `startPreviewNote` on channel `ch = kPreviewChannelBase + slot`:
  1. `sink.resetChannel (ch)`.
  2. Find `vch`, the snapshot channel whose `trackId` and `midiChannel` match the note, and `trackIndex = snapshot.trackIndexForId (note.trackId)`.
  3. If `vch` exists: replay, in order, every snapshot event with `e.virtualChannel == vch` and `e.tick <= note.startTick` (events are tick-sorted, so stop at the first `tick > startTick`) of kind Control, Program or PitchBend, copied with `virtualChannel = ch`. Skip CC64. When the track has a pan override, skip CC10/CC42, and after a CC121 re-apply the override. If `vch` does not exist: send Program `{ch, 0, midiChannel == 10 ? 1 : 0}`.
  4. If the track has a pan override, `sink.setChannelPan (ch, override)`.
  5. Velocity = `scaleVelocity (note.velocity, snapshot.gainPercent (trackIndex))` when `trackIndex >= 0`, else `note.velocity`. If it is > 0: NoteOn `{ch, pitch, velocity}`, mark the slot sounding, and set `previewFramesLeft = llround (min (kMaxPreviewSeconds, tempo(endTick) − tempo(startTick)) × sampleRate)` using `snapshot.tempo().ticksToSeconds`.
- Mute/solo is never consulted.

- [ ] **Step 1: Write the failing tests** in `Tests/PlaybackEngine_tests.cpp`, tag `[playback][engine][note-preview]`, using the file's `Rig` (48 kHz, 512-frame blocks, transport stopped unless stated):
  - `"PlaybackEngine: a preview sounds while stopped on the first preview channel with the track's program"`: the track has Program Change 24 (`addEvent (track, 0, {0xC0, 24})`) and a note on channel 1. Request one note (pitch 67, velocity 100, start 0, end 480). After one render: Program `{256, 24}` before NoteOn `{256, 67, 100}`, both at frame 0. No events on channels < 256 come from the preview.
  - `"PlaybackEngine: a preview stops after its own length, capped at one second"`: a note of 480 ticks at 120 BPM is 0.5 s, so the NoteOff on 256 is at a frame in [24000, 24512). A note of 4800 ticks is capped, so the NoteOff is in [48000, 48512).
  - `"PlaybackEngine: a new preview stops the previous one first"`: request pitch 60, render 1 block, request pitch 62, render 1 block. NoteOff {256, 60} comes before NoteOn {256, 62} in the second block.
  - `"PlaybackEngine: a chord preview uses one preview channel per note"`: three notes → NoteOns on 256, 257, 258.
  - `"PlaybackEngine: a preview chases controls up to its start, skips sustain, and ignores later ones"`: events CC7=30 @0, CC64=127 @0, CC7=90 @960. A preview at start 480 sends CC7=30 on 256 and no CC64, and no CC7=90.
  - `"PlaybackEngine: a preview on a channel the snapshot lacks gets program 0, or the drum kit on channel 10"`: notes with midiChannel 5, then 10, on a track with only channel-1 material → Program `{256, 0, 0}` and Program `{256, 0, 1}`.
  - `"PlaybackEngine: a preview follows the track gain and pan override and ignores mute"`: `snapshot->setGainPercent (t, 50)`, `setPanOverride (t, -40)`, `setAudible (t, false)`. NoteOn velocity = `scaleVelocity (100, 50)`, and `sink.pans` contains `{256, -40}`.
  - `"PlaybackEngine: a preview keeps sounding across a snapshot swap while playing"`: play, request, render 1 block, publish a fresh `buildSnapshot`, render 1 block. No NoteOff on 256 before its length (RecordingSink records `releaseAll` only as a count; the engine sends no preview NoteOff on a swap).
  - `"PlaybackEngine: a full preview queue drops the request"`: nine `requestPreview` calls without rendering. The ninth returns false.
  - In `Tests/PlaybackController_tests.cpp`, `"PlaybackController: previewNotes reaches the engine"`: a controller with a RecordingSink; `previewNotes` one note; `controller.engine().renderBlock (...)` once gives a NoteOn on 256.
- [ ] **Step 2: Run** `ctest --test-dir build -R "PlaybackEngine|PlaybackController" --output-on-failure`. Expected: the build fails (`requestPreview` / `previewNotes` missing), which counts as RED.
- [ ] **Step 3: Implement** as described above.
- [ ] **Step 4: Run** the same command, then the whole suite: `ctest --test-dir build --output-on-failure`. Expected: all PASS. Known flakes under `-j`: the two `TempoMeterEditorWindow` tests, which pass when run alone.
- [ ] **Step 5: Commit** `feat(ui): live note preview path in the playback engine`.

---

### Task 4: SourceRollEditor reports inserted and moved notes

**Files:**
- Modify: `Source/UI/SourceRollEditor.h`, `Source/UI/SourceRollEditor.cpp`, `Source/UI/PianoRollComponent.h`, `Source/UI/PianoRollComponent.cpp`
- Test: `Tests/SourceRollEditor_tests.cpp`

**Interfaces:**
- Consumes: `NoteChange`, `NoteChangeListener` (Task 1, `NotePreview.h`).
- Produces:
  ```cpp
  void SourceRollEditor::setNoteChangeListener (NoteChangeListener listener);
  void PianoRollComponent::setNoteChangeListener (NoteChangeListener listener);   // forwards to sourceEditor, as setPitchFilter does
  ```
- When the listener fires:
  - `createNoteAt` → `(inserted, { newNote })`, after the note is added. A double-click refused by the pitch filter or landing on a note does not fire.
  - Move drag, in `mouseDrag`: if any dragged note's pitch property changed in **this step**, fire `(edited, every dragged note)` once for the step and set a new member `bool movePitchChanged` (reset in `mouseDown`).
  - Move drag, in `mouseUp`: if `!movePitchChanged` and any dragged note's `startTick` differs from its `DragOriginal::startTick`, fire `(edited, every dragged note)` once. (Read `dragMode` / `dragOriginals` before they are cleared.)
  - Never fired for resize (either edge), rubber band, clicks, delete, quantize or undo.

- [ ] **Step 1: Write the failing tests**, tag `[source-roll-editor][note-preview]`, using the file's `Fixture` and a recorder lambda that pushes `(change, notes.size(), first note's pitch)`:
  - `"SourceRollEditor: a double-click insert reports the new note"`: one `inserted` call with the new note.
  - `"SourceRollEditor: a pitch-filter refusal reports nothing"`: filter refuses everything → no calls.
  - `"SourceRollEditor: a drag reports each new row at once, and nothing more on release"`: press on noteA, drag up one row, then two rows, then release. Two `edited` calls (pitches 61, 62), none on release.
  - `"SourceRollEditor: a drag that only moves in time reports once, on release"`: drag noteA sideways by a quarter's width (same row). No call during the drag; one `edited` call on `mouseUp`.
  - `"SourceRollEditor: a drag that leaves and returns to its row while moving in time reports no release preview"`: up one row, back down, sideways, release. Two `edited` calls during the drag, none on release.
  - `"SourceRollEditor: a group drag reports every selected note"`: select noteA and noteB, drag up one row → one call with 2 notes.
  - `"SourceRollEditor: resizing reports nothing"`: right-edge drag and left-edge drag → no calls.
- [ ] **Step 2: Run** `ctest --test-dir build -R SourceRollEditor --output-on-failure`. Expected: the build fails, which counts as RED.
- [ ] **Step 3: Implement** as specified.
- [ ] **Step 4: Run** the same command. Expected: PASS.
- [ ] **Step 5: Commit** `feat(ui): SourceRollEditor reports inserted and moved notes`.

---

### Task 5: Options ▸ Note Preview menu and the panel wiring

**Files:**
- Modify: `Source/UI/EditorMenuModel.h`, `Source/UI/TrackEditorPanel.h`, `Source/UI/TrackEditorPanel.cpp`
- Test: `Tests/MenuModel_tests.cpp`, `Tests/TrackEditorPanel_tests.cpp`

**Interfaces:**
- Consumes: `NotePreviewSettings`, `previewWanted` (Task 1), `makePreviewRequest` (Task 1), `PlaybackController::previewNotes` (Task 3), `PianoRollComponent::setNoteChangeListener` (Task 4).
- Produces:
  - `EditorMenuCommandId`: append `EditorOptionsPreviewOnInsert, EditorOptionsPreviewOnEdit, EditorOptionsPreviewOnVelocity` after `EditorOptionsSnapToKeyEnabled` (before `EditorSnapRootBase`).
  - `EditorMenuState::notePreview` (`NotePreviewSettings`, default all off).
  - `menu_detail::notePreviewSubmenu (const EditorMenuState&)`: `submenu ("Note Preview", { item (EditorOptionsPreviewOnInsert, "Preview notes when inserting", {}, true, s.notePreview.onInsert), …edit…, …velocity… })`. Options becomes: Snap to Grid, Snap to Key ▸, Note Preview ▸. All items are always enabled.
  - `TrackEditorPanel`:
    ```cpp
    void setNotePreview (const NotePreviewSettings& settings) noexcept;   // does not fire the callback
    NotePreviewSettings getNotePreview() const noexcept;
    std::function<void (const NotePreviewSettings&)> onNotePreviewChanged; // fired by the three menu items with the new value
    ```
    The constructor installs a `roll.setNoteChangeListener` lambda. It does nothing without `playback`, without a track (`currentTrackId == -1`), or when `! previewWanted (notePreview, change)`. Otherwise `if (auto r = makePreviewRequest (currentTrackId, notes)) playback->previewNotes (*r);`.

- [ ] **Step 1: Write the failing tests:**
  - `Tests/MenuModel_tests.cpp`: update the Options test to expect `labels == { "Snap to Grid", "Snap to Key>", "Note Preview>" }` and rename it to "…, then Snap to Key and Note Preview". The `snapToKeyMenu` helper's `REQUIRE (options.items.size() == 2)` becomes `3`. Add `"EditorMenuModel: Note Preview holds three checkboxes, enabled with or without a track, ticked by the settings"`: the child labels and ids are exactly the three above, all enabled with the default state, and each ticked only when its own flag is set.
  - `Tests/TrackEditorPanel_tests.cpp`: the existing menu-bar count check becomes `getMenuForIndex (3, "Options").getNumItems() == 3`. With `MarkerPanelFixture`, tag `[track-editor][note-preview]`, and a helper `previewNoteOns (f)` that renders one block through `f.controller->engine().renderBlock` and returns the NoteOn pitches on channels ≥ `kPreviewChannelBase`:
    - `"TrackEditorPanel: Note Preview starts all off, and inserting is silent"`: `getNotePreview() == NotePreviewSettings{}`; double-click row 62 → no preview NoteOns.
    - `"TrackEditorPanel: Preview notes when inserting sounds the inserted note and reports the change"`: record `onNotePreviewChanged`. Pick `EditorOptionsPreviewOnInsert` → the callback gets `onInsert == true`. Double-click row 62 → preview NoteOns `{62}`. Pick it again → off, and the next insert is silent.
    - `"TrackEditorPanel: Preview notes when editing sounds each new row of a drag"`: enable editing via `setNotePreview`. Drag the fixture's note (pitch 60) up two rows, one row per drag step, rendering a block after each step → `{61}` then `{62}`. With only `onInsert` on, the same drag is silent.
    - `"TrackEditorPanel: a drag of more than six notes, or of notes that do not start together, is silent"`: seven notes at tick 0 selected and dragged up a row → no preview; two notes at ticks 0 and 480 → no preview.
    - `"TrackEditorPanel: without playback, a preview-enabled insert still adds the note"`: a panel without `setPlayback`, `onInsert` on → double-click adds a note, no crash.
    - `"TrackEditorPanel: Preview notes when velocity changes toggles and reports but nothing triggers it yet"`: pick it → `getNotePreview().onVelocity` and the callback fires.

    Add test-access helpers as needed (`dragNoteRows (panel, note, rows)` pressing at the note's centre via `geometry.noteBounds`, dragging one row per `handleEditorMouseDrag`, then `handleEditorMouseUp`; `selectNotes` via `roll.sourceEditor->addToSelection`).
- [ ] **Step 2: Run** `ctest --test-dir build -R "MenuModel|TrackEditorPanel" --output-on-failure`. Expected: the build fails or the tests FAIL.
- [ ] **Step 3: Implement** as specified (menu state in `MenuModel::getMenuForIndex`: `s.notePreview = owner.notePreview;` and dispatch in `invokeMenuCommand`).
- [ ] **Step 4: Run** the same command, then the whole suite. Expected: all PASS.
- [ ] **Step 5: Commit** `feat(ui): MIDI editor Options > Note Preview`.

---

### Task 6: Saved settings, Preferences ▸ Editing, and docs

**Files:**
- Modify: `Source/UI/AppSettings.h`, `Source/UI/Preferences/EditingPreferencesPage.{h,cpp}`, `Source/UI/SongsmithMainComponent.{h,cpp}`, `Source/UI/MainWindow.cpp`
- Docs: `docs/UI_GUIDE.md` (#28 Options paragraph near the Snap to Key text; the Preferences ▸ Editing description), `docs/ARCHITECTURE.md` (§9.14 the preview path and the `releaseAll` scope; the TrackEditorPanel bullet; §9.16 Editing page), `docs/TESTING.md` (test count and bullets), `CLAUDE.md` (status line: append "and the MIDI editor's Options ▸ Note Preview (insert / edit / velocity checkboxes, saved; a live preview path on reserved synth channels)")
- Test: `Tests/AppSettings_tests.cpp`, `Tests/ViewPreferencesPages_tests.cpp`

**Interfaces:**
- Consumes: `NotePreviewSettings` (Task 1), `TrackEditorPanel::setNotePreview` / `onNotePreviewChanged` (Task 5).
- Produces:
  - `AppSettings`: `NotePreviewSettings notePreview() const;` and `bool setNotePreview (const NotePreviewSettings&);` (writes all three keys, one save, and reports a failed save like the other setters).
  - `EditingPreferencesPage`: a "Note preview" heading label and three `juce::ToggleButton`s with the menu's labels, after the follow toggle. Accessors `previewInsertToggleForTesting()`, `previewEditToggleForTesting()`, `previewVelocityToggleForTesting()`. A click writes `setNotePreview` with all three toggle states, then `notify()`.
  - `SongsmithMainComponent`: `void setNotePreview (const NotePreviewSettings&);` (forwards to `trackEditor`) and `std::function<void (const NotePreviewSettings&)> onNotePreviewChanged;` (wired from `trackEditor->onNotePreviewChanged`, as for snap to grid).
  - `MainWindow`: `songsmith.onNotePreviewChanged = [this] (const NotePreviewSettings& s) { appSettings.setNotePreview (s); };`, and `applyViewSettings()` adds `songsmith.setNotePreview (appSettings.notePreview());`.

- [ ] **Step 1: Write the failing tests:**
  - `Tests/AppSettings_tests.cpp`, `"AppSettings: note preview defaults all off, round-trips each flag and persists across a re-opened file"`. Also `"AppSettings: a junk note preview value reads off"` (stored `"yes"` → false).
  - `Tests/ViewPreferencesPages_tests.cpp`, `"Editing page: the note preview toggles reflect the settings; a click writes them, applies and notifies"`: labels match the menu's exactly, default unticked; clicking the edit toggle → `settings.notePreview().onEdit`, the others still off, `applyCalls == 1`, `changedCalls == 1`; a reopened page shows it ticked.
- [ ] **Step 2: Run** `ctest --test-dir build -R "AppSettings|Editing page" --output-on-failure`. Expected: the build fails, which counts as RED.
- [ ] **Step 3: Implement** the code. The Editing page must show every control at the Preferences dialog's opening size (`PreferencesDialog` `setSize (640, 400)`). If the new rows do not fit in the page area, raise that initial height (keep `preferencesMinHeight`).
- [ ] **Step 4: Run** the whole suite: `cmake --build build && ctest --test-dir build --output-on-failure`. Expected: all PASS. Record the new test count for `docs/TESTING.md`.
- [ ] **Step 5: Update the docs** listed above, in the docs' existing voice. Add the spec and plan to the CLAUDE.md docs map, following the pattern of the other rows.
- [ ] **Step 6: Commit** `feat(ui): save Note Preview settings and show them on Preferences > Editing`, plus the docs.
