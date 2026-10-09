# Songsmith MIDI import options — design

Date: 2026-10-08. Status: awaiting review.

## Intent

Importing a MIDI file (File ▸ Import ▸ MIDI…, or dropping a `.mid`/`.midi`)
gains two independent choices, modelled on REAPER's "MIDI File Import" box:

1. **Tempo map** — *keep* the Song's existing tempo map (today's behaviour) or
   *replace* it with the file's.
2. **Tracks** — *expand* the file into separate tracks (today's behaviour) or
   *merge* all of its note tracks into one track.

A new **Preferences ▸ Import** page holds one setting, **Import Track
Options**: *Ask* (show the choices dialog on every import) or *Import Expanded
Always* (no dialog; identical to today).

Success: a user who often wants a one-track import can pick it at import time;
a user who never does can switch the prompt off and sees no change from today;
an unedited merged import still exports note-for-note with each note on its
original MIDI channel.

Said by the user: both choices, merge-into-one as a feature they use often,
the settings name "Import Track Options" with exactly the values Ask / Import
Expanded Always, a Preferences "import branch", the import only adds MIDI
information to the Song object. Chosen with the user: **Always = keep the
existing tempo map** (zero behaviour change); **options live in the planner**
(approach A); merged-track metadata and tempo-replace semantics as in
"Planner behaviour" below; **default setting is Ask**.

## Scope

In: `ImportOptions`, planner support for merge and tempo-replace, an
`ImportOptionsDialog`, the `import.trackOptions` setting, the Preferences ▸
Import page, wiring in `MainWindow::openMidiFromPath`.

Out: other import options (markers/cues, per-track pick lists), remembering
the last dialog choice, a CLI equivalent, changing `Source/Core/` (this is
Source/UI editing/import logic; see the `forge-engine-ui-boundary` skill).

## Options and settings

- `struct ImportOptions { TempoMode tempo = TempoMode::keep; TrackMode tracks = TrackMode::expanded; }`
  with `enum class TempoMode { keep, replace }` and
  `enum class TrackMode { expanded, merged }`, in `Source/UI/MidiImportPlan.h`.
  Defaults equal today's behaviour, so existing callers and tests are unchanged.
- `AppSettings` gains `ImportTrackOptions importTrackOptions() const` /
  `setImportTrackOptions (ImportTrackOptions)`, with
  `enum class ImportTrackOptions { ask, expandedAlways }`. Key
  `import.trackOptions`, stored `"ask"` / `"expanded"`; an unrecognised or
  missing value reads as `ask`. Written through and saved immediately, like the
  two confirmation settings.
- Decision function (pure, in `AppSettings.h`):
  `shouldAskImportOptions (const AppSettings&)` is true for `ask`.

## Planner behaviour

`planMidiImport (song, raw, isFirstImport, diagnostics)` gains an
`const ImportOptions& options` parameter (defaulted), and
`appendImportedMidi` / `importMidiFile` pass it through.

- **Tempo = keep:** unchanged. The first import writes the conductor
  (`plan.writesConductor`), later ones do not. The existing "file's tempo map
  differs" warning stays.
- **Tempo = replace:** `writesConductor` is true on any import. The existing
  conductor's song-wide events (tempo, time signature, key signature, SMPTE
  offset, marker, copyright — `isSongWideMetaEvent`) and the tempo/meter map
  nodes are removed, then the file's are written. Existing tracks' ticks do
  not change, so they now play against the new tempo map. One Info diagnostic
  (source "SongModelBridge") says the tempo map was replaced. On the first
  import into an empty Song, replace and keep are the same.
- **Tracks = expanded:** unchanged, one `PlannedTrack` per raw track.
- **Tracks = merged:** one `PlannedTrack` carrying every note link and event of
  all the file's note-bearing raw tracks.
  - Each note keeps its own MIDI `channel`, so export writes it on the original
    channel.
  - Events keep provenance (`order` within source, `relocatedFrom`); the merged
    track's event order uses a deterministic sort key (tick, then source raw
    track index, then original `order`).
  - Note-less raw tracks are not merged; they import as they do today.
  - Metadata: name = the file's stem + " (merged)"; `sourceProgram` and
    `sourceMidiChannel` from the first note-bearing track; `defaultChannel`
    from that track's channel; `endTick` = the maximum of the merged tracks'.
- Planning failures (format 2, parser disagreement) behave as today: Error
  diagnostic, document unchanged, `false` returned.

## Dialog

`ImportOptionsDialog` (`Source/UI/ImportOptionsDialog.{h,cpp}`): a small modal
`juce::DialogWindow` via `launchAsync` (like `AboutBox`), content named after
the file.

- Tempo map group, radio buttons: "Keep existing tempo map" (default) /
  "Replace existing tempo map". Disabled, with the file's tempo map implied,
  when the Song has no tempo map yet (first import).
- Tracks group, radio buttons: "Expand into separate tracks" (default) /
  "Merge into one track". Always enabled.
- OK delivers an `ImportOptions` through a callback; Cancel (button, Escape,
  title-bar X) imports nothing and changes nothing.

## Flow (`MainWindow::openMidiFromPath`)

Both entry points — the file chooser and dropped files — already end in
`openMidiFromPath`, so the change is there only.

- `shouldAskImportOptions (appSettings)` true: open the dialog; on OK run
  `importMidiFile (…, options)` and the existing diagnostics/fit-timeline
  follow-up; on Cancel do nothing.
- Otherwise: run the import at once with default `ImportOptions`.

## Preferences ▸ Import page

A second entry in `preferencePages()` after General, plus
`ImportPreferencesPage`: one dropdown, **Import Track Options**, values *Ask* /
*Import Expanded Always*, writing through `AppSettings` immediately. A note
under it: "With Ask, every import shows the tempo map and track choices.
Merging into one track is only available through Ask."

## Errors

No new failure modes: the options are enums, and planning failures are handled
as above. A custom error type is added only if implementation finds a new one.

## Testing

Integration first (the repo's preference).

1. Planner, merged: one track; note count = sum of the source tracks'; every
   note's channel preserved; events keep provenance; note-less tracks still
   import as separate tracks.
2. Round trip: import merged, export, and compare the notes and their channels
   with the original file.
3. Planner, tempo: replace on a later import swaps the tempo and meter maps and
   emits the Info diagnostic; keep leaves them and keeps today's warning;
   first import is identical for both.
4. `AppSettings`: default is `ask`; round trip; unrecognised value reads `ask`;
   `shouldAskImportOptions`.
5. `ImportOptionsDialog`: defaults; tempo group disabled on a first import;
   OK delivers the chosen options; Cancel delivers none.
6. Preferences tree lists General then Import; the Import page's dropdown
   reflects and writes the setting.
7. Existing planner/bridge tests pass unchanged (defaults = today).

Not headless-testable: `MainWindow::openMidiFromPath` wiring and the modal
dialog behaviour. A manual checklist covers them (Ask shows the dialog from
both the menu and a drop; Cancel leaves the Song untouched; merged import
yields one track that plays and exports; Always imports with no dialog;
the setting survives a restart).

## Docs

`docs/ARCHITECTURE.md` (a new subsection for import options, and the planner
section), `docs/UI_GUIDE.md` (dialog and Import page), `docs/TESTING.md`
(count, new test files), `CLAUDE.md` (status line and docs map row), this
spec's status.
