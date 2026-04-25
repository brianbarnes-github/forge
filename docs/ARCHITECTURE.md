# Forge — Architecture

An in-depth tour of how Forge works. Organised by functional group, top-down: raw MIDI comes in the left side, LOTRO-playable ABC comes out the right side, and every transformation in between has a named home.

> **Build layout**
>
> ```
> forge_core   (static lib, JUCE-free public API)  ─►  forge_tests (Catch2)
>        ▲                                                 ▲
>        │ linked by                                       │
>        ├─────────────────────────────────────────────────┤
> forge (CLI exe)                              forge_ui (JUCE GUI exe)
> ```

## Contents

1. [Core data types](#1-core-data-types)
2. [MIDI import](#2-midi-import)
3. [Config — loading, validation, writing](#3-config--loading-validation-writing)
4. [Instrument assembly](#4-instrument-assembly)
5. [The constraint pipeline](#5-the-constraint-pipeline)
6. [ABC writing — emission model](#6-abc-writing--emission-model)
7. [Diagnostics](#7-diagnostics)
8. [CLI](#8-cli)
9. [GUI (forge_ui)](#9-gui-forge_ui)
10. [End-to-end data flow](#10-end-to-end-data-flow)

---

## 1. Core data types

All public headers under `Source/Core/` are JUCE-free. Everything the CLI, tests, and future tooling depend on is plain C++17 standard-library types.

### `Note` — `Source/Core/Note.h`

The atom of the whole system. Pure POD, 7 fields:

```cpp
struct Note {
    int  pitch           = 0;    // absolute MIDI pitch
    int  startTick       = 0;    // MIDI ticks from song start
    int  durationTicks   = 0;    // positive; zero-duration notes get dropped
    int  velocity        = 0;    // 0..127
    bool isDrum          = false;
    int  sourceTrackIndex = -1;  // which MIDI-file track this came from
    int  sourceEventIndex = -1;  // which note-on inside that track
};
```

The `sourceTrackIndex` / `sourceEventIndex` pair is the **provenance handle**: every `Diagnostic` emitted downstream carries it, so a future editor can jump from "Warning on this note" back to the exact originating MIDI event, even if the note was transposed, range-folded, or merged across tracks along the way.

### `Track` — `Source/Core/Track.h`

A logical LOTRO part — one output `X:N` in the ABC:

```cpp
struct Track {
    std::string                   name;                       // shown in T: header
    std::vector<Note>             notes;
    std::vector<DynamicChangeRef> dynamicChanges;             // from DynamicMapper
    LotroInstrument               instrument  = LotroInstrument::LuteOfAges;
    bool                          enabled     = true;
    int                           transposeSemitones = 0;     // legacy, currently 0
    int                           sourceMidiChannel  = 0;     // 10 = drums (GM)
    int                           x = 0;                      // ABC X: index
    std::optional<DrumMap>        drumMap;                    // per-track override
};
```

`DynamicChangeRef` is a `(startTick, marking)` pair — the buckets are `ppp, pp, p, mp, mf, f, ff, fff`.

### `Song` — `Source/Core/Song.h`

The whole composition:

```cpp
struct Song {
    int                      ticksPerQuarter = 480;
    std::vector<Track>       tracks;
    std::vector<TempoChange> tempoMap;       // {tick, bpm}
    std::vector<MeterChange> meterMap;       // {tick, numerator, denominator}
    std::string              title;
    std::string              transcriber = "Forge v0.1.0";
    DrumMap                  drumMap     = defaultDrumMap();
};
```

`tempoMap` and `meterMap` preserve mid-song changes. They start in original-MIDI-tick space after import, and get rescaled into stream-tick space by `applyTempoCollapseToSongMaps` at the end of the pipeline.

### `Config` — `Source/Core/Config.h`

The user's spec for how to assemble the output:

```cpp
struct ConfigSource {
    int midiTrackIndex      = -1;   // required; 0-based into raw Song.tracks
    int transposeSemitones  = 0;    // stacks additively with Config.transpose
    int volumePercent       = 0;    // 0 = no change, +N louder, -N quieter; > -100
};

struct ConfigInstrument {
    int                        x = 0;
    std::string                name;            // LOTRO enum identifier
    std::optional<std::string> label;           // T: header suffix (fallback: source name)
    std::vector<ConfigSource>  sources;         // ≥ 1 source per instrument
    std::optional<std::string> drumMap;         // JSON path; only valid on Drums
};

struct Config {
    std::string                   input;
    std::optional<std::string>    output;
    std::optional<std::string>    title, transcriber;
    std::optional<double>         tempo;
    int                           transpose = 0;
    std::vector<ConfigInstrument> instruments;
};
```

Multiple `ConfigSource`s can feed the same `ConfigInstrument` — that's how track merging is expressed. Per-source transpose + global transpose are both additive.

### `Diagnostic` — `Source/Core/Diagnostics.h`

Every transformation that drops, clamps, trims, or rescales a note emits one of these:

```cpp
enum class Severity { Info, Warning, Error };

struct Diagnostic {
    Severity    severity         = Severity::Warning;
    std::string source;                          // "RangeConstraint", "VolumeScale", ...
    std::string message;
    int         trackIndex        = -1;          // Song.tracks index (post-assembly)
    int         tick              = -1;
    int         pitch             = -1;
    int         sourceTrackIndex  = -1;          // raw MIDI track
    int         sourceEventIndex  = -1;          // raw MIDI note-on ordinal
};
```

The two locator pairs (`trackIndex/tick/pitch` for the assembled view, `sourceTrackIndex/sourceEventIndex` for the raw-MIDI view) let a GUI navigate both directions.

### `LotroInstrument` — `Source/Core/LotroInstrument.{h,cpp}`

Enum of the 19 instruments LOTRO supports, plus a lookup table of per-instrument native MIDI ranges. Every non-drum instrument is exactly 36 semitones wide; the ABC letter range `C, .. c'` maps onto that native range (see [§6](#6-abc-writing--emission-model)).

```cpp
{ LuteOfAges / BasicLute / Harp / Bassoon / Cowbell / MoorCowbell:  36..72 }
{ Theorbo:                                                           24..60 }
{ Clarinet / Horn / Pibgorn:                                         48..84 }
{ Flute + Fiddle family (7):                                         60..96 }
{ Drums:                                                              0..0  }
```

Helpers: `rangeFor(instrument) → {midiLow, midiHigh}`, `parseName(str, out) → error-string`, `displayName(enum) → std::string_view`.

### `DrumMap` — `Source/Core/DrumMap.{h,cpp}`

GM drum pitch → ABC drum-slot letter. Hash-backed, populated by `defaultDrumMap()` from spec §2.6 + GM extensions. Supports merge-on-top semantics via `DrumMapLoader`: loading a JSON doesn't replace the defaults, it overlays them.

---

## 2. MIDI import

**File:** `Source/Core/MidiImporter.{h,cpp}`. Only place in the repo that includes `juce_audio_basics` / `juce::MidiFile`.

### Pipeline

```
istream  ─►  juce::MidiFile::readFrom  ─►  walk tracks  ─►  raw lotro::Song
```

1. **Read the whole stream into a memory buffer.** MIDI files are small; JUCE's `MemoryInputStream` wraps the bytes.
2. **Parse** via `juce::MidiFile::readFrom`. Throws `MidiImportError` on parse failure.
3. **Time format.** `midi.getTimeFormat()` must be positive — SMPTE (negative) formats are rejected. The positive value is stored as `song.ticksPerQuarter`.
4. **Tempo/meter meta events** (`importTempoAndMeter`) are extracted across *all* tracks into the song-level `tempoMap` / `meterMap`:

   ```cpp
   if (message.isTempoMetaEvent()) {
       song.tempoMap.push_back({ tick, 60.0 / message.getTempoSecondsPerQuarterNote() });
   } else if (message.isTimeSignatureMetaEvent()) {
       message.getTimeSignatureInfo(numerator, denominator);
       song.meterMap.push_back({ tick, numerator, denominator });
   }
   ```

5. **Note pairing.** For each note-on, JUCE's MIDI file pre-matches the corresponding note-off. The importer:
   - increments a per-track `noteOnOrdinal` counter, assigns it to `sourceEventIndex`
   - drops unmatched note-ons with a Warning diagnostic
   - drops zero-duration notes with a Warning
6. **GM channel 10 → Drums.** Any track whose notes arrive on MIDI channel 10 is auto-tagged `instrument = Drums`. This is the only auto-identification Forge does; it's justified because the General MIDI standard declares channel 10 as percussion.
7. **Empty tracks are dropped.** Their tempo/meter events were already consolidated into the song-level maps.
8. **Default tempo/meter.** If the MIDI had no tempo or no meter events at all, fill in `{0, 120 BPM}` / `{0, 4/4}`.

The importer does **no** assembly logic, no transpose, no fold. It's a faithful parser whose output is a raw `Song` with one track per MIDI track.

---

## 3. Config — loading, validation, writing

Three file formats (JSON, TOML, XML) round-trip through three parsers and three serialisers. The principle: **permissive load, strict validate, strict write**.

### Loading — `Source/Core/ConfigLoader.{h,cpp}`

Entry points:

```cpp
std::string loadConfigFromFile(const std::string& path,
                               Config& out,
                               Diagnostics& migrationDiagnostics,
                               std::optional<ConfigFormat> formatOverride = {});

std::string loadConfig(const std::string& content, ConfigFormat, Config&, Diagnostics&);
```

Return value is an empty string on success, a human-readable error message on first failure. All three format-specific loaders (`loadJson`, `loadXml`, `loadToml`) share the same contract:

- **Format detection** is extension-based with `formatOverride` as escape hatch.
- **Source shorthand.** A bare integer `5` in the `sources` array is equivalent to `{midiTrack: 5}`. The full object form carries `transposeSemitones` and `volumePercent`.
- **Missing `midiTrack` is a hard error** — explicitly named in the message, so a user who typed `sources: [{}]` gets told why.
- **Migration warnings.** If a legacy file has `transposeSemitones` or `volumePercent` at the *instrument* level (they moved to sources), the fields are silently ignored but a `Warning` diagnostic lands in `migrationDiagnostics`. Loading proceeds; the user sees what was dropped.

Libraries: `juce::JSON`, `juce::XmlDocument`, and the vendored single-header `toml++`.

### Validation — `Source/Core/Config.{h,cpp}`

```cpp
std::string validateConfig(const Config& cfg, int midiTrackCount);
```

All-at-once, returns the first error it finds:

- `input` must be non-empty.
- ≥ 1 instrument; each has ≥ 1 source.
- `x ≥ 1`; unique across instruments.
- `name` parses via `parseName()` to a known LOTRO instrument.
- Every `source.midiTrackIndex` is `≥ 0` and `< midiTrackCount`, unique within its instrument.
- `volumePercent > -100` (anything at or below -100 would invert or silence the velocity).
- `drumMap` field only valid when `name == "Drums"`.

### Writing — `Source/Core/ConfigWriter.{h,cpp}`

One serialiser per format, shared pattern: **omit anything at its default**. An empty optional, `volumePercent == 0`, `transposeSemitones == 0`, `x == 0` — none of those get written. This keeps saved configs readable and lets the permissive loader fill defaults on re-read.

```cpp
std::string writeConfigToFile(const std::string& path, ConfigFormat, const Config&);
```

---

## 4. Instrument assembly

**File:** `Source/Core/InstrumentAssembly.{h,cpp}`

```cpp
Song assembleInstruments(const Song& raw, const Config&, Diagnostics&);
```

Takes the raw `Song` (one track per MIDI track) + the validated `Config` and produces an assembled `Song` whose tracks correspond 1-to-1 with `Config.instruments` — possibly merging multiple raw sources into one assembled track.

Algorithm per instrument:

1. Copy song-level metadata (`ticksPerQuarter`, `tempoMap`, `meterMap`, `drumMap`), override title / transcriber / first-tempo from `Config` if set.
2. Resolve the instrument's `name` via `parseName`. Set `t.x`, `t.instrument`.
3. **Label fallback chain:** explicit `inst.label` → first source's MIDI-track name → LOTRO enum name.
4. For each `ConfigSource`:
   - look up the raw track by `midiTrackIndex`
   - propagate channel-10 drum detection onto the merged track
   - compute `totalTranspose = src.transposeSemitones + config.transpose` and `volumeScale = 1.0 + src.volumePercent/100.0`
   - for each note: add `totalTranspose` to `pitch`; if `volumeScale ≠ 1.0`, scale velocity with clamp to `[1, 127]` and emit a `VolumeScale` Warning on overflow
5. `std::stable_sort` all gathered notes by `startTick` (the merge point — stable so same-tick notes keep their per-source order).
6. If `instrument == Drums` and `inst.drumMap` is set: `loadDrumMapFromFile(path, dm)` over a fresh `defaultDrumMap()`; attach to `track.drumMap`. Failures become `InstrumentAssembly` Warnings and the song-level default is used.

Any MIDI track not referenced by any `ConfigSource` emits an `Info` diagnostic so you know what was skipped.

**No fold here.** The per-instrument octave fold (§5.1) runs in `RangeConstraint`, after assembly. Assembly just sums transposes.

---

## 5. The constraint pipeline

**File:** `Source/Core/Pipeline.{h,cpp}`

```cpp
void runPipeline(Song& song, Diagnostics&);
```

Per enabled track, in order:

```
Range → Chord → Duration → Tempo → Collision → Dynamic
```

Then once per song: `applyTempoCollapseToSongMaps(song)` rescales the `tempoMap` and `meterMap` tick columns into the same stream-tick space the notes now live in.

**Order is load-bearing.** Each pass depends on invariants the previous ones established. Every pass annotates its diagnostics with `trackIndex` after it runs.

There's also a synthesis entry point:

```cpp
Config synthesiseConfig(const Song& raw, const std::string& input, const std::string& output,
                        std::optional<double> tempo, int transpose,
                        const std::map<int, LotroInstrument>& overrides);
```

Used by the no-config CLI path and by the GUI on fresh MIDI load. Every non-drum track defaults to `LuteOfAges` (no heuristic guessing — see CLAUDE.md's guiding principle); channel-10 tracks stay `Drums`; `overrides` let the CLI replace individual instruments.

### 5.1 `RangeConstraint`

**File:** `Source/Core/Constraints/RangeConstraint.{h,cpp}`

Fold each note into the instrument's native 36-semitone MIDI range by whole-octave shifts, preserving pitch class:

```cpp
bool foldInto(int& pitch, int low, int high) noexcept {
    while (pitch < low  && pitch + 12 <= high) pitch += 12;
    while (pitch > high && pitch - 12 >= low)  pitch -= 12;
    return pitch >= low && pitch <= high;
}
```

- Skips Drums entirely.
- Uses `rangeFor(track.instrument).midiLow/midiHigh` directly — **no** intersection with any narrower "ABC envelope". The letter shift happens in `AbcWriter`, not here.
- 36 semitones is always wide enough that the fold converges for any integer pitch; a convergence failure would emit a defensive `RangeConstraint` Warning and drop the note, but in practice never triggers.

### 5.2 `ChordConstraint`

**File:** `Source/Core/Constraints/ChordConstraint.{h,cpp}`

Enforces the LOTRO 6-note-per-chord cap.

```cpp
// group by startTick
// for each group exceeding 6 notes: sort by velocity DESC, pitch DESC, keep top 6
// emit aggregate Warning naming original/kept counts
// re-sort kept notes ascending by pitch so chord tokens render low-to-high
```

### 5.3 `DurationConstraint`

**File:** `Source/Core/Constraints/DurationConstraint.{h,cpp}`

Drops notes with `durationTicks <= 0`. Silent — no diagnostic. Near-no-op in practice; the importer already filters these, this is defensive.

### 5.4 `TempoCollapse`

**File:** `Source/Core/Constraints/TempoCollapse.{h,cpp}`

The heart of the pipeline. LOTRO's parser honours only the *first* `Q:` and `M:` in a part, so mid-song tempo/meter changes have to be **baked into the tick math** under a single fixed main tempo.

Core function:

```cpp
int scaleTickToMainTempo(int originalTick,
                         const std::vector<TempoChange>& tempoMap,
                         double mainBpm) noexcept;
```

Walks the tempo map, scaling each segment from `(segStart → segEnd)` by `(mainBpm / segBpm)` and summing — so a segment at half the main tempo occupies twice the stream-space ticks, preserving perceived duration.

Applied per track:

1. For each note, look up the local BPM at its original start tick.
2. Rescale `durationTicks` by `mainBpm / localBpm`.
3. Rescale `startTick` via `scaleTickToMainTempo(...)`.
4. If rescaling would round a note down to 0 ticks, clamp to 1 and emit a `TempoCollapse` Warning.

Then `applyTempoCollapseToSongMaps` snapshots the original `tempoMap`, rescales `meterMap[i].tick` and `tempoMap[i].tick` in place against that snapshot. The snapshot is essential — mutating in place would corrupt the scaling of later entries.

Net effect: after this pass, all note timings, all bar boundaries, and all tempo-change ticks agree on a single stream-tick timeline under `main BPM`. The ABC can safely emit one `Q:` and one `M:` in the header, and the `ChordEmitter` can use `meterMap` entries to know where bar lines fall even though LOTRO will ignore the later ones.

### 5.5 `CollisionGuard`

**File:** `Source/Core/Constraints/CollisionGuard.{h,cpp}`

Same-pitch overlaps confuse LOTRO. For each pitch: sort by `startTick`; if note `i` ends after note `i+1` starts, trim note `i`'s duration to `(next.start - this.start - 1)` tick. If that's `≤ 0`, drop note `i` with a Warning. Different pitches never interact here.

### 5.6 `DynamicMapper`

**File:** `Source/Core/Constraints/DynamicMapper.{h,cpp}`

Maps velocity → `ppp..fff` buckets (roughly 16 units per bucket across `[0, 127]`). Because LOTRO allows only *one* dynamic marking per tick on an instrument, the mapper picks the loudest velocity in each start-tick cluster and buckets that. It records only *changes* — `track.dynamicChanges` holds the transitions, which `AbcWriter` emits as `+mf+` style annotations. The initial implicit state is `mf`.

See `findings/dynamics.md` for why "loudest per tick" is the load-bearing policy and not a workaround.

---

## 6. ABC writing — emission model

**File:** `Source/Core/AbcWriter.{h,cpp}`

### 6.1 The cluster-at-boundary / z-pulse trick

ABC is fundamentally monophonic with chord tokens; LOTRO ignores `V:` voices. Representing polyphony in one part requires encoding overlap some other way. The technique below is **Vydor's 2011 z-pulse encoding** ("Vydor" is the LOTRO handle of Forge's author; the `rideintochetwood.abc` reference output in the repo root is from that period and pins this invariant via `BarAlignment_tests.cpp`):

- Walk notes by **start-tick cluster** (all notes with the same `startTick` become one emission unit).
- Each cluster emits one chord token `[n1dur1 n2dur2 ...]` where **every inner element carries its own duration**.
- The chord's **stream advance** is the *shortest* inner element — longer voices keep ringing while subsequent tokens play on top. No ties needed.
- When a held voice extends past the next cluster's start, a `z` rest lives *inside the chord brackets* as the **pulse** element. Non-standard ABC, but LOTRO accepts it.
- The z-pulse's duration is **capped at the remainder of the current bar** — so `% bar N` comments stay aligned to absolute stream ticks, and each bar's tokens sum to exactly one bar length. The underlying held notes keep their full ring duration; the cap affects only the chord's advance.

### 6.2 Per-instrument pitch shift

LOTRO ABC notation is always `C, .. c'` (3 octaves) regardless of which instrument plays it — LOTRO applies its own per-instrument pitch shift on playback so `C,` sounds at MIDI 24 on Theorbo, 36 on Lute, 48 on Clarinet, 60 on Flute.

Before the letter/octave math, `emitNotePart` shifts:

```cpp
const int shiftedPitch = note.pitch - rangeFor(instrument).midiLow + 48;
```

Letter math then runs on `shiftedPitch` in the standard ABC convention (MIDI 48 = `C,`, 60 = `C`, 72 = `c`, 84 = `c'`). The output always lands in `[C, .. c']` — no `,,` or `''` ever appears, whatever the instrument.

### 6.3 Pitch → letter → token

```cpp
const char* letterFor(int pitchClass, bool& needsSharp) noexcept;   // C/D/E/F/G/A/B + sharp flag

std::string emitNotePart(const Note&, LotroInstrument, bool isDrum, const DrumMap&);
```

For pitched notes: pitch class (0..11 via `shifted % 12`) → letter; `octaveIndex = floor((shifted - 60) / 12)` → commas (`<= 0`) or apostrophes (`>= 1`) plus lowercase-vs-uppercase casing.

For drums: `drumMap.lookup(note.pitch)` → ABC drum-slot token (e.g. `"F"`, `"^c"`). Unmapped pitches return empty string and are dropped from the emission.

`abcDurationToken(durationTicks, ppq)` turns ticks into the ABC suffix at the fixed `L:1/8` unit, reducing via GCD: integer multiples print plain, unit-fractions print `/N`, general fractions print `N/M`.

### 6.4 Clustering and cluster emission

```cpp
std::vector<NoteGroup> groupByStartTick(const std::vector<Note>&);
```

O(n) single pass. Each group carries `startTick` and the *shortest* `durationTicks` in the cluster (the natural chord advance).

```cpp
ClusterEmission buildCluster(const NoteGroup&, const Track&, int advanceNeeded,
                             int ticksPerQuarter, const DrumMap&);
```

Emits each inner element as `letter + durationToken`; if the longest note in the cluster extends past `advanceNeeded`, inserts a `z{advanceNeeded}` pulse inside the brackets; returns a `ClusterEmission { chord, chordAdvance, fillerTicks, totalAdvance }`. The caller (`ChordEmitter`) decides what `advanceNeeded` is — usually the distance to the next cluster, but capped at the bar boundary.

### 6.5 `ChordEmitter` — the stream state machine

Anonymous-namespace class that owns the emit-loop state:

- `body` — the accumulating ABC text.
- `streamTick` — how far the stream has advanced in the output.
- `nextBarTick` — stream tick where the next bar begins.
- `barNumber` — sequential counter for `% bar N` comments.
- `reportedTempoIdx` / `reportedMeterIdx` — cursors into the song-level maps so mid-song `% tempo: N bpm` / `% meter: n/d` annotations are emitted exactly once, at the bar they take effect.

Key methods:

- `emitDynamic(marking)` → appends `+marking+`.
- `emitRest(ticks)` → chunks at bar boundaries; each chunk becomes `z{dur}` plus a `flushBarBreaks()` check.
- `emitCluster(emission)` → appends the chord, advances by `chordAdvance`, then emits any `fillerTicks` as rests.
- `flushBarBreaks()` → when `streamTick` hits `nextBarTick`: newline, bump `barNumber`, emit `% bar N`, look up the next bar length from `meterMap` at the new bar start (**meter-aware**), call `emitChangesInRange(newBarStart, nextBarTick)` to drop any `% tempo:` / `% meter:` annotations that fall in the new bar.
- `barRemainderFrom(fromTick)` → used by `emitBody` to cap chord advance at the bar line.

### 6.6 Top-level walk — `emitBody` and `writeAbc`

`emitBody(song, track)`:

1. `groupByStartTick` the track's notes.
2. Construct a `ChordEmitter` with the song's maps.
3. For each group, in order:
   a. Emit any pending dynamic changes whose tick ≤ the group's tick.
   b. If the emitter has fallen behind, emit a rest to bridge the gap.
   c. Compute `advanceNeeded = nextGroup.startTick - group.startTick`.
   d. Cap that at the current bar remainder for the z-pulse (`advanceForChord`). Anything left over becomes a regular rest on the next bar (`advanceAfterChord`).
   e. `buildCluster(...)` with `advanceForChord`, pick the effective drum map (track's own, else song's), `emitCluster(...)`, then `emitRest(advanceAfterChord)`.
4. `emitter.finish()` → return body.

`writeAbc(song)`:

- Header block (`% Generated by ...`).
- Collect enabled, non-empty tracks. `stable_sort` by `x` — explicit-`x` tracks in order, then `x == 0` tracks auto-numbered after the highest explicit one.
- For each: `emitHeader(song, track, x)` (`X: T: Z: L:1/8 Q: M: K:`) + `emitBody(song, track)` + blank line.

### 6.7 Public pitch helpers

- `abcPitchToken(int midiPitch)` — standard ABC semantics; equivalent to passing `LotroInstrument::Clarinet` (identity shift). Kept so test code doesn't need to know about the shift.
- `abcPitchToken(int midiPitch, LotroInstrument)` — per-instrument shift.
- `abcDurationToken(int, int)` — as described in 6.3.

---

## 7. Diagnostics

The system tracks problems out-of-band rather than failing fast, because the user can usually live with a few dropped notes in a long song. Every pass that changes anything emits a `Diagnostic`:

| Source tag | Emitted by | Severity | What it means |
|---|---|---|---|
| `MidiImporter` | `importMidi` | Warning | unmatched note-on; zero-length note dropped |
| `InstrumentAssembly` | `assembleInstruments` | Info / Warning | unreferenced MIDI track; drum-map load failure |
| `VolumeScale` | `InstrumentAssembly` | Warning | velocity clamped at 1 or 127 after scale |
| `RangeConstraint` | `applyRangeConstraint` | Warning | note couldn't fold into instrument range (defensive) |
| `ChordConstraint` | `applyChordConstraint` | Warning | chord trimmed from N to 6 notes at tick |
| `TempoCollapse` | `applyTempoCollapse` | Warning | duration rounded to zero under rescale |
| `CollisionGuard` | `applyCollisionGuard` | Warning | same-pitch note dropped (no room after trim) |
| `Pipeline` | `runPipeline` | back-fills `trackIndex` only |

CLI `-v` prints them to stderr via `formatDiagnostic(d)`. The GUI shows them in a 6-column table.

---

## 8. CLI

### Command shape

```
forge [OPTIONS] INPUT.mid [OUTPUT.abc]
  --config PATH            Load a config file (JSON/TOML/XML)
  --config-format FMT      Override format detection (json|toml|xml)
  --instrument N=NAME      Assign instrument to track N  (config-less mode only)
  --tempo BPM              Override detected main tempo
  --transpose N            Global semitone transpose     (stacks onto config)
  --drum-map PATH          Load drum-map JSON, merged onto defaults
  --list-tracks            Print track table and exit
  --list-instruments       Print valid instrument NAME values and exit
  -v, --verbose            Log Diagnostics to stderr
  -h, --help               Print this help and exit
```

### Mode split

- **Config mode** (`--config` given): loads the file, applies `--tempo` / `--transpose` as additive overrides, resolves `input` / `output` paths *relative to the config file's directory* if they weren't absolute, `validateConfig`s, and rejects any `--instrument N=NAME` flags as inconsistent with config-driven conversion.
- **Ad-hoc mode** (no `--config`): collects `--instrument N=NAME` flags into a `std::map<int, LotroInstrument>` and passes them to `synthesiseConfig(raw, input, output, tempo, transpose, overrides)`.

### Main flow — `Source/Main.cpp`

```
parseCli → CliOptions
   ↓
--help / --list-instruments → print + exit
   ↓
open INPUT.mid, importMidi → Song
   ↓
--drum-map → DrumMapLoader merges onto song.drumMap
   ↓
--list-tracks → print + exit
   ↓
Config mode: loadConfigFromFile + migrationDiags + validateConfig
Ad-hoc mode: synthesiseConfig
   ↓
assembleInstruments → assembled Song
   ↓
runPipeline
   ↓
writeAbc → string
   ↓
write output file; print diagnostics if -v; return 0 / 1 (MIDI I/O) / 2 (config)
```

`DrumMapLoader.cpp` parses a flat JSON object `{"<gm-pitch>": "<abc-token>", ...}` (keys starting with `_` are treated as comments). Merge semantics mean you only specify the pitches you want to override.

---

## 9. GUI (`forge_ui`)

Everything under `Source/UI/`. Uses JUCE modules `juce_gui_basics` and `juce_gui_extra` on top of the Core library — the Core stays JUCE-free at its public surface.

### 9.1 App entry — `UiMain.cpp`

`UiApp : juce::JUCEApplication`. Minimal: create a `MainWindow` on `initialise`, null it on `shutdown`, `allowsMoreThanOneInstance = true`.

### 9.2 `MainWindow` — `Source/UI/MainWindow.{h,cpp}`

`juce::DocumentWindow`, implements `MenuBarModel` (File menu) and `FileDragAndDropTarget`. Responsibilities:

- **Non-native title bar** (`setUsingNativeTitleBar(false)`) — fixes a WSLg drift-on-focus issue.
- **File menu**: Open MIDI (Ctrl+O), Open Config (Ctrl+Shift+O), Save Config As (JSON/TOML/XML submenu), Save ABC As, Quit.
- **Drag-drop** — `.mid`/`.midi` routed to Open MIDI, `.json`/`.toml`/`.xml` to Open Config.
- **Body**: an inner `Body` class owning the outer splitter between `EditorPane` (left) and `DiagnosticsPane` (right). Splitter has `setInterceptsMouseClicks(false, true)` so drags hit the bar but clicks pass through to the children.
- **State**: `lastAbc` (populated by Run Converter, consumed by Save ABC).
- **Run**: `runConversion()` = `validateConfig → assembleInstruments → runPipeline → writeAbc → DiagnosticsPane.show(diags, abc)`.

### 9.3 `EditorPane` — `Source/UI/EditorPane.{h,cpp}`

Owns the mutable `Config` and the (effectively immutable) raw `Song`. Child layout:

- `InstrumentsTree` — top 40%.
- `PropertyPageHost` — middle/lower.
- Run Converter button — bottom.

Callbacks: `onConfigChanged` (wired to refresh the ABC preview / etc.), `onRunRequested` (wired to `MainWindow::runConversion`). `loadFromMidi(newRaw, newCfg)` swaps state, rebuilds the tree, forces the Song property page, enables the Run button if any tracks were imported.

### 9.4 `InstrumentsTree` — `Source/UI/InstrumentsTree.{h,cpp}`

`juce::TreeView` + three nested `TreeViewItem` subclasses. Invariant pinned at the top of the `.cpp`: every stored index (`instrumentIdx`, `sourceIdx`) is only valid for that item's lifetime — any `config.instruments` mutation MUST call `rebuild()` so a fresh item tree is constructed.

- **`SongItem`** (root; always one). Label = `config.title` if set, else input filename stem, else `(no MIDI loaded)`, drawn bold. Right-click menu: `Add Instrument`; `Clear All Instruments` (disabled when empty — opens a confirmation dialog).
- **`InstrumentItem`**. Label = `X:N  NAME — "label"`. Lazy-populates `SourceItem` children on first expand via `itemOpennessChanged`. Right-click: `Add Source ▸` submenu listing unused raw MIDI tracks, separator, `Delete Instrument`.
- **`SourceItem`**. Label = `MIDI N: trackname (chan C, notecount)`. Right-click: `Delete Source`.

Left-click on any item fires `notifySelection(Kind, instrumentIdx, sourceIdx)` which `EditorPane` wires to `PropertyPageHost::showFor(...)`. Every mutation fires `notifyMutation()` and then `rebuild()`.

### 9.5 `PropertyPageHost` — `Source/UI/PropertyPageHost.{h,cpp}`

A page switcher with three pre-instantiated child components. `showFor(Kind, iIdx, sIdx)` toggles visibility and calls `editInstrument` / `editSource` / `refresh` on the newly-visible page. `refresh()` refreshes all three (used after external mutations). `resized()` assigns `getLocalBounds()` to all three so only the visible one actually paints.

### 9.6 Property pages

Each is a `juce::Component` + `juce::TextEditor::Listener` that pushes every keystroke straight into `Config`.

- **`SongPropertyPage`** (`Source/UI/SongPropertyPage.{h,cpp}`). Six rows: Input MIDI (read-only label), Output ABC (read-only label), Title (text), Transcriber (text), Tempo BPM (numeric), Global transpose semitones (numeric).
- **`InstrumentPropertyPage`** (`Source/UI/InstrumentPropertyPage.{h,cpp}`). X: index (numeric), Name (dropdown populated from `allInstrumentNames()`), Label (text), Drum map (text + Browse button; enabled only when Name == Drums).
- **`SourcePropertyPage`** (`Source/UI/SourcePropertyPage.{h,cpp}`). MIDI track (read-only — shows index + name + channel + note count), Transpose semitones (numeric), Volume % (numeric).

### 9.7 `DiagnosticsPane` — `Source/UI/DiagnosticsPane.{h,cpp}`

Right half of the main splitter. Inner `Body` class owns the horizontal splitter between the list (top ~50%) and the preview (bottom).

- **`DiagnosticListView`** — `juce::TableListBox` with 6 columns: Severity (coloured dot + text — blue Info / orange Warning / red Error), Source, Tick, Pitch, Track, Message. `--` for unset locator fields. Empty state message when nothing to show.
- **`AbcPreviewView`** — read-only `juce::TextEditor` + grey status-line label ("`5,824 bytes · 184 bars · 3 parts`").

`DiagnosticsPane::show(Diagnostics, std::string abc)` updates both views in one go.

---

## 10. End-to-end data flow

### CLI mode

```
argv
  │
  ▼
parseCli                                         [Source/Cli/CliOptions.cpp]
  │
  ▼
importMidi                                        [Source/Core/MidiImporter.cpp]
  │        raw lotro::Song
  ▼
(loadConfigFromFile + validateConfig)   OR   synthesiseConfig
  │        Config
  ▼
assembleInstruments                               [Source/Core/InstrumentAssembly.cpp]
  │        assembled lotro::Song
  ▼
runPipeline                                       [Source/Core/Pipeline.cpp]
  │        Range → Chord → Duration → Tempo → Collision → Dynamic
  │        + applyTempoCollapseToSongMaps
  ▼
writeAbc                                          [Source/Core/AbcWriter.cpp]
  │        std::string (ABC text)
  ▼
write to file; print Diagnostics to stderr if -v
```

### GUI mode

```
drop .mid                 drop .json/.toml/.xml         File menu
   │                             │                           │
   ▼                             ▼                           ▼
MainWindow::openMidiFromPath   openConfigFromPath         (various)
   │                             │
   ▼                             ▼
importMidi + synthesiseConfig   loadConfigFromFile + validateConfig
   │                             │
   └─────────────┬───────────────┘
                 ▼
EditorPane::loadFromMidi(Song, Config)
                 │
                 ▼
InstrumentsTree.rebuild()  ─► Song/Instrument/Source items
                 │
       user edits via tree context menus / property pages
                 │  every edit mutates Config in place, fires
                 │  notifyMutation() or onConfigChanged()
                 ▼
Run Converter button
                 │
                 ▼
validateConfig + assembleInstruments + runPipeline + writeAbc
                 │
                 ▼
DiagnosticsPane.show(diagnostics, abc)
```

---

## Cross-reference — where to look

| If you're looking for…                                         | File(s)                                         |
|----------------------------------------------------------------|-------------------------------------------------|
| The data types everything flows through                        | `Source/Core/{Note,Track,Song,Config,Diagnostics}.h` |
| How MIDI bytes become a `Song`                                 | `Source/Core/MidiImporter.cpp`                  |
| Config shape, loaders, writers, validation                     | `Source/Core/Config{,Loader,Writer}.{h,cpp}`    |
| How the user's Config turns one+ MIDI tracks into one LOTRO part | `Source/Core/InstrumentAssembly.cpp`            |
| What the pipeline does and why (+ `synthesiseConfig`)          | `Source/Core/Pipeline.cpp`                      |
| Per-constraint behaviour                                       | `Source/Core/Constraints/*.cpp`                 |
| The `Q:`/`M:` bake-in math                                     | `Source/Core/Constraints/TempoCollapse.cpp`     |
| Cluster-at-boundary, z-pulse, bar labels, per-instrument shift | `Source/Core/AbcWriter.cpp` (top-of-file comment and `ChordEmitter` class) |
| CLI flags, `--config` vs ad-hoc, drum-map loader               | `Source/Cli/*.cpp`, `Source/Main.cpp`           |
| GUI layout, tree interaction, property pages                   | `Source/UI/*.cpp`, `docs/UI_GUIDE.md`           |
| Guiding principle ("MIDI is truth, no auto")                   | `CLAUDE.md` → *Guiding principle*               |

See also `docs/UI_GUIDE.md` for a reference of the GUI's named regions, field → Config-path table, and right-click context menus.
