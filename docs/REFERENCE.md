# Reference material in this repo

- `drummaps/default.txt`, `drums.txt`, `cymbals.txt` — **Maestro-format
  drum maps, AGPL-licensed reference data only**. Read for GM → LOTRO
  drum-note mappings; do not ship verbatim. The defaults live in
  `Source/Core/DrumMap.cpp`'s `specDefaults` array; users can override
  at runtime via `--drum-map drum_map.json`.
- `drum_map.json` (repo root) — sample JSON reproducing the built-in
  defaults. Edit to customise.
- `midi/*.mid` — test fixtures. `Barnes Brothers Band - Pull The Wires.mid`
  is the end-to-end reference used by `EndToEnd_tests.cpp`.
- `correct right.abc`, `rideintochetwood.abc` — Vydor's 2011-era
  reference outputs, kept as compatibility/regression fixtures for
  the z-pulse encoding. ("Vydor" is Brian's LOTRO handle.) They are
  NOT Maestro outputs; Maestro derived from this work later, not the
  other way around.
- `docs/superpowers/specs/2026-04-23-config-driven-conversion-design.md`
  — full schema for the `--config` JSON/TOML/XML format (see
  `docs/ARCHITECTURE.md` §8 for the implemented behaviour).
- `findings/dynamics.md`, `findings/drum-timing.md` — settled
  investigations behind two of the entries in
  `docs/DELTAS_FROM_SPEC.md`.
