# Findings: dynamics handling

**Status:** Settled. `DynamicMapper` emits at most one marking per tick,
using the loudest velocity within each tick-cluster. This is the correct
end state — not a workaround — and no further smoothing, hysteresis, or
centroid-pooling passes should be added.

## The original symptom

Before the loudest-per-tick fix, generated ABC emitted stacked markings
like `+ff++mp+` before a single note. ABC Player accepts this (the last
marking wins per standard decoration stacking), but:

- Output is ugly to anyone editing by hand.
- Information is thrown away — the quieter stacked dynamic has no
  effect on playback.
- A strict ABC validator may reject it.

Seen on `midi/land.mid`: 142 occurrences in a single conversion pre-fix.

## Root cause

Originally, `DynamicMapper.cpp` walked `track.notes` sequentially and
emitted a change whenever the current note's velocity bucket differed
from the previous. Simultaneous-start notes in a chord frequently fall
into different velocity buckets (common in human-played MIDI), so
multiple changes could fire at the same tick.

(Historical note: this was first observed with a short-lived
`PolyphonyFlatten` pass that produced per-slice chords. That pass has
since been removed — z-pulse emission in `AbcWriter` supersedes it. The
dynamics stacking remained because clusters naturally group notes with
different velocities, which is unrelated to slicing.)

## Current design: loudest-per-tick

`DynamicMapper.cpp` groups notes by `startTick` and takes the **maximum
velocity** within each group before bucketing. A dynamic change emits
at most once per tick, and only when that tick's bucket differs from
the previous tick's bucket.

Verification:
```bash
build/converter_artefacts/Debug/converter midi/land.mid /tmp/land.abc
grep -cE '\+[a-z]+\+\+[a-z]+\+' /tmp/land.abc
# expect: 0
```

## Why loudest-wins is correct, not a compromise

LOTRO's ABC player has a **single active dynamic per instrument at any
moment** — every note the instrument plays sounds at whatever dynamic
was most recently set. If `DynamicMapper` aggregated per-tick
velocities using the median or the mean (a chord-centroid approach),
the loudest voice — typically the melody — would sound at a softer
dynamic than the author intended, and worst case become inaudible
against other instruments.

Loudest-wins guarantees the loudest voice in any cluster is always at
least as loud as authored. Softer voices ride along at that same
dynamic. This is the minimum necessary editorial decision under the
one-dynamic-per-instrument constraint; nothing weaker preserves
audibility.

## Why there will be no smoothing / hysteresis pass

The converter's guiding principle is that the MIDI is the source of
truth and the ABC should reflect it as literally as possible, with the
fewest editorial decisions the converter can get away with. Any
hysteresis, drift-suppression, or threshold-based smoothing would
deliberately hide detail the user authored in the MIDI — even if it's
"only cosmetic" (e.g. collapsing a `+mp++mf+` drift caused by velocity
jitter near a bucket boundary).

If a real song produces cluttered dynamics in the output, the fix is
in the source MIDI (flatten the velocities) or in the user's
expectations (that's what they played), not in the converter.

## Retired proposals

Earlier versions of this doc listed four "production fix options" for
`DynamicMapper`:

- **A. Chord-centroid (median/mean) velocity pooling.** Retired:
  violates loudest-wins under the one-dynamic-per-instrument
  constraint.
- **B. Duration- or magnitude-based hysteresis.** Retired: suppresses
  detail that the user authored. Contrary to the MIDI-is-truth
  principle.
- **C. Per-source-note velocity baseline using `Note::sourceEventIndex`.**
  Retired: this was aimed at a slice-based emission model
  (`PolyphonyFlatten`) that no longer exists. Each `Note` now maps
  1:1 with a source MIDI note-on, so there is no intra-note velocity
  drift to suppress.
- **D. Per-voice dynamics via ABC `V:` voices.** Retired: LOTRO does
  not honour `V:` in ABC.

The previous "recommended combo A + B" guidance in this doc is
**withdrawn**. Do not propose or implement either.

## Files and tests

- `Source/Core/Constraints/DynamicMapper.cpp` — groups notes by
  `startTick`, takes max velocity per group before bucketing.
- `Tests/DynamicMapper_tests.cpp` — multi-note-same-tick regression
  test plus base cases.

## Signals this should be revisited

- A LOTRO client update that changes the one-dynamic-per-instrument
  constraint (e.g. honouring `V:` voices with independent dynamics).
  Then per-voice dynamics open up.
- A complaint that a specific song's ABC is less audible than the
  MIDI suggests. Investigation should start at the bucket boundaries
  in `bucketForVelocity` or the MIDI's own velocities, not at adding
  a smoothing pass.
