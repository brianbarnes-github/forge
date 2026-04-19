# Findings: drum timing "feels off" on humanized MIDI

**Status:** Known limitation. Not fixing. v0.1 behavior is spec-compliant; groove flattening on humanized source MIDI is an accepted tradeoff.

## Symptom

Drums in the converted `midi/right.abc` sound like they drift or lose the groove compared to `midi/right.mid`. Not a hard-pitched error — just a feel issue.

## What the MIDI actually contains

`midi/right.mid` drum track (channel 10):
- PPQ: 480
- 1,811 drum hits across 10 GM pitches (35, 38, 42, 44, 45, 46, 49, 55, 56, 57)
- Two "grids" are mixed:
  - **On-grid hits** (952 at offset 0, 466 at offset 240, a few at 120/360 — exact 1/16 positions)
  - **Humanized / swung hits** (366 at offset **386** within each beat)

The offset-386 hits are the smoking gun: 386 ticks into a 480-tick beat = 80.4% of the way through — just after the "a" (last 1/16) at tick 360, but not quite on the next beat. This is either:
  - Drummer-style humanization (a consistent 26-tick "push" past the 16th)
  - A swing-16ths feel authored with a non-Western grid

## Why the converter flattens it

`Source/Core/Constraints/DurationConstraint.cpp:43` snaps every note start to the nearest 1/16 grid tick:

```cpp
const int gridTicks  = std::max (1, song.ticksPerQuarter / 4);  // 120 at PPQ 480
const int quantStart = roundToGrid (note.startTick, gridTicks);
```

`roundToGrid` is round-to-nearest. For `startTick = 4226`: `(4226 + 60) / 120 * 120 = 4200`. The hit moves **26 ticks earlier** (~31 ms at 103 BPM). Consistently — same shift on every humanized hit.

Knock-on effect on gaps:
- Pre-snap gap from "offbeat hi-hat" (4080) to "push hi-hat" (4226): **146 ticks**.
- Post-snap gap (4080 → 4200): **120 ticks** (tightened 26).
- Pre-snap gap from push (4226) to next downbeat (4320): **94 ticks**.
- Post-snap gap (4200 → 4320): **120 ticks** (widened 26).

Gaps that were "rushed then laid-back" become "even 16ths". The hi-hat loses its forward lean. Perceptually: the drummer stops pushing and plays on the grid. Not wrong, but not what the MIDI does.

This is **spec §2.4 behavior** ("round note start and end times to the nearest 1/16 of a beat"), so the converter is doing exactly what the spec says. The complaint is really with the spec's chosen grid resolution.

## Decision: accept 1/16 snap, don't chase finer grids

Finer grids (1/32, 1/48, 1/64) were considered and **rejected** for the following reasons:

- The spec's 1/16 grid is the grid LOTRO's ABC Player is reliably quantized against. Going finer means emitting fractional tokens (`/4`, `3/4`, `5/4`) that the player must interpret, and LOTRO's internal rounding behavior at those resolutions isn't documented — what looks sample-accurate in our output can re-round inside the player and cumulatively drift over long songs.
- The snap we do now is **deterministic and non-accumulating**: a hit at tick 386 always moves to 360. The same hit, snapped the same way, every bar. The song loses a groove feature but stays structurally correct to the last bar.
- The error budget for "consistent groove flattening" is bounded and easy to reason about. The error budget for "finer grid that might or might not be honored" is open-ended.
- Spec §2.4 is explicit: "round note start and end times to the nearest 1/16 of a beat." We follow it.

## What we accept

- Humanized drum tracks (consistent off-16th offsets) will have their humanization flattened. The groove sounds more "on the grid" than the MIDI.
- Swing 16ths authored at non-1/16 offsets will sound square.
- Triplet feels approximated by off-grid MIDI positions will be collapsed to the nearest 1/16 (triplets authored with explicit 1/12 grid positions still round cleanly via 1/16, and the `/3` duration token handles the duration side).

If a user strongly needs the humanized groove preserved, they can re-author the MIDI with drum hits placed on the 1/16 grid.

## Considered-and-rejected alternatives

- **Finer grid globally.** Fractional-token fragility. Rejected.
- **Finer grid for drums only.** Same risk, smaller surface; still rejected.
- **`--grid N` CLI flag.** Pushes the fragility decision to the user, who has no way to judge it per song. Rejected.
- **Auto-detect dominant subdivision.** Expensive, still emits finer fractions at the end. Rejected.

## Not a root cause

These were checked and ruled out:
- DrumMap: all 10 pitches in `midi/right.mid`'s drum track are mapped or cleanly dropped. Unmapped (55, 56, 57) are silently discarded, which might also be a minor complaint but isn't what the user reported.
- PolyphonyFlatten: drum slices after flatten are on the 1/16 grid; no off-grid slice boundaries.
- Tempo: single tempo event (103 BPM), `Q:103` emitted, no tempo collapse scaling applied.
- Collision: CollisionGuard doesn't apply to drum tracks in a meaningful way (each hit is a single attack; same-pitch overlaps are rare and handled fine).

## Signals this needs revisiting

If one of these shows up, re-open this decision:
- A user reports *drifting* timing (song gets longer or shorter than the MIDI), not *groove change*. Drift would be a different bug, not this one.
- LOTRO introduces ABC duration tokens finer than 1/16 as a first-class feature.
- We gain a way to validate LOTRO's internal rounding at fine durations empirically.
