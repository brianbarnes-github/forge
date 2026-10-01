---
name: juce-valuetree-conventions
description: Use when reading, writing, or reviewing code that touches Songsmith's SongDocument ValueTree (Source/UI/SongDocument.*, SongModelBridge.*, or any Source/UI component that mutates Song/Parts/Assignments state) — covers id schemes, undo-transaction boundaries, and property naming.
---

# Songsmith ValueTree Conventions

Reference for the `SongDocument` data model defined in
`/home/brian/.claude/plans/lets-talk-about-how-optimized-marble.md`
("Songsmith: JUCE MIDI Editor + LOTRO Arrangement UI"). Read that plan's
"Data model — ValueTree schema" section for the full node/property layout
before writing any `SongDocument`/`SongModelBridge` code — this skill only
covers the conventions that are easy to get subtly wrong.

## Synthetic ids, not array indices

`MIDI_TRACK.trackId` and `PART.partId` are monotonically-increasing
`int64` values minted by `SongDocument`, never the child's position in its
parent. `SourceMidi.Tracks[]` accumulates across multiple MIDI imports, so
a positional index would collide/shift on re-import. `Assignment.trackId`
always references a synthetic id, resolved to a *positional* index only at
the `SongModelBridge::buildConfigAndRawSong` boundary (because
`Config.midiTrackIndex` is positional into the flattened raw `Song`). Never
resolve a synthetic id to an index that is then *used as an index*
anywhere except that one bridge function. One display-only exception is
allowed and exists: UI labels such as the assignment chip's `Tk<n>`
(`AssignmentChipComponent`) may call `indexOf` on the live tree to show a
1-based position, re-resolved on every paint and never stored or used to
address anything.

## Undo transaction boundaries

One `juce::UndoManager` per document, passed to *every*
`ValueTree::setProperty`/`addChild`/`removeChild` call — a single raw
mutation (passing `nullptr` where an `UndoManager*` belongs) silently
breaks undo for that action with no compile error and no visible symptom
until a user hits Ctrl+Z. Rules:
- **Bulk MIDI import** (`appendImportedSong`) bypasses undo entirely — use
  `nullptr` for the UndoManager. Importing hundreds of notes is not a
  user-undoable "edit."
- **Everything else** (note create/move/resize/delete, quantize, drag-drop
  assignment, property-page edits) goes through the UndoManager.
- **One gesture = one transaction.** A note drag calls
  `undoManager.beginNewTransaction()` on mouse-up, not per mouse-move — an
  in-progress drag should coalesce into a single undo step.

## Property naming mirrors forge_core

`NOTE` node properties (`pitch`, `startTick`, `durationTicks`, `velocity`,
`isDrum`, `sourceTrackIndex`, `sourceEventIndex`) are named identically to
`lotro::Note`'s fields (`Source/Core/Note.h`) — this is deliberate so the
translation layer is a straight property copy, not a renaming exercise.
Don't invent alternate names (e.g. `tick` instead of `startTick`) for
"brevity" — it just adds a mapping step for zero benefit.

## Provenance is the join key

`sourceTrackIndex`/`sourceEventIndex` on a `NOTE` node must always be
copied verbatim from the imported `forge_core::Note` and never
regenerated or defaulted. Anything downstream that computes a preview diff
(`PreviewNoteDiff`) or maps a `Diagnostic` back to a note joins on this
pair — a note with a zeroed-out or reassigned provenance pair breaks that
join silently (wrong note gets flagged, not a crash).

## `addListener` needs a persistent handle, never a temporary

`juce::ValueTree::addListener` stores the `Listener*` on the `ValueTree`
**handle object itself** (`listeners` is a per-handle `ListenerList`
member — see `JUCE/modules/juce_data_structures/values/juce_ValueTree.cpp`),
not on the shared tree data. The underlying `SharedObject` only keeps a
raw back-pointer to *that handle*. So:

```cpp
// WRONG — compiles, runs, registers nothing observable:
doc.getSourceMidiNode().addListener (this);
```

The temporary returned by `getSourceMidiNode()` is destroyed at the end
of that statement, taking its `listeners` list — and the registration —
with it. No warning, no crash: the component just never hears about
document changes again, silently, forever. This is exactly the bug found
2026-09-11 in `TrackListComponent`/`PartStripComponent` (latent since
Phase 4): the track list and part strip never rebuilt after a real MIDI
import or drag-and-drop assignment in the actual running app, despite
`doc` mutating correctly and every `ctest` test passing.

Fix: store the node as a member, initialized in the constructor's
init-list, and call `addListener`/`removeListener` on *that member* in
the constructor/destructor:

```cpp
class Foo : private juce::ValueTree::Listener
{
    SongDocument&   doc;
    juce::ValueTree sourceMidiNode;   // persists for Foo's lifetime
    Foo (SongDocument& d) : doc (d), sourceMidiNode (d.getSourceMidiNode())
    {
        sourceMidiNode.addListener (this);
    }
    ~Foo() override { sourceMidiNode.removeListener (this); }
};
```

Reads elsewhere (`getNumTracks()`, `getTrack(i)`, etc.) don't need this —
only `addListener`/`removeListener` call sites do.

**Why every existing test missed this:** `TrackListComponent_tests.cpp`
drove `rebuild()`/`selectTrack()` directly via a friend-access struct,
bypassing the real `ValueTree::Listener → AsyncUpdater → rebuild()` chain
entirely. Testing a `ValueTree`-reactive component's wiring (not just its
rebuild logic) requires a real mutation through a real pumped JUCE
message loop:

```cpp
juce::ScopedJuceInitialiser_GUI juceInit;   // once per test needing this
// ... real component construction, real doc mutation ...
juce::MessageManager::getInstance()->runDispatchLoopUntil (300);
// NOT runDispatchLoop()+stopDispatchLoop() — stopDispatchLoop() is a
// one-shot latch on MessageManager's process-wide singleton, easy to
// leave stale across unrelated tests sharing the same test binary.
// runDispatchLoopUntil needs JUCE_MODAL_LOOPS_PERMITTED=1 on the target.
```

## Quick check before committing ValueTree code

- [ ] Every mutation site passes the document's real `UndoManager*`, except the designated bulk-import path.
- [ ] No code resolves an `Assignment.trackId` to a positional index outside `SongModelBridge`.
- [ ] `NOTE` property names match `lotro::Note` field names exactly.
- [ ] `sourceTrackIndex`/`sourceEventIndex` are copied from the source `Note`, never synthesized.
- [ ] Every `addListener`/`removeListener` call site uses a persistent `ValueTree` member, never a fresh `doc.getXNode()` temporary.
- [ ] Any new `ValueTree`-reactive component has at least one test that mutates the document for real and pumps a real JUCE message loop — not just a friend-access `rebuild()` call.
