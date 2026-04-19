# Findings: dynamics handling

**Status:** v0.1 workaround ("loudest-per-tick") is in place and
shipping. Production refinements deferred until a GUI is available to
drive the decisions.

## Symptom

Before the fix, generated ABC emitted stacked dynamic markings like
`+ff++mp+` or `+p++mp+` before a single note. ABC Player accepts this
(the last marking wins per standard ABC decoration stacking), but:
- The output is ugly and confusing to anyone editing the ABC by hand.
- Information is thrown away — the quieter dynamic in the stack has no
  effect on playback.
- A stricter ABC validator may reject it.

Seen on `midi/land.mid`: 142 occurrences in a single conversion
(pre-fix).

## Root cause

Originally, `DynamicMapper.cpp` walked `track.notes` sequentially and
emitted a change whenever the current note's velocity bucket differed
from the previous. Simultaneous-start notes in a chord frequently fall
into different velocity buckets (common in human-played MIDI), so
multiple changes could fire at the same tick.

(Historical note: this was first observed with a short-lived
`PolyphonyFlatten` pass that produced per-slice chords. That pass has
since been removed — the z-pulse emission in `AbcWriter` supersedes it.
The dynamics problem remained because clusters naturally group notes
with different velocities.)

## Current fix: loudest-per-tick

`DynamicMapper.cpp` groups notes by `startTick` and takes the **maximum
velocity** within each group before bucketing. A dynamic change emits
at most once per tick, and only when that tick's bucket differs from the
previous tick's bucket.

Verification:
```bash
build/converter_artefacts/Debug/converter midi/land.mid /tmp/land.abc
grep -cE '\+[a-z]+\+\+[a-z]+\+' /tmp/land.abc
# expect: 0
```

## Known limitations of the workaround

1. **Softer voices get masked.** A soft sustained pad underneath a loud
   melody gets the melody's dynamic. Fine for melody-dominant material,
   wrong for ensemble balance.
2. **Rapid velocity variance produces rapid markings.** A melody with
   expressive per-note dynamics (normal for human-played MIDI) still
   emits a change per tick when the bucket boundary is crossed. LOTRO
   plays that faithfully but the ABC output clutters.
3. **No hysteresis.** A one-note blip into `+ff+` then back to `+mp+`
   is emitted as written, even if it lasts one 1/16th note.

## Production fix options (pick when a GUI exists)

### A. Chord-centroid dynamic
Instead of loudest-wins, take the **velocity median** (or average) of
all notes at the tick. More faithful to ensemble feel; still one
marking per tick.

### B. Hysteresis / minimum-duration threshold
Only emit a change if the new bucket holds for ≥ N ticks, or differs
by ≥ 2 buckets from the previous. Smooths out noisy velocity data.
Pair with A.

### C. Per-source-note velocity baseline
Drop dynamic changes that are just "slice velocity drift" within one
source note's lifetime. With `Note::sourceTrackIndex` /
`sourceEventIndex` now available, DynamicMapper can identify these.

### D. Per-voice dynamics via ABC `V:` voices
Split into voices, each with its own dynamic track. **LOTRO does not
honor `V:` in ABC — ignored.** Out unless LOTRO adds voice support.

**Recommended combo for production:** A + B. Median gives a more
musical result than loudest; hysteresis kills the micro-change clutter.

## Files touched by the workaround

- `Source/Core/Constraints/DynamicMapper.cpp` — groups notes by
  `startTick` and takes max velocity per group before bucketing.
- `Tests/DynamicMapper_tests.cpp` — multi-note-same-tick regression
  test.
