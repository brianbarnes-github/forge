# Songsmith Playback — design

Status: Draft, awaiting user review (2026-10-03). Project 3 of 3
(1 = MIDI fidelity, done; 2 = Song file, done).

## Intent

Let the user hear the Song inside Songsmith. Playback renders the **source
MIDI** (the imported tracks and the user's edits) through a SoundFont
synthesizer, so edits can be checked by ear without leaving the tool.

Success: pressing Play on the main screen or in the MIDI editor plays every
audible track from the playhead, in time with the Song's tempo map; mute/solo
on the main track headers decides what is audible everywhere; the playhead is
shown identically on the main-screen track canvas and the MIDI editor canvas;
edits, undo and redo during playback take effect without a restart and without
stuck notes.

## Decisions (all settled with the user)

- **Synth: TinySoundFont** (MIT, single header). Rejected: Windows GS
  Wavetable, FluidSynth, notes-only playback.
- **Source MIDI only.** The LOTRO preview canvas gets no playback and no
  playhead in this project. Its player comes later, probably separate, with its
  own SoundFont, driven by the ABC conversion. Whether it ever links to this
  transport is decided then.
- **One transport, one playhead**, shared by the main screen and the MIDI
  editor. The playhead is kept decoupled from any one canvas so the LOTRO
  canvas can join later.
- **Play starts from the playhead**, from either window.
- **Mute/solo live on the main track headers** and apply everywhere,
  including when the editor window is open. The editor has no controls of its
  own. Solo is additive, mute beats solo, and the state is session-only (not
  undoable, not saved in the `.songsmith` file).
- **Bundled SoundFont:** `TimGM6mb.sf2` (GPL-2, about 6 MB, md5
  `1f1ad87ae6f87033d9a591eca567d919`). `LotroInstruments_3b2ef040.sf2`
  (249 MB) is never bundled; it is user-supplied, in a later pass.
- **Engine approach A:** live rendering in the audio callback. Rejected: B
  (pre-render the whole song to PCM: laggy after every edit, tens of MB) and C
  (OS MIDI synth).
- **No device picker** (system default output), no metronome/count-in, no
  loop regions, no tempo scaling.

## Architecture

All new code lives in `Source/UI/Playback/`. `Source/Core/` is untouched, per
the engine/UI boundary (check the `forge-engine-ui-boundary` skill if that
changes).

- **`PlaybackSnapshot`**: immutable, flattened, tick-sorted event list.
  `buildSnapshot (const SongDocument&)` is a pure function run on the message
  thread. It holds note on/off with velocity, program changes, volume (CC7),
  expression (CC11), pan (CC10) and pitch bend, each tagged with `trackId` and
  channel, plus a tempo map converting ticks to samples from `TEMPO_MAP`.
  Every track is included; mute/solo is applied at play time so toggling is
  instant. SysEx and unmapped CCs are ignored; meter changes do not affect
  sound.
- **`Transport`**: playing flag and the playhead as an atomic tick position.
  Operations: play, pause, stop, rewind, go to start, go to end. Auto-stops at
  the last note-off and leaves the playhead at the end; Play from the end
  restarts from the beginning.
- **`MuteSoloState`**: per-track mute/solo flags keyed by `trackId`, exposed to
  the audio thread as an atomic bitmask. Cleared on New, Open and Close; a
  removed track's entry is dropped.
- **`SynthVoice`**: TinySoundFont wrapper and SoundFont loading. Each channel
  uses the preset the MIDI asks for; channel 10 plays as the bank's drum kit.
- **`PlaybackEngine`**: JUCE `AudioSource` glue. Each block it advances the
  playhead, fires due events whose track is audible, and renders through
  `SynthVoice`. It exposes a device-free `renderBlock (AudioBuffer<float>&)`.
  The audio thread takes snapshots by atomic pointer swap and never allocates,
  locks or throws; with no snapshot or SoundFont it outputs silence.
- **Device glue**: a JUCE `AudioDeviceManager` on the system default output.

### Data flow

A document change rebuilds the snapshot on the message thread and the audio
thread swaps it in at the next block boundary. Playback continues from the
current playhead. On a swap, all sounding voices are released
(`tsf_note_off_all`), so held notes are cut rather than stuck; only notes
starting at or after the playhead sound afterwards. Undo, redo, Import and Open
use the same path; New and Open also stop playback and reset the playhead,
mirroring `documentReplaced()`. A mute/solo change flips the bitmask only and
releases that track's sounding voices; it never rebuilds the snapshot. A
`juce::Timer` (about 30 Hz) on the message thread reads the playhead atomic and
repaints the canvases.

## UI

- **Transport strip** at the top of the main window and the same strip in the
  MIDI editor window, both bound to the one `Transport`: Go to start, Rewind,
  Play/Pause, Stop, Go to end. Space toggles Play/Pause in both windows (the
  editor registers it itself; it does not inherit the dead Ctrl+N/O/S
  shortcuts issue). "Rewind" means step back one bar — *unconfirmed with the
  user*, to be settled in the plan.
- **`PlayheadOverlay`**: a mouse-transparent component drawing a vertical line
  over the main-screen track canvas and the editor's piano roll. Each canvas
  supplies its own tick-to-x mapping (`TimelineViewState` on the main screen,
  the editor's own geometry in the editor), since zoom/scroll are independent.
  It repaints only when the line moves. Not added to the LOTRO canvas.
- **Seeking**: click or drag in a thin ruler strip along the top of each
  canvas moves the playhead. Plain ticks, no bar numbers. Whether a ruler
  already exists in each canvas is to be verified in the plan.
- **Follow**: while playing, each canvas page-flips independently to keep the
  playhead in view.
- **Mute/solo buttons**: small M and S toggles in `TrackRowComponent`'s left
  info column (`trackInfoWidth`, 180 px) after the name; none on the conductor
  row. A muted row dims; a soloed row is highlighted; rows silenced by another
  track's solo dim.
- **SoundFont…** menu item: a file chooser; the path is stored in app settings,
  not in the Song file. Default is the bundled `TimGM6mb.sf2` next to the
  executable.

## Errors

A custom `PlaybackError` type with kinds `SoundFontMissing`,
`SoundFontInvalid` and `AudioDeviceUnavailable`.

- SoundFont error: message box, fall back to the bundled file; if that is bad
  too, Play is disabled with a visible reason.
- Device error: message on Play; the transport stays stopped.

## Testing

TDD, integration tests preferred. Tests drive `renderBlock` directly, so no
sound card is needed (Wine- and CI-safe). A real `SongDocument` from existing
fixtures is used to check:

- **Timing:** a note at tick N sounds at the sample offset the tempo map
  predicts, including a mid-song tempo change.
- **Playhead:** advances by the expected amount; auto-stops at the song end.
- **Mute/solo:** muted track renders silence; solo additive; mute beats solo.
- **Edits mid-play:** a document edit swaps the snapshot with no stuck notes
  and no playhead reset.

Unit-level: `buildSnapshot` event ordering, tick-to-sample conversion,
`MuteSoloState`. Not automated (like `MainWindow` today): the real audio
device, the transport strip and the space-key wiring; these are build-verified
plus the user's manual test on Windows. The GUI is never launched from agents.

## New dependencies and licensing

- **TinySoundFont**: MIT, new. Vendored as the single header under
  `ThirdParty/` with its version noted (no submodule).
- **`juce_audio_devices`**: new JUCE module, linked to `forge_ui` only. (The
  `forge_ui` CMake target is a historical name; it builds the `song-smith`
  binary.)
- **`TimGM6mb.sf2`**: GPL-2 data file, bundled. `CLAUDE.md`'s licensing
  guardrails are updated. Windows CI packaging and the deploy copy to
  `/mnt/c/Apps/SongSmith/` must ship it alongside `song-smith.exe`.

## Out of scope

- LOTRO canvas playback and playhead (later project).
- Device picker, metronome/count-in, loop regions, tempo scaling.
- Saving the playhead in the Song file.
- The open Song-file minors, the emptied-track reopen bug, and editor-created
  channel-10 notes getting `isDrum=false` (`SourceRollEditor.cpp` ~363-367).
  Playback keys drums off channel 10, so they sound correct; the flag stays
  inconsistent until a separate fix.

## Open items for the plan

- Confirm "Rewind" = step back one bar.
- Verify which canvases already have a ruler strip; scope the seek ruler
  accordingly.
- Choose the exact TinySoundFont version to vendor and record it.
- Confirm where app settings (SoundFont path) are stored today.
