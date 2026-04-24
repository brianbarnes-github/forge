# Instruments Treeview UI redesign — design

**Status:** Design accepted; implementation pending.
**Author:** Brian Barnes, with Claude, during a 2026-04-23 brainstorming session.

## Summary

Reimagine the Editor pane of `converter_ui` as a three-level treeview with per-node property pages, replacing the current Global Settings View + Instruments Table + Instrument Detail Form triplet. The tree's root is a **Song** node (with the global settings as its property page); its children are **Instrument** nodes (one per `ConfigInstrument`); their children are **Source** nodes (one per MIDI track that feeds an instrument). Source nodes gain real per-source knobs for `transposeSemitones` and `volumePercent`; the per-instrument versions of those knobs are removed. Left-click a node to open its property page below the tree; right-click to reach context actions (Add Instrument / Add Source / Delete Instrument / Delete Source).

## Goals

- A single, unified tree that's the canonical editor surface — no more three-region editor.
- Make it obvious which MIDI tracks feed which instrument (they're literally the children of the instrument in the tree).
- Finer control over merged tracks: each source can carry its own transpose and volume adjustment, so a user merging Lead + Pad tracks into one LOTRO part can balance them without touching the underlying MIDI.
- Simpler instrument-level model: the Instrument node is pure identity (x, name, label, drumMap). Audio-mutating knobs live at the macro level (song-wide transpose) and the micro level (per source).

## Non-goals

- Keyboard shortcuts for Add / Delete.
- Drag-and-drop of Source nodes between Instrument parents.
- Undo / redo.
- Tree-search / filter.
- A "Set transpose on all sources of this instrument" convenience action. (Might add later; not needed for v1 of the redesign.)
- Visual polish (animations, themed icons, etc.).

## Guiding principles inherited

- **MIDI is the source of truth.** Nothing in this redesign changes what the converter produces for a given Config; it only changes how the user edits the Config.
- **Testing cohort audience.** GUI testing stays manual-smoke only.
- **Forced decisions only.** The data model additions (per-source transpose + volume) are user-requested knobs, not editorial passes.

## Data model

### `ConfigSource` (new)

```cpp
struct ConfigSource
{
    int midiTrackIndex      = -1;   // required; 0-based index into Song.tracks
    int transposeSemitones  = 0;    // additive with Config.transpose
    int volumePercent       = 0;    // 0 = no change, +N louder, -N quieter
};
```

`ConfigSource` replaces the bare `int` that previously lived in `ConfigInstrument.sources`. Default-initialised instances are invalid (`midiTrackIndex == -1`); the validator enforces that every source has a real index.

### `ConfigInstrument` (modified)

```cpp
struct ConfigInstrument
{
    int                                x = 0;
    std::string                        name;
    std::optional<std::string>         label;
    std::vector<ConfigSource>          sources;     // was vector<int>
    std::optional<std::string>         drumMap;

    // REMOVED: int transposeSemitones   (moved to song + source)
    // REMOVED: int volumePercent        (moved to source only)
};
```

The instrument is pure identity: `x`, `name`, optional `label`, its sources, and (only for Drums) a `drumMap`.

### `Config` (unchanged top-level)

`Config.transpose` stays as the song-wide transpose. **No `Config.volumePercent`** — volume lives only on sources, per the user's explicit correction during brainstorming.

### Assembly math

Per note, computed when the pipeline runs:

- **Pitch:** `final_pitch = raw_pitch + source.transposeSemitones + config.transpose`
- **Velocity:** `final_velocity = clamp(raw_velocity × (1 + source.volumePercent / 100), 1, 127)`

Simpler than the current two-layer formula because the instrument middle layer is gone.

### Validation additions

- `ConfigSource.midiTrackIndex` must be ≥ 0 and < MIDI track count. (Same rule previously applied to the integer entries.)
- `ConfigSource.volumePercent` must be > -100 (at or below that value produces silence or inverted velocity).
- Within one instrument, `midiTrackIndex` values must be unique — the same MIDI track cannot appear twice in one instrument's sources. (Cross-instrument reuse is still allowed; same MIDI track can legitimately feed two different LOTRO instruments.)

## File-format changes

All three formats (JSON, TOML, XML) gain the new `ConfigSource` shape for the `sources` array. For every format, writers emit the new object shape; loaders accept both the new object shape and — as backward-compat shorthand — the old bare-integer form.

### JSON

```json
"sources": [
    { "midiTrack": 0 },
    { "midiTrack": 2, "transposeSemitones": -12, "volumePercent": 10 }
]
```

Bare integer `"sources": [0, 2]` is accepted and treated as `[{"midiTrack": 0}, {"midiTrack": 2}]` with default (zero) transposeSemitones and volumePercent. Writer always emits the object shape.

### TOML

```toml
[[instruments]]
x = 1
name = "LuteOfAges"

[[instruments.sources]]
midiTrack = 0

[[instruments.sources]]
midiTrack = 2
transposeSemitones = -12
volumePercent = 10
```

Bare-integer shorthand also accepted (`sources = [0, 2]` loads as two default sources).

### XML

```xml
<instrument x="1" name="LuteOfAges">
  <sources>
    <source midiTrack="0" />
    <source midiTrack="2" transposeSemitones="-12" volumePercent="10" />
  </sources>
</instrument>
```

Bare-integer text content in `<source>` accepted: `<source>0</source>` loads identically to `<source midiTrack="0" />`.

### Migration from old instrument-level fields

Old configs with `ConfigInstrument.transposeSemitones` or `ConfigInstrument.volumePercent` still **load** (loaders are permissive), but the removed fields are **dropped** and a `Warning`-severity `Diagnostic` is emitted on next Run:

> `ignoring removed field 'transposeSemitones' on instrument X:1; use per-source or Config.transpose instead`

Users see the warning and can migrate. Writes always use the new shape, so once a config is resaved through the UI the old fields disappear.

Default-omission: `transposeSemitones` and `volumePercent` are omitted from written JSON / TOML / XML source objects when they're at their default value of `0`. A user who never touches the per-source knobs sees tidy `{"midiTrack": N}` entries.

## Tree structure

```
📄 Pull The Wires                      ← Song node (root, always present)
├─ 🎵 X:1  LuteOfAges — "Lead"         ← Instrument node
│   ├─ 🎹 MIDI 0: Drums (chan 10, 142)         ← Source node
│   └─ 🎹 MIDI 2: Lead Guitar (chan 2, 211)
├─ 🎵 X:2  Theorbo — "Bass"
│   └─ 🎹 MIDI 1: Bass (chan 1, 88)
└─ 🎵 X:3  Drums
    └─ 🎹 MIDI 9: Drums (chan 10, 56)
```

Label conventions:

- **Song** — `Config.title` if set, else the input MIDI filename stem.
- **Instrument** — `X:<n>  <name>`, with ` — "<label>"` appended when a label is set.
- **Source** — `MIDI <index>: <midiTrackName> (chan <channel>, <noteCount>)`. The MIDI track info comes from the loaded `Song.tracks[midiTrackIndex]`.

Emoji decoration is optional — JUCE's `TreeViewItem::paintItem` can render custom icons if useful, but plain text labels are acceptable for v1.

Nodes refresh when:

- A MIDI is loaded (tree rebuilds from scratch).
- An Add / Delete action mutates the Config (tree refreshes and tries to re-select the same node).
- A property page edit changes a tree-visible field (e.g. the Instrument's label is typed in — `Instrument` node's label prefix updates live).

## Interaction

| Gesture | Behaviour |
|---|---|
| **Left-click** a node | Selects it; `PropertyPageHost` swaps to show that node's property page. |
| **Right-click** a node | Pops a context menu appropriate to the node type (see below). Does **not** change selection. |
| **Double-click** | No special behaviour. |
| **Arrow keys** (while tree has focus) | Standard JUCE TreeView navigation (up / down / left-collapse / right-expand). Selection follows. |

Selection persists across tree refreshes: after a mutation, the tree attempts to re-select the same node by identity (Song is always selectable; Instruments by `x`; Sources by parent instrument's `x` + `midiTrackIndex`). If the previously selected node no longer exists, selection falls back to the Song.

## Context menus

### Song node

```
Add Instrument
```

Creates a new `ConfigInstrument` with the next free `x` value (max(existing x) + 1, starting at 1), `name = "LuteOfAges"` (the project's standard default), empty `sources`, no label. The new node becomes selected and expanded. The Song node itself cannot be deleted — it's the root.

### Instrument node

```
Add Source ▸
   MIDI 0: Drums (chan 10, 142)                 ← MIDI tracks NOT already
   MIDI 3: Pad (chan 3, 64)                     ←   in THIS instrument
   MIDI 5: Rhythm (chan 4, 180)
   …
──────────
Delete Instrument
```

**Add Source** lists every MIDI track in the loaded `Song` that's not already in this instrument's `sources`. Selecting one appends a new `ConfigSource{midiTrackIndex=N, 0, 0}` to this instrument and re-selects the parent. (Per-source knobs start at 0; the user opens the new Source node to tweak them.)

If every MIDI track is already sourced by this instrument, the Add Source submenu is empty; render a single disabled "(no unused MIDI tracks)" entry rather than hiding the menu (clearer UX).

**Delete Instrument** removes the instrument and all its child Source nodes. Selection falls back to the Song. An Instrument left with zero sources is not auto-deleted — the user might be mid-refactor.

### Source node

```
Delete Source
```

Removes this `ConfigSource` from its parent instrument's `sources`. If the instrument now has zero sources, the instrument stays in the tree; `validateConfig` will catch it as an Error on Run.

## UI components

### New classes

| Path | Responsibility |
|---|---|
| `Source/UI/InstrumentsTree.{h,cpp}` | `juce::TreeView` + `TreeViewItem` subclasses (`SongItem`, `InstrumentItem`, `SourceItem`). Builds items from the `Config` + `Song`, handles right-click context menus, dispatches selection changes, rebuilds on mutation. |
| `Source/UI/SongPropertyPage.{h,cpp}` | Form for the Song node. Absorbs today's `GlobalSettingsView`. |
| `Source/UI/InstrumentPropertyPage.{h,cpp}` | Form for an Instrument node (`x`, `name` dropdown, `label`, `drumMap` when applicable). |
| `Source/UI/SourcePropertyPage.{h,cpp}` | Form for a Source node (read-only MIDI info; editable `transposeSemitones`, `volumePercent`). |
| `Source/UI/PropertyPageHost.{h,cpp}` | Owns all three pages; swaps which is visible based on tree selection. Has a `showFor (SelectionKind, int/int)` method: `Song`, `Instrument idx`, `Source instrumentIdx,sourceIdx`. |

### Classes removed

- `Source/UI/GlobalSettingsView.{h,cpp}` — replaced by `SongPropertyPage`.
- `Source/UI/InstrumentsTable.{h,cpp}` — replaced by `InstrumentsTree`.
- `Source/UI/InstrumentDetailForm.{h,cpp}` — replaced by `InstrumentPropertyPage` + `SourcePropertyPage`.

Their `.h` and `.cpp` files are deleted. CMake `target_sources` list for `converter_ui` updated accordingly.

### `EditorPane` reshape

Three children: `InstrumentsTree` (top), `PropertyPageHost` (middle), `Run Converter` button (bottom). No more `GlobalSettingsView` at the top. A vertical splitter between the tree and the property page host is nice-to-have but can start as a fixed ratio (e.g. tree gets top 40%, property page gets the rest).

### `InstrumentAssembly` math updates

- Per-source loop reads `src.transposeSemitones` and `src.volumePercent` from the `ConfigSource` directly. The "per-instrument totalTranspose" variable goes away.
- The `VolumeScale` clamp Diagnostic's `message` gains the source label for clarity:
  `velocity clamped during volume scale on '<instrument label>' source MIDI-<N>`
  (previously just `on '<instrument label>'`, which was ambiguous when an instrument had multiple sources).

## Testing

Test suite diff:

### Updated

- `Tests/InstrumentAssembly_tests.cpp` — transpose+volume tests poke `ConfigSource` fields (the old per-instrument knobs are gone). Net-behaviour assertions (velocity 127 clamp + Diagnostic, -20% scales 100→80, etc.) stay the same.
- `Tests/Config_tests.cpp` — replace the retired instrument-field tests with new source-field tests: missing `midiTrackIndex` fails, duplicate `midiTrackIndex` within one instrument fails, `volumePercent <= -100` still fails but now against a `ConfigSource`.
- `Tests/ConfigLoader_tests.cpp` / `ConfigLoaderXml_tests.cpp` / `ConfigLoaderToml_tests.cpp` — round-trip tests cover the new object shape *and* the bare-integer shorthand (one test per format for each).
- `Tests/ConfigWriter_tests.cpp` — writer always emits object shape; `richConfig()` helper updated.
- `Tests/ConfigPipeline_tests.cpp` / `EndToEndConfig_tests.cpp` — Configs built in tests use `ConfigSource` objects.
- `Tests/Pipeline_tests.cpp` — `synthesiseConfig` returns `ConfigSource` objects now (default `{midiTrackIndex=i, 0, 0}` per raw track). Test expectations updated.

### Retired

- Any test that directly pokes `InstrumentsTable` / `InstrumentDetailForm` / `GlobalSettingsView`. (Currently there are no unit tests for those components — they were deemed manual-smoke only — so there's nothing to retire.)

### No new GUI tests

The treeview is GUI code; the testing cohort principle from `project_ui_audience.md` says manual smoke testing is sufficient for the UI. The backing data-mutation logic IS covered by the unit tests above (which exercise `Config` / `ConfigSource` / loader / writer / assembler).

## Backward compatibility

- **Old-format configs** (bare-integer sources, instrument-level `transposeSemitones` / `volumePercent`) continue to load. Removed instrument fields get a Warning Diagnostic. Users see the warning, can migrate.
- **Old-format saves** — Save As in the UI always writes the new object shape, so any config resaved through the UI is migrated automatically.
- **CLI** behaviour is unchanged externally: `converter --config foo.json in.mid out.abc` works with either format. The CLI synthesiseConfig path builds the new `ConfigSource` objects internally from the raw MIDI.

## Out of scope (deferred)

- Drag-and-drop reordering of Sources between Instruments.
- Keyboard shortcuts (Del to delete, Enter to rename, etc.).
- Undo / redo.
- Tree filter / search box.
- Icon or theme customisation.
- "Apply transpose to all sources" bulk action on Instrument context menu.

## File plan

### New files

- `Source/UI/InstrumentsTree.h` / `.cpp`
- `Source/UI/SongPropertyPage.h` / `.cpp`
- `Source/UI/InstrumentPropertyPage.h` / `.cpp`
- `Source/UI/SourcePropertyPage.h` / `.cpp`
- `Source/UI/PropertyPageHost.h` / `.cpp`

### Deleted files

- `Source/UI/GlobalSettingsView.h` / `.cpp`
- `Source/UI/InstrumentsTable.h` / `.cpp`
- `Source/UI/InstrumentDetailForm.h` / `.cpp`

### Modified files

- `Source/Core/Config.h` — add `ConfigSource`, change `ConfigInstrument::sources`, remove the two instrument-level int fields.
- `Source/Core/Config.cpp` — validator additions/deletions per the rule changes.
- `Source/Core/ConfigLoader.cpp` — per-format handling of the new `ConfigSource` shape plus bare-integer shorthand. Permissive drop of removed instrument fields with Warning Diagnostic.
- `Source/Core/ConfigWriter.cpp` — per-format emission of the new object shape; drop instrument-level int fields.
- `Source/Core/InstrumentAssembly.cpp` — per-source transpose/volume reads; improved VolumeScale Diagnostic message.
- `Source/Core/Pipeline.cpp` — `synthesiseConfig` emits `ConfigSource` objects.
- `Source/UI/EditorPane.{h,cpp}` — lay out tree + property-page-host + Run button (no Global Settings View).
- `Source/UI/MainWindow.{h,cpp}` — small adjustments if drag-drop or menu wiring needs to refresh the tree on new MIDI.
- `CMakeLists.txt` — swap the deleted `.cpp` files for the new ones in `target_sources`.
- `docs/UI_GUIDE.md` — rewrite the Editor pane section around the new tree + property pages.

## Open questions

None at spec completion. Anything surfaced during implementation should be appended here as a delta.

## Rollout

Single phase; tasks roughly:

1. Data model changes + validator + unit tests.
2. Loader changes (JSON, TOML, XML) + backward-compat shorthand + tests.
3. Writer changes + round-trip tests.
4. InstrumentAssembly math update + test updates.
5. `synthesiseConfig` output-shape update.
6. New `SongPropertyPage`, `InstrumentPropertyPage`, `SourcePropertyPage` components.
7. `PropertyPageHost` (the view-switcher below the tree).
8. `InstrumentsTree` (TreeView + TreeViewItem subclasses, context menus, rebuild-on-mutation).
9. `EditorPane` rewire — new children, layout, delete the old sub-view classes.
10. CMake + file deletions.
11. Smoke test + `docs/UI_GUIDE.md` rewrite.

Phases aren't independent here (the tree depends on the property pages, which depend on the data model), but each task is a small focused commit and can land sequentially.
