---
name: implementing-a-songsmith-phase
description: Use when implementing any phase of the Songsmith MIDI-editor UI (SongDocument, SongModelBridge, PreviewPipeline, PianoRollComponent, PartStrip, or any other Source/UI component from the Songsmith plan) — before writing tests or code for that phase.
---

# Implementing a Songsmith Phase

Songsmith's full design — data model, translation-boundary functions,
live-preview pipeline, component list, and the 8-phase build order — is
written in
`/home/brian/.claude/plans/lets-talk-about-how-optimized-marble.md`. Read
the section for the phase you're implementing before writing anything;
this skill is the workflow wrapper around that plan, not a replacement
for it.

**REQUIRED SUB-SKILLS:** `juce-valuetree-conventions` (id/undo/naming
rules for any `SongDocument`/`ValueTree` code) and
`forge-engine-ui-boundary` (what belongs in `forge_core` vs. the UI) —
load both if the phase touches either area.

## Workflow

1. **Re-read the plan's phase entry** for what you're building — file
   list, effort/risk callout, and the phase's "Verification" step.
2. **TDD, per `superpowers:test-driven-development`.** For pure logic
   (`SongDocument`, `SongModelBridge`, `PreviewPipeline`,
   `PreviewNoteDiff`) write a failing Catch2 test first, in `Tests/`,
   following the existing style (see `Tests/Provenance_tests.cpp` for a
   representative example: local helper functions building fixtures, one
   `TEST_CASE` per behavior, tag in brackets e.g. `"[songdocument]"`).
   Add the new `.cpp` to `Tests/CMakeLists.txt`'s `forge_tests` sources.
3. **Build and run:**
   ```
   cmake --build build
   ctest --test-dir build -R <NewTestTag> --output-on-failure
   ```
   For UI-only work with no meaningful headless test (piano-roll paint
   code, drag-and-drop), build `forge_ui` and manually exercise it via
   `./run-ui.sh` per the plan's Phase 4-8 verification recipe — say so
   explicitly rather than claiming test coverage that doesn't exist.
4. **Cross-check against the CLI where the plan says to.** Phase 3's
   verification is a literal diff against
   `build/forge_artefacts/Debug/forge`'s ad-hoc-mode output on the same
   MIDI file — don't skip this just because the new code "looks right."
5. **Full suite must stay green:** `ctest --test-dir build
   --output-on-failure` — every pre-existing test plus whatever
   this phase added. A regression in an existing `forge_core`
   test means something leaked into the engine that shouldn't have (see
   `forge-engine-ui-boundary`).
6. **Commit** with a conventional-commit prefix (`feat:`, `fix:`,
   `refactor:`) per `CLAUDE.md`'s Git Conventions, scoped to just this
   phase's change — don't bundle two phases into one commit even if both
   are done.

## Common mistakes

| Mistake | Why it bites later |
|---|---|
| Writing `PianoRollComponent` or `SongModelBridge` code before a Catch2 test exists for the logic it's not purely visual | Silent wrong-output bugs (per the plan: this is "the seam where index spaces meet" — bugs here don't crash, they produce wrong ABC) |
| Skipping the Phase 3 CLI-diff check because "the pipeline is unchanged, why would it differ" | The bridge/translation layer is new code even though the pipeline isn't — that's exactly what needs checking |
| Deleting `EditorPane`/`InstrumentsTree`/property-page classes before Phase 6 is actually done | Plan explicitly says delete only once `SongsmithMainComponent` covers their functionality — deleting early leaves no working fallback UI |
| Adding a new `Config`/`ConfigSource` field to support a UI feature (quantize, an editing convenience) | See `forge-engine-ui-boundary` — this is very likely the wrong layer |
