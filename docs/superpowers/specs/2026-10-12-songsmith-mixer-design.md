# Songsmith Mixer panel — design

Status: draft for review (2026-10-12). TODO item #2.

## Goal

A dockable **Mixer** panel: one compact channel strip per source-MIDI track
(pan knob, mute, solo, vertical volume fader, track number and name) plus a
pinned **Master** strip with a fader. It gives one place to balance every
track by ear. It is a second view of per-track playback state that the track
heads already expose, and an audition aid only: nothing in it reaches the MIDI
or ABC export.

The look follows a conventional DAW mixer (tall faders, M/S buttons at the top,
number and name at the bottom). No meters, FX, routing or record-arm.

## Constraint: the mixer and transport serve more than the MIDI source

Later, once LOTRO playback exists, the transport (play, stop, seek, rewind) and
the mixer must act on the **LOTRO parts** when the LOTRO grid is the active
side, and on the **source MIDI** when the MIDI section is active. The two sides
must be able to tell which is playing and coordinate. LOTRO playback is **not**
built here; this spec only builds the seam so it can be added without reworking
the panel.

- **`MixerTarget`** (pure interface, no JUCE component types): the channels it
  exposes (`id`, name, colour, volume 0-100, pan or "follows source",
  muted / soloed / silenced-by-solo), setters for volume, pan, mute and solo,
  the master level and its setter, and a change notification. `MixerPanel`
  depends on this interface only, never on `SongDocument` or
  `PlaybackController` directly. The transport gets the same treatment when the
  LOTRO side arrives; it is unchanged now.
- **`SourceMidiTarget`**: the only implementation now. It reads and writes
  `playbackVolume` / `playbackPan` on the `MIDI_TRACK` nodes through
  `SongDocument` (undoable) and uses `PlaybackController` for mute, solo and
  master.
- **`ActivePlaybackTarget`**: a tiny holder returning the active `MixerTarget`
  and notifying when it changes. Today it always returns the source-MIDI
  target. `MixerPanel` rebinds to its strips when it notifies.
- LOTRO parts will need their own state. `ASSIGNMENT.volumePercent` is the LOTRO
  export offset, not an audition level, and must not be reused.
- **Open, decided in the LOTRO-playback phase:** what makes a side active
  (focus, selection, an explicit toggle, or a combination). Only
  `ActivePlaybackTarget` is affected by that decision.

Out of scope here: any LOTRO engine, target-switching UI, transport changes.

## Data

- **Per-track pan:** `MIDI_TRACK.playbackPan` (`SongIDs::playbackPan`), int
  -100..+100. Absent means "follow the file's own pan". Saved in the Song like
  `playbackVolume`: removed rather than written when unset, never exported.
  `SongDocument::validateLoaded` rejects a non-integer or out-of-range value with
  `InvalidStructure` ("a track pan is out of range."). No format-version bump.
  Changes go through `doc.setProperty` / `removeProperty` with the
  `startsGesture` convention the volume slider uses (one undo step per drag).
- **Master level:** int 0-100, held by `PlaybackController`, **session only**
  (not in the Song, not undoable, always 100 at start). 100 is unity; it never
  boosts.
- Mute and solo stay session-only in `MuteSoloState`, unchanged.

## Playback engine (`Source/UI/Playback/`; `Source/Core/` untouched)

- **Master:** an atomic gain the engine applies to the rendered stereo buffer
  each block (after all `sink.render` calls).
- **Pan override:** an atomic per-track pan value in `PlaybackSnapshot`,
  alongside the per-track gain, initialised by `buildSnapshot` from
  `playbackPan`. While a track has an override the engine:
  - drops that track's file CC10 `Control` events (live and in `chase`), so a
    seek cannot undo the mixer pan;
  - applies the override to the track's virtual channels through a new
    `EventSink::setChannelPan (virtualChannel, pan)`, implemented in
    `SynthVoice` and in the test `RecordingSink`. It is applied in `chase` after
    `resetChannel`, and again when the value changes (like
    `releaseNewlyMutedTracks` tracks `appliedAudible`), so sounding notes pan
    live.
  - Clearing the override re-chases the current position so the file's last
    CC10 before the playhead takes effect again.
- `PlaybackController::valueTreePropertyChanged` handles `playbackPan` on a
  `MIDI_TRACK` like `playbackVolume` (`applyTrackPan`, no rebuild, which would
  cut held notes).

## UI

- **`MixerPanelComponent`** hosts the pinned **Master strip** (fader and label;
  no meter, mute or solo) on the left, then the track strips in a horizontal
  viewport that scrolls sideways on overflow. It binds to the
  `ActivePlaybackTarget`.
- **`MixerStripComponent`**, about 64 px wide, top to bottom: pan knob, mute and
  solo buttons, vertical fader (height follows the panel), then the track number
  and a short name in the track colour (tooltip: full name).
  - Pan knob: double-click clears the override ("follow file"; the strip shows a
    small "MIDI" marker and the tooltip explains it).
  - Fader: double-click resets to 100.
  - Mute/solo reuse the track heads' drawn icons (shared drawing helper, not a
    copy) and the same "silenced by another track's solo" dimming.
  - The conductor track has no strip.
- **Sync:** the target's change notification covers volume, pan, name, colour,
  track add/remove/reorder and replaced documents (`documentReplaced`). A fader
  move changes the track head's slider through its existing `refreshHead`; a
  head slider move changes the fader. Setting a control from a notification
  never re-fires its callback. The track heads get no pan control.

## Docking and menu

- Registry entry: id `mixer`, title "Mixer", `Zone::bottom`, docked, **hidden by
  default**, `defaultOrder` after Parts, `defaultWeight` about 0.6, `minSize`
  about 150. Saved layouts from before it existed gain it through
  `defaultOrder`.
- `MenuCommandId::ViewMixerToggle`, `MenuState::mixerVisible`;
  `allCommandIds` grows from 33 to 34 (and the `size() == 34` check in
  `MenuModel_tests.cpp`). The View menu entry is hand-written in
  `MainWindow` (the menu is not yet generated from `panelDefs()`).
- `SongsmithMainComponent::isMixerVisible` / `setMixerVisible`, as for Parts.

## Testing (TDD, integration-leaning)

- **Engine with `RecordingSink`:** master scales the output; a pan override
  calls `setChannelPan` on that track's channels only; file CC10 is dropped
  while an override is set (live and through a seek); clearing the override
  restores the file's pan; a pan change does not rebuild the snapshot.
- **`SourceMidiTarget`:** reads and writes the tree and controller state, and
  notifies on changes made elsewhere.
- **Mixer panel, real document and `PlaybackController`:** fader, pan, mute and
  solo changes propagate both ways with the track heads; strips follow track
  add, remove and reorder and `documentReplaced`; a notification does not echo
  back into the target.
- **File and layout:** `validateLoaded` rejects bad `playbackPan`; save/load round
  trip; `DockLayoutState` migration and JSON; `MenuModel` command and state;
  `SongsmithMainComponent` toggle events.
- Not headless-testable: real audio panning and fader dragging feel. The user
  verifies those on the deployed exe.

## Out of scope

Level meters, FX, routing, record-arm, pan on the track heads, a master mute or
solo, any effect on MIDI/ABC export, LOTRO-side playback, and generating the
View menu from `panelDefs()`.

## Docs to update when built

`docs/ARCHITECTURE.md` (§9.13 `playbackPan`, §9.14 pan/master, §9.19 mixer
panel), `docs/UI_GUIDE.md`, `docs/TESTING.md`, `docs/songsmith-ui-map.html`,
`CLAUDE.md` status line, `docs/TODO.md`.
