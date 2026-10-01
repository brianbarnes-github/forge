---
name: forge-engine-ui-boundary
description: Use before adding any field, parameter, or logic to Source/Core (Config, ConfigSource, ConfigInstrument, InstrumentAssembly, or any Constraints/*.cpp pass) — and before proposing where quantize, snap, humanize, or any other note-timing/editing feature should live.
---

# forge_core Does Conversion, Not Editing

`forge_core` converts MIDI-derived notes to LOTRO ABC. It does not edit
notes. If a feature lets a user reshape their music (quantize/snap
timing, humanize, manual note nudges, anything with a
"before/after preview of *my* edit") the logic goes in `Source/UI/*`,
operating on Songsmith's `SongDocument` note data, **before** those notes
are ever handed to `forge_core` via `SongModelBridge`. `forge_core` sees
only final tick/pitch/duration values — it must never gain a
`Config`/`ConfigSource` field or a `Constraints/*` pass whose job is
"make editing easier."

## Why this is a hard line, not a style preference

Two independent sources say so:

1. **`CLAUDE.md`'s guiding principle:** *"No smoothing, hysteresis,
   quantization, or 'clean-up' passes... Transformations are only
   acceptable when LOTRO's parser forces our hand"* — and the enumerated
   forced list is exactly: range clamp (instrument can't play out-of-range
   notes), chord cap (LOTRO's 6-note limit), single `Q:`/`M:` per part,
   single dynamic per instrument, same-pitch collision guard. Quantize
   isn't on that list.
2. **It was tried and reverted.** `docs/DELTAS_FROM_SPEC.md` and
   `findings/drum-timing.md` document that spec §2.4's quantize-to-1/16
   pass was implemented, then removed after empirical playback testing
   showed the snap itself introduced audible drift on humanized MIDI.
3. **The Songsmith plan's explicit architecture decision** (see
   `/home/brian/.claude/plans/lets-talk-about-how-optimized-marble.md`):
   quantize is a piano-roll editing *action* on `SourceMidi.Tracks[]`,
   never a `Config`/pipeline parameter — stated directly by the project
   owner: *"the converter should not being doing any note timing changes,
   it just converts to ABC."*

## Rationalization table

| Rationalization | Reality |
|---|---|
| "It's opt-in / per-source, not mandatory or global like the reverted pass — that's materially different" | Opt-in-ness doesn't change *where* the logic belongs. The UI can offer an opt-in quantize tool that never touches `forge_core` at all — Songsmith edits the note's `startTick` directly in `SongDocument` before export. Putting it in `Config` makes the engine stateful about an editing decision the UI is fully capable of applying upstream. |
| "The grid needs to vary per source, so it has to live in `ConfigSource`" | This problem doesn't exist once notes are edited before assembly — by the time notes reach `forge_core`, they already have their final tick values. No per-source engine parameter is needed. |
| "It's just one int field, doesn't hurt anything" | Every added `Config`/pipeline field is a permanent engine surface others build on. `docs/DELTAS_FROM_SPEC.md` exists because this exact feature was added once and had to be surgically removed after real-world audio testing. |
| "The pipeline already loops per-source in `InstrumentAssembly.cpp`, so it's the natural place to add one more transform" | Convenience of the existing loop is not evidence of correct ownership. Transpose/volume live there because they're config-time *scaling* the engine must apply consistently across formats/CLI/GUI — quantize is a one-time *edit* a human makes once, interactively, with visual feedback the engine has no way to provide. |
| "The guiding principle is about the converter deciding for the user; here the user is choosing it themselves via a config field" | The user chooses it themselves by using the piano-roll quantize tool in the UI — that already satisfies "the user chose it." Routing the same choice through a `Config` file field adds a second, redundant, unreviewed path into the engine for no benefit. |

## Red flags — stop and reroute to Source/UI

- Proposing a new field on `Config`, `ConfigInstrument`, or `ConfigSource`
  for anything framed as "snap," "quantize," "grid," "humanize," or
  "clean up."
- Proposing a new `Constraints/*.cpp` pass for anything not in the
  forced-transform list above.
- "I'll add validation for the new field in `validateConfig`" as the
  first design step for an editing feature — if you're validating a
  timing-edit parameter in `Config.cpp`, it's already in the wrong file.
- Finding `CLAUDE.md`'s principle, noting the tension, and proceeding
  anyway with a rationale for why *this* case is different. Every case
  will feel different in the moment — that's what the rationalization
  table above is for.

## What legitimately does belong in forge_core

Range clamp (`RangeConstraint`), chord cap (`ChordConstraint`), duration
drop (`DurationConstraint`), tempo/meter bake-in (`TempoCollapse`),
same-pitch overlap trim (`CollisionGuard`), loudest-per-tick dynamics
(`DynamicMapper`) — all forced by LOTRO's ABC parser, all already in
`Source/Core/Constraints/`. If a new requirement is *provably* forced by
a LOTRO parser limitation (not a nice-to-have), it may belong here — but
confirm that forcing fact explicitly, in the same way `docs/DELTAS_FROM_SPEC.md`
documents each existing pass's justification, before adding one.
