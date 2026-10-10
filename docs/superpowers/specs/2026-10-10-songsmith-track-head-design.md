# Songsmith: track head redesign — design

Date: 2026-10-10. Status: implemented. The amendments below (marked *Amended*) record where
the implementation differs from the original proposal.

## Goal

Redesign the track head (the 180 px info column at the left of every
`TrackRowComponent`) as a two-row, directly editable control:

- the user can click the colour swatch to pick a new track colour;
- the user can rename a track from a right-click menu on the head;
- mute and solo are drawn icons instead of "M" / "S" text buttons;
- a per-track playback volume slider sits beside them, and is saved in the
  `.songsmith` file;
- the note-count / range line is removed;
- the head is 200 px wide (was 180).

## Out of scope

- **Channel display / change.** Deferred to a separate feature: the options
  (default channel for new notes, re-channelling a whole track, drum handling)
  do not fit a single click, and will get a dedicated Channel Info UI element.
- Any change to the instrument band on the canvas side of the row.
- Volume is **playback only**. It never reaches the MIDI or ABC export.

## Context and constraints

- Guiding principle (`CLAUDE.md`): the MIDI is the source of truth. The volume
  is a mixing aid for audition; it must not alter exported data.
- Core/UI boundary: all of this is editing/display logic and lives in
  `Source/UI/`. `Source/Core/` is untouched.
- Tree rules (`juce-valuetree-conventions`): one undo transaction per user
  gesture, no write when the value is unchanged, no tree mutation mid-drag
  except through a single coalesced transaction.
- Today the head is painted inside `TrackRowComponent::paint`
  (`TrackRowComponent.cpp:199`) with two `juce::TextButton`s in a 40 px
  `muteSoloWidth` column. Mute/solo are session-only (`MuteSoloState`) and are
  not touched by this design.

## Decisions taken with the user

| Question | Decision |
|---|---|
| Scope of the redesign | Icons, colour picker, rename, no note line, 200 px wide, two rows, volume |
| Width | Whole head 200 px (`trackInfoWidth` 180 → 200) |
| Rename trigger | Right-click context menu on the head (room for future items); not double-click, which still opens the track editor |
| Volume persistence | Saved in the `.songsmith` file |
| Volume control | Horizontal slider, 0–100 %, default 100 %, double-click resets, one undo step per drag |
| Icon source | Vector `juce::Path` drawn in code (no asset files) |
| Channel | Deferred, see Out of scope |

## Layout

The head is split into two equal rows by the row height (rows are 30–120 px
tall, so at the minimum each row is about 15 px).

```
row 1:  [index 16] [swatch 14] [name ………………………]
row 2:  [M icon] [S icon] [volume slider ▬▬●▬▬]
```

- The conductor row keeps its muted look and shows only the name: no swatch
  picker, no rename, no M/S, no volume.
- A muted or solo-silenced row is dimmed exactly as today.
- Row-1 text and swatch derive from the actual bounds, as `minRowHeight`'s
  comment requires. `minRowHeight` is re-verified against the new content
  (icons and slider must neither clip nor overlap at 30 px).

## Components

### `TrackHeadComponent` (new, `Source/UI/`)

Owns the swatch, name, mute/solo icon buttons, volume slider and the head's
context menu. `TrackRowComponent` creates one, places it in the left
`trackInfoWidth` px, and forwards its callbacks. Row painting loses the info
text and swatch code.

Public surface (callbacks mirror the row's existing style):

- `onMuteToggled`, `onSoloToggled` — moved from the row, same signature
  `(juce::int64 trackId, bool)`; the row re-exposes them so
  `TrackListComponent` is unchanged.
- `setMuteSolo (muted, soloed, silencedBySolo)` — same semantics as today.
- `muteButtonForTesting()`, `soloButtonForTesting()` — kept on the row and
  delegated, so existing tests survive (return type changes from
  `juce::TextButton&` to the icon button type; tests adjusted).
- *Amended (callbacks, not a `SongDocument` handle).* The head takes only the
  track node and display index, so `TrackRowComponent`'s constructor is
  unchanged. Edits (colour, name, volume) are reported through callbacks
  (`onColourChanged`, `onRenamed`, `onVolumeChanged`) that `TrackListComponent`,
  which owns the `SongDocument`, turns into undoable writes, the same pattern
  as `onSetInstrumentRequested`. A commit that leaves the value unchanged
  writes nothing; setting the volume to 100 removes the property.

### Icon buttons

Two small `juce::Button` subclasses (or one parameterised) that draw a
`juce::Path` — speaker-with-slash for mute, a headphone / "S" mark for solo —
filled with the existing on/off colours (orange-red, gold, muted grey when
off). Toggle behaviour, tooltips ("Mute", "Solo"), and click callbacks are
unchanged.

### Colour swatch

A click inside the swatch opens a `juce::ColourSelector` in a
`juce::CallOutBox` (JUCE built-in, no new dependency). Changes write
`colorArgb` live for preview within one undo transaction that closes when the
callout is dismissed; dismissing without a change records nothing.

### Rename

Right-click on the head (but not the instrument band, which keeps its own
menu) shows a `juce::PopupMenu` with **Rename…**. The menu builder returns the
item list from one place so later features add items there. Rename shows an
in-place `juce::TextEditor` over the name: Enter commits, Escape or focus loss
cancels, and an empty or all-whitespace name is rejected (old name restored).
A commit is one undo step and is a no-op if the name is unchanged.

### Volume slider

A horizontal `juce::Slider`, range 0–100, step 1, default 100. Double-click
resets to 100. The value shows as `NN%` in a tooltip / popup while dragging.
`dragStarted` begins one coalesced undo transaction and `dragEnded` closes it,
so a drag is a single undo step. Keyboard / wheel changes are one step each.

## Data and file format

New `MIDI_TRACK` property, `SongIDs::playbackVolume` (int, 0–100). The name is
deliberately not `volumePercent`, which already means the LOTRO volume offset
on `ASSIGNMENT` nodes.

- Absent property reads as 100, so existing `.songsmith` files load unchanged
  and no version bump is needed for reading. New tracks do not write the
  property until the user moves the slider (100 is the implicit default).
- `SongDocument::validateLoaded` rejects a value outside 0–100 or of a
  non-integer type, like the existing SECTION checks, with a `SongFileError`.
- `SongFile` round-trips the property; saving writes it only when not 100.

## Playback

- `PlaybackSnapshot` gains a per-track gain beside the `audible` flags: an
  atomic per track (0–100), initialised from `playbackVolume` at snapshot
  build time and settable from the message thread without a rebuild
  (`setGain (trackIndex, percent)` / `gain (trackIndex)`), exactly like
  `setAudible`.
- `PlaybackController` listens for `playbackVolume` property changes on a
  `MIDI_TRACK` and applies them to the live snapshot (as `applyMuteSolo`
  does for mute/solo). The slider therefore changes loudness mid-playback
  without restarting or rebuilding. `isPlaybackRelevant` must not trigger a
  full rebuild for this property.
- *Amended (no CC7 / CC11 scaling).* The engine scales each `NoteOn`
  velocity by the track's gain (`scaleVelocity`) and nothing else. Controllers
  cannot undo a velocity multiplier, which was the original reason for scaling
  CC7 / CC11 too, and scaling CCs would go stale mid-playback. The gain applies
  to notes that start after the change; sounding notes finish at their old
  level. Velocity is clamped to at least 1 for a non-zero source velocity at
  non-zero gain; gain 0 drops the `NoteOn` (the matching `NoteOff` still
  passes, which is harmless).
- Open item: velocity scaling can change timbre in velocity-layered SoundFont
  presets. If listening shows an audible difference, switch to a post-synth
  per-channel mix gain; the snapshot and UI surface are identical either way.
- Mute/solo behaviour is unchanged and takes precedence: an inaudible track
  stays silent whatever its gain.

## Testing (TDD, integration-first)

- Save/load round trip of `playbackVolume`, including absent → 100, and
  `validateLoaded` rejecting out-of-range and wrong-type values.
- Snapshot / engine integration with the recording sink: a track at 50 %
  delivers halved velocities and halved CC7/CC11; 0 % delivers no NoteOn;
  changing the gain mid-playback takes effect without a rebuild; mute still
  beats gain.
- Head gestures on a real document: the colour change, rename and a volume
  drag each produce exactly one undo step, undo restores the old value, and
  an unchanged commit writes nothing.
- Rename edge cases: Escape cancels, empty name rejected, conductor has no
  rename item.
- Layout: at `minRowHeight` and `maxRowHeight` the icons, slider and swatch
  neither clip nor overlap; the head is `trackInfoWidth` (200) wide and the
  note preview starts at 200.
- Existing mute/solo tests adapted to the icon buttons and still green; full
  suite green.

## Docs to update

`docs/UI_GUIDE.md` (head region, context menu, field → property table),
`docs/songsmith-ui-map.html` (the `#N` map), `docs/ARCHITECTURE.md` §9.13
(`playbackVolume` in the file format and validation), §9.14 (snapshot gain,
live update) and the track-list section, and the test count in `CLAUDE.md`
and `docs/TESTING.md`.

## Risks

- *Amended (smaller than first stated).* The info column spans the row plus
  the 16 px instrument band, so at `minRowHeight` 30 each head row is about
  22 px and the icons and slider fit; `minRowHeight` stays 30.
- The velocity-versus-mix-gain choice above.
- The `muteButtonForTesting()` type change touches existing tests.

## Known limitations

- An open rename editor does not update when the name changes underneath it
  (for example on undo); it keeps the text being typed until it commits or
  cancels.
- An open `TrackEditorWindow` keeps its old title after a rename (pre-existing:
  the title is set when the window is pointed at the track).
- The head's tooltips ("Mute", "Solo", "Playback volume") are inert, like all
  tooltips in the app, because no `juce::TooltipWindow` is created.
