# Findings: drum timing "feels off" on humanized MIDI

**Status:** Resolved by removing quantization. This document is kept for
historical context — the original decision (accept 1/16 snap) was
reconsidered after empirical playback testing showed the snap itself was
the audible problem, not a solution to it.

## Summary of the investigation

1. **Original complaint.** Drums in `midi/right.abc` didn't groove the
   same as `midi/right.mid`. Side-by-side comparison pinned the
   difference on hi-hat hits: the MIDI had 366 hits at tick offset 386
   within each beat (just past "the a"), and the converter was snapping
   those to offset 360 (on "the a"). A consistent 26-tick forward shift.
2. **First take** (this doc, pre-revision): accept the snap as
   spec-compliant behaviour; document that humanized MIDI will be
   flattened; recommend users re-author on the 1/16 grid if they care.
3. **Second take** (after user pushback): remove quantization entirely.
   The user's reasoning — fine-grained fractional durations produce
   fragile output that LOTRO may re-round internally, so let's preserve
   exact ticks and trust LOTRO's playback — turned out to be correct
   when tested. Exact-tick emission sounded right; snapped emission did
   not.

## Current behaviour

- `DurationConstraint` no longer quantizes — it only drops zero-duration
  notes. The 1/16-grid rounding code is gone (git log
  `Source/Core/Constraints/DurationConstraint.cpp` shows the shrinkage).
- Every MIDI tick is preserved exactly into the ABC output. Fractional
  duration tokens like `c31/80` appear freely and are correct.
- `BarAlignment_tests.cpp` pins the invariant that each `% bar N` block
  sums to exactly one bar of ticks — bar structure stays aligned even
  though individual note positions are off-grid.

## Why the original "snap to 1/16" argument was wrong

- The spec's §2.4 instruction to round to 1/16 was treating LOTRO as
  more fragile than it actually is. ABC Player accepts fractional
  durations like `/30` fine.
- "Deterministic and non-accumulating" was true, but the snap was
  *systematically wrong* — every humanized hit drifted by the same
  26 ticks, which is far more audible than the author of that argument
  appreciated.
- The 2011-era reference (`rideintochetwood.abc` in the repo root)
  emits un-snapped fractional durations everywhere. That file plays
  correctly in LOTRO to this day.

## Deltas vs spec §2.4

The spec mandates:
- Round note start and end times to the nearest 1/16 — **ignored.**
- Minimum duration 1/16, rounded up — **ignored** (we emit short notes
  at their true duration).
- Split long notes at bar lines with tied segments — **ignored** (ties
  aren't used; see `findings` on the cluster-at-boundary with z-pulse
  emission model in `AbcWriter.cpp`'s header comment).

These deltas are documented in `CLAUDE.md` under "Deltas from the spec".

## Signals this should be revisited

- If a user reports drifting timing (song gets longer/shorter than the
  MIDI), investigate `ChordEmitter` / `AbcWriter` tick accounting, not
  `DurationConstraint`.
- If LOTRO updates break fractional-duration handling, we'd have to
  reintroduce quantization. Unlikely — ABC fractional durations have
  been stable since the format was defined.
