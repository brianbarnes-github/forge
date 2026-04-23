# Config-driven conversion — design

**Status:** Design accepted; implementation pending.
**Author:** Brian Barnes, with Claude, during a 2026-04-23 brainstorming session.

## Summary

Add a `--config PATH` mode to the converter. The config file
authoritatively describes the output's structure — which MIDI tracks
become which ABC parts, what they're called, what LOTRO instruments
they play, and what per-instrument transformations apply (volume
scaling, octave shift, drum map). Three formats supported: JSON, TOML,
and XML. The existing CLI remains identical for quick-command-line use.

## Goals

- Make the CLI a robust backend for a future GUI: the GUI builds a
  config, invokes `converter --config PATH`, done.
- Still easy to hand-run for testing: `converter song.mid song.abc`
  keeps working exactly as today.
- Keep the binary lightweight. Three formats supported natively to
  cover a wide developer audience, but only with parsers that are
  already present (JUCE JSON + XML) plus one small dependency for TOML.

## Non-goals (deferred)

- `--dump-config` — generate a starter config from a MIDI file. Useful,
  not on the critical path. Revisit in v0.3.
- Per-source-track volume scaling. Current design is one
  `volumePercent` per instrument. Adding per-source would be additive
  if ever needed.
- GUI. This design is what a GUI will call; the GUI itself is a
  separate project.
- Machine-readable progress or structured error output.

## Guiding principles inherited

- **MIDI is the source of truth.** Transformations the user explicitly
  requests (merge, transpose, volume scale) are fine. Silent editorial
  passes (smoothing, hysteresis, quantization) are not. See
  `findings/dynamics.md` and the CLAUDE.md guiding principle.
- **LOTRO limits dictate forced transformations only.** Chord cap at 6,
  pitch envelope clamp, single `Q:`/`M:` per part, one dynamic per
  instrument. These are already in place; nothing new to add there.
- **Diagnostics surface every forced decision.** The existing `-v` flag
  and `Diagnostic` population continue unchanged. The volume clamp and
  unclaimed-track skip each emit a `Diagnostic`.

## Pipeline architecture

```
--config PATH ─┐
               │
         load + validate config    (fail fast; no MIDI touched yet)
               │
               ▼
         parse MIDI file           (juce::MidiFile as today)
               │
               ▼
         assemble virtual tracks   (one per ConfigInstrument; merges
               │                    sources, applies transposeSemitones
               │                    and volumePercent to each note)
               │
               ▼
         per-track pipeline        Range → Chord → Duration → Tempo
               │                    → Collision → Dynamic, as today
               ▼
         applyTempoCollapseToSongMaps
               │
               ▼
         AbcWriter::writeAbc       parts emitted in ascending x order;
                                    T: uses instrument.label; drumMap
                                    from instrument (falls back to
                                    song default).
```

When `--config` is absent, a trivial internal `Config` is synthesised
from the existing CLI flags (one instrument per MIDI track, x assigned
in declaration order, no merging, no volume scale) and fed through the
same pipeline. Existing tests and invocations behave identically.

## Config schema

JSON is the canonical example below; TOML and XML carry the identical
data model.

```json
{
  "input":       "song.mid",
  "output":      "song.abc",
  "title":       "My Song",
  "transcriber": "Brian",
  "tempo":       120,
  "transpose":   0,
  "instruments": [
    {
      "x":                  1,
      "name":               "LuteOfAges",
      "label":              "Lead",
      "sources":            [0, 2],
      "transposeSemitones": -12,
      "volumePercent":      110
    },
    {
      "x":                  2,
      "name":               "Theorbo",
      "label":              "Bass",
      "sources":            [3],
      "volumePercent":      80
    },
    {
      "x":                  3,
      "name":               "Drums",
      "sources":            [9],
      "drumMap":            "kit.json"
    }
  ]
}
```

### Top-level fields

| Field | Type | Required | Default | Notes |
|---|---|---|---|---|
| `input` | string | yes | — | Path to input MIDI. Relative paths resolved against the config file's directory. |
| `output` | string | no | `<input-stem>.abc` | Path to output ABC. |
| `title` | string | no | input stem | Song title used in the ABC `T:` header (prefix before the `-` separator). |
| `transcriber` | string | no | `LotroAbcConverter v0.1` | ABC `Z:` header. |
| `tempo` | integer | no | MIDI-detected | Overrides the first tempo entry. Equivalent to today's `--tempo`. |
| `transpose` | integer (semitones) | no | `0` | Global semitone shift; additive with per-instrument `transposeSemitones`. Equivalent to today's `--transpose`. |
| `instruments` | array | yes | — | At least one entry. |

### Instrument fields

| Field | Type | Required | Default | Notes |
|---|---|---|---|---|
| `x` | integer ≥ 1 | yes | — | ABC `X:` index. Required, must be unique across instruments; gaps allowed; emission ordered by ascending `x`. |
| `name` | string | yes | — | LOTRO instrument identifier — one of the enum values exposed by `--list-instruments` (e.g. `LuteOfAges`, `Theorbo`, `Drums`). |
| `label` | string | no | first source's MIDI track name, else `name` | Human-readable display name for the ABC `T:` header (suffix after the `-` separator). |
| `sources` | array of integers ≥ 0 | yes | — | MIDI track indices (0-based) whose notes compose this instrument. Must be non-empty. Indices must reference existing tracks in the MIDI file. |
| `transposeSemitones` | integer | no | `0` | Per-instrument semitone shift. Applied to every note from every source **before** any constraint pass. Additive with the global `transpose`. |
| `volumePercent` | integer | no | `100` | Per-instrument velocity scale. `110` = +10 %, `80` = -20 %. Applied to every note from every source as `velocity = clamp(round(original × percent / 100), 1, 127)`. Clamp hits emit a `Diagnostic`. |
| `drumMap` | string (path) | only allowed on `name: "Drums"` | — | Path to a drum-map JSON file, merged onto the built-in drum defaults for this instrument. Error if supplied on a non-Drums instrument. Relative paths resolved against the config file's directory. |

## Merging semantics

When an instrument's `sources` array has more than one entry, the notes
from the listed MIDI tracks are **concatenated and sorted by `startTick`
into a single virtual track at import time**, before any constraint
runs. From that point on, the pipeline treats it as one track:

- Simultaneous notes from different sources become chord clusters in
  the virtual track. `ChordConstraint` trims clusters larger than 6 by
  existing velocity-then-pitch rules.
- Per-instrument `transposeSemitones` and `volumePercent` apply
  uniformly to every note from every source.
- `DynamicMapper`'s loudest-wins continues to govern the emitted
  dynamic markings: the merged stream's per-tick max velocity is what
  buckets. A soft source and a loud source at the same tick produce a
  dynamic at the loud source's bucket.
- Provenance (`sourceTrackIndex`, `sourceEventIndex`) is preserved per
  note, so diagnostics still point back to the originating MIDI event.

MIDI tracks not referenced by any instrument's `sources` are silently
dropped from output; each emits an info-level `Diagnostic` so `-v`
surfaces them.

## Volume scaling

- Applied after MIDI parsing, before any constraint.
- Formula: `scaled = clamp(round(original × percent / 100), 1, 127)`.
- Clamp hits (either floor at 1 or ceiling at 127) emit a `Diagnostic`
  with source `VolumeScale`, the affected note's tick and pitch, and
  its source track/event IDs.
- `DynamicMapper` runs downstream as usual, bucketing the scaled
  velocities.

## Transpose handling

- `transposeSemitones` (per-instrument) applies to every note from
  every source before any constraint pass.
- Global `transpose` (top-level config or `--transpose` CLI flag) is
  added on top, not substituted. If an instrument has
  `transposeSemitones: 5` and the global is `-12`, each note shifts by
  a net `-7` semitones.
- The sum is applied inside or before `RangeConstraint`, reusing
  `Track::transposeSemitones`. `RangeConstraint` continues to
  octave-correct into the instrument's native range if the result
  falls outside.

## X: index rules

- `x` is required on every instrument.
- Duplicates error out during config validation before MIDI processing
  begins.
- Gaps are allowed; the output contains whatever `x` values the user
  specified, in ascending order.
- Emission order is strictly `x`-ascending. Declaration order in the
  config file has no effect on output.

## Drum-map handling

- Drum-map is a per-instrument concern. Only `name: "Drums"`
  instruments may have a `drumMap` field; non-Drums + `drumMap` is a
  config error.
- Each Drums instrument's `drumMap` is loaded and merged onto the
  built-in defaults (same semantics as today's `--drum-map`: unlisted
  pitches keep their defaults). The result is stored on that
  instrument's virtual `Track`.
- `AbcWriter::emitNotePart` consumes the per-track drum map when
  rendering drum tokens; falls back to `Song::drumMap` (still exists
  for no-config backward compatibility) otherwise.
- CLI `--drum-map PATH` retains its meaning as "apply to all Drums
  instruments that don't have their own `drumMap`."

## Validation and error handling

All validation runs before any MIDI processing. Errors go to stderr
with the offending path/field; the process exits with code 2 (reserved
for CLI/config errors, distinct from MIDI I/O errors at code 1). Error
cases:

- Config file not found / unreadable / malformed (format-parser error).
- `instruments` array empty or missing.
- Instrument missing a required field (`x`, `name`, `sources`).
- `sources` empty for any instrument.
- Any `sources[i]` negative or ≥ MIDI track count.
- `x` value duplicated across instruments.
- `x` < 1.
- `name` not a valid LOTRO instrument identifier (reuses
  `parseName`'s existing error message that lists valid names).
- `drumMap` supplied on a non-Drums instrument.
- `drumMap` file not found / unreadable / malformed.
- `volumePercent` ≤ 0 (rejected — no silent-or-inverted notes).
- Unknown top-level or instrument field (strict schema — typos fail
  loud).

MIDI tracks absent from every `sources` array are **not** errors; they
emit info-level diagnostics.

## CLI behaviour

The existing flag set stays exactly as it is today:

```
converter [OPTIONS] INPUT.mid [OUTPUT.abc]
  --instrument N=NAME   Assign instrument to track N (repeatable)
  --tempo BPM           Override detected main tempo
  --transpose N         Global semitone transpose (pre range-clamp)
  --drum-map PATH       Load drum-map JSON file, merged onto defaults
  --list-tracks         Print track table and exit
  --list-instruments    Print valid instrument NAME values and exit
  -v, --verbose         Log Diagnostics to stderr
  -h, --help            Print this help and exit
```

New additions:

```
  --config PATH             Load a JSON / TOML / XML config file.
  --config-format FMT       Override format detection (json|toml|xml).
```

Interaction:

- With `--config`, the config file supplies all settings. Any
  additional CLI flags override the corresponding config value:
  `--tempo` overrides `tempo`, `--transpose` is additive on top of
  `transpose`, `--instrument` is rejected (config controls instrument
  assignment), `--drum-map` sets the default for Drums instruments
  without a `drumMap` field, and positional `INPUT.mid`/`OUTPUT.abc`
  override the config's `input`/`output`.
- Without `--config`, a trivial internal `Config` is synthesised from
  the CLI flags and MIDI file (auto-picks instruments, one per
  track, x in track order, no merging, no volume scale). Output is
  identical to today.

## Format detection and parsers

- JSON: parsed via `juce::JSON` (already integrated).
- XML: parsed via `juce::XmlDocument` (already in `juce_core`, no new
  dependency).
- TOML: parsed via `toml++` (header-only, C++17, MIT licence). Vendored
  as a single header at `Source/ThirdParty/tomlplusplus/toml.hpp`. No
  submodule; the file is small and rarely updated.
- Format detected by file extension (`.json`, `.toml`, `.xml`),
  case-insensitive. `--config-format` overrides detection for stdin or
  oddly-named files.

Each parser's job is to produce a `Config` struct; downstream code
doesn't know or care which parser ran.

## Data model additions

New files under `Source/Core/`:

- **`Config.h` / `Config.cpp`** — `Config` and `ConfigInstrument`
  structs plus validation helpers. POD where possible; validation
  returns a `std::optional<std::string>` error message.
- **`ConfigLoader.h` / `ConfigLoader.cpp`** — format dispatch
  (`loadConfig(path, formatHint)` → `Config` or error). Internal
  per-format parsers are implementation details of this unit.

Modifications:

- **`Track.h`** — add `std::optional<DrumMap> drumMap`. Populated
  during virtual-track assembly for Drums instruments that have a
  `drumMap` config field. Empty means "fall back to `Song::drumMap`."
- **`MidiImporter.cpp`** or a new assembly unit — function
  `assembleTracks(const Song& rawSong, const Config& config, Diagnostics&)
  → Song` that produces the post-merge Song.
- **`Main.cpp::runPipeline`** — unchanged at the per-track level;
  `main()` gains a config-loading branch.
- **`AbcWriter.cpp`** — consumes `instrument.x` for the emitted `X:`
  header; iterates song.tracks in x-sorted order; consumes
  `track.drumMap` if set, else `song.drumMap`.

## Testing strategy

New tests:

- **`ConfigLoader_tests.cpp`** — per format: round-trip a hand-written
  fixture, verify all fields parse. Malformed inputs per format. Field
  validation errors (missing, duplicates, out-of-range sources, etc.).
- **`ConfigPipeline_tests.cpp`** — end-to-end: a small synthetic MIDI
  plus config running through the full pipeline. Cases:
  - Two sources merged into one instrument; overlapping notes form a
    chord; `ChordConstraint` trims if > 6.
  - Volume scale up; clamp diagnostic fires on a max-velocity input.
  - Volume scale down; no clamp.
  - Per-instrument transpose adds with global transpose.
  - Drums instrument with custom `drumMap`.
  - Non-contiguous `x` values emitted in ascending order.
  - Unreferenced MIDI track dropped with diagnostic.
- **`Backward_tests.cpp`** or extending `EndToEnd_tests.cpp` — run
  without `--config` and verify output matches a pinned snapshot (the
  trivial-config path must preserve today's behaviour).

Fixtures: small hand-constructed MIDI files plus canonical config
fixtures in each of the three formats.

## Open questions

None at spec completion. Any ambiguity surfaced during implementation
should be captured here as a delta.

## Rollout

Phase-1 (single commit or small series):

1. `Config` / `ConfigInstrument` data model + validation.
2. JSON loader (builds on existing `juce::JSON`).
3. Virtual-track assembly + volume scale + per-instrument transpose.
4. CLI wiring for `--config`.
5. Per-instrument drum map storage and emission.
6. `AbcWriter` x-sorted emission + `label` in `T:` header.
7. Tests covering all of the above.

Phase-2:

1. XML loader (`juce::XmlDocument`).
2. TOML loader (`toml++` integration).
3. `--config-format` flag.
4. Format-specific tests.

Phase-3 (deferred):

1. `--dump-config` starter generator.

Phase-1 lands the feature usably; phase-2 broadens format support
without changing semantics.
