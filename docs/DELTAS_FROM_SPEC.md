# Deltas from the spec

The original spec is `lotro-abc-converter-spec.md` (560 lines). These
are intentional deviations from it, verified against test fixtures —
don't assume a spec section is still authoritative without checking here
first.

- **Spec §2.4 "quantize to 1/16 grid" was removed.** We preserve exact
  MIDI tick positions. See `findings/drum-timing.md` for context —
  empirical testing showed quantization introduced audible drift; the
  user-requested "preserve exact ticks" emission proved accurate when
  playback was verified against the MIDI source.
- **Spec §2.4 "split long notes at bar lines with ties"**: we don't
  split note tokens. `DurationConstraint` passes notes through
  unchanged except for dropping zero-duration ones. Long sustains
  survive intact inside cluster chord tokens.
- **Ties** (`-`): removed entirely. Not needed with the z-pulse
  emission model.
- **PolyphonyFlatten**: existed briefly, removed. The z-pulse trick
  supersedes slice-based flattening and eliminates the re-articulation
  problem for held voices.
- **ABC `L:1/8`** is still the fixed default length. `Q:` `M:` `K:`
  headers match the spec format.
- **Mid-song tempo changes**: LOTRO honours only the first `Q:` per
  part, so `TempoCollapse` absorbs all tempo changes into scaled
  `startTick` + `durationTicks` values under a single fixed `Q:`.
  Rest gaps stretch or compress with the tempo just like notes do.
- **Mid-song meter changes**: LOTRO also honours only the first `M:`.
  `applyTempoCollapseToSongMaps` rescales `meterMap[i].tick` into
  stream-tick space, and `ChordEmitter` looks up the current bar
  length per bar boundary so `% bar N` labels track the real bar
  structure even with a single emitted `M:`.
- **`% tempo:` and `% meter:` comments**: emitted at the bar where a
  change takes effect (LOTRO ignores comments; purely diagnostic for
  humans).
- **Dynamics**: `DynamicMapper` does loudest-per-tick bucketing. This
  is the correct end state under LOTRO's one-dynamic-per-instrument
  constraint, not a workaround. See `findings/dynamics.md` — the
  previously-proposed smoothing/hysteresis/centroid passes are
  retired under the MIDI-is-truth principle (see `CLAUDE.md`'s
  guiding principle).
- **Songsmith's MIDI reader is strict; the CLI's is not.** Songsmith
  imports through `RawMidi` as well as `importMidi` and refuses a file
  if either fails or they disagree, so a truncated file or one with
  system-common bytes that the CLI still converts is rejected by the GUI
  with an Error diagnostic. There is no notes-only fallback.

See `docs/ARCHITECTURE.md` for how the current (post-delta) pipeline
actually works.
