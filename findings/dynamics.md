# Findings: dynamics handling

**Status:** v0.1 workaround in place — production fix deferred.

## Symptom

Generated ABC emits stacked dynamic markings like `+ff++mp+` or `+p++mp+` before a single note. ABC Player accepts this (the last marking wins per standard ABC decoration stacking), but:

- The output is ugly and confusing to anyone editing the ABC by hand.
- Information is thrown away — the "quieter" dynamic in the stack has no effect on playback.
- A stricter ABC validator may reject it.

Seen on `midi/land.mid`: 142 occurrences in a single conversion.

## Root cause

`Source/Core/Constraints/DynamicMapper.cpp` walks `track.notes` sequentially and emits a change whenever the current note's velocity bucket differs from `previous`. After `PolyphonyFlatten` produces per-slice chords, each slice can contain several notes with different velocities — so DynamicMapper emits a change per note, not per slice, producing stacks at the same tick.

LOTRO ABC supports only a single inline `+xx+` marking that applies to a voice from that point forward. There is no per-chord-note dynamic; whatever the tick-level dynamic is applies to everything sounding.

## Current workaround (v0.1)

**Loudest-at-each-tick.** When multiple notes share a `startTick`, use the maximum velocity among them to determine the bucket. Emit at most one change per tick, and only when that tick's bucket differs from the previous tick's bucket.

This preserves the most audibly-significant dynamic at each moment at the cost of losing softer voices' dynamic detail. It's cheap, deterministic, and produces valid ABC.

## Known limitations of the workaround

1. **Softer voices get masked.** A soft sustained pad underneath a loud melody gets the melody's dynamic. Fine for melody-dominant material, wrong for ensemble balance.
2. **Rapid velocity variance produces rapid markings.** A melody with expressive per-note dynamics (normal for human-played MIDI) still emits a change per tick when the bucket boundary is crossed. LOTRO plays that faithfully but it clutters the ABC.
3. **No de-buzz / smoothing.** A one-note blip into `+ff+` then back to `+mp+` is emitted as written, even if it lasts one 1/16th note.

## Production fix options (pick one when we get there)

### A. Per-voice dynamics via ABC `V:` voices
Split track into N voices (one per source MIDI note that gets its own dynamic), emit `V:1` / `V:2` / etc. Each voice gets its own dynamic track. **LOTRO does not honor `V:` in ABC — ignored.** Option is out unless LOTRO adds voice support.

### B. Chord-centroid dynamic
Instead of loudest-wins, take the **velocity median** (or average) of all notes in a chord. More faithful to ensemble feel; still one marking per tick.

### C. Hysteresis / minimum-duration threshold
Only emit a change if the new bucket holds for ≥ N ticks, or differs by ≥ 2 buckets from the previous. Smooths out noisy velocity data. Pair with any of A/B.

### D. Per-voice velocity baseline
Record the dominant velocity per source-note. Drop all dynamic changes that are just "slice velocity drift" within one source note's lifetime. Requires threading a source-id through `PolyphonyFlatten` into `DynamicMapper`.

**Recommended combo for production:** B + C. Median gives a more musical result than loudest, and hysteresis kills the micro-change clutter.

## Files touched by the workaround

- `Source/Core/Constraints/DynamicMapper.cpp` — rewritten to group notes by `startTick` and take the max velocity per group before bucketing.
- `Tests/DynamicMapper_tests.cpp` — added a multi-note-same-tick test.

## How to verify

```bash
build/converter_artefacts/Debug/converter midi/land.mid /tmp/land.abc
grep -cE '\+[a-z]+\+\+[a-z]+\+' /tmp/land.abc
# expect: 0
```
