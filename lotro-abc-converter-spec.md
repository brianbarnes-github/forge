# LOTRO MIDI → ABC Converter — Build Specification

> **Purpose.** This document is a complete, self-contained build spec for a Windows desktop app that converts MIDI files into LOTRO-compatible ABC notation. It is written for a CLI coding agent (Claude Code, Cursor, etc.) to execute end-to-end. Hand it to the agent with the prompt: *"Read this spec in full, then implement it. Ask before deviating from it."*

---

## 1. Project Summary

A JUCE-based C++ desktop application that:

1. Loads a standard MIDI file (`.mid`, format 0 or 1).
2. Displays each track with a per-track LOTRO instrument picker.
3. Applies LOTRO ABC constraints (note range, chord size, duration quantization, tempo collapse, drum mapping).
4. Exports a multi-part ABC file that LOTRO will play without errors.

**Scope for v1:** multi-track → multi-part ABC, per-track instrument assignment. No section editor, no live preview synthesis, no polyphony graph. Those are v2.

**Platform:** Windows 10/11, 64-bit. Build with MSVC (Visual Studio 2022) via CMake.

**Reference:** The Maestro project — https://github.com/NikolaiVChr/maestro — is the canonical reference for LOTRO constraints. It is AGPL-licensed Java. **Do not copy code verbatim.** Read it for algorithms and lookup tables only; reimplement in C++ from scratch.

---

## 2. LOTRO ABC Constraints (Authoritative Rules)

These rules are non-negotiable. If the output ABC violates any of them, LOTRO will either error on playback or silently drop notes.

### 2.1 Header fields

Every part MUST have, in this order:

```
X:<index>         (integer, 1-based, unique per part in file)
T:<title>         (title, can include part suffix like " - Lute")
Z:<transcriber>   (free text, e.g. "LotroAbcConverter v0.1")
L:1/8             (default note length — ALWAYS 1/8 for v1)
Q:<bpm>           (main tempo as integer BPM, e.g. Q:120)
M:<num>/<den>     (meter; denom must be power of 2 ≤ 8; numer < 256)
K:<key>           (key signature, e.g. K:C, K:Gmaj, K:Am)
```

Missing `M:` or `L:` causes playback failure. `X:` must be unique per part.

### 2.2 Note range (per instrument)

LOTRO uses a 3-octave window that is offset per instrument. In ABC notation the playable span is `C,` (two octaves below middle C) through `c'` (one octave above middle C), but each instrument only covers 3 octaves within that range.

Use the following table (MIDI note numbers; middle C = 60):

| Instrument          | MIDI low | MIDI high | ABC low | ABC high | Notes                                    |
|---------------------|---------:|----------:|---------|----------|------------------------------------------|
| Lute of Ages        |       36 |        72 | `C,`    | `c`      | Default lute                             |
| Basic Lute          |       36 |        72 | `C,`    | `c`      | Use title "Basic Lute" or "New Lute"     |
| Harp (misty mtn)    |       36 |        72 | `C,`    | `c`      |                                          |
| Theorbo             |       24 |        60 | `C,,`   | `C`      | Bass instrument                          |
| Flute               |       60 |        96 | `C`     | `c'`     | High instrument                          |
| Clarinet            |       48 |        84 | `C,`    | `c`      | Full 3-octave range (post-UI 15)         |
| Horn                |       48 |        84 | `C,`    | `c`      | Full 3-octave range                      |
| Pibgorn             |       48 |        84 | `C,`    | `c`      | Full 3-octave range                      |
| Bassoon             |       36 |        72 | `C,`    | `c`      |                                          |
| Cowbell             |       36 |        72 | `C,`    | `c`      | Pitched percussion                       |
| Moor cowbell        |       36 |        72 | `C,`    | `c`      |                                          |
| Student Fiddle      |       55 |        91 | `G,`    | `g`      | Lowest note G3 (G2 in ABC); below = silent |
| Fiddle              |       55 |        91 | `G,`    | `g`      |                                          |
| Bardic Fiddle       |       55 |        91 | `G,`    | `g`      |                                          |
| Lonely Mtn Fiddle   |       55 |        91 | `G,`    | `g`      |                                          |
| Sprightly Fiddle    |       55 |        91 | `G,`    | `g`      |                                          |
| Barndance Fiddle    |       55 |        91 | `G,`    | `g`      |                                          |
| Travellers' Trusty  |       55 |        91 | `G,`    | `g`      |                                          |
| Drums               |      N/A |       N/A | N/A     | N/A      | Use drum map (see §2.6)                  |

**Red-note handling:** notes outside the instrument's range must be octave-transposed (add or subtract 12 semitones) toward the nearest in-range octave. Repeat until in range. If a note is unreachable even after transposition (shouldn't happen with a 3-octave range, but guard anyway), drop it and log a warning.

### 2.3 Chord limits

- Max **6** notes per chord. If an input moment has 7+ simultaneous notes, keep the 6 with the highest velocity (tiebreak: higher pitch wins).
- A chord is written `[ceg]` — **no spaces** inside the brackets.
- If notes within a chord have different durations, only the shortest matters for when the next note begins. Normalize chord durations to the shortest in v1.
- **Same-pitch collision rule:** a LOTRO instrument cannot play the same note twice simultaneously. If a chord sustains a pitch that also appears in the following melody before the chord ends, LOTRO errors. Detection: track each pitch's "currently-sounding-until" tick; if a new note starts at pitch P before the existing note at P has ended, shorten the earlier note to end at the new note's start tick minus 1 tick.

### 2.4 Duration and timing

- `L:1/8` is fixed. Every note duration is expressed as a multiplier of 1/8 note.
- Supported durations are fractions of the form `n` or `n/d` where the final duration in 1/8ths, reduced, fits in the LOTRO-accepted set.
- **Known-good durations** (multiples of 1/8): integers 1 through ~32, plus halves (`/2` = 1/16), thirds for triplets (`/3`), and three-quarters (`3/4`). Durations longer than ~8 (one whole note at L:1/8) should be broken into tied notes at bar lines.
- **Minimum duration:** anything shorter than 1/16 (i.e. `/2` at `L:1/8`) should be rounded up to 1/16. If two rapid notes at the same pitch collide after rounding, drop the second.
- **Maximum duration:** no single note longer than 8 eighths (one whole note in 4/4). Break longer notes at bar lines.
- **Quantization:** round note start and end times to the nearest 1/16 of a beat. Use the MIDI file's PPQ (ticks per quarter) to compute: `quantized_tick = round(tick / (PPQ / 4)) * (PPQ / 4)`.

### 2.5 Tempo handling

- Use the tempo of the **first** tempo event as the "main tempo" written to `Q:`.
- LOTRO does not honor mid-song tempo changes. For every tempo change after the first, scale all note durations in the affected region by `local_bpm / main_bpm`.
- Write each tempo change as a comment line `%%Q:<bpm>` immediately before the affected bar, so Maestro and ABC Player can still parse it round-trip.
- Meter denominator must be in {1, 2, 4, 8}; numerator < 256. If the MIDI's meter violates this, clamp (e.g. 16ths → 8ths) and log a warning.

### 2.6 Drum mapping

Drum tracks (MIDI channel 10, or any track explicitly marked as drums by the user) need a MIDI-note → LOTRO-drum-ABC-note map. Do not transpose drum tracks.

Implement a default General MIDI drum map → LOTRO drum map. For v1, use this starting table (derived from Maestro's default mapping — verify against the Maestro `LotroInstrument.java` and drum map files before shipping):

| GM drum name         | GM MIDI | LOTRO ABC note |
|----------------------|--------:|----------------|
| Bass Drum 1          |      35 | `C`            |
| Bass Drum 2          |      36 | `D`            |
| Side Stick           |      37 | `E`            |
| Snare                |      38 | `F`            |
| Hand Clap            |      39 | `G`            |
| Snare 2              |      40 | `A`            |
| Low Floor Tom        |      41 | `B`            |
| Closed Hi-Hat        |      42 | `c`            |
| High Floor Tom       |      43 | `d`            |
| Pedal Hi-Hat         |      44 | `e`            |
| Low Tom              |      45 | `f`            |
| Open Hi-Hat          |      46 | `g`            |
| Low-Mid Tom          |      47 | `a`            |
| Hi-Mid Tom           |      48 | `b`            |
| Crash Cymbal 1       |      49 | `c'`           |
| High Tom             |      50 | `d'`           |
| Ride Cymbal 1        |      51 | `e'`           |

Any GM drum not in the map → drop + log. The user should be able to remap drums in v2; for v1, ship the default and make the map data-driven (JSON file next to exe) so users can edit it.

### 2.7 Dynamics

MIDI velocity → LOTRO step dynamic, emitted as an inline `+xx+` marking when the dynamic changes from the previous note:

| Velocity range | Marking  |
|---------------:|----------|
|          1–15  | `+ppp+`  |
|         16–32  | `+pp+`   |
|         33–49  | `+p+`    |
|         50–65  | `+mp+`   |
|         66–81  | `+mf+`   | (default, don't emit)
|         82–97  | `+f+`    |
|         98–113 | `+ff+`   |
|       114–127  | `+fff+`  |

Emit the marking only when the bucket changes. `+mf+` is default — skip emitting it unless explicitly returning from another dynamic.

### 2.8 Same-pitch collision guard (final pass)

After all other transformations, run a per-part pass that scans notes in time order, per pitch, and trims any overlap where the same pitch starts again before the previous instance has ended. Minimum trim: previous note ends 1 tick before new note starts.

---

## 3. Architecture

Clean separation so the constraint engine is unit-testable without JUCE or MIDI deps.

```
LotroAbcConverter/
├── CMakeLists.txt
├── README.md
├── JUCE/                        ← added as git submodule (see §4.1)
├── Source/
│   ├── Main.cpp                 ← juce::JUCEApplication entry
│   ├── Gui/
│   │   ├── MainComponent.h/.cpp ← top-level: toolbar, track list, log
│   │   ├── TrackListComponent.h/.cpp
│   │   └── TrackRowComponent.h/.cpp ← one row per track: name, instrument combo, enable toggle
│   └── Core/
│       ├── LotroInstrument.h/.cpp  ← enum, ranges, toString
│       ├── Note.h                  ← POD struct
│       ├── Track.h                 ← std::vector<Note> + metadata
│       ├── Song.h                  ← std::vector<Track> + tempo map + meter
│       ├── MidiImporter.h/.cpp     ← juce::MidiFile → Song
│       ├── DrumMap.h/.cpp          ← loads drum_map.json, maps GM → ABC note
│       ├── Constraints/
│       │   ├── RangeConstraint.h/.cpp    ← §2.2 red-note transpose
│       │   ├── ChordConstraint.h/.cpp    ← §2.3 trim to 6
│       │   ├── DurationConstraint.h/.cpp ← §2.4 quantize + split
│       │   ├── TempoCollapse.h/.cpp      ← §2.5 scale durations
│       │   ├── DynamicMapper.h/.cpp      ← §2.7 velocity buckets
│       │   └── CollisionGuard.h/.cpp     ← §2.8 same-pitch trim
│       └── AbcWriter.h/.cpp        ← Song → std::string (ABC text)
├── Resources/
│   └── drum_map.json
└── Tests/
    ├── CMakeLists.txt
    └── (Catch2 unit tests — see §6)
```

### 3.1 Core data types

```cpp
// Note.h — pure POD, no JUCE
struct Note {
    int    pitch;          // MIDI note number, 0-127. Ignored for drum tracks (use pitch as the GM drum id)
    int    startTick;      // absolute tick in source MIDI resolution
    int    durationTicks;  // > 0
    int    velocity;       // 0-127
    bool   isDrum = false; // if true, pitch is a GM drum id, not a pitch
};

// Track.h
struct Track {
    juce::String           name;           // from MIDI track name meta-event, or "Track N"
    std::vector<Note>      notes;
    LotroInstrument        instrument = LotroInstrument::LuteOfAges;
    bool                   enabled = true;
    int                    transposeSemitones = 0; // user-applied, before range clamp
    int                    sourceMidiChannel = 0;  // for drum auto-detect (channel 10 = drums)
};

// Song.h
struct TempoChange { int tick; double bpm; };
struct MeterChange { int tick; int numerator; int denominator; };

struct Song {
    int                        ticksPerQuarter = 480;
    std::vector<Track>         tracks;
    std::vector<TempoChange>   tempoMap;
    std::vector<MeterChange>   meterMap;
    juce::String               title;
    juce::String               transcriber = "LotroAbcConverter v0.1";
};
```

### 3.2 Pipeline

```
MIDI file
   │
   ▼
MidiImporter ── juce::MidiFile ──► Song (raw)
   │
   ▼   [user picks instrument per track in GUI]
   │
   ▼
For each enabled Track in parallel:
   RangeConstraint       (transpose into instrument range)
   DurationConstraint    (quantize, split long notes, enforce min)
   TempoCollapse         (scale durations by local/main tempo ratio)
   ChordConstraint       (trim chords > 6, normalize chord durations)
   CollisionGuard        (trim same-pitch overlaps)
   DynamicMapper         (attach dynamic marks)
   │
   ▼
AbcWriter ── emits one ABC part per enabled track ──► .abc file
```

Each constraint takes a `Track&` and mutates it in place. Order matters — do not reorder without testing.

---

## 4. Build Setup

### 4.1 Dependencies

- **JUCE 7.x or 8.x** — added as a git submodule at `./JUCE`. Use `juce_add_gui_app` in CMake. Required modules: `juce_audio_basics`, `juce_audio_formats` (for `juce::MidiFile`), `juce_core`, `juce_events`, `juce_graphics`, `juce_gui_basics`.
- **Catch2 v3** — git submodule at `./Tests/Catch2` for unit tests.
- No other third-party deps. `drum_map.json` parsed with `juce::JSON`.

### 4.2 CMakeLists.txt (root) — skeleton

```cmake
cmake_minimum_required(VERSION 3.22)
project(LotroAbcConverter VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_subdirectory(JUCE)

juce_add_gui_app(LotroAbcConverter
    PRODUCT_NAME "LOTRO ABC Converter"
    COMPANY_NAME "YourName"
    VERSION "0.1.0")

juce_generate_juce_header(LotroAbcConverter)

target_sources(LotroAbcConverter PRIVATE
    Source/Main.cpp
    Source/Gui/MainComponent.cpp
    Source/Gui/TrackListComponent.cpp
    Source/Gui/TrackRowComponent.cpp
    Source/Core/LotroInstrument.cpp
    Source/Core/MidiImporter.cpp
    Source/Core/DrumMap.cpp
    Source/Core/AbcWriter.cpp
    Source/Core/Constraints/RangeConstraint.cpp
    Source/Core/Constraints/ChordConstraint.cpp
    Source/Core/Constraints/DurationConstraint.cpp
    Source/Core/Constraints/TempoCollapse.cpp
    Source/Core/Constraints/DynamicMapper.cpp
    Source/Core/Constraints/CollisionGuard.cpp)

target_include_directories(LotroAbcConverter PRIVATE Source)

target_compile_definitions(LotroAbcConverter PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:LotroAbcConverter,JUCE_PRODUCT_NAME>"
    JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:LotroAbcConverter,JUCE_VERSION>")

target_link_libraries(LotroAbcConverter PRIVATE
    juce::juce_audio_basics
    juce::juce_audio_formats
    juce::juce_core
    juce::juce_events
    juce::juce_graphics
    juce::juce_gui_basics
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags)

# Copy drum_map.json next to the exe on build
add_custom_command(TARGET LotroAbcConverter POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_SOURCE_DIR}/Resources/drum_map.json
        $<TARGET_FILE_DIR:LotroAbcConverter>/drum_map.json)

enable_testing()
add_subdirectory(Tests)
```

### 4.3 Build commands (Windows)

```powershell
git clone <your repo>
cd LotroAbcConverter
git submodule add https://github.com/juce-framework/JUCE.git JUCE
git submodule add https://github.com/catchorg/Catch2.git Tests/Catch2
cmake -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
ctest --test-dir build --output-on-failure
```

Exe will land at `build/LotroAbcConverter_artefacts/Release/LOTRO ABC Converter.exe`.

---

## 5. Implementation Notes (per module)

### 5.1 MidiImporter

```cpp
// Signature
Song importMidiFile(const juce::File& file);
```

- Use `juce::MidiFile::readFrom(juce::FileInputStream&)`.
- Call `midiFile.convertTimestampTicksToSeconds()` **only** if you need seconds; keep tick-based processing throughout.
- `ticksPerQuarter = midiFile.getTimeFormat()`. If negative (SMPTE), reject the file with a clear error.
- Iterate tracks: `for (int i = 0; i < midiFile.getNumTracks(); ++i)`.
- For each track, walk the `juce::MidiMessageSequence`:
  - NoteOn with velocity > 0 → push a pending note; match with NoteOff (or NoteOn velocity 0) of same pitch.
  - Track name meta-event → `track.name`.
  - If any note-on message is on channel 10 (9 zero-indexed) → `track.sourceMidiChannel = 10`; default `track.instrument = LotroInstrument::Drums`.
- Tempo events (meta type 0x51) → `song.tempoMap`. If none found, default 120 BPM.
- Time signature (meta type 0x58) → `song.meterMap`. Default 4/4.

### 5.2 RangeConstraint

```cpp
void apply(Track& t, const Song&);
```

- Skip if `t.instrument == Drums`.
- For each note: while `pitch < range.lowMidi`, `pitch += 12`; while `pitch > range.highMidi`, `pitch -= 12`. If still out of range (shouldn't happen for 3-octave instruments), drop and log.
- Apply `t.transposeSemitones` first, before clamping.

### 5.3 DurationConstraint

- Compute quantization grid: `gridTicks = ticksPerQuarter / 4` (1/16 note grid at L:1/8).
- For each note: round `startTick` and `endTick` (= start + duration) to nearest grid. Recompute `durationTicks`. Drop if `durationTicks == 0`.
- Split notes longer than `ticksPerQuarter * 4` (one whole note in 4/4 at L:1/8) at bar boundaries. Represented as tied notes in the writer (use `-` between note tokens).
- Bar boundaries come from meter map: walk bars, compute tick boundary of each.

### 5.4 TempoCollapse

- `mainBpm = tempoMap[0].bpm` (or 120).
- For each tempo change segment `[tick_i, tick_{i+1})`: multiply every affected note's `durationTicks` by `bpm_i / mainBpm`. Start ticks are NOT scaled (they stay on the original grid — only perceived duration changes).
- Stash original tempo changes for the writer to emit as `%%Q:` comments.

### 5.5 ChordConstraint

- Group notes by `startTick` (after quantization). Each group is a potential chord.
- If group size > 6: sort by velocity desc, keep top 6, drop the rest. Log dropped notes.
- Normalize each chord's notes to all end at the earliest end tick among them (shortest wins).

### 5.6 CollisionGuard

- Per pitch, walk notes in start-tick order. If note N's start < note N-1's end: set `N-1.durationTicks = N.startTick - N-1.startTick - 1`. If that makes duration ≤ 0, drop N-1.

### 5.7 DynamicMapper

- Compute a dynamic-change list: ordered list of `(startTick, marking)` where marking buckets differ from prior. AbcWriter consumes this.

### 5.8 AbcWriter

The actual ABC text generator. Output format:

```
% Generated by LotroAbcConverter v0.1
% Source: <original .mid filename>

X:1
T:<song title> - <instrument label>
Z:LotroAbcConverter v0.1
L:1/8
Q:120
M:4/4
K:C
|C2E2G2c2|z8|...

X:2
T:<song title> - <instrument label 2>
...
```

- One `X:` block per **enabled** track, numbered from 1.
- Title suffix is instrument display name (e.g. "My Song - Lute of Ages").
- Compute note token: pitch → letter with octave marks. Use this mapping:
  - MIDI 60 (middle C) = `C`
  - MIDI 48 (one octave below middle C) = `C,`
  - MIDI 36 = `C,,`
  - MIDI 72 = `c`
  - MIDI 84 = `c'`
  - MIDI 96 = `c''`
  - Sharps/flats: prepend `^` for sharp, `_` for flat, `=` for natural when overriding key sig. Determine accidental from current key signature (for v1, just always emit explicit accidentals — simpler and safe; optimize later).
- Duration token: `note_duration_in_eighths` as integer if >1, as `/2` for 1/16, `3/4` for dotted-eighth-half, etc. `1` is implicit (emit nothing after the letter).
- Chord token: `[<note1><note2>...]` no spaces, then shared duration suffix.
- Rest: `z<duration>`.
- Bar lines `|` at every meter bar.
- Line breaks every 4 bars for readability.
- Dynamic markings: emit `+mark+` before the note it applies to.
- Tempo-change comments: `%%Q:<bpm>` on its own line before the bar where the change happens.
- For drums: use the drum map's ABC note string directly (no octave math needed; the map already encodes it).

### 5.9 GUI

Minimum viable:

- **Toolbar row:** "Open MIDI..." button, "Export ABC..." button (disabled until a file loads), "Main Tempo: [spin]" (defaults to detected), "Transpose: [spin semitones]" (applies globally before per-track).
- **Track list:** scrollable `ListBox` with one row per MIDI track. Each row has:
  - Checkbox (enabled).
  - Track name label.
  - Instrument combo box (all LotroInstrument values).
  - Per-track transpose spinner (octaves; ±2 range is plenty).
  - Note count label ("142 notes, 7 out of range before clamp").
- **Log pane:** read-only text at the bottom showing warnings from the constraint pipeline.
- **Title bar** shows current file.

Use JUCE's `FileChooser` for open/export. Default export folder: `%USERPROFILE%\Documents\The Lord of the Rings Online\Music`.

---

## 6. Tests (Catch2)

Minimum test coverage for v1 — these are the tests the agent must write:

**`RangeConstraint_tests.cpp`**
- Note above range gets transposed down octaves until in range.
- Note below range gets transposed up.
- Note already in range is untouched.
- Drum track is skipped entirely.

**`ChordConstraint_tests.cpp`**
- Chord of 4 notes → unchanged.
- Chord of 6 notes → unchanged.
- Chord of 8 notes → trimmed to top 6 by velocity.
- Two notes at same startTick but different lengths → both end at shorter length.

**`DurationConstraint_tests.cpp`**
- Note with duration 1/32 → rounded up to 1/16.
- Note spanning 3 bars at 4/4 → split into 3 tied notes.
- Quantization of a note starting at tick 7 with PPQ 480 → snaps to 0 (grid = 120).

**`TempoCollapse_tests.cpp`**
- Single tempo → no duration change.
- Tempo doubles mid-song → durations in second half halved.
- Tempo change list preserved for writer.

**`CollisionGuard_tests.cpp`**
- Two overlapping notes at same pitch → first one trimmed.
- Two overlapping notes at different pitches → both untouched.

**`AbcWriter_tests.cpp`**
- Single C quarter note at 120 BPM in C major → output contains `C2` and correct header.
- Chord of C-E-G → `[CEG]` or `[C2E2G2]` depending on duration.
- Drum note (GM 38, snare) → emits `F` per drum map.
- Multi-part file has correct `X:1`, `X:2`, ...

**`DrumMap_tests.cpp`**
- All 17 default GM drums in the table map correctly.
- Unmapped GM drum → returns nullopt, caller drops + logs.

**`EndToEnd_tests.cpp`**
- Load a small fixture MIDI (commit 2-3 tiny `.mid` files under `Tests/fixtures/`), run the full pipeline, assert output ABC parses as valid (just structural — no full ABC parser needed; check headers and bar count).

---

## 7. Acceptance Criteria (v1 ship bar)

The app is done when all of these pass:

1. Builds clean on Windows with VS 2022 + CMake, zero warnings at `/W4`.
2. All unit tests pass (`ctest --output-on-failure`).
3. Opens a 3-track MIDI, lets the user assign an instrument to each track, exports an ABC.
4. The exported ABC plays in LOTRO without any "Note's duration is too long/short" or "bad chord" errors, for at least 3 different test songs (one simple melody, one with chords, one with a drum track).
5. Exported ABC passes validation when opened in Maestro (use Maestro as a reference validator).
6. Drum map is externalized to `drum_map.json` and reloads on app start.

---

## 8. Explicit Non-Goals for v1

Do NOT implement these — they're v2+:

- Live audio preview / synthesizer playback.
- Polyphony graph or dissonance graph.
- Section editor (per-region transpose, fermata, delays).
- Mid-song tempo change preservation beyond `%%Q:` comments.
- Pitch bend handling (drop pitch bends silently in v1 — log a warning).
- SMPTE time-format MIDI files (reject with error message).
- Project save/load (`.msx`-equivalent).
- MP3/WAV audio export.
- Multi-language UI.

---

## 9. Licensing Note

- **Maestro** (github.com/NikolaiVChr/maestro and github.com/digero/maestro) is AGPL-3.0. Do not copy its source code. Read its algorithm logic and data tables (drum map, instrument ranges) as reference, then reimplement from scratch.
- **JUCE** — if this will be distributed, review JUCE's license tiers. For personal use or closed-source internal use, the free Personal tier applies below revenue thresholds. Commercial distribution requires a paid JUCE license.
- Pick your own license for this project (MIT or Apache-2.0 recommended if open-sourcing).

---

## 10. Suggested Implementation Order

If the agent asks "where do I start?", this is the order:

1. CMake scaffolding + JUCE/Catch2 submodules + empty hello-world JUCE app that builds.
2. `Note.h`, `Track.h`, `Song.h`, `LotroInstrument.h` — pure data, no logic.
3. `MidiImporter` + its one test against a fixture MIDI.
4. `RangeConstraint` + tests.
5. `DurationConstraint` + tests.
6. `ChordConstraint` + tests.
7. `CollisionGuard` + tests.
8. `TempoCollapse` + tests.
9. `DynamicMapper` + tests.
10. `DrumMap` + `drum_map.json` + tests.
11. `AbcWriter` + tests.
12. GUI (last — it's the easy part once the pipeline is solid).
13. End-to-end test with real LOTRO playback verification.

---

## 11. Open Questions to Resolve During Implementation

These are things the Maestro source will answer definitively — the agent should check the reference project to confirm before hardcoding:

- Exact MIDI-low/MIDI-high values for each instrument (§2.2 table is a good starting point but verify against `LotroInstrument.java` in the Maestro repo).
- Full GM → LOTRO drum map beyond the 17 entries in §2.6.
- Whether Maestro treats the Basic Lute range differently from Lute of Ages (shouldn't, but double-check).
- Exact velocity thresholds for dynamics (§2.7 is a reasonable first cut — Maestro may bucket differently).
