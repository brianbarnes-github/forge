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

## Repository layout

```
Source/
├── Core/            forge_core — JUCE-free public API (see §1-§7)
│   └── Constraints/  the six pipeline passes (see §5)
├── Cli/              CliOptions (arg parser), DrumMapLoader
├── UI/               forge_ui — JUCE GUI (see §9)
└── Main.cpp          CLI entry point (see §8)
```

Only `Source/Core/MidiImporter.cpp` touches `juce::MidiFile` in production
(`Source/UI/JuceNoteReplica` mirrors its note pairing without using it, and
is test-checked against JUCE). Songsmith additionally reads files losslessly
through the JUCE-free `Source/UI/RawMidi`, §9.7. Every
constraint takes a `Track&` and mutates it in place. All `Source/Core`
public headers are `juce::`-free (verified: `grep juce Source/Core/*.h
Source/Core/**/*.h` returns nothing).

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
    int                           sourceProgram      = 0;     // first Program Change; informational
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

**File:** `Source/Core/MidiImporter.{h,cpp}`. The only *conversion* reader, and the only place in production code that includes `juce_audio_basics` / `juce::MidiFile`. Songsmith also reads each file losslessly through `Source/UI/RawMidi` (JUCE-free, §9.7) so its document can be exported back to MIDI; `importMidi` still produces the `Song` that conversion sees.

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

Full schema for the `--config` file format:
`docs/superpowers/specs/2026-04-23-config-driven-conversion-design.md`.

### Defaults

- Instrument per track defaults to `LuteOfAges` for every non-drum
  track. The converter does **not** auto-pick based on note ranges —
  the MIDI-is-source-of-truth principle (see `CLAUDE.md`) means we
  don't second-guess the song writer. The user is expected to pick the
  real instrument via `--instrument N=NAME` (CLI) or the Instrument
  property page dropdown (GUI).
- MIDI channel-10 tracks are imported as `Drums` in `MidiImporter`
  because General MIDI declares channel 10 as percussion; this is
  reading the MIDI, not a converter heuristic.
- Output path defaults to `<input-stem>.abc` next to the input.
- Drum mappings default to the spec §2.6 + extended-GM-percussion set
  in `defaultDrumMap()`. `--drum-map` merges overrides on top; unlisted
  pitches keep their defaults. See `docs/REFERENCE.md`.

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

Everything under `Source/UI/`. Uses JUCE modules `juce_gui_basics`, `juce_gui_extra`, and `juce_data_structures` (the last for Songsmith's `ValueTree`-based `SongDocument`) on top of the Core library — the Core stays JUCE-free at its public surface.

### 9.1 App entry — `UiMain.cpp`

`UiApp : juce::JUCEApplication`. Creates a `MainWindow` on `initialise` and opens the first existing `.songsmith` command-line argument via `openSongOnStartup`; `systemRequestedQuit` routes to `MainWindow::requestQuit()` (guarded); nulls the window on `shutdown`; `moreThanOneInstanceAllowed()` returns true.

### 9.2 `MainWindow` — `Source/UI/MainWindow.{h,cpp}`

`juce::DocumentWindow`, implements `MenuBarModel` (File/Edit/Song/Transport/View/Help menus) and `FileDragAndDropTarget`. Owns the single `SongDocument songDocument` for the process and a `SongSession session` (§9.13); each `importMidiFile` call is passed `songDocument.mintImportBatch()` (the counter lives on `SONG`, so it survives Save/Open). Responsibilities:

- **Non-native title bar** (`setUsingNativeTitleBar(false)`) — fixes a WSLg drift-on-focus issue.
- **Remembered placement** — owns a `juce::PropertiesFile` (`SongSmith/SongSmith.settings` in the OS per-user settings folder, e.g. `%APPDATA%` on Windows). The destructor (reached by every quit path) saves `getWindowStateAsString()` under `mainWindowPlacement`; the constructor restores it through `WindowPlacement` (`Source/UI/WindowPlacement.{h,cpp}`) before `setVisible`, re-applying maximised afterwards. `WindowPlacement::resolve` keeps the saved bounds only while the title bar is still grabbable (≥ 100 px of its 26 px strip on some display's user area); otherwise — e.g. closed on a monitor that is no longer connected — it re-centres on the primary display, shrinking to fit. Nothing saved yet → centred on the primary display as before.
- **File menu**: New (Ctrl+N), Open... (Ctrl+O), Save (Ctrl+S), Save As... (Ctrl+Shift+S), Import ▸ MIDI... (options dialog when Preferences ▸ Import is Ask, §9.17), Export ▸ MIDI... / ABC..., Preferences... (§9.16), Quit (Ctrl+Q). The menu bar's structure (labels, shortcut hints, order, enabled/ticked state) is plain data in header-only `MenuModel.h` (`buildMenus(MenuState)` and the `MenuCommandId` enum, pinned by `MenuModel_tests.cpp`); `MainWindow::currentMenuState()` gathers the live state and `getMenuForIndex` converts the description to a `juce::PopupMenu`. The shortcut hints are Windows/Linux-style `Ctrl`; `keyPressed` handles the five global shortcuts (N/O/S/Shift+S/Q) with `commandModifier`, routing through `menuItemSelected` so they share the guard. The Song flows (New/Open/Save/Save As, the unsaved-changes guard, the title) are described in §9.13. Export MIDI... is enabled once the song has anything besides its conductor (`getNumTracks() > 1`); `exportMidiAs()` opens a save `FileChooser` whose default name is `defaultExportFile(session.getFile(), ".mid", Documents)` (the Song's file with the extension swapped, else `Untitled.mid`; never the imported MIDI's name), appends `.mid` when the name has neither `.mid` nor `.midi` and asks before replacing an existing file (our own confirmation, run after the extension is appended; skipped when "Ask before replacing" is off in Preferences, §9.16), builds `writeMidiBytes(buildRawMidiFile(songDocument))` in full before touching the destination, and shows an "Export MIDI failed" message box on a `MidiExportError` or a failed write. Export ABC... (`saveAbcAs()`, default `<song>.abc`) is enabled once `runConversion()` has populated `lastAbc`. "Open Config" / "Save Config As" no longer exist.
- **Edit menu**: Undo/Redo (`canUndo()`/`canRedo()`), Split (`SongsmithMainComponent::splitAtMarker()`, no pointer; enabled by `TrackListComponent::canSplitAtMarker()`, which shares `anySplittable` with `splitSections`), Delete (`deleteSections()`, enabled by `hasSelectedSections()`), Select All (`selectAll()`; with the pointer over the menu it falls to `selectAllHeads`), Select All Tracks / Sections, Quantize, Grid Size. Enabled-ness needs no explicit `menuItemsChanged()` poke elsewhere: `getMenuForIndex` is called fresh every time a menu opens.
- **Transport menu**: Play/Pause (the Space key goes through the same command), Stop, Go to Start / End, Rewind One Bar, Clear Marker (enabled when `getMarkerTick()` has a value), all forwarding to `PlaybackController`.
- **Song menu**: "Default parts from tracks" (→ `synthesiseDefaultParts(songDocument)`) and "Run Converter" (→ `runConversion()`, then shows the export panel).
- **View menu**: one checkable item, "Export ABC panel" — toggles `Body`'s visible content between the Songsmith view and the export panel (`ViewExportPanelToggle` calls `body->setExportPanelVisible(!body->isExportPanelVisible())` and `menuItemsChanged()` so the checkmark repaints immediately on next open). There is only one editing surface now (Songsmith) — this toggle is between it and the export panel, not between two editors.
- **Help menu**: "About..." → `showAboutDialog(this)` (`Source/UI/AboutBox.{h,cpp}`), a modal `DialogWindow` hosting `AboutComponent`: app name, creator, `formatVersionLine`/`formatBuildLine` of `currentBuildInfo()` (`Source/UI/BuildInfo.{h,cpp}`). The build values come from `generated/ForgeBuildInfo.h` in the build tree, which the `forge_build_info` custom target rewrites on every build (`cmake/GenerateBuildInfo.cmake`, only when a value changed) — the build number is `git rev-list --count HEAD` plus the 7-char short hash and a dirty flag, so a CI build and a local build of the same commit report the same number. Only `BuildInfo.cpp` includes the generated header, so a new commit recompiles one file.
- **Drag-drop** — `.mid`/`.midi` routed to Import ▸ MIDI (`openMidiFromPath`), which (showing the import options dialog first when Preferences ▸ Import is Ask, §9.17) imports into `songDocument` via `importMidiFile` and shows the returned `Diagnostics` in the Songsmith view's own `DiagnosticListView` (§9.10) — not the export panel's `DiagnosticListView` (§9.9). `.songsmith` routed to `requestOpenSong` (behind the unsaved-changes guard, §9.13). Anything else is not accepted.
- **Command line** — `UiApp::initialise` opens the first existing `*.songsmith` argument via `MainWindow::openSongOnStartup` (no guard: nothing is open yet). `UiApp::systemRequestedQuit` and the window's close button both call `MainWindow::requestQuit()`, which runs the guard and then `JUCEApplication::quit()`.
- **Body**: an inner `Body` class holding both `SongsmithMainComponent` and the export panel (`DiagnosticsPane`) as permanent children (`addChildComponent`) — `setExportPanelVisible(bool)` only toggles which one is visible, so toggling never reparents anything. Songsmith is visible by default.
- **State**: `lastAbc` (populated by Run Converter, consumed by Export ▸ ABC… (`saveAbcAs`)).
- **Run**: `runConversion()` builds the full-export `Config` (`buildConfigAndRawSong(songDocument)`, no `partIds` filter), drops any zero-assignment part via `SongModelBridge::dropUnassignedInstruments` (§9.8) — warning once per drop rather than failing the whole export — then runs `validateConfig → assembleInstruments → runPipeline → writeAbc → DiagnosticsPane.show(diags, abc)`. This is the only conversion path; the scoped live-preview path (§9.8/9.12) never calls `validateConfig`.

### 9.3–9.6 Historical: the deleted classic Config-editing UI

Earlier phases had a second, form/tree-based "classic" editor —
`EditorPane` (owning the mutable `Config` + raw `Song`), `InstrumentsTree`
(a `juce::TreeView` over `Config.instruments`), `PropertyPageHost` + three
property pages (`SongPropertyPage`/`InstrumentPropertyPage`/
`SourcePropertyPage`, each pushing keystrokes straight into `Config`) —
toggleable alongside Songsmith via the View menu. All of it (plus its
CMake entries) was **deleted** at the end of Phase 6, per the Songsmith
plan's decision, once `SongsmithMainComponent` covered everything it did.
These classes no longer exist in `Source/UI/`; this section is kept only
as a pointer for anyone reading old commit history or the Songsmith plan's
early phases, not as documentation of current code.

### 9.7 `SongDocument` — `Source/UI/SongDocument.{h,cpp}`

Songsmith's editable-document data model — a `juce::ValueTree` rooted at a `SONG` node plus the single owned `juce::UndoManager` for the document. Schema: `SONG` (title/transcriber/tempoBpm/globalTranspose/inputMidiPath) → `SOURCE_MIDI` (ticksPerQuarter) → `MIDI_TRACK[]` (trackId/name/colorArgb/sourceMidiChannel/sourceProgram/importBatch/endTick/defaultChannel, plus `isConductor` on the conductor) → `NOTES` → `NOTE[]` (pitch/startTick/durationTicks/velocity/isDrum/sourceTrackIndex/sourceEventIndex, named identically to `lotro::Note`'s fields, plus the MIDI-fidelity fields below) and `MIDI_TRACK` → `EVENTS` → `EVENT[]` (tick/order/relocatedFrom/data); `SongDocument::getNotesNode(track)`/`getEventsNode(track)` return the two containers; `SONG` → `PARTS` → `PART[]` (partId/x/instrumentName/label/drumMapPath) → `ASSIGNMENT[]` (trackId reference/transposeSemitones/volumePercent/rangePolicy); and `SONG` → `TEMPO_MAP` → `TEMPO_CHANGE[]` (tick/bpm), `SONG` → `METER_MAP` → `METER_CHANGE[]` (tick/numerator/denominator) — both siblings of `SOURCE_MIDI`/`PARTS`, not children of `SOURCE_MIDI` (whose children must stay homogeneous `MIDI_TRACK` nodes, since `getNumTracks()`/`getTrack(index)` do raw `getNumChildren()`/`getChild(index)` over it).

**Lossless MIDI model (2026-10-03 MIDI-fidelity spec).** The document keeps every imported MIDI event so File → Export MIDI can reproduce the file event-for-event. Notes live under a `NOTES` container; every non-note event (controllers, program changes, pitch bend, SysEx, track-name and other metas, ...) is an `EVENT` under the track's `EVENTS` container, with `data` a `juce::MemoryBlock` of the raw bytes (`FF <type> <data>` for metas, `F0`/`F7` + data for SysEx, explicit status byte for channel messages), `tick`, `order` (its index within its source raw track) and, when it was moved into the conductor, `relocatedFrom` (the raw track it came from). Extra `NOTE` properties: `channel` (1..16), `offVelocity`, `offIsNoteOnZero` (off written as note-on velocity 0), and, for imported notes only, `onOrder`/`offOrder` (raw indices of the note-on/off) and `offSynthesized` (JUCE invented the note-off because the note-on was ended by a same-key re-strike or never closed). Extra `MIDI_TRACK` properties: `endTick` (End-of-Track tick) and `defaultChannel` (channel for notes created in the editor).

**The conductor.** Every `SongDocument` is constructed with exactly one conductor, `SOURCE_MIDI` child 0: a `MIDI_TRACK` with `isConductor = true`, name `"Conductor"`, `sourceMidiChannel = 0`, `colorArgb = 0`, and empty `NOTES`/`EVENTS` (created before any import; not undoable, since it is part of the empty document). On the first import (or any import with tempo replace, §9.17) it receives the file's song-wide events (tempo, time signature, key signature, SMPTE offset, marker, copyright — `isSongWideMetaEvent`): either the file's own conductor track (format 1, first track without a note-on of velocity > 0, `hasConductorTrack`) or, if the file has none, those events relocated out of the note tracks (marked with `relocatedFrom`). `getNumTracks()` therefore counts the conductor; "nothing imported yet" is `getNumTracks() <= 1`. `SongDocument::isAssignableTrack(track)` is true only for a non-conductor `MIDI_TRACK` with at least one note — note-less tracks (e.g. a track holding only controllers) exist in the document and export but cannot be dragged onto a part.

`Source/UI/RawMidi.{h,cpp}` is the JUCE-free lossless Standard MIDI File reader/writer (`readMidiFile`/`readMidiBytes` -> `RawMidiFile{format, ticksPerQuarter, tracks[]{events[]{tick, bytes}, endTick}}`; `writeMidiBytes`/`writeMidiFile`). It deliberately mirrors `juce::MidiFile::readFrom`'s track structure (RIFF wrapper, non-`MTrk` chunks consuming a track slot, meta/SysEx not cancelling running status) so raw track indices line up with `importMidi`'s `sourceTrackIndex`; the writer emits every status byte explicitly and one End-of-Track per track at `max(endTick, last event tick)`. `Source/UI/JuceNoteReplica.{h,cpp}` replicates, on raw events, exactly how `juce::MidiFile` pairs notes (the per-tick note-on/off reordering and the invented note-offs) so each `Song` note can be tied back to the raw events it came from; it is tested differentially against JUCE itself. `Source/UI/MidiImportPlan.{h,cpp}` (`planMidiImport`) is pure planning over `importMidi`'s `Song` and the `RawMidiFile` of the same bytes: it decides the conductor's contents, each note's `PlannedNoteLink` (channel, `onOrder`/`offOrder`, `offSynthesized`, off velocity/note-on-zero form) and which raw events become `EVENT` nodes; it never touches a `SongDocument`.

*Import walkthrough:* `importMidiFile` (§9.8) runs both `importMidi` and `readMidiBytes` on the same bytes -> `planMidiImport` -> `appendImportedMidi` writes the planned tracks, notes, events and (on the first import, or a tempo replace) the conductor into the document. *Export walkthrough:* `buildRawMidiFile(const SongDocument&)` (`Source/UI/MidiExport.{h,cpp}`) -> `writeMidiBytes` -> the file. Export always writes format 1, conductor first, then every other `MIDI_TRACK` in document order; parts/assignments never affect it. Per track, events and notes are merged and sorted by tick, then group (new note-offs, then original material in original `order`, then new note-ons), so an unedited import comes back exactly and edited/new notes sit after a tick's original events. Velocity is clamped to 1..127, pitch to 0..127, channel to 1..16. An `offSynthesized` note's off is skipped (JUCE invented it) unless the note-on that ended it on import is gone from that tick, in which case a real note-off is written. The destination file is written only after the full byte buffer is built, so a `MidiExportError` leaves an existing file untouched.

*Timing edits:* moving, resizing or quantizing a note (`SourceRollEditor::markTimingEdited`) removes its `onOrder`/`offOrder` and clears `offSynthesized` in the same undo transaction, so the note exports as a new note-on/off at its new ticks rather than at its original raw position.

`trackId`/`partId` are synthetic, monotonically-increasing `int64`s minted from two independent counters stored as hidden properties on the root `SONG` node — never array indices, never reused (counter increments bypass the `UndoManager` on purpose, so undoing a mint can't roll the counter back into a reissuable state). `PART.x`, by contrast, is a positional ABC `X:`-index minted as `(max existing PART.x) + 1` at add-time (or `1` when there are no parts) — expected to be renumbered across removals, unlike the stable synthetic ids. It is deliberately not `getNumParts() + 1`: that scheme mints a duplicate `x` after an add/remove/add sequence (add A/B/C → x 1/2/3; remove B; add D → `getNumParts() + 1 == 3`, colliding with C's still-live `x`), which `validateConfig` rejects (Phase 4 whole-branch review finding I4).

`title`/`transcriber`/`tempoBpm` are left **unset** on a freshly-constructed document rather than defaulted, mirroring `Config.title`/`Config.transcriber`/`Config.tempo`'s `std::optional` semantics — this is what lets the Phase 2 translation layer (`SongModelBridge`, §9.8) tell "no override, fall back to the imported MIDI's own value" apart from "explicitly set".

Every mutation helper (`addTrack`/`removeTrack`/`addPart`/`removePart`/`addAssignment`/`removeAssignment`/`setProperty`) opens one `UndoManager` transaction per call, so one call is one undo step. Two exceptions pass a `nullptr` `UndoManager`: the static `appendChildBulk`, and `addTrackBulk` — both used by `SongModelBridge`'s MIDI-import path (§9.8), where per-note/per-track undo isn't a meaningful user gesture. `addPart`/`addAssignment` take a trailing `bool newTransaction = true` (mirroring `setProperty`'s parameter of the same name): pass `false` to batch the call into the caller's already-open transaction instead of starting a new one — used by `synthesiseDefaultParts` (§9.8) to make an arbitrary number of part/assignment additions collapse into a single undo step.

`SongDocument::findAssignment(part, trackId)` locates the `ASSIGNMENT` on a `PART` referencing a given `trackId`, or an invalid tree if none exists. `SongDocument::assignTrackToPart(partId, trackId)` is the dedup'd, undoable entry point drag-and-drop assignment uses: it creates one `ASSIGNMENT` (transpose 0, volume 0, `"octaveShift"`) and returns `true`, unless the part doesn't exist, the track doesn't exist, or `trackId` is already assigned to that part — any of those three leaves the document untouched (no undo transaction opened) and returns `false`. This exists because `forge_core`'s `validateConfig` rejects a part with two sources naming the same MIDI track index, so the UI must never be able to construct one.

### 9.8 `SongModelBridge` — `Source/UI/SongModelBridge.{h,cpp}`

The translation layer between `SongDocument`'s `ValueTree` and `forge_core`'s plain `Config`/`Song` structs — the only place in `Source/UI/` that touches both. Four free functions in namespace `lotro`:

- **`appendImportedSong(SongDocument&, const Song& imported, int importBatch, Diagnostics&)`** — the import path. `diagnostics` is a required reference (Phase 4: previously an optional pointer defaulting to `nullptr`, since diagnostic collection on the import path is no longer something a caller can silently opt out of). Appends `imported`'s tracks (via non-undoable `addTrackBulk`/`appendChildBulk`) as new document state, minting fresh `trackId`s and copying `Note` fields verbatim; each new `MIDI_TRACK` gets its `sourceProgram` copied and `colorArgb = SongsmithColours::trackColourFor(gmFamilyFor(sourceProgram, sourceMidiChannel), n)`, where `n` counts tracks of that GM family already in the *document* (so a second import continues each family's 4-shade cycle rather than restarting it). Tracks accumulate across multiple imports rather than clearing — a second import's tracks are appended alongside the first's. Only the *first* import — defined as an empty `TEMPO_MAP` (`doc.getTempoMapNode().getNumChildren() == 0`), not "zero tracks": `importMidi` always seeds a non-empty tempo map (defaulting to `{0, 120.0}` when the source file has none), so this stays a reliable "no import has landed yet" signal even when a first file's every track was silent and dropped — sets the document's `ticksPerQuarter` and populates `TEMPO_MAP`/`METER_MAP`; concatenating two files' timelines end-to-end isn't meaningful (`TempoCollapse` assumes one sorted timeline), so a later import never appends to either map.

  **Time-base reconciliation (Phase 4, Ruling A-R3).** When a later import's `ticksPerQuarter` differs from the document's, the bridge now prefers *raising* the document's time base over the old lossy downscale: `newPpq = lcm(docPpq, importedPpq)` (via `std::lcm` on `long long`). If `newPpq <= 15360` (16 × 960 — every realistic MIDI PPQ, 96..1920, stays far below this) **and** `newPpq > docPpq`, the document is raised: every existing `NOTE.startTick`/`durationTicks` and every `TEMPO_CHANGE.tick`/`METER_CHANGE.tick` is multiplied by the exact integer `newPpq / docPpq` (`nullptr` `UndoManager` — a bulk time-base change, not a user gesture), `SOURCE_MIDI.ticksPerQuarter` becomes `newPpq`, and `doc.getUndoManager().clearUndoHistory()` is called — recorded undo steps hold pre-rescale tick values and would corrupt the document if replayed. The incoming import's own ticks are then rescaled by `newPpq / importedPpq` (also an exact integer, by construction of the LCM), so **both sides rescale exactly**; one `Severity::Info` Diagnostic is appended: `"Raised document time base from <docPpq> to <newPpq> PPQ (<N> existing note(s) rescaled)"` — no `Warning` is possible on this path, since nothing rounds. If the LCM is over the cap, or if `newPpq == docPpq` (the incoming PPQ divides the document's exactly), no raise happens and the bridge falls back to the pre-Phase-4 behavior: every incoming note's `startTick`/`durationTicks` is rescaled via `std::lround(tick * (double) docPpq / importedPpq)` (pitch/velocity/isDrum/provenance untouched); a lossy downscale can round a nonzero `durationTicks` all the way to 0, and the bridge never clamps/invents a duration to avoid that (a musical decision it is not allowed to make) — `DurationConstraint` silently drops such notes later in the pipeline. A rescaled import that rescaled at least one note or one event tick/`endTick` value appends one Diagnostic (`source = "SongModelBridge"`, wording `"Rescaled imported MIDI ticks from <importedPpq> to the document's <docPpq> PPQ"`): `Severity::Info` if the rescale was exact for every value and no note was zeroed, `Severity::Warning` otherwise, naming the rounded-value count and, if any, the zeroed-note count. Same-PPQ imports and imports where nothing was rescaled emit no rescale Diagnostic.

  **Tempo/meter timeline mismatch (Ruling A-R4).** Independently of the rescale diagnostic above, if a later import's tempo map (after the same rescaling) differs from the document's, one `Severity::Warning` Diagnostic names both sides' *first* `TEMPO_CHANGE`: `"Imported file's tempo map differs from the document's (file starts at <bpm> BPM, document at <bpm> BPM); its tracks will play at the document's tempo"`. If only the meter map differs (the tempo maps are identical), the wording instead says `"Imported file's meter map differs from the document's (file starts at <n>/<d>, document at <n>/<d>); its tracks will play at the document's tempo"`. Identical maps emit nothing; if both differ, the tempo wording takes precedence. There is no dialog, no time-scaling of the incoming tracks (a musical transformation the MIDI-is-truth principle forbids), and no per-track badge — the document's timeline always wins, this Diagnostic only names what's inconsistent.
- **`appendImportedMidi(SongDocument&, const Song&, const RawMidiFile&, int importBatch, Diagnostics&, const Diagnostics& importerDiagnostics = {}, const ImportOptions& options = {})`** — the raw-aware import used by `importMidiFile`. Calls `planMidiImport`; on `MidiImportPlanError` (format 2, or the two parsers disagreeing) appends one `Error` Diagnostic, leaves the document unchanged and returns `false`. Otherwise `importerDiagnostics` are appended with `trackIndex` remapped to the document row (the `SOURCE_MIDI` child index, since `importMidi`'s index counts only tracks with notes), then the plan's diagnostics (`Info`: "Merged N MIDI tracks" when tracks are merged; events moved into the conductor; events dropped; "Replaced the Song's tempo map…" on a later tempo replace), then the bridge's own. Only the first import writes the conductor, unless `options.tempo` is `replace` (§9.17), which lets a later import write it and replace the Song's; otherwise a later import's song-wide metas/conductor are dropped with an `Info` Diagnostic (the first import's conductor is kept), while its per-track `EVENT` ticks and `endTick` are PPQ-rescaled exactly like its notes. When the LCM time-base raise (above) fires, the document's existing `EVENT` ticks, every track's `endTick` (conductor included) and every stored `SECTION` `startTick`/`endTick` are rescaled by the same integer factor as the notes. `appendImportedSong` remains the `Song`-only variant (no events/conductor data).
- **`importMidiFile(SongDocument&, const juce::File& midiFile, int importBatch, Diagnostics&, const ImportOptions& options = {})`** — opens `midiFile`, runs `importMidi` (`sourceName` = the file's stem, matching the CLI's ad-hoc path in `Source/Main.cpp`) and `readMidiBytes` on the same bytes, and appends the result via `appendImportedMidi`. Sets `SONG.inputMidiPath` only if it is currently empty, so the *first* imported file's path (and thus its bridge-derived `rawSong.title`) always wins over later imports'. Never touches the `UndoManager`. Returns `false` (and appends one `Severity::Error` Diagnostic, document left unchanged) if the file can't be opened, if it opens but `importMidi` throws `lotro::MidiImportError` for content that doesn't parse (empty buffer, malformed data, unsupported SMPTE time format), if `readMidiBytes` fails, if the file is format 2, or if the planner finds the two parsers disagree (`MidiImportPlanError`, surfaced through `appendImportedMidi`) — the exception is caught at this boundary rather than escaping to the caller, matching the project's Diagnostics-as-error-channel convention.
- **`synthesiseDefaultParts(SongDocument&)`** (Phase 4, Ruling A-R1) — default-part synthesis, promoted from Phase 3 test-only scaffolding into production code. For every assignable `MIDI_TRACK` (`SongDocument::isAssignableTrack`: not the conductor, at least one note) not yet referenced by any `ASSIGNMENT` on any `PART`, adds one `PART` (`instrumentName = displayName(LotroInstrument::Drums)` when the track's `sourceMidiChannel == 10`, else `displayName(LotroInstrument::LuteOfAges)`; empty `label`) with one `ASSIGNMENT` (that track, transpose 0, volume 0, `"octaveShift"`). Tracks already referenced anywhere are skipped, so this is safe to call on a partially-arranged document. It is user-initiated, so it IS undoable — but the whole call is exactly ONE undo transaction: it calls `doc.getUndoManager().beginNewTransaction()` once up front and passes `newTransaction = false` to every `addPart`/`addAssignment` call thereafter, so one `doc.undo()` removes every part/assignment it added.
- **`buildConfigAndRawSong(const SongDocument&, const std::vector<juce::int64>& partIds = {})`** — the export path. Builds a `Config` and a flattened raw `Song` together, in lockstep, since `Config.midiTrackIndex` is positional into the raw `Song.tracks` it returns — walks `SOURCE_MIDI`'s children in order to establish that positional index space — skipping the conductor and any note-less track no `ASSIGNMENT` references (the CLI's `importMidi` never produces such tracks, and one would add an "unreferenced track" diagnostic the CLI never emits), so the index is the position in the *raw song*, not the `SOURCE_MIDI` child index — then resolves each `ASSIGNMENT.trackId` reference against it (a dangling reference to a removed track is silently skipped, not asserted). An empty `partIds` exports every `PART`; a non-empty list filters to that subset (used later for scoped live preview). `SONG`'s override properties (`title`/`transcriber`/`tempoBpm`) use the `hasProperty` gate described above; `PART`'s always-present `label`/`drumMapPath` instead map empty-string to `std::nullopt`, since those two properties are never absent, only possibly empty. `ASSIGNMENT.rangePolicy` is intentionally not translated into `ConfigSource` — `forge_core` offers only octave-fold range handling in v1 and has no matching `Config` field, so the property is written by `SongDocument` for future use but has nothing to become on the export side yet.

`Source/UI/SongsmithColours.h` (Phase 4, Ruling A-R5) is a header-only, JUCE-GUI-free palette: plain `constexpr juce::uint32` ARGB values for the Songsmith Arch doc's "Theming" section (background/panel-header/selected-row/border/text/muted/accent-amber/warning-red) plus the track-colour-by-instrument-family scheme: `GmFamily` (the 16 GM program families + `Drums`), `gmFamilyFor(program, midiChannel)`, a 17-entry `familyBaseColour` table, and `trackColourFor(family, indexWithinFamily)` cycling 4 shades of each base. It deliberately does not include `juce_gui_basics`, since `SongModelBridge.cpp` (which calls `trackColourFor` on every track import) is compiled into `forge_tests` — that constraint held even after Phase 5 (below) added `juce_gui_basics` to `forge_tests`' link set (for headless `TrackListComponent`/`TrackRowComponent` tests): `SongsmithColours.h` itself still doesn't need it, so it stays `juce_core`-only on principle.

### 9.9 `DiagnosticsPane` — `Source/UI/DiagnosticsPane.{h,cpp}`

The toggleable export panel: one of `MainWindow::Body`'s two permanent children (see §9.2), shown in place of `SongsmithMainComponent` by `Song → Run Converter` or `View → Export ABC panel`, and populated by `runConversion()`. `SongsmithMainComponent` does not use this component for its own (import-only) diagnostics — see §9.10, which hosts a bare `DiagnosticListView` instead. `DiagnosticsPane`'s own inner `Body` class owns the horizontal splitter between the list (top ~50%) and the preview (bottom).

- **`DiagnosticListView`** — `juce::TableListBox` with 6 columns: Severity (coloured dot + text — blue Info / orange Warning / red Error), Source, Tick, Pitch, Track, Message. `--` for unset locator fields. Empty state message when nothing to show.
- **`AbcPreviewView`** — read-only `juce::TextEditor` + grey status-line label ("`5,824 bytes · 184 bars · 3 parts`").

`DiagnosticsPane::show(Diagnostics, std::string abc)` updates both views in one go.

### 9.10 Songsmith part-strip UI — `Source/UI/{TrackRowComponent,TrackListComponent,AssignmentChipComponent,PartSlotComponent,PartStripComponent,SongsmithMainComponent}.{h,cpp}`

Phase 4's UI layer: six new `juce`-GUI components, all namespace `lotro`, that read/write `SongDocument` directly (no intermediate view-model) and rebuild themselves from `juce::ValueTree::Listener` callbacks rather than being pushed updates. Palette throughout: `Source/UI/SongsmithColours.h` (§9.8).

- **`TrackRowComponent`** — one row per `MIDI_TRACK`. The left `trackInfoWidth` (200 px, the 2 px column divider included) is a `TrackHeadComponent` child (see the next bullet); the row paints only its dividers, selected background, accent bar and instrument band. The old `"<n> notes · <lo>–<hi>"` line, `muteSoloWidth` and `buildSecondLine` are gone. `muteButtonForTesting()` / `soloButtonForTesting()` return `juce::Button&` and delegate to the head. Selected rows get a 3px left accent bar in the track's own swatch colour plus the shared `selectedRow` background. A 1px `trackDivider` line spans each row's bottom edge (the embedded `TrackNotePreview` is inset above it), and the preview draws notes in the track's `colorArgb`. `mouseDown` fires `onTrackSelected(trackId)`; `mouseDrag` (past a 4px threshold, so a plain click never also starts a zero-distance drag, and only for assignable tracks — `canDrag()`; the conductor and note-less rows neither drag nor react to double-click) starts a `juce::DragAndDropContainer` drag whose payload `var` is the synthetic `trackId` (never the row index — `TrackListComponent` rebuilds rows from scratch on every change, so an index would go stale mid-drag).
- **`TrackHeadComponent`** (`Source/UI/TrackHeadComponent.{h,cpp}`) — the two-row head: index, swatch and name on row 1; drawn mute / solo `IconButton`s (`juce::Path`, no assets) and a 0–100 `juce::Slider` on row 2. `layoutFor(bounds, conductor)` is the pure geometry; the conductor gets index and name only. The head is mouse-transparent itself, so the row keeps selection, drag-to-part and double-click; only the child controls and the swatch/right-click handlers react. It takes just the track node and display index and reports edits through callbacks (`onMuteToggled`, `onSoloToggled`, `onVolumeChanged(percent, startsGesture)`, `onRenamed`, `onColourChanged(argb, startsGesture)`), which the row forwards with the track id and `TrackListComponent` turns into undoable `SongDocument` writes. **Swatch picker:** a click opens a `ColourSelector` in a `CallOutBox`; the first change of a picker session has `startsGesture` true and later ones do not, so a session is one undo step, and dismissing without a change records nothing (the head dismisses the callout on destruction). **Rename:** right-click shows `buildContextMenu()` (the single place items are added; **Rename…** is disabled on the conductor); the inline `TextEditor` commits on Enter, cancels on Escape or focus loss, and `commitRename` trims and fires `onRenamed` only for a non-empty, changed name; the editor is deleted on the next message-loop turn so it can be closed from its own callbacks. **Volume:** `startsGesture` is true for the first change of a drag and for every change made outside a drag (keyboard, double-click reset to 100), so a drag is one undo step and each lone change its own. The slider shows `NN%` in its own popup bubble while dragging (`setPopupDisplayEnabled`, text suffix `%`; no `TooltipWindow`) and has its wheel disabled (`setScrollWheelEnabled (false)`), so a wheel over it falls through the parent chain to the list's `WheelViewport`. Known limit: an open rename editor does not update on undo.
- **`TrackListComponent`** — a `juce::Viewport` over a vertical stack of `TrackRowComponent`s, rebuilt from `doc.getSourceMidiNode()` on any child add/remove/reorder/property-change (`juce::ValueTree::Listener`), except that a `name`, `colorArgb` or `playbackVolume` change on a `MIDI_TRACK` calls `refreshHead(trackId)` and refreshes that row's head in place with no rebuild (a rebuild would destroy the slider, picker or rename editor mid-gesture). Volume edits write `playbackVolume` (removed when set to 100, so the default stays implicit); colour and name edits write `colorArgb` / `name`; all through `doc.setProperty` with the head's `startsGesture` as `newTransaction`. A `name` or `colorArgb` change (edit, undo or redo; not `playbackVolume`) also fires `onTrackAppearanceChanged(trackId)`: `SongsmithMainComponent` forwards it to `PartStripComponent::trackAppearanceChanged()` (a repaint, since the strip listens only to `PARTS` and its chips read the track colour at paint time) and to an open `TrackEditorWindow::trackAppearanceChanged(trackId)` (retitles the window for its own track, repaints the roll, whose notes and ghosts use track colours). Holds the selected `trackId` as transient UI state — never written to the document. **Wheel routing.** `juce::Viewport::mouseWheelMove` consumes every wheel event while its vertical scrollbar shows, so the list's own `mouseWheelMove` used to see (and zoom on) them only when there was none. The viewport is now a private `WheelViewport` that maps each event to list coordinates and calls the one decision function `handleWheel(WheelRegion, mods, wheel, pointerY)` (region = `Heads` when x is inside the left `notePreviewOriginX()` column, else `Canvas`); `TrackListComponent::mouseWheelMove`, reached from the ruler, calls it as `Canvas`. Ctrl/Cmd resizes the rows (`resizeRowsBy`: 4 px per notch on an unrounded `rowHeightExact` so touchpad sub-pixel deltas accumulate, clamped to `TrackRowComponent::minRowHeight`=30 / `maxRowHeight`=120 via the shared `WheelResize.h` helpers (`accumulateClamped`, `anchoredScroll`), then `applyRowHeight` re-sizes `content` and re-lays the rows, and the viewport Y is adjusted so the same fraction of the content stays under the pointer); Shift scrolls the timeline horizontally; a plain wheel zooms about the marker over the canvas and scrolls the rows (`scrollRowsBy`, 50 px per notch, no-op when they fit) over the heads. The row height is session state in the list (`rowHeight`, mirrored into `ListContent::rowHeight`), used by `rebuild()`/`resized()`/`ListContent::resized()`; rows derive their layout from their own bounds. **Instrument band.** Each row is `rowHeight + TrackRowComponent::instrumentBandHeight` (16 px) tall: the band is extra height across the top of the canvas side (painted by the row in `SongsmithColours::instrumentBand`, the note preview sits beneath it), labelled by `TrackRowComponent::instrumentLabel()` — the General MIDI name of `sourceProgram` (`GmProgramNames.h`), "Drum Kit" on channel 10, empty for the conductor. `getRowHeight()` and the Ctrl+wheel clamps stay the notes-area height; the list's row pitch is `rowHeight + instrumentBandHeight`.
- **`AssignmentChipComponent`** — one `ASSIGNMENT` inside a `PartSlotComponent`'s body: swatch (the referenced track's `colorArgb`, resolved via `doc.findTrackById`), `"Tk<n>"` (n = that track's current 0-based `SOURCE_MIDI` child index (the conductor is 0 and never assigned, so chips start at `Tk1`), re-resolved on every paint so it can't go stale), monospace transpose (`"+0"`/`"−12"`, U+2212 minus for negatives), and an `×` `juce::TextButton` that calls `SongDocument::removeAssignment`. Dashed border in the track's colour, matching the mockup.
- **`PartSlotComponent`** — one `PART`: header (`x` index, monospace muted; an instrument badge; the label) over a wrapping row of `AssignmentChipComponent`s, or a dashed "drop here" placeholder when empty. Selected → 2px amber top border + lighter fill. A `juce::DragAndDropTarget`: `isInterestedInDragSource` accepts only an `int64` `var` that resolves via `doc.findTrackById`; `itemDragEnter`/`itemDragExit` toggle a highlight fill; `itemDropped` calls `doc.assignTrackToPart(partId, trackId)` and returns immediately after — dedup is the document's job (`assignTrackToPart` silently no-ops a repeat drop of the same track onto the same slot), and the resulting `ASSIGNMENT` add fires `PartStripComponent`'s listener, which rebuilds the whole strip and destroys this slot, so nothing may touch `this` after that call. Right-click opens a `juce::PopupMenu`: an "Instrument" submenu (every `allInstrumentNames()` entry, current one ticked) writing `instrumentName` via `doc.setProperty`; "Rename…" (async `juce::AlertWindow` text prompt → `label`); "Remove part" (`doc.removePart`) — the same just-mutated/component-destroyed caveat applies to every branch.
- **`PartStripComponent`** — a horizontal strip of `PartSlotComponent`s, rebuilt from `doc.getPartsNode()` on any subtree change (a `PART` add/remove, or an `ASSIGNMENT` add/remove/property-change underneath one, since chips live under parts). Header bar: `"PARTS · DROP TRACKS TO ASSIGN"`, panel-header colour, uppercase with extra letter-spacing, matching the mockup. Trailing "+ Add" button calls `doc.addPart(displayName(LotroInstrument::LuteOfAges), "")` and auto-selects the new slot.
- **`SongsmithMainComponent`** — the top-level Songsmith view: a `juce::Component` + `juce::DragAndDropContainer` (the drag source/target root for the whole view) split top/bottom by a user-resizable `SplitterComponent` (§9.11) into an `UpperRegion` (header label `"▲ MIDI SOURCE · drag tracks down to assign"`, `TrackListComponent` fixed 220px left, `PianoRollComponent` — §9.11 — filling the remainder) and a `LowerRegion` (`PartStripComponent` fixed height, then a bare `DiagnosticListView` — deliberately NOT the full `DiagnosticsPane`, §9.9). At the time this list was added (Phase 4), Songsmith had no export path at all, so driving `DiagnosticsPane`'s ABC-preview half with an empty string produced a permanently-visible `"0 bytes · 0 bars · 0 parts"` status line that read as a failed import on every single MIDI import (Phase 4 whole-branch review finding I2) — `MainWindow::openMidiFromPath` calls `getDiagnostics().setDiagnostics(...)` on this list directly instead of `DiagnosticsPane::show`. Phase 6 brought the ABC preview back, but as a separate, toggleable export panel (§9.9) populated by `MainWindow::runConversion()`, not by feeding this list — this list stays import-diagnostics-only by design now, not as a stopgap. `TrackListComponent::onTrackSelected` drives `trackSelected(trackId)`, which repoints a `SourceTrackNoteSource` (owned via `unique_ptr`, rebuilt on every selection) at `sourceRoll` along with the document's current `ticksPerQuarter`/meter map; `rebuild()` re-fires the selection callback on every `SOURCE_MIDI` change (not just when the selected track disappears — Phase 5 whole-branch review finding I1), so the roll always re-fits to the document's current time base, including after a Phase 4 LCM PPQ-raise triggered by a second MIDI import. The Phase 4-era fixed-proportion layout (40% source region / fixed part-strip height / remainder to diagnostics) and its horizontal-only `MainWindow::Body::Splitter` are both gone — see §9.11.

`MainWindow` wires this into the app: see §9.2's drag-drop bullet for how MIDI import reaches `SongsmithMainComponent`'s `DiagnosticListView`, and its View-menu bullet for the export-panel toggle (not a second editing mode — there is only one now).

### 9.11 Songsmith source piano roll — `Source/UI/{PianoRollNoteSource,SourceTrackNoteSource,PianoRollGeometry,PianoRollComponent,SourceRollEditor,GridSize,SplitterComponent}.{h,cpp}`

Phase 5's UI layer: the source-role piano roll (view-only — no note editing), plus a splitter generalized out of Phase 4's `MainWindow`-private one (originally shared with the classic editor's left/right split, since deleted along with it — see §9.3–9.6) so `SongsmithMainComponent` could reuse the same drag-to-resize logic instead of a bespoke one.

- **`PianoRollNoteSource`** (header-only) — the abstract note-source interface the roll paints from, so it never depends on `ValueTree` directly. One `PianoRollNote` per note (`pitch`/`startTick`/`durationTicks`/`colourArgb`, no provenance or velocity — Phase 5 is read-only display, so nothing needs a handle back to a `NOTE` node yet; Phase 6's ghost/dropped-note diff and Phase 7's click-to-edit will need to decide what that handle looks like). Phase 6 adds a second implementation (`PreviewNoteSource`) over a `PreviewResult`; this interface is the seam that lets `PianoRollComponent` stay agnostic to which one it's given.
- **`SourceTrackNoteSource`** — the Phase 5 implementation, wrapping one `MIDI_TRACK` `ValueTree`. Reads `NOTE` children live on every call (no internal cache — matches this project's ValueTree-as-source-of-truth style; v1 tracks don't gain notes after import, so this isn't a performance concern). `isTrackLive()` checks `track.getParent().isValid()` (not `track.isValid()` — `ValueTree::removeChild` orphans a node without invalidating it, so a removed-but-still-referenced tree stays `isValid()==true`) and reports zero notes / a zero-length range once a track is gone, rather than silently keep showing stale content.
- **`PianoRollGeometry`** — pure, `juce_core`-only pixel↔model coordinate math (deliberately no `juce::Rectangle`, which lives in `juce_graphics` and isn't linked into `forge_tests`; `PianoRollNoteBounds` is a plain 4-int struct instead). Fixed keyboard-gutter width and per-semitone row height; `topPitch` (which pitch draws at the top) and `contentOriginTick` (the tick mapped to content-space x at the gutter's right edge) are the two knobs `fitToContent` sets once per track selection to fit the whole track without scrolling. **Scroll position is not tracked here at all — it lives entirely in `PianoRollComponent`'s `juce::Viewport`**; `contentOriginTick` is a fixed content-space origin, not a live "currently visible" window (an earlier revision named it `visibleTickRange` with `get`/`setVisibleTickRange` accessors and a doc comment describing it as a Phase 6 scroll-sync seam — both were wrong, since the setter was never called outside tests and the accessor's `Range::getEnd()` was never read anywhere; renamed to avoid a future implementer wiring real scroll state through it and getting double-scrolling). `isBlackKey(pitch)` (the 5 pitch classes {1,3,6,8,10}) drives the roll's row-band shading — plain semitone-parity (`pitch % 2`) disagrees with the real keyboard at the E/F and B/C boundaries, where two white keys are adjacent.
- **`PianoRollComponent`** — the shared canvas for both roles (a `Role` enum selects between them). A `juce::Viewport` wraps an inner `Canvas` component (row bands, bar-boundary gridlines from the document's first `METER_MAP` entry plus whole-to-1/64 note-value divisions (pure `computeGridLines` in `GridLines.{h,cpp}`, drawn by `paintGridLines` in `GridLinePaint.h`, also used by `TrackNotePreview`; a division shows once its lines are ≥ `minGridLinePixels` = 8 apart, each tick at its coarsest level, white at a per-level alpha), note rectangles in the track's colour) for two-axis scroll; a separate `Gutter` component is added *outside* the viewport's scrollable content, pinned at the left edge and kept vertically in sync via a `ScrollAwareViewport::visibleAreaChanged` override (JUCE's `Viewport` has no listener interface — subclassing and overriding this is the documented mechanism) — this is what keeps the keyboard labels visible while horizontal scroll moves notes underneath them. The gutter is mouse-transparent (`setInterceptsMouseClicks(false, false)`) and its bounds trim for the viewport's horizontal scrollbar when one is showing, so it doesn't paint over the scrollbar thumb. Wheel handling: `Canvas::mouseWheelMove` -> `wheelScroll` (thin: derives `WheelRegion` = `Gutter` when the pointer's x in this component's own coordinates is left of the gutter width, else `Canvas`, which works because the gutter is mouse-transparent and the Canvas is the deepest handler, so the `Viewport` never sees the event) -> the one decision function `handleWheel(region, mods, wheel, pointerY)`, which returns false only for Preview's plain wheel (left to the viewport's own scroll). `Role::Source` follows the same model as `TrackListComponent` but for pitch rows: plain wheel over the `Canvas` zooms (`zoom` adjusts `pixelsPerQuarterNote`, clamped `[2, 400]`) after centring the start marker in the visible notes (no marker: about the current middle of the view); plain wheel over the `Gutter` scrolls vertically (50 px per notch, no-op when the range fits, never zooms); Shift+wheel pans horizontally; Ctrl/Cmd+wheel anywhere resizes the pitch rows (`resizeRowsBy`: 1 px per notch on an unrounded `rowHeightExact`, clamped to `minRowHeight`=10 / `maxRowHeight`=40, default 14; the min keeps the 9 pt gutter labels legible; pushed into `geometry` and `sourceEditor->setGeometry`, content re-sized, viewport Y adjusted so the same pitch stays under the pointer). The accumulate/clamp and pointer-anchor arithmetic is shared with the track list in `WheelResize.h`. The row height lives in the component (`rowHeight`), not in `geometry`, because `fitTimeline`/`setNoteSource` replace the whole geometry (`fitToContent` knows only the default) and re-apply it each time; it is session-only, per instance, and never affects Preview. `Role::Preview` keeps the original mapping (Ctrl/Cmd+wheel zooms, a plain wheel falls through to the `Viewport`'s own scroll handling) pending a decision for the LOTRO canvas. `Role::Source` pins its layout pitch range to the full MIDI range `[0, 128)` (`effectivePitchRange`), centres the viewport vertically on the track's own notes after `setNoteSource` (middle C when empty), and keeps a `timelineFitted` flag — set by `setNoteSource`, cleared by wheel `zoom` — under which `resized()` re-runs the horizontal fit. For both roles the canvas is sized to the viewport's scroll-bar-adjusted visible area (`visibleSizeFor`), not its full bounds, so a canvas that fits doesn't overhang by a scroll bar's thickness and summon the other bar; the gutter is re-laid out after every content resize since zoom alone can toggle the horizontal bar. `Role::Preview` has done real work since Phase 6 (range-band/ghost/dropped-note overlays, §9.9's diff-driven display) — it does not "do nothing."
- **`SourceRollEditor`** (Phase 7) — all mouse/keyboard-gesture note-editing logic for `Role::Source`, zero JUCE painting (pure `juce_data_structures`/`juce_events`, unit-tested directly against a hand-built `ValueTree`). `PianoRollComponent` owns one only when constructed with `Role::Source` and a non-null `SongDocument*`; its `Canvas` forwards `mouseDown`/`mouseDoubleClick`/`mouseDrag`/`mouseUp`/`keyPressed` to it through one delegation point (`handleEditor*` in `PianoRollComponent`), which calls `afterEditorGesture` to repaint/resize the canvas when a gesture changed anything. Owns selection state (click/shift-click/rubber-band), in-progress drag-gesture state, and every mutation's undo-transaction boundary — one gesture (a full drag, or a delete/quantize regardless of selection size) is one `SongDocument::UndoManager` transaction, never one per note. Hit-testing compares a click point directly against each note's already-computed `PianoRollGeometry::noteBounds` rect rather than round-tripping through `tickForX`/`pitchForY`; only placing a new/moved/resized value converts pixel to tick. `GridSize.h` (header-only enum + `gridSizeToTicks()`) is `SongsmithMainComponent`'s toolbar vocabulary translated into the tick count `SourceRollEditor::setGridTicks` consumes for quantize snap size and create's default duration.
- **`SplitterComponent`** — generalized from `MainWindow::Body`'s original private, left/right-only `Splitter` class (drag-to-resize a fraction `[0.15, 0.85]` of the available space along either axis, via a thin bar between two sibling components it positions but doesn't own/parent). Self-enforces `setInterceptsMouseClicks(false, true)` in its own constructor (so the splitter's full-bounds hit area doesn't swallow clicks meant for its siblings). `MainWindow.cpp`'s own `Body` no longer uses a splitter at all — its two children (`SongsmithMainComponent` and the export panel) are shown one-at-a-time, never side-by-side, since the classic editor's left/right split was deleted along with it (§9.3–9.6). `SongsmithMainComponent` uses this class twice: once (`Orientation::topBottom`) between its `UpperRegion` (header + track list + piano roll) and `LowerRegion` (part strip + preview region + diagnostics), and again, inside `LowerRegion`, between the preview region and the diagnostics list (Phase 6) — replacing the Phase 4 fixed 40%-of-height split.

### 9.13 Song file — `Source/UI/{SongFile,SongFileError,SongSession,DiscardGuard}.{h,cpp}`

A Song (the `SONG` tree: source MIDI tracks, notes, events, conductor, parts, assignments, tempo/meter maps) is saved as a binary `.songsmith` file. `Source/UI/SongFile.{h,cpp}`:

```
0   4  magic "SGSM"
4   4  formatVersion (uint32 LE; one number covers container + tree schema; currently 1)
8   8  uncompressedLength (uint64)
16  4  crc32 of the uncompressed payload
20  8  payloadLength (uint64)
28  n  payload: gzip of ValueTree::writeToStream(SONG)
```

`readSongBytes` proves the bytes intact before parsing any tree, in this order: magic (`NotASongFile`), header size (`Truncated`), version 0 (`Corrupt`) or newer than `songFileFormatVersion` (`UnsupportedVersion`), length sanity (`Corrupt`), payload shorter (`Truncated`) or longer (`Corrupt`) than `payloadLength`, gunzip length + crc32 (`ChecksumMismatch`), `ValueTree::readFromStream` (`Corrupt`), then `SongDocument::validateLoaded` (`InvalidStructure`). Besides the id/counter checks, `validateLoaded` rejects damaged SECTION nodes under each track's `SECTIONS`: a child that is not a `SECTION`, a section id below 1, at or above `nextSectionId` (when present) or shared with another section, and a tick range with `startTick < 0` or `endTick <= startTick`. Overlapping sections are allowed. All are thrown as `SongFileError` (`SongFileError.h`) with a plain-English message shown verbatim in the "Could not open Song" dialog; an unreadable file is `NotASongFile`. `saveSongFile` builds the whole buffer first, then `File::replaceWithData` (sibling temp file + atomic replace), so a failed save leaves any existing file intact. There is no migration chain yet (only version 1). The undo history is never saved.

`SongDocument::replaceContents(loaded)` validates, then restores **in place**: `SONG`'s properties are replaced and the `SOURCE_MIDI`/`PARTS`/`TEMPO_MAP`/`METER_MAP` node objects are kept while their properties and children are replaced, non-undoably, then undo history is cleared. In-place is deliberate: `TrackListComponent`, `PartStripComponent`, `SongSession` and others hold `ValueTree` listener handles on those nodes, which a swapped-in root would orphan. It throws before touching anything if the tree is invalid. `resetToEmpty()` (New) is `replaceContents` of a fresh document, so counters reset too.

**Track playback volume.** A `MIDI_TRACK` may carry `playbackVolume` (`SongIDs::playbackVolume`, int 0–100; deliberately not `volumePercent`, which is the LOTRO offset on `ASSIGNMENT`). Absent reads as 100 (`trackPlaybackVolume(track)`), so older files load unchanged with no format-version bump, and the property is removed rather than written when set to 100. `validateLoaded` rejects a non-integer or out-of-range value with `InvalidStructure` ("a track volume is out of range."). It is an audition aid only: it never reaches the MIDI or ABC export, and mute / solo remain session-only.

`nextTrackId`, `nextPartId` and `nextImportBatch` are persisted on `SONG` (`validateLoaded` requires all three), so ids and import-batch numbers minted after a load never collide with loaded ones; `mintImportBatch()` replaced `MainWindow`'s old private counter.

`SongSession` (`SongSession.{h,cpp}`) holds the open file (empty = untitled) and the dirty flag. It listens on a persistent `ValueTree` handle to the root and sets dirty on **any** property/child change, including non-undoable imports, so undoing back to the saved state still reads dirty. `markClean(file)` (after save/load) and `markNew()` (New) clear it; `onChanged` fires only when the state flips. `displayTitle()` is `<name>[*] — Songsmith`. `withExtensionIfMissing` and `defaultExportFile` (Save As / export default names) live here too.

`DiscardGuard` (`confirmDiscardChanges(dirty, hooks, onProceed)`) is a callback chain with two async hooks, `prompt` (Save / Don't Save / Cancel) and `save`; it proceeds immediately when clean, after Don't Save, or after a successful save, and never on Cancel or a failed/cancelled save. `MainWindow` supplies native-dialog hooks (guarded by `SafePointer`, since Quit can outlive the window) and routes New, Open, `.songsmith` drops and quit through it.

After a Song is replaced (`MainWindow::afterDocumentReplaced`): `SongsmithMainComponent::documentReplaced()` closes the Track editor window, clears the ghosted-track set, both selections, the preview-part selection and refits the timeline; then the diagnostics list is cleared, `lastAbc` is dropped, the export panel is emptied and hidden, and the menus are refreshed. Open also does `session.markClean(file)`; New `session.markNew()`. Save As appends `.songsmith` first and then asks before replacing a different existing file (unless switched off in Preferences, §9.16) (the native chooser's own overwrite warning is off because it would check the pre-extension name).

### 9.14 Playback — `Source/UI/Playback/*` (+ wiring in `MainWindow`, `SongsmithMainComponent`, `TrackListComponent`, `TrackRowComponent`, `TrackEditorWindow`, `PianoRollComponent`)

Playback renders the **source MIDI** (never the LOTRO preview) through a SoundFont (TinySoundFont, vendored at `Source/ThirdParty/tinysoundfont/`, pinned at `853a0a1`). Everything is under `Source/UI/Playback/`; `Source/Core/` is untouched. Design: `docs/superpowers/specs/2026-10-03-songsmith-playback-design.md`.

Units:

- **`PlaybackSnapshot`** (`buildSnapshot(doc)`) — immutable, flattened, **seconds**-timed event list built from the `SONG` tree (plus per-track audible flags written by `MuteSoloState::apply` and per-track gain percents, the only mutable parts). Conductor events are ignored; `SONG.tempoBpm` (the ABC override) is ignored; the tempo comes from `TEMPO_MAP`, 120 BPM before its first entry. `PlaybackError.h` is the custom error type (`SoundFontMissing`, `SoundFontInvalid`, `AudioDeviceUnavailable`).
- **Track gain.** `PlaybackSnapshot` holds a per-track atomic gain (0–100): `buildSnapshot` initialises it from `playbackVolume`, and `setGainPercent` / `gainPercent` read and write it from the message / audio threads like the audible flags. `PlaybackEngine` scales each audible `NoteOn`'s velocity with `scaleVelocity(velocity, gain)` (100 passes through, 0 gives 0, otherwise rounded and at least 1); a result of 0 drops the `NoteOn` (its `NoteOff` still passes). Only velocity is scaled, not CC7 / CC11, so a live gain change needs no controller bookkeeping; it applies to notes starting after the change and sounding notes finish at their old level. `PlaybackController::valueTreePropertyChanged` handles `playbackVolume` on a `MIDI_TRACK` by `applyTrackGain` (look up the track index by id, `setGainPercent`) and returns without scheduling a rebuild, which would cut held notes. Mute / solo still win: an inaudible track stays silent at any gain.
- **`TempoMap`** — tick ↔ seconds over the tempo map; owned by the snapshot.
- **`Transport`** — the one clock: atomic playhead **in seconds**, play/pause/stop/seek, play-start position, seek generation. `advance(from, seconds, end)` is a CAS from the position the engine's block *started* at and is dropped (returns false) if the message thread moved the playhead meanwhile; reaching `end` clears `playing`. `previousBarTick` (Rewind) is a pure helper beside it; Rewind uses the **first** meter-map entry only (the one-meter-timeline convention; 4/4 when there is none).
- **`MuteSoloState`** — session-only flags keyed by `trackId`: solo additive, mute beats solo. Not in the Song, not undoable.
- **`HandOff<T>`** — lock-free publication of immutable objects (snapshots, tsf instances) message → audio thread. Ownership is a per-publish multimap, so republishing the same object is safe; retired pointers go through a fixed FIFO and the **message thread** frees them (`collectRetired`): retired snapshots by the controller's 30 Hz timer, retired synth instances by `MainWindow`'s 2 Hz `SynthGc` timer. `acquire()` is audio-thread only, and its pointer is valid until the next `acquire()`.
- **`EventSink`** — the interface the engine drives (`prepare`, `beginBlock`, `handle`, `releaseChannel`, `releaseAll`, `resetChannel`, `render`); tests substitute a recording sink. Same-tick firing order is the `PlaybackEventKind` enumerator order: Control, Program, PitchBend, NoteOff, NoteOn (so a CC0/CC32 bank select precedes the program change; the per-channel setup Program at tick 0 is prepended before the stable sort and so still precedes a track's own tick-0 program).
- **`PlaybackEngine`** — device-free `renderBlock()`. Chases whenever it must resync: a seek-generation change, a snapshot swap, a sink replacement, or **any playhead position it did not itself leave** (`expectedPosition`; `-1` after a dropped advance or while stopped). A chase is: `releaseAll`, then `resetChannel` for every snapshot channel (tsf keeps channel state for the whole session, so earlier bends, CC7/CC11 fades and sustain would otherwise survive a Stop or seek), then a replay of the Program/Control/PitchBend events before the position. Releases all voices on those events and on pause; notes of a newly-muted track are released per virtual channel.
- **`SynthVoice`** — the TinySoundFont `EventSink` (one `TSF_IMPLEMENTATION` TU). 192 voices; every (track, MIDI channel) pair gets its own **virtual channel** (up to `kMaxVirtualChannels` = 256, assigned in `buildSnapshot`; drum channel 10 → the drum bank), so two tracks on the same MIDI channel never share program/controller state and all 256 are pre-initialised on the message thread when a font instance is built, then handed over via `HandOff`. `releaseAll` releases (tails ring out) rather than cutting. `resetChannel` restores tsf's initial channel state with allocation-free calls: CC121 (volume/expression/pan/RPN/data/pitch range/tuning; also bank 0) plus pitch wheel 8192 and CC64 = 0 (CC121 leaves the wheel and sustain alone); the preset is left to the chase.
- **`PlaybackController`** — message-thread owner: `Transport`, `MuteSoloState`, the engine, the current snapshot, a 30 Hz timer that frees retired objects and notifies listeners of position/state changes. It listens on `SOURCE_MIDI` and `TEMPO_MAP` only; changes are coalesced by an `AsyncUpdater` into one `buildSnapshot` + `publishSnapshot`. Cosmetic `name`/`colorArgb` changes and `PARTS`/assignment changes do **not** rebuild (a rebuild cuts held notes). `play`/`seekToTick`/`goToEnd` flush a pending rebuild first (`flushRebuild`) so they never act on a stale snapshot. `documentReplaced()` (New/Open) resets the transport (`Transport::reset`: stopped, position and play-start 0), clears mute/solo and rebuilds unconditionally. `onBeforePlay` lets `MainWindow` veto Play.
- **`AudioOutput`** — `forge_ui` only: `AudioDeviceManager` + `AudioSourcePlayer` pulling `PlaybackEngine::renderBlock`. Opened lazily on the first Play (so a machine with no audio device can still edit) and destroyed before the controller and synth.
- **`TransportStrip`**, **`TimelineRuler`**, **`PlayheadOverlay`** — the controls (§UI_GUIDE #30, #32, #33). The overlay and ruler take a tick ↔ x function from their owner because the main track canvas and the editor roll zoom/scroll independently. `TimelineRuler` (two rows, 28 px) draws what the pure `computeRulerMarks` (`TimelineRulerMarks.{h,cpp}`) returns for the visible tick range: bar lines (thinned to 1/2/5/10… bars) with bar number + clock time from the `TempoMap`, and beat ticks once ≥ 24 px apart. Hosts supply `setMarks (xForTick, grid)` (the grid, `rulerGridFromDocument`, is rebuilt per paint so tempo/meter edits show) and repaint it on every tick→x change (`TrackListComponent::refreshOverlay`, `PianoRollComponent::onViewChanged`). Both canvases follow the playhead by page-flipping while playing (without dropping fitted mode when they cannot scroll further).
- **Start marker** — `PlaybackController::setMarkerTick`/`clearMarker`/`getMarkerTick` (session-only, in ticks, cleared by `documentReplaced`; `setMarkerTick` is ignored while there is nothing to play — the same `endSeconds() > 0` test as `play()` — so no marker can be set with no MIDI open). `play()` seeks to the marker first, so Play always starts there, including after Pause or Stop (Stop then returns to it, since the play-start is the marker); with no marker Play starts from the playhead. `MarkerOverlay` (in `PlayheadOverlay.h`) draws the amber line down both canvases and is fully mouse-transparent; the line's top continues in the timing bar, where `TimelineRuler` draws the line through both rows and the down-pointing triangle handle (`markerHandleBounds`), and pressing the triangle clears the marker (`onClearMarker`). A click in the track list's note preview (`TrackNotePreview::onTimelineClicked`, not the ghost toggle) or a plain, non-dragging left click on *empty* editor canvas (`PianoRollComponent::handleEditorMouseDown/Up`; not on a note, not a double- or right-click) sets it. A **left** press/drag in the timing bar (`TimelineRuler`, `onSetMarker`) sets it to the exact tick under the pointer (no snap); a **right** press/drag there moves the playhead (`onSeek`) — the marker never moves the playhead, and vice versa.

Data flow: a `SOURCE_MIDI`/`TEMPO_MAP` edit → `AsyncUpdater` (coalesced) → `buildSnapshot` + mute/solo flags → `HandOff` publish → the next block's `acquire()` sees a swap → `releaseAll` + chase to the current position → playback continues. Open/New, Undo/Redo and imports use the same path.

**Why seconds.** The playhead is stored in seconds so it is unambiguous across tempo changes and cheap for the audio thread; ticks are only a view (`getPositionTicks`). The consequence is that a tempo edit during playback moves the musical (tick) position.

**Threading rules.** The audio thread (`PlaybackEngine::renderBlock`, `SynthVoice::beginBlock/handle/releaseChannel/releaseAll/render`, `HandOff::acquire`, `Transport::advance`) never allocates, locks, frees or throws. `SynthVoice::prepare()`, every `HandOff::publish` and `AudioOutput` construction are **message-thread only** (the constructor runs `engine.prepare` synchronously on the calling thread). The audio device must stop before the engine and synth are destroyed.

**Wiring (`MainWindow`).** `MainWindow` owns the `SynthVoice`, the `PlaybackController` and the lazily-created `AudioOutput`; `settings` holds `soundFontPath`. `loadStartupSoundFont` tries the stored path, then `resources/SongSmith.sf2` beside the exe; failure is silent and `ensurePlaybackReady` (the `onBeforePlay` hook) shows "No SoundFont" or "Audio unavailable" on Play (Play itself stays enabled; dialogs and the device are lazy so the app opens and edits Songs without audio or a SoundFont). `Song → SoundFont…` loads a new font (a failed load keeps the old one); the same load/clear paths back Preferences ▸ Playback (§9.16). `MainWindow::keyPressed` maps plain Space to `togglePlayPause`; `TrackEditorWindow` does the same. The SoundFont itself is a local, git-ignored GPL-2 file (`resources/soundfonts/SongSmith.sf2`), copied next to the exe by a CMake post-build step; see `docs/BUILD.md`.

**Known gaps.**
- Tooltips (`M`/`S`, the ruler) are inert app-wide: the app creates no `juce::TooltipWindow`.
- Mono output is not downmixed (the engine points both channels at one buffer).
- No limiter or headroom: up to 192 voices sum at 0 dB.
- A failed audio-device prepare after the device opened is silent.
- The timing bar spans the full width including the info column; clicks there seek to a tick scrolled out of view, but marks are only drawn right of `setContentLeft` (the preview origin / keyboard gutter).
- LOTRO preview playback is a later project.
- Playback tests that need the SoundFont return early with a warning (reported as passed) when the local file is absent (always on CI).

### 9.15 Track sections — `Source/UI/SectionEdit.{h,cpp}`, `Source/UI/SectionViewState.h` (+ `TrackListComponent`, `TrackNotePreview`, `TrackRowComponent`, `PreviewNoteDiff`)

A track's notes are divided into **sections** that the user can split, move, resize and delete on the main canvas. Spec: `docs/superpowers/specs/2026-10-06-songsmith-sections-design.md`. `Source/Core/` is untouched.

**Data model.** `MIDI_TRACK` may hold a `SECTIONS` child of `SECTION` nodes (`sectionId`, `startTick`, `endTick`, half-open). A `NOTE` carries an optional `sectionId` naming its owner. Section ids come from `SongDocument::mintSectionId()`. A track with no `SECTIONS` node has one **virtual** section (id 0, `[0, last note end)`); it is *materialised* (written, non-undoably, with the notes tagged) only when a mutation touches the track, so opening and playing a Song never writes the tree. The conductor and note-less tracks have no sections.

**`SectionEdit` read model** (pure, no UI): `sectionsOf(track)` (stored or virtual), `sectionIdOfNote` (a note's tag, else the nearest section to its start: distance 0 inside, ties to the earlier start; a note drawn far from every section still belongs to the nearest one), `hitTestSection` (Body / LeftEdge / RightEdge / None for a tick at a zoom) and `markNoteTimingEdited`. **Mutations**, each in exactly one undo transaction, materialising what it touches first, and a no-op (no transaction, no write) when nothing would change: `splitAt` (tick strictly inside a section; a note straddling the tick is cut in two, both halves keeping provenance), `moveSections` (one delta, clamped so nothing starts below 0), `resizeSections` (edge to an absolute tick), `resizeSectionsBy` (each edge moves by a **delta** from where it is, each clamped on its own to at least 1 tick wide, so selected sections with different ends all change by the same amount), `deleteSections`.

**Downstream.** `PreviewNoteDiff` pairs notes that share a provenance key (the halves of a cut note, editor-made notes) in passes: identical tick and pitch, then identical tick, then the leftovers in start order, because the pipeline can erase individual members and rank alone would mis-pair survivors. Playback and export read the notes as they are; sections add no events.

**UI state.** `SectionViewState` (selected `SectionRef`s plus an optional drag preview) lives in `TrackListComponent` and is read by every row's `TrackNotePreview`; it is never in the tree. **No mutation mid-drag:** any tree change rebuilds the rows and destroys the strip that holds the mouse, so a press only records a `SectionGesture`, a drag only updates `sectionView.drag` (after a 3-pixel threshold), and mouse-up commits once through `moveSections` / `resizeSectionsBy`. `rebuild()` cancels a drag and prunes selected sections whose track or section no longer exists. **Two independent selections.** The *head* selection is `selectedTrackIds` plus the anchor `selectedTrackId` (`selectTrack`: click, Ctrl/Cmd toggle, Shift range; `selectAllHeads`); a head click never touches `sectionView.selected`. The *canvas* selection is `sectionView.selected`, built only by `sectionPressed(trackId, hit, tick, mods)` from strip mouse-downs (`TrackNotePreview::onSectionPressed` and the row forward the `ModifierKeys`; the row's `onStripSelected` is no longer wired by the list): plain = just that section, except a section already in a multi-selection is kept on press (the gesture's `refs` are the whole selection) and `SectionGesture::collapseTo` collapses it on a release at the press tick; Ctrl/Cmd toggles with no gesture; Shift selects, on every non-conductor row between the `canvasAnchor` (last plain/Ctrl-clicked `SectionRef`) and the clicked row, the section whose `startTick` equals the anchor's (Ctrl+Shift extends); a plain press on empty strip clears the canvas and does a plain head click. After each of these `mirrorCanvasToHeads` sets `selectedTrackIds` to the non-conductor tracks owning a selected section (one way; `rebuild()` prunes both selections separately and never mirrors). `selectAllCanvases` selects every `sectionsOf` section (virtual id 0 included) of every non-conductor track and mirrors.

**Keys.** `MainWindow::keyPressed` forwards plain `S`, `Delete`/`Backspace` and Ctrl/Cmd+A through `SongsmithMainComponent::splitSections/deleteSections/selectAll` to `TrackListComponent::splitAtPointer` / `splitSections(pointerTick, pointerTrackId)` / `deleteSelectedSections` / `selectAll`. `selectAll` is a thin wrapper that resolves the real pointer (`pointerIsOverNoteStrips`: inside the viewport and a row's note strip) and calls `selectAllCanvases` or `selectAllHeads`, the two testable entry points. The decision logic (pointer tick, else start marker, else nothing; tracks owning a selected section, else the pointer's track, never the head selection; delete clears the stored selection) is all in `TrackListComponent` so it is testable; `splitSections` and `deleteSelectedSections` return whether they acted, and `keyPressed` returns true only then (Ctrl/Cmd+A always acts). The new keys are skipped while an editable component (`juce::TextInputTarget`) has focus.


**Merging into another track** (`Source/UI/NoteMerge.{h,cpp}`; spec
`docs/superpowers/specs/2026-10-10-songsmith-merge-tracks-design.md`).
`mergeSections(doc, refs, targetTrackId, copy)` carries the notes of the
referenced sections (`notesInSection`) into a non-conductor target at their own
ticks, in one undo transaction; `canMergeInto` answers whether a target can take
them. Rules are per pitch: a carried note inside an existing same-pitch note is
dropped, one that overlaps or touches joins with it (the earliest-starting note's
properties win, a grown existing note is `markNoteTimingEdited`), a bridging note
collapses the two notes it joins, and different pitches coexist. Pre-existing
overlaps in the target are never rewritten. Inserted notes lose `onOrder`/`offOrder`/
`sectionId`, get `sourceTrackIndex`/`sourceEventIndex` = -1, take the target's
`defaultChannel` (1 if unset) with `isDrum = (channel == 10)`, and are tagged with
the target section holding their start or the nearest (a note outside every target
section does not resize it). A move removes the carried notes from the source (its
sections are left as they are); a call that changes nothing opens no transaction.
`MergeResult` counts each carried note in exactly one of inserted/dropped/extended.
The gesture lives in `TrackListComponent` (`MergeGesture`): an Alt + left press on a
strip (`TrackNotePreview::onMergePressed`, forwarded by `TrackRowComponent`) records
the carried sections (the canvas selection if the pressed section is in it, else
just that section); dragging (after 3 px) only updates `SectionViewState::merge`
(`MergeDragPreview`: target row, valid, copy, ghosts), which `TrackNotePreview::
paintMergePreview` draws on the target row; release commits once through
`mergeSections`, reading Ctrl/Cmd then for copy, and selects the target's sections.
**Esc** (`cancelSectionDrag`) and `rebuild()` drop the gesture and preview. Known
limits: stray note-on/off pairs kept in `EVENTS` are not carried.

**Instrument changes** (`ProgramChanges.{h,cpp}`, `InstrumentEdit.{h,cpp}`, `splitAtTicks` in `SectionEdit`; spec `docs/superpowers/specs/2026-10-10-songsmith-instrument-changes-design.md`). `programChangesOf (track)` reads every two-byte `Cn pp` `EVENT` of a track (tick order, ties by `order`; each `ProgramChange` carries its `EVENT` node, channel 1..16 and program) and never mutates; `instrumentSegmentsOf` turns them into one `InstrumentSegment` per distinct instrument (the span before a late first change, or a track with none, uses `sourceProgram`; repeats merge; two changes on one tick keep the later; empty for the conductor) and `bandSegments` maps those to pixels through the shared `TimelineViewState`. `TrackRowComponent::paint` draws a coloured, labelled cell per segment when there are two or more (drum and conductor rows keep a plain label; a single segment is labelled with the program the track starts on, which `instrumentLabel()` reads from the segments rather than `sourceProgram`). A right-click in the band (`inInstrumentBand`, not on the conductor) shows `buildInstrumentMenu()`: item 1 **Auto split on instrument change** (enabled when `canAutoSplit`) and a **Set track instrument to** submenu (16 GM families, leaf ids `1000 + program`, the first segment's program ticked); `instrumentMenuChosen` fires `onAutoSplitRequested` / `onSetInstrumentRequested`, which `TrackListComponent` wires to `autoSplitOnInstrumentChange` and `setTrackInstrument`. `splitAtTicks` splits one track at several ticks in a single undo transaction (the per-track body of `splitAt` is shared as `splitTrackAt`); `setTrackInstrument` sets the first program change per channel and moves it to tick 0 (with an order below every event and imported note, so export puts it before the tick-0 notes), deletes later ones, inserts one at tick 0 on `defaultChannel` when there is none and updates `sourceProgram`, opening no transaction when nothing would change. It does not recolour the track.

`mergeSections` takes a `MergeScope` (`MergeScope.h`, `notesOnly` default). With `allEvents`, every non-note `EVENT` of a referenced section with tick in `[startTick, endTick)` is also copied to the target (same tick, fresh `order`, channel nibble rewritten to the target's `defaultChannel`, `relocatedFrom` unset) and removed from the source on Move; note on/off, End-of-Track and track-name metas never travel, and nothing is synthesized for an instrument set before the section. `MergeResult::eventsCarried` counts them and a merge that only carries events still counts as `changed`. `TrackListComponent::setMergeScope` holds the value read at release.

### 9.16 Preferences — `Source/UI/AppSettings.h`, `Source/UI/Preferences/{PreferencesDialog,PreferencesServices,GeneralPreferencesPage,PlaybackPreferencesPage,AppearancePreferencesPage,EditingPreferencesPage}.{h,cpp}` (the Import page is §9.17) (+ `MainWindow`)

`File → Preferences…` (`FilePreferences`, between Export ▸ and Quit, no shortcut, always enabled) opens a fully modal `juce::DialogWindow` via `launchAsync` (like `AboutBox`) through `showPreferencesDialog (PreferencesServices&, juce::Component*)`. Changes apply immediately; there is no OK/Cancel, only a Close button (Escape and the title-bar X also dismiss it).

- **`AppSettings`** (header-only) is a typed view over `MainWindow`'s existing `juce::PropertiesFile` (`appSettings`, declared right after `settings`). Keys and defaults live only there: `confirm.unsavedChanges` and `confirm.replaceFile`, both default on; anything other than an explicit `"0"` reads as on. A third key, `import.trackOptions` (default `ask`), is described in §9.17. Setters `saveIfNeeded()` at once and return whether the save succeeded (`lastSaveFailed()` also reports it); the dialog uses this to show a failure notice (below). Reads are live (asked at the moment of use), so no restart is needed. `shouldPromptForUnsavedChanges (settings, isDirty)` and `shouldConfirmReplace (settings)` are the decision functions.
- **Unsaved-changes prompt off = discard silently** (no auto-save): `MainWindow::guarded()` feeds `shouldPromptForUnsavedChanges` into `confirmDiscardChanges`, so a dirty Song counts as clean and the action proceeds.
- **"Ask before replacing" covers both overwrite sites:** `chooseAndConfirm (owner, confirmReplace, …)` (Export MIDI / Export ABC) and Song Save As in `MainWindow`.
- **`PreferencesDialog`** is a `juce::Component`: left `TreeView` of pages, a `SplitterComponent` (leftRight, fraction 0.25, clamped to [0.15, 0.85]) and the right-hand page host. `preferencePages()` is the registry; **adding a page = one `PreferencesPage { name, make }` entry plus its component.** General is first and selected. `GeneralPreferencesPage` holds the two toggles. Pages take an optional `onChanged` callback (`make (PreferencesServices&, std::function<void()>)`) that they call after a setter; `PreferencesDialog` passes one that shows a "could not save settings" notice when `lastSaveFailed()` is set and hides it after a successful save. The window has resize limits of 480 × 300 minimum to 1600 × 1200 maximum (`preferencesMinWidth|Height`).
- **`PreferencesServices`** (`PreferencesServices.h`) is what the page registry's `make` receives (the General and Import pages still take the `AppSettings&`, which the registry passes as `s.settings`; only the Playback, Appearance and Editing pages take the services): `settings` (the `AppSettings&`) plus four `std::function`s that `MainWindow` fills in with lambdas capturing `this` (the member `preferencesServices`, declared after `appSettings`): `loadSoundFont (File)`, `useBundledSoundFont()`, `activeSoundFontLabel()` and `applyViewSettings()` (defaults to empty; a page calls it only if set). The load functions return `SoundFontLoad { SoundFontResult result; juce::String detail; }`, with `SoundFontResult` one of `loaded`, `failed`, `bundledUnavailable`. Pages call them only from their constructor and click/chooser handlers, never from a destructor.
- **`AppSettings` SoundFont accessors:** `soundFontPath()` (empty = never chosen or cleared), `setSoundFontPath (File)` and `clearSoundFontPath()`, over the existing `soundFontPath` key; the setters save at once and return whether it succeeded, like the others.
- **`MainWindow` SoundFont helpers.** `activeSoundFont` is the file currently loaded in `synth` (empty = none), set on every successful load. `loadSoundFontAndRemember (file)` loads, and only on success sets `activeSoundFont` and saves the path; a failed load keeps the previous SoundFont and writes nothing. `useBundledSoundFont()` always clears the saved path first; then, if `SongSmith.sf2` beside the exe (`resources/`) is missing it returns `bundledUnavailable` and the current SoundFont stays loaded (`SynthVoice` has no unload), otherwise it loads the bundled file (`loaded` or `failed`). `activeSoundFontLabel()` gives "No SoundFont loaded", "Using bundled SongSmith.sf2" or "Using <full path>". `loadStartupSoundFont` tries the saved path, then `SongSmith.sf2` beside the exe, and is silent when neither loads. `Song → SoundFont…` goes through `loadSoundFontAndRemember` too and shows an error box only on `failed`.
- **`PlaybackPreferencesPage`** (third page): a SoundFont heading, a label with `activeSoundFontLabel()`, **Browse…** (an async `FileChooser` for `*.sf2`, guarded by a `SafePointer`; a chosen file goes to `loadSoundFont`), **Clear** (enabled only while `settings.soundFontPath()` is non-empty; calls `useBundledSoundFont`) and a status line. Outcomes: *loaded* ("Loaded." after Browse, "Using the bundled SoundFont." after Clear); *failed* (Browse: "Could not load: <detail>. Still using the previous SoundFont."; Clear: the choice was cleared but the bundled file could not be loaded, with the detail); *bundledUnavailable* (the choice was cleared but the bundled `SongSmith.sf2` was not found, so the current SoundFont stays until you quit). After each outcome it refreshes the label and Clear, then calls `onChanged`. `loadChosenFileForTesting` runs what the chooser callback runs.
- **View settings.** `AppSettings` has three more keys, all written through like the others: `appearance.restoreWindowPlacement` (default on), `editing.followPlayhead` (default on) and `editing.defaultGrid` (default `off`; stored `"off"` / `"quarter"` / `"eighth"` / `"sixteenth"`, any other value reads as `GridSize::Off`). Accessors: `restoreWindowPlacement()` / `setRestoreWindowPlacement`, `followPlayhead()` / `setFollowPlayhead`, `defaultGrid()` / `setDefaultGrid (GridSize)`. The boolean ones follow the "anything but an explicit `"0"` is on" rule.
- **`MainWindow::applyViewSettings()`** reads `followPlayhead` and `defaultGrid` and calls `SongsmithMainComponent::setFollowPlayhead` and `setDefaultGridSize`. It runs once at the end of the constructor and is the `preferencesServices.applyViewSettings` lambda, so a Preferences change shows at once. The view setters: `PianoRollComponent::setFollowPlayhead`, `TrackListComponent::setFollowPlayhead`, and `TrackEditorWindow::setFollowPlayhead` on an open editor. `SongsmithMainComponent` also remembers the follow and default-grid values, and applies them to a Track editor window when `trackDoubleClicked` creates one.
- **No band switch.** The instrument range band, its out-of-range wash, the ghosts and the dropped-note overlays are always drawn (Preview-role roll only): the band is essential to making an ABC, so it is deliberately not user-hideable.
- **Default grid.** `setDefaultGridSize` only stores the value; it is applied (`setActiveEditorGridSize`) when a Track editor window is created, so it never changes an editor that is already open, and `Edit > Grid size` never writes the setting.
- **Placement switch.** The `MainWindow` constructor reads `restoreWindowPlacement()`: when off it skips `WindowPlacement::restoreWindow` and `applyMaximised` and centres the window at 1000 x 700, so it takes effect at the next launch. It is read only there and is not part of `applyViewSettings`. Quit still saves the placement (`~MainWindow`), so switching it back on restores the last-closed position.
- **`AppearancePreferencesPage`** (fourth page): has one toggle, "Restore the window position and size on launch" (with the note "Takes effect the next time Songsmith starts."). **`EditingPreferencesPage`** (fifth): a "Default grid size" combo (Off, 1/4, 1/8, 1/16; item id is `(int) GridSize + 1`) with a note, and a "Follow the playhead during playback" toggle. Each change writes the setting, calls `applyViewSettings` if set, then `onChanged`. Test hooks: `restorePlacementToggleForTesting`, `gridComboForTesting`, `followToggleForTesting`.
- **Merge scope.** `AppSettings` also has `editing.mergeScope` (`"notes"` / `"all"`; absent or unrecognised reads `notesOnly`; `MergeScope` is in `MergeScope.h`). `EditingPreferencesPage` adds a radio pair (Notes only / All events) below the follow toggle; each button acts only when it is the one turned on, because turning one radio on also notifies the one it turns off. `MainWindow::applyViewSettings` pushes it through `SongsmithMainComponent::setMergeScope` to the track list (§9.15).
- **Lifetime.** `showPreferencesDialog` returns a `juce::Component::SafePointer<juce::DialogWindow>`; `MainWindow` keeps it and ends the dialog's modal state in `~MainWindow`, before the settings and `preferencesServices` (whose lambdas capture `this`) are torn down. `exitModalState` does not delete the window: JUCE's modal manager deletes it asynchronously (or at manager teardown), so neither the dialog nor its pages may touch `AppSettings` in their destructors, which is why the page rule exists (`PreferencesDialog.h`). The dialog also calls `refreshSaveNotice()` at the end of its constructor, so a save that failed before it opened shows the notice at once. (The import options dialog is dismissed before the import runs: §9.17.)
- **Tests:** `AppSettings_tests` (keys, defaults, write-through, and the save-failure tests: a failed save is reported and keeps the value, the next good save clears it), `PlaybackPreferencesPage_tests` (the page against a fake `PreferencesServices`: label, Clear enabling, load success/failure, Clear outcomes, failed save), `ViewPreferencesPages_tests` (Appearance and Editing pages), the view-setting cases in `PianoRollComponent_tests`, `TrackListComponent_tests` and `SongsmithMainComponent_tests`, `PreferencesDialog_tests` (tree/splitter/page layout, registry including the Playback, Appearance and Editing pages, Close button, General page write-through, the save-failure notice from either page, visible on open when the last save already failed and cleared by a later good save, the minimum size), and `SongDocument_tests` / `SectionEdit_tests` (`validateLoaded` rejects damaged `SECTION` nodes; documents built with the real section operations, overlap included, pass it). The modal window, menu dispatch and `MainWindow` wiring are not headless-testable (`MainWindow.cpp` is not in the test binary); they are covered by the manual checklist and a `forge_ui` build.

### 9.17 Import options — `Source/UI/{MidiImportPlan,SongModelBridge,ImportOptionsDialog,AppSettings}.{h,cpp}`, `Source/UI/Preferences/ImportPreferencesPage.{h,cpp}` (+ `MainWindow`)

Importing a MIDI file (File ▸ Import ▸ MIDI... or a dropped `.mid`/`.midi`) carries an `ImportOptions { TempoMode tempo = keep|replace; TrackMode tracks = expanded|merged; }` (`MidiImportPlan.h`). The defaults equal the pre-existing behaviour, so every caller that does not pass options is unchanged. Flow: `MainWindow::openMidiFromPath` -> `importMidiWithOptions` -> `importMidiFile (…, options)` -> `appendImportedMidi (…, importerDiagnostics, options)` -> `planMidiImport (…, options)`. Design spec: `docs/superpowers/specs/2026-10-08-songsmith-import-options-design.md`.

- **Setting and dialog.** `AppSettings` gains `ImportTrackOptions { ask, expandedAlways }` under the key `import.trackOptions` (stored `"ask"` / `"expanded"`; missing or unrecognised reads as `ask`), written through and saved at once; `shouldAskImportOptions (settings)` is true for `ask`. `openMidiFromPath` (shared by the file chooser and drops) calls `showImportOptionsDialog` when it is true and imports on OK; Cancel (button, Escape, title-bar X) does nothing. **Import Expanded Always** = no dialog and a default `ImportOptions {}` (keep the tempo map, expand), i.e. exactly the old behaviour. `openMidiFromPath` first asks `previewImportTrackCount (file)` (both parsers plus `planMidiImport`, no changes) how many tracks Expand will add and passes it to the dialog, whose Expand label reads "Expand N tracks into separate tracks" for N >= 2 and the plain label for one track or an unreadable file. `ImportOptionsComponent` holds two radio groups (tempo, tracks); the tempo group is disabled when the Song has no tempo map yet (first import), where replace and keep are the same. The page is `ImportPreferencesPage` (one dropdown, second entry in `preferencePages()`, §9.16). On OK the dialog window exits its modal state and is hidden before the options are handed on, so a long import does not run under a still-painted dialog.
- **Tempo replace.** `plan.writesConductor` is true on any import with `TempoMode::replace` (first import only under `keep`), so the planner emits the file's conductor as it does for a first import. The bridge then replaces the Song's conductor `EVENT`s and its `TEMPO_MAP` / `METER_MAP` wholesale (the new conductor already carries the file's own SysEx and other conductor events, so keeping the old ones would duplicate them). The file's ticks are rescaled to the Song's time base like any later import, after any LCM raise (which rescales the existing material first). Existing tracks' ticks are untouched, so they now play against the new tempo map. One `Info` Diagnostic (source `SongModelBridge`) says the tempo map was replaced, only when the Song already had one. No `UndoManager` use (import is a non-undoable bulk operation, as before). Under `keep` the existing "file's tempo map differs" warning is unchanged.
- **Merge.** `TrackMode::merged` folds every note-bearing raw track into one `PlannedTrack` (`PlannedTrack::mergedTrack`, with `mergedSongTrackIndices` naming the source `Song` tracks so importer diagnostics land on the merged row). Each note keeps its own `channel`. Because `order` is per raw track, the merged track renumbers all raw events by (tick, source raw track index, original index) and remaps each note link's `onOrder`/`offOrder` and each leftover event's `order`, preserving each source track's relative order, so an unedited merged import exports note-for-note. Notes are stable-sorted by start tick; the merged `MIDI_TRACK`'s `sourceTrackIndex` is the first source raw track's, while each note keeps its own `sourceTrackIndex`/`sourceEventIndex` provenance; metadata is the file's stem + " (merged)", `sourceProgram`/`sourceMidiChannel`/`defaultChannel` from the first note-bearing track, `endTick` the maximum. One `Info` Diagnostic reports how many tracks were merged. Fewer than two note-bearing tracks = nothing to merge: it imports as in expanded mode with no rename. Note-less raw tracks are not merged.
- **Tests:** `ImportOptions_tests` (bridge-level: replace, LCM raise, meter map, merge, export round trip, diagnostic mapping), `MidiImportPlan_tests` (planner), `ImportOptionsDialog_tests`, `AppSettings_tests`, `PreferencesDialog_tests`. `MainWindow::openMidiFromPath`'s dialog wiring and the modal behaviour are not headless-testable (`MainWindow.cpp` is not in the test binary); they are covered by a manual checklist and a `forge_ui` build.

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
drop .mid / Import ▸ MIDI...                 File ▸ Open... / drop .songsmith
   │                                              │
   ▼                                              ▼
MainWindow::openMidiFromPath             MainWindow::openSongFromPath (guarded)
   │                                       loadSongFile → SongDocument::replaceContents
   ▼                                              │
SongModelBridge::importMidiFile                   │
   │  (importMidi + readMidiBytes →               │
   │   planMidiImport → appendImportedMidi)       │
   ▼                                              ▼
SongDocument (ValueTree)
                 │
       user drags tracks onto parts, edits assignments/labels via
       SongsmithMainComponent — every edit goes through
       SongDocument's UndoManager
                 │
                 ▼
Song → Run Converter (MainWindow::runConversion)
                 │
                 ▼
SongModelBridge::buildConfigAndRawSong(doc) ─► Config + raw Song
                 │
                 ▼
SongModelBridge::dropUnassignedInstruments   (skips zero-assignment
                 │                            parts, warns per skip)
                 ▼
validateConfig + assembleInstruments + runPipeline + writeAbc
                 │
                 ▼
DiagnosticsPane.show(diagnostics, abc)   ← the export panel

File → Export MIDI… (MainWindow::exportMidiAs)
                 │
                 ▼
buildRawMidiFile(doc) → writeMidiBytes → file   ← independent of parts
```

---

## Cross-reference — where to look

| If you're looking for…                                         | File(s)                                         |
|----------------------------------------------------------------|-------------------------------------------------|
| The data types everything flows through                        | `Source/Core/{Note,Track,Song,Config,Diagnostics}.h` |
| How MIDI bytes become a `Song`                                 | `Source/Core/MidiImporter.cpp`                  |
| Lossless MIDI read/write, note-pairing replica, import plan, export | `Source/UI/{RawMidi,JuceNoteReplica,MidiImportPlan,MidiExport}.{h,cpp}` |
| Config shape, loaders, writers, validation                     | `Source/Core/Config{,Loader,Writer}.{h,cpp}`    |
| How the user's Config turns one+ MIDI tracks into one LOTRO part | `Source/Core/InstrumentAssembly.cpp`            |
| What the pipeline does and why (+ `synthesiseConfig`)          | `Source/Core/Pipeline.cpp`                      |
| Per-constraint behaviour                                       | `Source/Core/Constraints/*.cpp`                 |
| The `Q:`/`M:` bake-in math                                     | `Source/Core/Constraints/TempoCollapse.cpp`     |
| Cluster-at-boundary, z-pulse, bar labels, per-instrument shift | `Source/Core/AbcWriter.cpp` (top-of-file comment and `ChordEmitter` class) |
| CLI flags, `--config` vs ad-hoc, drum-map loader               | `Source/Cli/*.cpp`, `Source/Main.cpp`           |
| GUI layout, Songsmith component interaction                    | `Source/UI/*.cpp`, `docs/UI_GUIDE.md`           |
| Guiding principle ("MIDI is truth, no auto")                   | `CLAUDE.md` → *Guiding principle*               |

See also `docs/UI_GUIDE.md` for a reference of the GUI's named regions, menus, and right-click context menus.
