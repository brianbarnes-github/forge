# Songsmith — MIDI editor Options ▸ Note Preview (design)

Date: 2026-10-16

## Intent

While editing a track in the MIDI editor panel (`TrackEditorPanel`, #28), the
user wants to *hear* a note at the moment they place it or move it, so they can
find the right pitch by ear instead of by reading the keyboard gutter. "Preview"
means sounding the note(s) through the same SoundFont synth used for playback.

## Goals

- A new **Options ▸ Note Preview ▸** submenu in the MIDI editor's menu bar,
  after Snap to Key ▸, holding three checkbox items:
  - **Preview notes when inserting**
  - **Preview notes when editing**
  - **Preview notes when velocity changes**
- Each checkbox starts **unticked** and is **saved** in `AppSettings` (not in
  the `.songsmith` file), like Snap to Grid.
- The same three toggles also appear on **Preferences ▸ Editing**, sharing the
  same settings values as the menu (as Snap to Grid already does). *(Claude's
  call, approved with the summary.)*

## Behaviour

### What sounds

- Only the note(s) being inserted or edited sound. Other notes at that point
  in the song do not sound with them (user: "just that note on its own").
- **Inserting** (double-click on empty canvas, `SourceRollEditor::createNoteAt`):
  the new note sounds. A double-click refused by Snap to Key creates nothing and
  plays nothing.
- **Editing** means a change to a note's **pitch or position (start time)**:
  - During a body drag, each time the dragged selection lands on a **new
    pitch row**, it sounds at the new pitch straight away. Each new
    preview cuts off the previous one.
  - A drag that changed only the timing (start time) and no pitch plays
    **once on release**.
  - A drag whose pitch changed **at any point** ends with no extra sound on
    release beyond the row-change previews already heard, even if it came
    back to its original row and also moved in time. *(Claude's call: the
    last row-change preview already played the final pitch.)*
  - Resizing (dragging a note's left/right edge) does **not** preview: neither
    pitch nor position changes in the sense the user meant. *(Claude's call,
    approved with the summary.)* Note that a left-edge resize technically
    moves the start; it still does not preview.
  - A future **Ctrl+Up/Down/Left/Right** move of selected notes (not built yet,
    not part of this feature) is expected to preview under this checkbox.
    Build the preview hook so a later keyboard move can call it without
    rework.
- **Velocity changes**: no velocity editing exists yet (double-click inserts
  velocity 100; nothing edits it afterwards). The checkbox is fully functional
  as a setting (tickable, saved, mirrored in Preferences) but nothing triggers
  it until a later velocity-editing feature calls the same preview hook.

### Which notes sound when several are selected

The notes that sound are the notes that changed: one inserted note, or the
whole moved selection.

- One note: it sounds.
- Several notes: they sound together as a chord **only if all of them start at
  the same tick and there are six or fewer**. Otherwise (a melody, staggered
  starts, or more than six notes) nothing sounds (user: "if it's more than
  that, then we won't preview it because it just sounds stupid").
- The six-note limit matches LOTRO's chord cap.

### How it sounds

- It sounds **as you'd hear it in playback** at the note's start (approved
  with the summary). Concretely, **match playback exactly**, not the
  instrument band:
  - Instrument: the program and bank `buildSnapshot`
    (`Source/UI/Playback/PlaybackSnapshot.cpp`) would have on that note's own
    MIDI `channel` at its start tick. That means replaying that channel's
    Program Change and CC0/CC32 events up to the start tick, starting from
    program 0 at tick 0, with MIDI channel 10 as the drum kit (bank 128).
    Do **not** use `instrumentSegmentsOf` / `sourceProgram`
    (`ProgramChanges.*`). That is the band's display model and differs from
    playback when the first Program Change comes after tick 0, for merged
    multi-channel tracks, and for bank/drum tracks. A chord whose notes are
    on different channels sounds each note on its own channel's instrument.
  - Per-track mixing applies as in playback: the track's saved
    `playbackVolume` (velocity scaling via `scaleVelocity`), the Mixer pan
    override, and the channel's controller state at the start tick (CC7
    volume, CC11 expression, pitch bend, and the rest of what the engine's
    `chase` restores). A track turned down in the Mixer previews quietly.
    Only mute/solo is ignored (see below). *(Answered by the approved "as
    you'd hear it in playback"; reviewer question.)*
- Velocity: each note's own velocity (scaled as playback scales it).
- Length: the note's own duration, capped at about **1 second** so long notes
  don't drone. *(Claude's call, approved.)*
- A new preview stops the previous preview's notes (release tail is fine).
- Previews ignore mute/solo: they sound even on a muted track or while another
  track is soloed. They do pass through the master gain. *(Claude's call,
  approved.)*
- Previews sound while the song is playing, mixed on top of the playback.
  *(Claude's call, approved.)*
- With no SoundFont loaded, or no audio device, previews do nothing quietly:
  no error, no dialog. *(Claude's call.)*

## Non-goals

- No velocity editing, no Ctrl+arrow note moves (separate later features).
- No preview for resize, delete, quantize, Move/Copy/merge in the track list,
  MIDI import, undo/redo, or any edit outside the MIDI editor panel's
  `SourceRollEditor`. The LOTRO preview roll and the main-view track list
  don't preview.
- No sound for the active row moving (Navigate ▸ Move Up / Down a Semitone),
  clicking or selecting a note, or hovering over the keyboard gutter.
- Nothing saved in the `.songsmith` file; no undo step for toggling.

## Anti-goals

- Preview must never change the document. It is sound only: no undo
  transaction, no dirty flag.
- Preview must not glitch or block playback. The audio thread must not
  allocate, lock, free or throw (the existing `PlaybackEngine`/`EventSink`
  contract).
- A wall of sound: more than six notes, or non-chord selections, stay silent.

## Constraints

- No new dependencies. Use the existing TinySoundFont `SynthVoice` and the
  existing `AudioOutput`/`PlaybackEngine` path. Today the engine only plays
  from published `PlaybackSnapshot`s while the transport runs; there is
  **no live note path yet**. The builder adds one, for example a lock-free
  single-producer queue of preview note-on/note-off requests consumed by the
  engine in `renderBlock`, on a dedicated virtual channel (or channels)
  outside the snapshot's channels, so mute/solo, pan override and
  `releaseAll`/re-chase on seek don't touch it. The note-off after the capped
  length is timed in the engine (sample count), not with a message-thread
  timer. The exact mechanism is the builder's choice within the audio-thread
  contract.
- The preview channel must take the note's program/bank and chased
  controller state, resolved on the message thread and handed over in the
  preview request. The preview channel(s) must survive the engine's
  `releaseAll()` / re-chase, which runs on every snapshot swap and so on
  every drag step (each drag step edits the document).
- Core/UI boundary: everything lives in `Source/UI/` (see the
  `forge-engine-ui-boundary` skill); nothing goes in `Source/Core/`.
- Project compiles as C++17 (no defaulted comparisons).
- Strict TDD; prefer integration-style tests through `TrackEditorPanel` /
  `SourceRollEditor` with a recording sink, as existing playback tests do.

## Decided structure (from the conversation)

- Menu: Options ▸ Snap to Grid, Snap to Key ▸, **Note Preview ▸ {Preview notes
  when inserting, Preview notes when editing, Preview notes when velocity
  changes}**, all checkboxes, always enabled (also with no track loaded, so
  the user can set them up ahead of time). *(Enabled-state: Claude's call.)*
- Settings keys in `AppSettings`, editing section (e.g.
  `editing.previewOnInsert`, `editing.previewOnEdit`,
  `editing.previewOnVelocity`), default false. *(Key names: builder's call.)*
- `SourceRollEditor` reports "these notes were inserted / moved" through a
  callback (like `setPitchFilter`), and `TrackEditorPanel` decides whether a
  preview plays (checkbox state, chord rule) and asks the playback side to
  sound it. *(Claude's call: keeps the editor free of audio and settings.)*

## Left to the builder

- The live-note mechanism inside `PlaybackEngine`/`SynthVoice` and how
  `TrackEditorPanel` reaches it (through `PlaybackController` or a small
  seam interface it is handed, as the Mixer uses `MixerTarget`).
- Exactly how "new pitch row" is detected mid-drag (compare the grabbed
  note's pitch before and after each `mouseDrag` step is the obvious way).
- Where the chord rule function lives (a small pure helper in `Source/UI/`
  is the natural fit and easy to test).
- Doc updates: `docs/UI_GUIDE.md` (#28 Options paragraph and Preferences ▸
  Editing), `docs/ARCHITECTURE.md` (§9.14 playback, the TrackEditorPanel
  bullet, §9.16 Preferences), `docs/TESTING.md` count, `CLAUDE.md` status line.
