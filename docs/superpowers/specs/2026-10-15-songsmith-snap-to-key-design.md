# Songsmith: Snap to Key (MIDI editor Options menu) — design

Date: 2026-10-15. Status: approved by the user.

## Intent

While writing notes in the MIDI editor panel, the user wants a guard that only
lets them add notes that belong to a chosen scale or chord. A double-click on an
off-key pitch simply does nothing — the editor never picks a nearby pitch for
them (guiding principle: make as few decisions for the user as possible).

Identifying or fixing off-key notes that already exist is explicitly **later
work**; this feature does not touch existing notes.

## Goals

- Options ▸ Snap to Key submenu in the MIDI editor panel's own menu bar
  (`EditorMenuModel.h`), below Snap to Grid.
- When enabled, note creation (double-click on empty canvas,
  `SourceRollEditor::mouseDown` → `createNoteAt`) is refused for any pitch whose
  pitch class is not in the chosen set.
- Root/Type start from the song key (`SongDocument::getSongKey()`), then keep
  whatever the user picks.

## Non-goals

- No restriction on moving notes (drag), semitone nudges (Navigate ▸ Move Up /
  Down a Semitone acts on the active row only), Alt-drag merge, cross-track
  Move/Copy, resize, quantize, or MIDI import. Existing off-key notes are left
  exactly as they are, never highlighted or altered.
- Nothing saved in the `.songsmith` file and nothing in AppSettings/Preferences.
- No change to the song key itself; picking a Root/Type here never calls
  `setSongKey`.

## Anti-goals

- Snapping a refused click to the nearest allowed pitch (user: "doesn't decide
  for you where it goes").
- Any `Source/Core/` change — this is editing logic, `Source/UI/` only.

## Menu (user-specified structure)

```
Options
  Snap to Grid                 (existing, unchanged)
  Snap to Key ▸
      Enabled                  (checkbox)
      Root ▸                   (note names, current ticked)
      Type ▸
          Major
          Natural minor
          Melodic minor
          Harmonic minor
          Major pentatonic
          Minor pentatonic
          Major blues
          Minor blues
          Chords ▸
              Major triad
              Minor triad
              Major seventh
              Minor seventh
              Dominant seventh
              Diminished triad
              Diminished seventh
              Augmented fifth triad
```

The current Type is ticked (a chord type ticks inside Chords ▸).

### Root list

User example: "C-flat, B, B-flat, A-sharp, A, … A-flat, yada yada" — sharp and
flat spellings, listed high to low.

*Builder's call (mine):* the 18 names that are real major or minor key
signatures, high to low:
C♭, B, B♭, A♯, A, A♭, G♯, G, G♭, F♯, F, E, E♭, D♯, D, D♭, C♯, C
(UTF-8 ♯/♭, same glyphs as `KeyCandidate::name()`). Enharmonic names (A♯/B♭)
allow identical pitches; the spelling only affects which item is ticked.

### Pitch-class sets (semitones above root)

| Type | Intervals |
|---|---|
| Major | 0 2 4 5 7 9 11 |
| Natural minor | 0 2 3 5 7 8 10 |
| Melodic minor (ascending form — my call) | 0 2 3 5 7 9 11 |
| Harmonic minor | 0 2 3 5 7 8 11 |
| Major pentatonic | 0 2 4 7 9 |
| Minor pentatonic | 0 3 5 7 10 |
| Major blues | 0 2 3 4 7 9 |
| Minor blues | 0 3 5 6 7 10 |
| Major triad | 0 4 7 |
| Minor triad | 0 3 7 |
| Major seventh | 0 4 7 11 |
| Minor seventh | 0 3 7 10 |
| Dominant seventh | 0 4 7 10 |
| Diminished triad | 0 3 6 |
| Diminished seventh | 0 3 6 9 |
| Augmented fifth triad | 0 4 8 |

Allowed iff `(pitch - rootPitchClass) mod 12` is in the set, any octave.

## Behaviour

- **Starting choice (user):** while the user has not picked anything, Root and
  Type follow the song key: root = the key's tonic spelled as `KeyCandidate`
  spells it (always one of the 18 names), Type = Major or Natural minor. If the
  song key changes (Set Key, Detect Key, import, undo) before any pick, the
  submenu follows it.
- **After a pick (user):** once the user picks a Root or a Type, the pair is
  frozen at that choice; later song-key changes don't move it. *My call:* a pick
  of either one freezes both.
- **Lifetime (user: "just for editing", not saved):** session-only state in
  `TrackEditorPanel`. *My calls:* Enabled starts off at launch; loading another
  song (New/Open, the panel's `clearTrack`) returns Root/Type to following the
  new song's key (Enabled kept as-is); switching the edited track keeps it all.
- **No key (user: "disable Snap to Key when there's no key set"):** when the song
  has no key and the user has not picked a Root, Enabled and Type ▸ are greyed
  and nothing is blocked. Root ▸ stays enabled. *My call:* picking a Root then
  uses Type = Major until a Type is picked.
- **Refused click:** returns no change — no note, no selection change, no undo
  step, no sound, no message.
- Enabled is independent of having a track loaded (like Snap to Grid).

## Where it lives (builder's latitude within these lines)

- Pure, JUCE-free data/logic in `Source/UI/` (e.g. `ScaleSnap.{h,cpp}`): the
  root names, the type enum and interval table, `isPitchAllowed(...)`, mapping
  from a song key index (`keyAt`) to a starting root/type. Free functions,
  lowercase enumerators (house style).
- `EditorMenuModel.h`: new ids (root/type ranges like `SongKeyBase`) and
  `EditorMenuState` fields; tested in the menu-model tests.
- `SourceRollEditor`: a setter for the allowed-pitch filter (e.g.
  `std::optional<std::array<bool,12>>`, nullopt = no filter) consulted in the
  double-click create path; pushed via `PianoRollComponent` from
  `TrackEditorPanel`.
- `TrackEditorPanel` owns the session state and needs to see the song key (it
  already holds `SongDocument&`).
- Tests (TDD, integration-leaning): a refused double-click on an off-key row
  adds no NOTE and no undo transaction; an allowed one adds a note; disabled or
  no key → no filtering; follow-then-freeze with Set Key; menu enable/tick
  states.
- Docs: `docs/UI_GUIDE.md` editor Options menu, `docs/ARCHITECTURE.md` editor
  panel section, `docs/TESTING.md` count, `CLAUDE.md` status line.

## Open for the builder

- Exact id numbering, file names, and how the filter is plumbed to the roll.
