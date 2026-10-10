# Songsmith Playback — design

Status: Implemented (2026-10-03). Project 3 of 3.
(1 = MIDI fidelity, done; 2 = Song file, done.) Code walkthrough:
`docs/ARCHITECTURE.md` §9.14.

## Intent

Let the user hear the Song inside Songsmith. Playback renders the **source
MIDI** (the imported tracks and the user's edits) through a SoundFont
synthesizer, so edits can be checked by ear without leaving the tool.

Success: pressing Play on the main screen or in the MIDI editor plays every
audible track from the playhead, in time with the Song's tempo map; mute/solo
on the main track headers decides what is audible everywhere; the playhead is
shown identically on the main-screen track canvas and the MIDI editor canvas;
edits, undo and redo during playback take effect without a restart and without
stuck notes; starting mid-song sounds the same as having played up to that
point.

## Decisions (all settled with the user)

- **Synth: TinySoundFont** (MIT, single header). Rejected: Windows GS
  Wavetable, FluidSynth, notes-only playback.
- **Source MIDI only.** The LOTRO preview canvas gets no playback and no
  playhead in this project. Its player comes later, probably separate, with its
  own SoundFont, driven by the ABC conversion. Whether it ever links to this
  transport is decided then.
- **One transport, one playhead**, shared by the main screen and the MIDI
  editor. The playhead is decoupled from any one canvas so the LOTRO canvas
  can join later.
- **Play starts from the playhead**, from either window (one exception: Play
  at the end of the song restarts from the beginning).
- **Mute/solo live on the main track headers** and apply everywhere,
  including when the editor window is open. The editor has no controls of its
  own. Solo is additive, mute beats solo, and the state is session-only (not
  undoable, not saved in the `.songsmith` file).
- **The SoundFont is NOT committed to the repo and not shipped by CI.**
  `TimGM6mb.sf2` (GPL-2, about 6 MB, md5 `1f1ad87ae6f87033d9a591eca567d919`;
  the user's copy is in `C:\Apps\NewPlayer\resources`) is kept as a local,
  git-ignored file and copied into `resources/` beside the executable by a CMake post-build
  step when present. `LotroInstruments_3b2ef040.sf2` (249 MB) is likewise
  never committed; it is user-supplied via "SoundFont…", in a later pass.
- **Engine approach A:** live rendering in the audio callback. Rejected: B
  (pre-render the whole song to PCM: laggy after every edit, tens of MB) and C
  (OS MIDI synth).
- **No device picker** (system default output), no metronome/count-in, no
  loop regions, no tempo scaling.

## Architecture

All new code lives in `Source/UI/Playback/`. `Source/Core/` is untouched, per
the engine/UI boundary.

- **`PlaybackSnapshot`**: immutable, flattened event list, built by the pure
  function `buildSnapshot (const SongDocument&)` on the message thread.
  - Event times are **seconds** (double), computed from `TEMPO_MAP`. The
    snapshot also keeps the tempo map for tick↔seconds conversion in the UI.
    Seconds are independent of PPQ and of the device sample rate, so a later
    import that raises the document PPQ (`SongModelBridge.cpp:136-154`) does
    not move the playhead.
  - Tempo: 120 BPM before the first `TEMPO_CHANGE` and when `TEMPO_MAP` is
    empty. `SONG.tempoBpm` is the ABC override and is ignored by playback.
  - Content: note on/off with velocity, program changes, **every** control
    change (forwarded as-is, so CC64 sustain and bank select CC0/CC32 work),
    and pitch bend. SysEx is ignored; meter changes do not affect sound. The
    conductor track's EVENTs are ignored for sound (tempo comes from
    `TEMPO_MAP`).
  - **Virtual channels:** each distinct (trackId, channel) pair gets its own
    TinySoundFont channel, assigned at build time. `NOTE.channel` is per note,
    so a track may span several channels, and two tracks on one MIDI channel
    with different programs no longer collide. A (track, channel 10) pair is
    set to bank 128 via `tsf_channel_set_bank_preset`.
  - Every track is included; mute/solo is applied at play time. The snapshot
    owns one `std::atomic<uint8_t>` audible flag per track (the only mutable
    part), indexed by a position baked into each event.
- **`Transport`**: playing flag and the playhead as an atomic **seconds**
  position (double). Operations: play, pause, stop, rewind, go to start, go to
  end. The UI derives ticks from seconds via the snapshot's tempo map; seeking
  converts ticks to seconds the same way.
  - Pause leaves the playhead in place.
  - Stop halts playback, lets release tails ring out (the engine keeps
    rendering until `tsf_active_voice_count` is 0), and returns the playhead
    to where Play was last started. Go to start is always 0.
  - Auto-stop at the last note-off leaves the playhead at the end; Play from
    there restarts at the beginning.
  - Play on a song with no notes is a no-op.
- **`MuteSoloState`**: message-thread owner of the per-trackId mute/solo flags.
  It recomputes audibility (mute beats solo, solo additive) and writes the
  current snapshot's audible flags with release semantics, and again on every
  snapshot rebuild. Entries are kept across `removeTrack` so undo restores
  them, and cleared only on New, Open and Close.
- **`SynthVoice`**: TinySoundFont wrapper and SoundFont loading. Exactly one
  translation unit defines `TSF_IMPLEMENTATION` (`SynthVoice.cpp`). A pinned
  tsf version with `tsf_set_max_voices()` is vendored; the voice limit is set
  and every virtual channel the snapshot uses is initialised (for example via
  `tsf_channel_set_presetnumber`) on the message thread before publishing, so
  `tsf_note_on` and first channel use never allocate on the audio thread.
- **`PlaybackEngine`**: `juce::AudioSource` (juce_audio_basics only). Each
  block it advances the playhead in seconds, converts due events to sample
  offsets using the rate from `prepareToPlay`, and dispatches
  (sampleOffset, event, virtualChannel) to an injectable **`EventSink`**; the
  production sink drives `SynthVoice`, and tests use a recording fake. It
  exposes a device-free `renderBlock (AudioBuffer<float>&)`. It never
  allocates, locks or throws; with no snapshot or SoundFont it outputs silence.
- **`AudioOutput`**: the `AudioDeviceManager` + `AudioSourcePlayer` glue on
  the system default output, in its own file compiled only into `forge_ui`
  (`forge_tests` compiles `Source/UI/*.cpp` directly and does not link
  `juce_audio_devices`).

### Chase (starting mid-song)

On Play, seek and every snapshot swap, before firing note-ons the engine first
resets every virtual channel the snapshot uses to the synth's initial state
(volume, expression, pan, pitch wheel, sustain, pitch range, tuning; the
`EventSink::resetChannel` step, so nothing left over from earlier playback
survives a Stop, seek or restart), then scans the snapshot up to the playhead
and applies, per virtual channel, the last program / bank / CC / pitch bend.
Starting at bar 20 therefore sounds the same as having played to bar 20.
Notes already sounding at the playhead are not retriggered.

Events at the same tick fire in the order Control, Program, PitchBend,
NoteOff, NoteOn (the `PlaybackEventKind` enumerator order), so a CC0/CC32
bank select lands before the program change that selects within that bank.

### Snapshot and synth hand-off

The message thread owns every object. It publishes a new snapshot (and, for
the SoundFont… menu, a new fully-initialised `tsf` instance) into a
single-slot atomic hand-off. At the start of a block the audio thread
exchanges the slot, makes the new object current, and pushes the old one to a
lock-free single-producer retire queue. The message thread frees retired
objects from the ~30 Hz timer. The audio thread never frees anything.

### Data flow

Document changes mark playback dirty; one `AsyncUpdater` rebuilds a single
snapshot per message-loop pass (import fires a notification per note and a
drag fires per-pixel changes, so rebuilds are coalesced). The audio thread
swaps it in at the next block boundary and playback continues from the current
playhead. On a swap, all sounding voices are released and the chase
re-applies, so edits never leave stuck notes. Known v1 trade-off: a held note
(a pad, a bass) is cut and not retriggered when any edit lands during playback;
diffing sounding notes to release only vanished ones is a possible follow-up.
Undo, redo, Import and Open use the same path; New and Open also stop playback
and reset the playhead, mirroring `documentReplaced()`. A mute/solo change
updates the audible flags only and releases that track's sounding voices; it
never rebuilds the snapshot. Known GM-synth behaviour: a note-off for a key
releases every voice of that key on the channel, so overlapping same-pitch
notes on one channel end early.

## UI

- **Transport strip** at the top of the main window, plus the same strip in
  the MIDI editor window, both bound to the one `Transport`: Go to start,
  Rewind, Play/Pause, Stop, Go to end. `TrackEditorWindow` currently hosts the
  roll with `setContentNonOwned (&roll)`, so the strip and ruler need a new
  container component. Space toggles Play/Pause in both windows; transport
  buttons use `setWantsKeyboardFocus (false)` so Space is never treated as a
  button click. "Rewind" (labelled `<<`) steps back one bar (see "Open items
  (resolved)").
- **`PlayheadOverlay`**: a mouse-transparent component drawing the playhead
  line. The "main-screen track canvas" is not one component: it is a
  `Viewport` of `TrackRowComponent`s, each with its own preview starting at
  `trackInfoWidth`. The overlay sits over that viewport, clips to it and
  offsets by `notePreviewOriginX()`. In the editor, `PianoRollComponent`'s
  canvas sits in a `Viewport` with a pinned gutter: the overlay subtracts the
  viewport's horizontal scroll and never draws over the gutter. Each canvas
  supplies its own tick→x mapping (`TimelineViewState` on the main screen, the
  editor's geometry in the editor). It repaints only when the line moves, from
  a ~30 Hz `juce::Timer`. Not added to the LOTRO canvas.
- **Seeking**: no ruler exists in `TrackListComponent`, `TrackNotePreview` or
  `PianoRollComponent`, so a thin ruler strip along the top of each of the two
  canvases is **new UI**. Plain ticks, no bar numbers. Click or drag sets the
  playhead.
- **Follow**: while playing, each canvas page-flips independently to keep the
  playhead in view. On the main screen this goes through
  `TimelineViewState::setScrollOffsetTicks` plus the horizontal-bar sync, and
  must not fight `fitTimelineToDocument`'s `timelineFitted` refit on resize.
- **Mute/solo buttons**: small M and S toggles in `TrackRowComponent`'s left
  info column (`trackInfoWidth`, 180 px at the time; now 200 px with drawn icons, see the track-head spec) after the name; none on the conductor
  row. A muted row dims; a soloed row is highlighted; rows silenced by another
  track's solo dim.
- **SoundFont…** menu item: a file chooser; the chosen path is stored in
  `MainWindow::settings` (the `SongSmith.settings` `PropertiesFile`), not in
  the Song file. Default: `resources/TimGM6mb.sf2` beside the executable
  (`juce::File::getSpecialLocation (currentExecutableFile)`), which also covers
  `./run-ui.sh` and `build/forge_ui_artefacts/Debug/` on Linux.

## Errors

A custom `PlaybackError` type with kinds `SoundFontMissing`,
`SoundFontInvalid` and `AudioDeviceUnavailable`.

As shipped (this amends the original wording, which had Play disabled with a
visible message): Play stays enabled, and the dialogs are raised lazily, on
Play, so the app can open and edit Songs on a machine with no audio device or
no SoundFont.

- No SoundFont found (the normal state on CI and on a fresh clone): the app
  starts normally. Pressing Play shows a "No SoundFont" dialog (raised by
  `PlaybackController::onBeforePlay`, i.e. `MainWindow::ensurePlaybackReady`)
  explaining how to choose one with **Song → SoundFont…**; Play does nothing
  else.
- A SoundFont chosen via **Song → SoundFont…** that is missing or fails to
  load: a dialog is shown and the previously active SoundFont stays in use.
  At startup an unusable configured or bundled SoundFont is skipped
  silently (the stored path is tried, then the bundled file; the app starts
  either way and falls through to the "No SoundFont" dialog on Play).
- Device error: an "Audio unavailable" dialog on Play; the transport stays
  stopped.

## Testing

TDD, integration tests preferred. Tests drive `renderBlock` with a recording
`EventSink`, so no sound card or SoundFont is needed (Wine- and CI-safe). A
real `SongDocument` from existing fixtures checks:

- **Timing:** each event reaches the sink at the sample offset the tempo map
  predicts. `midi/anymore.mid` (3 set-tempo events) or `midi/syn5.mid` (440)
  cover the mid-song tempo-change case; empty `TEMPO_MAP` gives 120 BPM.
- **Chase:** play from tick N after a program change at tick 0 uses that
  preset; likewise CC7/CC10/pitch bend.
- **Playhead:** advances by the expected amount; auto-stops at the song end;
  Stop returns to the play-start position; PPQ raise leaves the playhead time
  unchanged.
- **Mute/solo:** muted track produces no note events; solo additive; mute beats
  solo; state survives remove + undo.
- **Virtual channels:** two tracks on one channel with different programs stay
  independent; a track spanning two channels works; (track, ch 10) uses the
  drum bank.
- **Edits mid-play:** a document edit swaps the snapshot with all voices
  released, chase re-applied and no playhead reset; coalescing yields one
  rebuild for an import.

Unit-level: `buildSnapshot` event ordering, seconds conversion,
`MuteSoloState`. One smoke test through real TinySoundFont checks non-silence;
it returns early with a warning (reported as passed) when no SoundFont file is present (always the case on CI). Not
automated (like `MainWindow` today): the real audio device, the transport
strip and the space-key wiring; these are build-verified plus the user's manual
test on Windows. The GUI is never launched from agents.

## New dependencies and licensing

- **TinySoundFont**: MIT, new. Vendored as the single header under
  `Source/ThirdParty/` (where `tomlplusplus` already lives) with its pinned
  version noted; no submodule.
- **`juce_audio_devices`**: new JUCE module, linked to `forge_ui` only. (The
  `forge_ui` CMake target is a historical name; it builds the `song-smith`
  binary.)
- **`TimGM6mb.sf2`**: GPL-2 data, **not in the repo and not in CI artefacts**.
  Kept at a git-ignored local path; a CMake post-build step copies it next to
  each `forge_ui` artefact (and the test binary) when present. Local deploy:
  copy it to `/mnt/c/Apps/SongSmith/` under `resources/` beside `song-smith.exe` (the user's
  copy is in `C:\Apps\NewPlayer\resources`). `CLAUDE.md`'s licensing guardrails
  and the deploy line are updated; since it is not distributed, no GPL
  redistribution obligation arises from this repo.

## Out of scope

- LOTRO canvas playback and playhead (later project).
- Device picker, metronome/count-in, loop regions, tempo scaling.
- Saving the playhead in the Song file.
- Diffing sounding notes across a snapshot swap (v1 cuts held notes).
- The open Song-file minors, the emptied-track reopen bug, and editor-created
  channel-10 notes getting `isDrum=false` (`SourceRollEditor.cpp` ~363-367).
  Playback keys drums off channel 10, so they sound correct; the flag stays
  inconsistent until a separate fix.

## Open items for the plan (resolved)

- Rewind = step back one bar (`previousBarTick`): implemented as proposed.
- Stop returns the playhead to the play-start position: implemented as
  proposed.
- TinySoundFont pinned at commit `853a0a1` (has `tsf_set_max_voices`).
- Local SoundFont path: `resources/soundfonts/TimGM6mb.sf2` (untracked).
- "SoundFont…" lives in the **Song** menu.
- The audio device opens lazily on the first Play, so a machine with no
  audio device can still open and edit Songs.
