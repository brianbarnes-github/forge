# Songsmith Song File Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Give Songsmith a binary `.songsmith` Song file (New / Open / Close / Save / Save As, with an unsaved-changes guard) that embeds every track, note, event, part and assignment, and restructure the File menu into Import ▸ / Export ▸.

**Architecture:** `SongFile` serializes the `SONG` ValueTree into a small binary container (magic, version, lengths, CRC-32, gzip payload). `SongDocument` gains `validateLoaded` / `replaceContents` / `resetToEmpty`, which restore a Song *in place* so the components' listener handles on `PARTS` / `SOURCE_MIDI` / `METER_MAP` stay valid. `SongSession` tracks the current file and dirty flag; `DiscardGuard` sequences the async Save / Don't Save / Cancel prompt; `SongsmithMainComponent::documentReplaced()` resets UI state that lives outside the tree. `MainWindow` is wired last. `forge_core` is not changed.

**Tech Stack:** C++17, JUCE 8 (`juce_core` gzip/streams, `juce_data_structures`, `juce_gui_basics`), Catch2 v3, CMake + Ninja.

**Spec:** `docs/superpowers/specs/2026-10-03-songsmith-song-file-design.md` — read it first; this plan argues from it. **One deliberate refinement of the spec:** the container header also stores a `uint32 crc32` of the *uncompressed* payload (Task 3), because `GZIPDecompressorInputStream` does not expose a failed zlib trailer check, so gzip's own CRC alone cannot be relied on to detect damage. Task 3 Step 1 amends the spec to match. No new dependency or JUCE module is added.

## Global Constraints

- `Source/Core/**` is not modified (see `forge-engine-ui-boundary`). No new fields on `Song`, `Track`, `Note`, `Config*`.
- Container (little-endian): magic `"SGSM"` (4 bytes), `uint32 formatVersion` (starts at 1; one number covers container and tree schema), `uint64 uncompressedLength`, `uint32 crc32` (of the uncompressed payload), `uint64 payloadLength`, then the gzip payload of `ValueTree::writeToStream(SONG)`. Header = 28 bytes.
- Extension `.songsmith`. Binary only. **No autosave, no recovery files.** Saving is always an explicit user action.
- No new dependencies and no new JUCE modules: gzip and streams are `juce_core`; `juce_cryptography` / SHA-256 is NOT used.
- Undo history is not saved. `nextTrackId`, `nextPartId` and the new `nextImportBatch` live on `SONG` and are saved.
- Custom error type `SongFileError` (kinds: `NotASongFile`, `UnsupportedVersion`, `Truncated`, `ChecksumMismatch`, `Corrupt`, `InvalidStructure`); no generic string errors.
- Read order: magic → version → lengths → decompress + length + CRC → only then `ValueTree::readFromStream` (Debug builds `jassertfalse` on garbage) → `validateLoaded`.
- A failed save never touches the existing file: build the whole buffer first, then `File::replaceWithData`.
- Config items are removed from the menu: Open Config, Save Config As, and the config drag-and-drop stub. Config import/export is out of scope.
- The unsaved-changes guard is asynchronous (callback chain). Never use a modal loop (`JUCE_MODAL_LOOPS_PERMITTED` is defined only for `forge_tests`).
- Export ▸ MIDI and Export ▸ ABC default to the Song file's stem (`Untitled` if unsaved); a missing extension is appended **before** our own `file.exists()` overwrite prompt.
- Import is non-undoable; edits are one undo transaction per gesture (unchanged).
- Conventional commits (`feat:`, `fix:`, `refactor:`, `test:`, `docs:`), each ending with `Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>`. Verify with `git log -1 --format=%b` after every commit — prior implementers wrote "Opus" despite instructions; amend message-only if wrong. Never push.
- Never launch the GUI (`song-smith`) from an agent. Use TDD; prefer integration tests; add types everywhere, no `any`-equivalents.
- Do not import new dependencies without mentioning it (none are expected).

## Build / test commands

```bash
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug     # once
cmake --build build
ctest --test-dir build --output-on-failure            # whole suite (391 tests before this plan)
ctest --test-dir build -R "<test-name regex>" --output-on-failure
./build-windows.sh forge_tests                        # Wine run, Task 10 only
```

`ctest -R` matches test **names**, not Catch2 tags. New tests are prefixed `SongFile:`, `SongSession:`, `DiscardGuard:` and `SongDocument:` (existing prefix).

Worktree setup (if executing in a worktree): reset to local `main`, then `git submodule update --init --recursive` before building. `git worktree remove` of a worktree with submodules needs `--force` after verifying it is clean and merged.

## File Structure

| File | Responsibility |
|---|---|
| `Source/UI/SongFileError.h` (new) | `SongFileErrorKind` + `SongFileError`; no dependencies (breaks the SongDocument↔SongFile include cycle) |
| `Source/UI/SongFile.{h,cpp}` (new) | container read/write, `saveSongFile`, `loadSongFile`, CRC-32 |
| `Source/UI/SongDocument.{h,cpp}` | `mintImportBatch`, `validateLoaded`, `replaceContents`, `resetToEmpty` |
| `Source/UI/SongSession.{h,cpp}` (new) | current file, dirty flag, `displayTitle()`, filename helpers |
| `Source/UI/DiscardGuard.{h,cpp}` (new) | async Save / Don't Save / Cancel sequencing, testable with fake hooks |
| `Source/UI/SongsmithMainComponent.{h,cpp}`, `TrackListComponent.*`, `PartStripComponent.*` | `documentReplaced()` session reset, selection getters/clearers |
| `Source/UI/MainWindow.{h,cpp}`, `UiMain.cpp` | menu, flows, title, drops, quit routing, command-line open, export defaults |
| `CMakeLists.txt`, `Tests/CMakeLists.txt` | register new sources/tests |
| `Tests/{SongFile,SongSession,DiscardGuard}_tests.cpp` (new), `SongDocument_tests.cpp`, `SongsmithMainComponent_tests.cpp`, `SongFileRoundTrip_tests.cpp` (new) | tests |
| `docs/{UI_GUIDE,ARCHITECTURE,TESTING,DELTAS_FROM_SPEC}.md`, `CLAUDE.md` | docs |

## Review Focus

Inputs and conditions the spec implies that a person using the software is most likely to hit. Each has a test in the task that owns the code.

1. **A file that isn't a Song file** (a `.mid`, a text file, an empty file, 3 bytes) is dropped or opened → clear "not a Song file" message, open Song untouched. (Task 3)
2. **A Song saved by a newer Songsmith** → "made by a newer version" message, not a crash or garbage. (Task 3)
3. **Truncated or bit-flipped file** (interrupted copy, bad disk) → specific error, no half-loaded Song, no Debug assert. (Task 3)
4. **Open while the current Song has unsaved edits**, and the user picks Cancel or the save fails → nothing changes. (Tasks 5, 8)
5. **Open / New with a track editor window open, a track selected, a part selected for preview** → editor closes and nothing refers to the old Song's ids. (Task 6)
6. **Export ABC after New / Open** must not write the previous Song's ABC. (Task 8)
7. **Export default filename** must never be the original imported `.mid`. (Tasks 4, 9)
8. **Import MIDI into an opened Song** gets fresh batch numbers and the later-import rules. (Tasks 1, 10)
9. **Quit via window close button or File ▸ Quit with unsaved changes** prompts; Cancel keeps the app running. (Task 8)

---

### Task 1: Error type, `nextImportBatch` counter, `validateLoaded`

**Files:**
- Create: `Source/UI/SongFileError.h`
- Modify: `Source/UI/SongDocument.h`, `Source/UI/SongDocument.cpp`, `Source/UI/MainWindow.h`, `Source/UI/MainWindow.cpp` (replace `nextImportBatch` member)
- Test: `Tests/SongDocument_tests.cpp`

**Interfaces:**
- Produces:
  - `enum class SongFileErrorKind { NotASongFile, UnsupportedVersion, Truncated, ChecksumMismatch, Corrupt, InvalidStructure };`
  - `class SongFileError : public std::runtime_error { SongFileError (SongFileErrorKind, const std::string& message); SongFileErrorKind kind() const noexcept; }`
  - `int SongDocument::mintImportBatch();` — returns the current `SONG.nextImportBatch` (starts 1) and stores +1, non-undoable.
  - `static std::optional<SongFileError> SongDocument::validateLoaded (const juce::ValueTree&);` — `std::nullopt` when valid, otherwise an `InvalidStructure` error with a plain-English message.

- [ ] **Step 1: Write the failing tests** — append to `Tests/SongDocument_tests.cpp` (add `#include "UI/SongFileError.h"`, `#include <optional>`):

```cpp
namespace
{
    const juce::Identifier nextTrackIdId ("nextTrackId");
    const juce::Identifier nextPartIdId ("nextPartId");
    const juce::Identifier nextImportBatchId ("nextImportBatch");

    bool isInvalid (const juce::ValueTree& t)
    {
        const auto err = SongDocument::validateLoaded (t);
        return err.has_value() && err->kind() == SongFileErrorKind::InvalidStructure;
    }

    // A document with one imported-style track (with a note), one part and one assignment.
    SongDocument arrangedDoc()
    {
        SongDocument doc;
        auto track = doc.addTrack ("Lead", 0xFF112233, 1, 1);
        juce::ValueTree note (SongIDs::NOTE);
        note.setProperty (SongIDs::pitch, 60, nullptr);
        note.setProperty (SongIDs::startTick, 0, nullptr);
        note.setProperty (SongIDs::durationTicks, 120, nullptr);
        note.setProperty (SongIDs::velocity, 90, nullptr);
        SongDocument::getNotesNode (track).addChild (note, -1, nullptr);
        auto part = doc.addPart ("Lute of Ages", "Part 1");
        doc.addAssignment (part, (juce::int64) track.getProperty (SongIDs::trackId), 0, 0, "octaveShift");
        return doc;
    }
}

TEST_CASE ("SongDocument: mintImportBatch counts up from 1 and is persisted on SONG", "[songdocument]")
{
    SongDocument doc;
    CHECK (doc.mintImportBatch() == 1);
    CHECK (doc.mintImportBatch() == 2);
    CHECK ((int) doc.getTree().getProperty (nextImportBatchId) == 3);
}

TEST_CASE ("SongDocument: a freshly constructed and an arranged document pass validateLoaded", "[songdocument]")
{
    SongDocument fresh;
    CHECK_FALSE (SongDocument::validateLoaded (fresh.getTree()).has_value());

    auto arranged = arrangedDoc();
    CHECK_FALSE (SongDocument::validateLoaded (arranged.getTree()).has_value());
}

TEST_CASE ("SongDocument: validateLoaded rejects structurally invalid trees", "[songdocument]")
{
    auto good = arrangedDoc().getTree();

    SECTION ("wrong root type")
    {
        CHECK (isInvalid (juce::ValueTree ("NOT_A_SONG")));
        CHECK (isInvalid ({}));
    }
    SECTION ("missing top-level node")
    {
        auto t = good.createCopy();
        t.removeChild (t.getChildWithName (SongIDs::METER_MAP), nullptr);
        CHECK (isInvalid (t));
    }
    SECTION ("unexpected extra child of SONG")
    {
        auto t = good.createCopy();
        t.addChild (juce::ValueTree ("SURPRISE"), -1, nullptr);
        CHECK (isInvalid (t));
    }
    SECTION ("no conductor at child 0")
    {
        auto t = good.createCopy();
        t.getChildWithName (SongIDs::SOURCE_MIDI).getChild (0).setProperty (SongIDs::isConductor, false, nullptr);
        CHECK (isInvalid (t));
    }
    SECTION ("a second conductor")
    {
        auto t = good.createCopy();
        t.getChildWithName (SongIDs::SOURCE_MIDI).getChild (1).setProperty (SongIDs::isConductor, true, nullptr);
        CHECK (isInvalid (t));
    }
    SECTION ("a track missing NOTES")
    {
        auto t = good.createCopy();
        auto track = t.getChildWithName (SongIDs::SOURCE_MIDI).getChild (1);
        track.removeChild (track.getChildWithName (SongIDs::NOTES), nullptr);
        CHECK (isInvalid (t));
    }
    SECTION ("a track missing EVENTS")
    {
        auto t = good.createCopy();
        auto track = t.getChildWithName (SongIDs::SOURCE_MIDI).getChild (1);
        track.removeChild (track.getChildWithName (SongIDs::EVENTS), nullptr);
        CHECK (isInvalid (t));
    }
    SECTION ("duplicate trackId")
    {
        auto t = good.createCopy();
        auto sm = t.getChildWithName (SongIDs::SOURCE_MIDI);
        sm.getChild (1).setProperty (SongIDs::trackId, sm.getChild (0).getProperty (SongIDs::trackId), nullptr);
        CHECK (isInvalid (t));
    }
    SECTION ("duplicate partId")
    {
        auto doc = arrangedDoc();
        doc.addPart ("Lute of Ages", "Part 2");
        auto t = doc.getTree().createCopy();
        auto parts = t.getChildWithName (SongIDs::PARTS);
        parts.getChild (1).setProperty (SongIDs::partId, parts.getChild (0).getProperty (SongIDs::partId), nullptr);
        CHECK (isInvalid (t));
    }
    SECTION ("an assignment pointing at a track that does not exist")
    {
        auto t = good.createCopy();
        t.getChildWithName (SongIDs::PARTS).getChild (0).getChild (0).setProperty (SongIDs::trackId, 9999, nullptr);
        CHECK (isInvalid (t));
    }
    SECTION ("counters that do not exceed the existing ids")
    {
        auto a = good.createCopy(); a.setProperty (nextTrackIdId, 1, nullptr);   CHECK (isInvalid (a));
        auto b = good.createCopy(); b.setProperty (nextPartIdId, 1, nullptr);    CHECK (isInvalid (b));
        auto c = good.createCopy(); c.setProperty (nextImportBatchId, 1, nullptr); CHECK (isInvalid (c)); // track has importBatch 1
    }
    SECTION ("a missing counter")
    {
        auto t = good.createCopy();
        t.removeProperty (nextImportBatchId, nullptr);
        CHECK (isInvalid (t));
    }
}
```

- [ ] **Step 2: Run to verify failure** — `cmake --build build --target forge_tests` fails to compile (`mintImportBatch`, `validateLoaded`, `SongFileError.h` missing).

- [ ] **Step 3: Implement.**

`Source/UI/SongFileError.h`:
```cpp
#pragma once

#include <stdexcept>
#include <string>

namespace lotro
{

enum class SongFileErrorKind
{
    NotASongFile,
    UnsupportedVersion,
    Truncated,
    ChecksumMismatch,
    Corrupt,
    InvalidStructure
};

// Thrown by the Song file reader and by SongDocument::replaceContents. The
// message is plain English and shown verbatim in the user's error dialog.
class SongFileError : public std::runtime_error
{
public:
    SongFileError (SongFileErrorKind kindIn, const std::string& message)
        : std::runtime_error (message), errorKind (kindIn) {}

    SongFileErrorKind kind() const noexcept { return errorKind; }

private:
    SongFileErrorKind errorKind;
};

} // namespace lotro
```

`Source/UI/SongDocument.h`: add `#include "SongFileError.h"` and `#include <optional>`; in the public mutations section add:
```cpp
    // Mints the next import-batch number (starts at 1) and stores the one
    // after it on SONG.nextImportBatch. Non-undoable, like mintTrackId.
    int mintImportBatch();

    // Structural check of a loaded Song tree (see the Song file spec,
    // "InvalidStructure"). Returns nullopt when valid.
    static std::optional<SongFileError> validateLoaded (const juce::ValueTree& candidate);
```

`Source/UI/SongDocument.cpp`: next to the other private identifiers add `static const juce::Identifier nextImportBatch ("nextImportBatch");`, set `tree.setProperty (SongIDs::nextImportBatch, 1, nullptr);` in the constructor right after `nextPartId`, and implement:
```cpp
int SongDocument::mintImportBatch()
{
    const int batch = (int) tree.getProperty (SongIDs::nextImportBatch, 1);
    tree.setProperty (SongIDs::nextImportBatch, batch + 1, nullptr);
    return batch;
}

std::optional<SongFileError> SongDocument::validateLoaded (const juce::ValueTree& t)
{
    const auto bad = [] (const std::string& why)
    {
        return std::optional<SongFileError> (SongFileError (SongFileErrorKind::InvalidStructure,
                                                            "This Song file is damaged: " + why));
    };

    if (! t.isValid() || ! t.hasType (SongIDs::SONG))
        return bad ("it does not contain a Song.");

    for (const auto* counter : { &SongIDs::nextTrackId, &SongIDs::nextPartId, &SongIDs::nextImportBatch })
        if (! t.hasProperty (*counter))
            return bad ("a bookkeeping counter is missing.");

    const juce::Identifier topLevel[] = { SongIDs::SOURCE_MIDI, SongIDs::PARTS, SongIDs::TEMPO_MAP, SongIDs::METER_MAP };
    if (t.getNumChildren() != 4)
        return bad ("it has unexpected sections.");
    for (const auto& id : topLevel)
    {
        int count = 0;
        for (int i = 0; i < t.getNumChildren(); ++i)
            if (t.getChild (i).hasType (id))
                ++count;
        if (count != 1)
            return bad ("a required section is missing or repeated.");
    }

    const auto sourceMidi = t.getChildWithName (SongIDs::SOURCE_MIDI);
    if (sourceMidi.getNumChildren() < 1)
        return bad ("it has no conductor track.");

    const auto nextTrack = (juce::int64) t.getProperty (SongIDs::nextTrackId);
    const auto nextPart  = (juce::int64) t.getProperty (SongIDs::nextPartId);
    const auto nextBatch = (juce::int64) t.getProperty (SongIDs::nextImportBatch);

    std::vector<juce::int64> trackIds;
    for (int i = 0; i < sourceMidi.getNumChildren(); ++i)
    {
        const auto track = sourceMidi.getChild (i);
        if (! track.hasType (SongIDs::MIDI_TRACK))
            return bad ("a track entry is not a track.");
        if (((bool) track.getProperty (SongIDs::isConductor, false)) != (i == 0))
            return bad ("the conductor track is missing, repeated or not first.");
        if (! getNotesNode (track).isValid() || ! getEventsNode (track).isValid())
            return bad ("a track is missing its notes or events section.");

        const auto id = (juce::int64) track.getProperty (SongIDs::trackId, -1);
        if (id < 1 || id >= nextTrack)
            return bad ("a track id is out of range.");
        if (std::find (trackIds.begin(), trackIds.end(), id) != trackIds.end())
            return bad ("two tracks share an id.");
        trackIds.push_back (id);

        if ((juce::int64) track.getProperty (SongIDs::importBatch, 0) >= nextBatch)
            return bad ("an import batch number is out of range.");
    }

    std::vector<juce::int64> partIds;
    const auto parts = t.getChildWithName (SongIDs::PARTS);
    for (int i = 0; i < parts.getNumChildren(); ++i)
    {
        const auto part = parts.getChild (i);
        if (! part.hasType (SongIDs::PART))
            return bad ("a part entry is not a part.");

        const auto id = (juce::int64) part.getProperty (SongIDs::partId, -1);
        if (id < 1 || id >= nextPart)
            return bad ("a part id is out of range.");
        if (std::find (partIds.begin(), partIds.end(), id) != partIds.end())
            return bad ("two parts share an id.");
        partIds.push_back (id);

        for (int a = 0; a < part.getNumChildren(); ++a)
        {
            const auto assignment = part.getChild (a);
            if (! assignment.hasType (SongIDs::ASSIGNMENT))
                continue;
            const auto ref = (juce::int64) assignment.getProperty (SongIDs::trackId, -1);
            if (std::find (trackIds.begin(), trackIds.end(), ref) == trackIds.end())
                return bad ("a part refers to a track that does not exist.");
        }
    }

    return std::nullopt;
}
```
(`#include <vector>` in the .cpp.)

Replace `MainWindow`'s counter: in `MainWindow.h` delete `int nextImportBatch = 1;`; in `MainWindow.cpp` `openMidiFromPath` change `nextImportBatch++` to `songDocument.mintImportBatch()`.

- [ ] **Step 4: Run** — `cmake --build build && ctest --test-dir build -R "SongDocument:" --output-on-failure` → PASS; then the whole suite: `ctest --test-dir build --output-on-failure` → all 391 + new pass.

- [ ] **Step 5: Commit**
```bash
git add Source/UI/SongFileError.h Source/UI/SongDocument.h Source/UI/SongDocument.cpp Source/UI/MainWindow.h Source/UI/MainWindow.cpp Tests/SongDocument_tests.cpp
git commit -m "feat(songsmith): validate loaded Songs and persist the import-batch counter"   # + trailer
```

---

### Task 2: `replaceContents` and `resetToEmpty`

**Files:** Modify `Source/UI/SongDocument.{h,cpp}`; Test `Tests/SongDocument_tests.cpp`.

**Interfaces:**
- Consumes: `validateLoaded`, `SongFileError` (Task 1).
- Produces:
  - `void SongDocument::replaceContents (const juce::ValueTree& loaded);` — validates first (throws `SongFileError`, document unchanged), then restores `SONG`'s properties and the `SOURCE_MIDI` / `PARTS` / `TEMPO_MAP` / `METER_MAP` nodes **in place** (same node objects), non-undoably, then clears undo history.
  - `void SongDocument::resetToEmpty();` — `replaceContents` of a fresh empty Song.

- [ ] **Step 1: Write the failing tests** (append to `Tests/SongDocument_tests.cpp`; add `#include <juce_data_structures/juce_data_structures.h>` if not transitively present):

```cpp
namespace
{
    struct CountingListener : juce::ValueTree::Listener
    {
        int events = 0;
        void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override { ++events; }
        void valueTreeChildAdded (juce::ValueTree&, juce::ValueTree&) override { ++events; }
        void valueTreeChildRemoved (juce::ValueTree&, juce::ValueTree&, int) override { ++events; }
    };
}

TEST_CASE ("SongDocument: replaceContents makes the document equivalent to the loaded tree", "[songdocument]")
{
    auto source = arrangedDoc();
    source.mintImportBatch();
    source.setProperty (source.getTree(), SongIDs::title, "My Song");

    SongDocument target;
    target.addPart ("Harp", "stale");   // pre-existing state must be wiped
    target.replaceContents (source.getTree());

    CHECK (target.getTree().isEquivalentTo (source.getTree()));
}

TEST_CASE ("SongDocument: replaceContents keeps the long-lived node objects and notifies listeners", "[songdocument]")
{
    SongDocument target;
    auto partsBefore = target.getPartsNode();
    auto sourceMidiBefore = target.getSourceMidiNode();
    auto meterBefore = target.getMeterMapNode();

    CountingListener partsListener;
    partsBefore.addListener (&partsListener);

    auto source = arrangedDoc();
    target.replaceContents (source.getTree());

    CHECK (target.getPartsNode() == partsBefore);           // same underlying node object
    CHECK (target.getSourceMidiNode() == sourceMidiBefore);
    CHECK (target.getMeterMapNode() == meterBefore);
    CHECK (partsListener.events > 0);                       // listener on the old handle still hears the change
    CHECK (target.getNumParts() == 1);

    partsBefore.removeListener (&partsListener);
}

TEST_CASE ("SongDocument: replaceContents clears undo history and does not record an undo step", "[songdocument]")
{
    SongDocument target;
    target.addPart ("Lute of Ages", "x");
    REQUIRE (target.canUndo());

    auto source = arrangedDoc();
    target.replaceContents (source.getTree());

    CHECK_FALSE (target.canUndo());
    CHECK_FALSE (target.canRedo());
}

TEST_CASE ("SongDocument: replaceContents throws on an invalid tree and leaves the document untouched", "[songdocument]")
{
    SongDocument target;
    target.addPart ("Lute of Ages", "keep me");
    const auto before = target.getTree().createCopy();

    auto bad = arrangedDoc().getTree().createCopy();
    bad.setProperty (nextTrackIdId, 1, nullptr);

    CHECK_THROWS_AS (target.replaceContents (bad), SongFileError);
    CHECK (target.getTree().isEquivalentTo (before));
}

TEST_CASE ("SongDocument: resetToEmpty returns to a fresh empty Song", "[songdocument]")
{
    auto doc = arrangedDoc();
    doc.mintImportBatch();
    doc.resetToEmpty();

    SongDocument fresh;
    CHECK (doc.getTree().isEquivalentTo (fresh.getTree()));
    CHECK (doc.getNumTracks() == 1);
    CHECK (doc.mintImportBatch() == 1);
}
```

- [ ] **Step 2: Run** — compile error (`replaceContents`, `resetToEmpty` undefined).

- [ ] **Step 3: Implement.** `SongDocument.h` (public, after `validateLoaded`):
```cpp
    // Restores the document from a loaded Song tree IN PLACE: SONG's
    // properties and the SOURCE_MIDI / PARTS / TEMPO_MAP / METER_MAP node
    // objects are kept (components hold listener handles on them) while
    // their contents are replaced, non-undoably. Validates first and throws
    // SongFileError (document unchanged) if `loaded` is invalid. Clears the
    // undo history. Callers reset UI state that lives outside the tree.
    void replaceContents (const juce::ValueTree& loaded);

    // New / Close: replaceContents of a fresh empty Song (counters reset).
    void resetToEmpty();
```
`SongDocument.cpp`:
```cpp
void SongDocument::replaceContents (const juce::ValueTree& loaded)
{
    if (auto error = validateLoaded (loaded))
        throw *error;

    // Deep copy first: `loaded` may be another document's live tree.
    const auto source = loaded.createCopy();

    tree.copyPropertiesFrom (source, nullptr);
    for (const auto& id : { SongIDs::SOURCE_MIDI, SongIDs::PARTS, SongIDs::TEMPO_MAP, SongIDs::METER_MAP })
        tree.getChildWithName (id).copyPropertiesAndChildrenFrom (source.getChildWithName (id), nullptr);

    undoManager.clearUndoHistory();
}

void SongDocument::resetToEmpty()
{
    SongDocument fresh;
    replaceContents (fresh.getTree());
}
```

- [ ] **Step 4: Run** — `ctest --test-dir build -R "SongDocument:" --output-on-failure` PASS; full suite PASS.
- [ ] **Step 5: Commit** `feat(songsmith): restore a Song's contents in place for Open and New` (+ trailer; verify with `git log -1 --format=%b`).

---

### Task 3: Song file container (`SongFile`)

**Files:**
- Create: `Source/UI/SongFile.h`, `Source/UI/SongFile.cpp`, `Tests/SongFile_tests.cpp`
- Modify: `CMakeLists.txt` (add `Source/UI/SongFile.cpp` to `forge_ui` sources), `Tests/CMakeLists.txt` (add `SongFile_tests.cpp` and `${CMAKE_SOURCE_DIR}/Source/UI/SongFile.cpp`), `docs/superpowers/specs/2026-10-03-songsmith-song-file-design.md`

**Interfaces:**
- Consumes: `SongDocument::validateLoaded`, `SongFileError` (Task 1).
- Produces (namespace `lotro`):
  - `constexpr std::uint32_t songFileFormatVersion = 1;`
  - `juce::MemoryBlock writeSongBytes (const juce::ValueTree& song);`
  - `juce::ValueTree readSongBytes (const juce::MemoryBlock& bytes);` // throws `SongFileError`
  - `bool saveSongFile (const SongDocument& doc, const juce::File& file);` // false if the write failed; destination untouched
  - `juce::ValueTree loadSongFile (const juce::File& file);` // throws `SongFileError` (unreadable file → `NotASongFile` "Could not read ...")

- [ ] **Step 1: Amend the spec** — in `docs/superpowers/specs/2026-10-03-songsmith-song-file-design.md`, in the container table add a row `| crc32 | uint32 | CRC-32 of the uncompressed payload |` between `uncompressedLength` and `payloadLength`; in "Read order" step 3 replace the gzip-CRC reasoning with: "Decompress fully via `GZIPDecompressorInputStream` (gzip format). JUCE's stream does not expose a failed zlib trailer check, so integrity is checked by the decompressed byte count (`uncompressedLength`) **and** our own `crc32` of the decompressed bytes → else `ChecksumMismatch`." Also change the Decisions bullet "Integrity via gzip's own CRC32" to "Integrity via our own CRC-32 plus stored lengths". Commit the spec change with this task.

- [ ] **Step 2: Write the failing tests** — `Tests/SongFile_tests.cpp`:

```cpp
// SongFile: the binary .songsmith container — round trip, and every way a
// file can be wrong is refused with its specific SongFileError kind.

#include "UI/SongDocument.h"
#include "UI/SongFile.h"
#include "UI/SongModelBridge.h"
#include "UI/MidiExport.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <juce_core/juce_core.h>

using namespace lotro;

namespace
{
    constexpr std::size_t headerSize = 28;   // magic 4, version 4, uncompressedLength 8, crc 4, payloadLength 8

    SongDocument sampleDoc()
    {
        SongDocument doc;
        auto track = doc.addTrack ("Lead", 0xFF112233, 1, doc.mintImportBatch());
        juce::ValueTree note (SongIDs::NOTE);
        note.setProperty (SongIDs::pitch, 60, nullptr);
        note.setProperty (SongIDs::startTick, 0, nullptr);
        note.setProperty (SongIDs::durationTicks, 120, nullptr);
        note.setProperty (SongIDs::velocity, 90, nullptr);
        SongDocument::getNotesNode (track).addChild (note, -1, nullptr);
        juce::ValueTree event (SongIDs::EVENT);
        const std::uint8_t raw[] = { 0xFF, 0x03, 0x01, 'x' };
        event.setProperty (SongIDs::data, juce::var (juce::MemoryBlock (raw, sizeof (raw))), nullptr);
        event.setProperty (SongIDs::order, 0, nullptr);
        SongDocument::getEventsNode (track).addChild (event, -1, nullptr);
        auto part = doc.addPart ("Lute of Ages", "Part 1");
        doc.addAssignment (part, (juce::int64) track.getProperty (SongIDs::trackId), 0, 0, "octaveShift");
        doc.setProperty (doc.getTree(), SongIDs::title, "T");
        return doc;
    }

    // Returns the error kind readSongBytes throws; fails the test if it does not throw.
    SongFileErrorKind kindOf (const juce::MemoryBlock& bytes)
    {
        try { readSongBytes (bytes); }
        catch (const SongFileError& e) { return e.kind(); }
        FAIL ("readSongBytes did not throw");
        return SongFileErrorKind::Corrupt;
    }

    std::uint8_t* at (juce::MemoryBlock& b, std::size_t i) { return static_cast<std::uint8_t*> (b.getData()) + i; }
}

TEST_CASE ("SongFile: bytes round-trip to an equivalent tree", "[songfile]")
{
    const auto doc = sampleDoc();
    const auto bytes = writeSongBytes (doc.getTree());

    CHECK (bytes.getSize() > headerSize);
    CHECK (std::memcmp (bytes.getData(), "SGSM", 4) == 0);
    CHECK (readSongBytes (bytes).isEquivalentTo (doc.getTree()));
}

TEST_CASE ("SongFile: a file that is not a Song file is refused", "[songfile]")
{
    CHECK (kindOf (juce::MemoryBlock()) == SongFileErrorKind::NotASongFile);
    CHECK (kindOf (juce::MemoryBlock ("abc", 3)) == SongFileErrorKind::NotASongFile);
    CHECK (kindOf (juce::MemoryBlock ("MThd\0\0\0\6\0\1\0\1\1\xe0 and plenty more bytes here", 40)) == SongFileErrorKind::NotASongFile);
}

TEST_CASE ("SongFile: a newer format version is refused as unsupported", "[songfile]")
{
    auto bytes = writeSongBytes (sampleDoc().getTree());
    *at (bytes, 4) = 2;
    CHECK (kindOf (bytes) == SongFileErrorKind::UnsupportedVersion);
}

TEST_CASE ("SongFile: version 0 is corrupt, not a real version", "[songfile]")
{
    auto bytes = writeSongBytes (sampleDoc().getTree());
    *at (bytes, 4) = 0;
    CHECK (kindOf (bytes) == SongFileErrorKind::Corrupt);
}

TEST_CASE ("SongFile: truncation and trailing bytes are detected", "[songfile]")
{
    const auto good = writeSongBytes (sampleDoc().getTree());

    auto shortHeader = good;  shortHeader.setSize (headerSize - 1);
    CHECK (kindOf (shortHeader) == SongFileErrorKind::Truncated);

    auto shortPayload = good; shortPayload.setSize (good.getSize() - 5);
    CHECK (kindOf (shortPayload) == SongFileErrorKind::Truncated);

    auto trailing = good;     trailing.append ("x", 1);
    CHECK (kindOf (trailing) == SongFileErrorKind::Corrupt);
}

TEST_CASE ("SongFile: a damaged payload or wrong stored length is a checksum mismatch", "[songfile]")
{
    const auto good = writeSongBytes (sampleDoc().getTree());

    auto flipped = good;
    *at (flipped, headerSize + (good.getSize() - headerSize) / 2) ^= 0x01;
    CHECK (kindOf (flipped) == SongFileErrorKind::ChecksumMismatch);

    auto wrongLength = good;
    *at (wrongLength, 8) += 1;                 // uncompressedLength low byte
    CHECK (kindOf (wrongLength) == SongFileErrorKind::ChecksumMismatch);

    auto wrongCrc = good;
    *at (wrongCrc, 16) ^= 0xFF;                // crc32 low byte
    CHECK (kindOf (wrongCrc) == SongFileErrorKind::ChecksumMismatch);
}

TEST_CASE ("SongFile: an intact container holding an invalid tree is InvalidStructure", "[songfile]")
{
    // Built from a VALID serialization of a bad tree (never from garbage bytes,
    // which would trip ValueTree::readFromStream's Debug asserts).
    auto tree = sampleDoc().getTree().createCopy();
    tree.getChildWithName (SongIDs::SOURCE_MIDI).getChild (1).setProperty (SongIDs::isConductor, true, nullptr);
    CHECK (kindOf (writeSongBytes (tree)) == SongFileErrorKind::InvalidStructure);
}

TEST_CASE ("SongFile: save then load via a real file", "[songfile]")
{
    const auto doc = sampleDoc();
    auto file = juce::File::createTempFile (".songsmith");

    REQUIRE (saveSongFile (doc, file));
    CHECK (loadSongFile (file).isEquivalentTo (doc.getTree()));

    file.deleteFile();
}

TEST_CASE ("SongFile: loading a missing file throws NotASongFile with a readable message", "[songfile]")
{
    const auto missing = juce::File::createTempFile (".songsmith");   // created then deleted -> absent
    missing.deleteFile();
    try { loadSongFile (missing); FAIL ("did not throw"); }
    catch (const SongFileError& e)
    {
        CHECK (e.kind() == SongFileErrorKind::NotASongFile);
        CHECK (std::string (e.what()).find ("Could not read") == 0);
    }
}

TEST_CASE ("SongFile: a failed save leaves the existing file intact", "[songfile]")
{
    const auto doc = sampleDoc();
    auto file = juce::File::createTempFile (".songsmith");
    REQUIRE (saveSongFile (doc, file));
    const auto before = file.loadFileAsString();

    // A destination inside a directory that does not exist cannot be written.
    const auto impossible = file.getSiblingFile ("no-such-dir-songsmith").getChildFile ("x.songsmith");
    CHECK_FALSE (saveSongFile (doc, impossible));
    CHECK (file.loadFileAsString() == before);

    file.deleteFile();
}
```
Add the new test file and source to `Tests/CMakeLists.txt` and `Source/UI/SongFile.cpp` to `CMakeLists.txt`; add `#include <cstring>`.

- [ ] **Step 3: Run** — fails to compile (`SongFile.h` missing).

- [ ] **Step 4: Implement.**

`Source/UI/SongFile.h`:
```cpp
#pragma once

#include "SongDocument.h"
#include "SongFileError.h"

#include <juce_core/juce_core.h>
#include <juce_data_structures/juce_data_structures.h>

#include <cstdint>

// The binary .songsmith container (Song file spec, "File layer").
//
//   offset  size  field
//   0       4     magic "SGSM"
//   4       4     formatVersion (uint32, little-endian; one number covers container + tree schema)
//   8       8     uncompressedLength (uint64)
//   16      4     crc32 of the uncompressed payload
//   20      8     payloadLength (uint64)
//   28      n     payload: gzip of ValueTree::writeToStream(SONG)
namespace lotro
{

constexpr std::uint32_t songFileFormatVersion = 1;

juce::MemoryBlock writeSongBytes (const juce::ValueTree& song);

// Throws SongFileError. Nothing is parsed until the bytes are proven intact.
juce::ValueTree readSongBytes (const juce::MemoryBlock& bytes);

// Builds the whole buffer first, then File::replaceWithData (sibling temp
// file + atomic replace). Returns false on a write failure; the destination
// is then untouched.
bool saveSongFile (const SongDocument& doc, const juce::File& file);

// Throws SongFileError; an unreadable file is NotASongFile ("Could not read ...").
juce::ValueTree loadSongFile (const juce::File& file);

} // namespace lotro
```
`Source/UI/SongFile.cpp`:
```cpp
#include "SongFile.h"

#include <array>
#include <cstring>

namespace lotro
{

namespace
{
    constexpr char magic[4] = { 'S', 'G', 'S', 'M' };
    constexpr std::size_t headerSize = 28;
    constexpr juce::int64 maxUncompressedBytes = 256LL * 1024 * 1024;   // guards against a decompression bomb

    std::uint32_t crc32 (const void* data, std::size_t size)
    {
        static const auto table = []
        {
            std::array<std::uint32_t, 256> t {};
            for (std::uint32_t i = 0; i < 256; ++i)
            {
                std::uint32_t c = i;
                for (int k = 0; k < 8; ++k)
                    c = (c & 1u) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
                t[i] = c;
            }
            return t;
        }();

        std::uint32_t crc = 0xFFFFFFFFu;
        const auto* p = static_cast<const std::uint8_t*> (data);
        for (std::size_t i = 0; i < size; ++i)
            crc = table[(crc ^ p[i]) & 0xFFu] ^ (crc >> 8);
        return crc ^ 0xFFFFFFFFu;
    }
}

juce::MemoryBlock writeSongBytes (const juce::ValueTree& song)
{
    juce::MemoryOutputStream raw;
    song.writeToStream (raw);

    juce::MemoryOutputStream compressed;
    {
        juce::GZIPCompressorOutputStream gzip (compressed, 9, juce::GZIPCompressorOutputStream::windowBitsGZIP);
        gzip.write (raw.getData(), raw.getDataSize());
        gzip.flush();
    }   // gzip destroyed here: trailer written

    juce::MemoryOutputStream out;
    out.write (magic, sizeof (magic));
    out.writeInt ((int) songFileFormatVersion);
    out.writeInt64 ((juce::int64) raw.getDataSize());
    out.writeInt ((int) crc32 (raw.getData(), raw.getDataSize()));
    out.writeInt64 ((juce::int64) compressed.getDataSize());
    out.write (compressed.getData(), compressed.getDataSize());
    return out.getMemoryBlock();
}

juce::ValueTree readSongBytes (const juce::MemoryBlock& bytes)
{
    if (bytes.getSize() < sizeof (magic) || std::memcmp (bytes.getData(), magic, sizeof (magic)) != 0)
        throw SongFileError (SongFileErrorKind::NotASongFile, "This is not a Songsmith Song file.");
    if (bytes.getSize() < headerSize)
        throw SongFileError (SongFileErrorKind::Truncated, "This Song file is incomplete (it ends inside its header).");

    juce::MemoryInputStream in (bytes, false);
    in.setPosition (sizeof (magic));
    const auto version = (std::uint32_t) in.readInt();
    const auto uncompressedLength = in.readInt64();
    const auto expectedCrc = (std::uint32_t) in.readInt();
    const auto payloadLength = in.readInt64();

    if (version == 0)
        throw SongFileError (SongFileErrorKind::Corrupt, "This Song file is damaged (invalid version).");
    if (version > songFileFormatVersion)
        throw SongFileError (SongFileErrorKind::UnsupportedVersion,
                             "This Song was saved by a newer version of Songsmith. Update Songsmith to open it.");

    const auto remaining = (juce::int64) (bytes.getSize() - headerSize);
    if (payloadLength < 0 || uncompressedLength < 0 || uncompressedLength > maxUncompressedBytes)
        throw SongFileError (SongFileErrorKind::Corrupt, "This Song file is damaged (invalid lengths).");
    if (remaining < payloadLength)
        throw SongFileError (SongFileErrorKind::Truncated, "This Song file is incomplete (it was cut short).");
    if (remaining > payloadLength)
        throw SongFileError (SongFileErrorKind::Corrupt, "This Song file is damaged (unexpected extra data).");

    // No older versions exist yet, so there is no migration chain to run here.
    juce::MemoryInputStream compressed (static_cast<const char*> (bytes.getData()) + headerSize,
                                        (std::size_t) payloadLength, false);
    juce::GZIPDecompressorInputStream gunzip (&compressed, false, juce::GZIPDecompressorInputStream::gzipFormat);
    juce::MemoryOutputStream raw;
    const auto written = raw.writeFromInputStream (gunzip, uncompressedLength + 1);

    if (written != uncompressedLength
        || crc32 (raw.getData(), raw.getDataSize()) != expectedCrc)
        throw SongFileError (SongFileErrorKind::ChecksumMismatch,
                             "This Song file is damaged (its contents failed an integrity check).");

    juce::MemoryInputStream treeStream (raw.getData(), raw.getDataSize(), false);
    auto tree = juce::ValueTree::readFromStream (treeStream);
    if (! tree.isValid())
        throw SongFileError (SongFileErrorKind::Corrupt, "This Song file is damaged (its contents could not be read).");

    if (auto error = SongDocument::validateLoaded (tree))
        throw *error;

    return tree;
}

bool saveSongFile (const SongDocument& doc, const juce::File& file)
{
    const auto bytes = writeSongBytes (doc.getTree());
    return file.replaceWithData (bytes.getData(), bytes.getSize());
}

juce::ValueTree loadSongFile (const juce::File& file)
{
    juce::MemoryBlock bytes;
    if (! file.loadFileAsData (bytes))
        throw SongFileError (SongFileErrorKind::NotASongFile, "Could not read " + file.getFullPathName().toStdString());
    return readSongBytes (bytes);
}

} // namespace lotro
```
Note `GZIPDecompressorInputStream(InputStream* , bool deleteSource, Format, int64 uncompressedLength = -1)` — if the compiler objects to the argument order, check `JUCE/modules/juce_core/zip/juce_GZIPDecompressorInputStream.h:72`.

- [ ] **Step 5: Run** — `ctest --test-dir build -R "SongFile:" --output-on-failure` → PASS. If the "damaged payload" test passes for the wrong reason (e.g. the flipped bit lands in redundant data), keep the test but choose the flip index so the decompressed bytes differ; the CRC check is what must catch it. If the "failed save" test cannot fail on this platform (replaceWithData creating parent dirs), report it rather than weaken it.
- [ ] **Step 6: Commit** `feat(songsmith): add the binary .songsmith container` (+ trailer; spec change in the same commit).

---

### Task 4: `SongSession` and filename helpers

**Files:**
- Create: `Source/UI/SongSession.h`, `Source/UI/SongSession.cpp`, `Tests/SongSession_tests.cpp`
- Modify: `CMakeLists.txt`, `Tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `SongDocument` (`getTree()`).
- Produces:
  - `class SongSession : private juce::ValueTree::Listener` with
    `explicit SongSession (SongDocument&)`, `bool isDirty() const noexcept`, `const juce::File& getFile() const noexcept`, `bool isUntitled() const noexcept`, `juce::String getName() const` (file stem, or `"Untitled"`), `juce::String displayTitle() const` (`"<name>[*] — Songsmith"`), `void markClean (const juce::File& file)` (save or load: sets the file, clears dirty), `void markNew()` (untitled, clean), `std::function<void()> onChanged` (fired when title/dirty/file changes).
  - `juce::File withExtensionIfMissing (const juce::File& file, const juce::StringArray& acceptedExtensions, const juce::String& defaultExtension);` — appends `defaultExtension` unless the name already ends with one of `acceptedExtensions` (case-insensitive).
  - `juce::File defaultExportFile (const juce::File& songFile, const juce::String& extension, const juce::File& fallbackDirectory);` — `songFile` with its extension replaced, or `fallbackDirectory/Untitled<extension>` when `songFile == juce::File()`.

- [ ] **Step 1: Write the failing tests** — `Tests/SongSession_tests.cpp`:

```cpp
#include "UI/SongDocument.h"
#include "UI/SongSession.h"

#include <catch2/catch_test_macros.hpp>

using namespace lotro;

TEST_CASE ("SongSession: starts untitled and clean", "[songsession]")
{
    SongDocument doc;
    SongSession session (doc);

    CHECK_FALSE (session.isDirty());
    CHECK (session.isUntitled());
    CHECK (session.getName() == "Untitled");
    CHECK (session.displayTitle() == juce::String::fromUTF8 ("Untitled \xe2\x80\x94 Songsmith"));
}

TEST_CASE ("SongSession: any tree change marks the Song dirty, including non-undoable ones", "[songsession]")
{
    SongDocument doc;
    SongSession session (doc);

    SECTION ("an undoable edit")      { doc.addPart ("Lute of Ages", "x"); CHECK (session.isDirty()); }
    SECTION ("a non-undoable bulk edit")
    {
        doc.addTrackBulk ("t", 0, 1, 1);
        CHECK (session.isDirty());
    }
    SECTION ("a property write with no UndoManager")
    {
        doc.getTree().setProperty (SongIDs::inputMidiPath, "x.mid", nullptr);
        CHECK (session.isDirty());
    }
    SECTION ("a deep note edit")
    {
        auto track = doc.addTrack ("t", 0, 1, 1);
        session.markClean (juce::File ("/tmp/a.songsmith"));
        REQUIRE_FALSE (session.isDirty());
        juce::ValueTree note (SongIDs::NOTE);
        SongDocument::getNotesNode (track).addChild (note, -1, nullptr);
        CHECK (session.isDirty());
    }
}

TEST_CASE ("SongSession: markClean records the file and clears dirty; the title shows the name and a star when dirty", "[songsession]")
{
    SongDocument doc;
    SongSession session (doc);
    doc.addPart ("Lute of Ages", "x");

    session.markClean (juce::File ("/tmp/My Song.songsmith"));
    CHECK_FALSE (session.isDirty());
    CHECK_FALSE (session.isUntitled());
    CHECK (session.getName() == "My Song");
    CHECK (session.displayTitle() == juce::String::fromUTF8 ("My Song \xe2\x80\x94 Songsmith"));

    doc.addPart ("Harp", "y");
    CHECK (session.displayTitle() == juce::String::fromUTF8 ("My Song* \xe2\x80\x94 Songsmith"));
}

TEST_CASE ("SongSession: undoing back to the saved state still reads as dirty", "[songsession]")
{
    SongDocument doc;
    SongSession session (doc);
    session.markClean (juce::File ("/tmp/a.songsmith"));

    doc.addPart ("Lute of Ages", "x");
    doc.undo();
    CHECK (session.isDirty());
}

TEST_CASE ("SongSession: replaceContents then markClean leaves the session clean", "[songsession]")
{
    SongDocument source;
    source.addPart ("Lute of Ages", "x");

    SongDocument doc;
    SongSession session (doc);
    doc.replaceContents (source.getTree());
    session.markClean (juce::File ("/tmp/a.songsmith"));

    CHECK_FALSE (session.isDirty());
}

TEST_CASE ("SongSession: markNew forgets the file and clears dirty", "[songsession]")
{
    SongDocument doc;
    SongSession session (doc);
    session.markClean (juce::File ("/tmp/a.songsmith"));
    doc.addPart ("Lute of Ages", "x");

    session.markNew();
    CHECK (session.isUntitled());
    CHECK_FALSE (session.isDirty());
}

TEST_CASE ("SongSession: onChanged fires when dirty state flips, not on every edit", "[songsession]")
{
    SongDocument doc;
    SongSession session (doc);
    int fired = 0;
    session.onChanged = [&] { ++fired; };

    doc.addPart ("Lute of Ages", "1");   // clean -> dirty
    const int afterFirst = fired;
    doc.addPart ("Lute of Ages", "2");   // already dirty
    CHECK (afterFirst == 1);
    CHECK (fired == afterFirst);

    session.markClean (juce::File ("/tmp/a.songsmith"));
    CHECK (fired == afterFirst + 1);
}

TEST_CASE ("SongSession: withExtensionIfMissing appends before any overwrite check", "[songsession]")
{
    const juce::StringArray mid { ".mid", ".midi" };
    CHECK (withExtensionIfMissing (juce::File ("/d/song"), mid, ".mid") == juce::File ("/d/song.mid"));
    CHECK (withExtensionIfMissing (juce::File ("/d/song.v2"), mid, ".mid") == juce::File ("/d/song.v2.mid"));
    CHECK (withExtensionIfMissing (juce::File ("/d/song.MID"), mid, ".mid") == juce::File ("/d/song.MID"));
    CHECK (withExtensionIfMissing (juce::File ("/d/song.midi"), mid, ".mid") == juce::File ("/d/song.midi"));
}

TEST_CASE ("SongSession: defaultExportFile uses the Song stem, never the imported MIDI", "[songsession]")
{
    const juce::File docs ("/home/u/Documents");
    CHECK (defaultExportFile (juce::File ("/songs/My Song.songsmith"), ".mid", docs) == juce::File ("/songs/My Song.mid"));
    CHECK (defaultExportFile (juce::File ("/songs/My Song.songsmith"), ".abc", docs) == juce::File ("/songs/My Song.abc"));
    CHECK (defaultExportFile (juce::File(), ".mid", docs) == docs.getChildFile ("Untitled.mid"));
}
```
Register `SongSession_tests.cpp` and `SongSession.cpp` in `Tests/CMakeLists.txt`, `SongSession.cpp` in `CMakeLists.txt`.

- [ ] **Step 2: Run** — compile error (`SongSession.h` missing).

- [ ] **Step 3: Implement.** `Source/UI/SongSession.h`:
```cpp
#pragma once

#include "SongDocument.h"

#include <juce_core/juce_core.h>
#include <juce_data_structures/juce_data_structures.h>

#include <functional>

namespace lotro
{

// Where the open Song lives on disk and whether it has unsaved changes.
// Dirty is any change to the document tree after the last markClean()/markNew()
// -- including non-undoable imports -- so undoing back to the saved state
// still reads as dirty (simple and safe).
class SongSession : private juce::ValueTree::Listener
{
public:
    explicit SongSession (SongDocument& document);
    ~SongSession() override;

    bool isDirty() const noexcept { return dirty; }
    const juce::File& getFile() const noexcept { return file; }
    bool isUntitled() const noexcept { return file == juce::File(); }
    juce::String getName() const;
    juce::String displayTitle() const;

    // After a save or a load: remember `file`, clear the dirty flag.
    void markClean (const juce::File& newFile);
    // After New / Close: untitled and clean.
    void markNew();

    std::function<void()> onChanged;

private:
    void setDirty();
    void notify() { if (onChanged) onChanged(); }

    void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override { setDirty(); }
    void valueTreeChildAdded (juce::ValueTree&, juce::ValueTree&) override { setDirty(); }
    void valueTreeChildRemoved (juce::ValueTree&, juce::ValueTree&, int) override { setDirty(); }
    void valueTreeChildOrderChanged (juce::ValueTree&, int, int) override { setDirty(); }

    // Persistent handle: ValueTree::addListener registers on THIS handle
    // object, not the shared tree, so a temporary from doc.getTree() would
    // silently drop the registration (same convention as TrackListComponent).
    juce::ValueTree rootNode;
    juce::File file;
    bool dirty = false;
};

// Appends `defaultExtension` unless the name already ends with one of
// `acceptedExtensions` (case-insensitive). Used BEFORE our own overwrite
// confirmation, because the native chooser warns on the typed name.
juce::File withExtensionIfMissing (const juce::File& file, const juce::StringArray& acceptedExtensions,
                                   const juce::String& defaultExtension);

// Default Save-As target for an export: the Song file with its extension
// replaced, or <fallbackDirectory>/Untitled<extension> for an unsaved Song.
juce::File defaultExportFile (const juce::File& songFile, const juce::String& extension,
                              const juce::File& fallbackDirectory);

} // namespace lotro
```
`SongSession.cpp`:
```cpp
#include "SongSession.h"

namespace lotro
{

SongSession::SongSession (SongDocument& document) : rootNode (document.getTree())
{
    rootNode.addListener (this);
}

SongSession::~SongSession() { rootNode.removeListener (this); }

juce::String SongSession::getName() const
{
    return isUntitled() ? juce::String ("Untitled") : file.getFileNameWithoutExtension();
}

juce::String SongSession::displayTitle() const
{
    return getName() + (dirty ? "*" : "") + juce::String::fromUTF8 (" \xe2\x80\x94 Songsmith");
}

void SongSession::setDirty()
{
    if (dirty)
        return;
    dirty = true;
    notify();
}

void SongSession::markClean (const juce::File& newFile)
{
    file = newFile;
    dirty = false;
    notify();
}

void SongSession::markNew()
{
    file = juce::File();
    dirty = false;
    notify();
}

juce::File withExtensionIfMissing (const juce::File& file, const juce::StringArray& acceptedExtensions,
                                   const juce::String& defaultExtension)
{
    for (const auto& ext : acceptedExtensions)
        if (file.getFileName().endsWithIgnoreCase (ext))
            return file;
    return file.getSiblingFile (file.getFileName() + defaultExtension);
}

juce::File defaultExportFile (const juce::File& songFile, const juce::String& extension,
                              const juce::File& fallbackDirectory)
{
    if (songFile == juce::File())
        return fallbackDirectory.getChildFile ("Untitled" + extension);
    return songFile.withFileExtension (extension);
}

} // namespace lotro
```
Note `session.markClean()` fires `onChanged` even if nothing flipped — acceptable (title/file may change); the "does not fire on every edit" test only checks edits.

- [ ] **Step 4: Run** — `ctest --test-dir build -R "SongSession:" --output-on-failure` PASS; full suite PASS.
- [ ] **Step 5: Commit** `feat(songsmith): track the Song's file and unsaved state` (+ trailer).

---

### Task 5: `DiscardGuard` — async Save / Don't Save / Cancel sequencing

**Files:** Create `Source/UI/DiscardGuard.{h,cpp}`, `Tests/DiscardGuard_tests.cpp`; Modify `CMakeLists.txt`, `Tests/CMakeLists.txt`.

**Interfaces:**
- Produces:
  - `enum class DiscardChoice { Save, DontSave, Cancel };`
  - `struct DiscardGuardHooks { std::function<void (std::function<void (DiscardChoice)>)> prompt; std::function<void (std::function<void (bool)>)> save; };` — both asynchronous: each calls its continuation exactly once, possibly later. `save`'s continuation reports whether the save succeeded (false also for a cancelled Save As chooser).
  - `void confirmDiscardChanges (bool dirty, const DiscardGuardHooks& hooks, std::function<void()> onProceed);` — clean → `onProceed` immediately and no prompt. Dirty → prompt; `DontSave` → `onProceed`; `Cancel` → nothing; `Save` → `save`, then `onProceed` only if it reported success.

- [ ] **Step 1: Write the failing tests** — `Tests/DiscardGuard_tests.cpp` (fake hooks capture continuations so the tests also prove asynchronous behaviour):

```cpp
#include "UI/DiscardGuard.h"

#include <catch2/catch_test_macros.hpp>

using namespace lotro;

namespace
{
    struct Fake
    {
        int prompts = 0, saves = 0, proceeds = 0;
        std::function<void (DiscardChoice)> pendingPrompt;
        std::function<void (bool)> pendingSave;

        DiscardGuardHooks hooks()
        {
            return {
                [this] (std::function<void (DiscardChoice)> done) { ++prompts; pendingPrompt = std::move (done); },
                [this] (std::function<void (bool)> done)          { ++saves;   pendingSave = std::move (done); }
            };
        }
    };
}

TEST_CASE ("DiscardGuard: a clean Song proceeds immediately without prompting", "[discardguard]")
{
    Fake f;
    confirmDiscardChanges (false, f.hooks(), [&] { ++f.proceeds; });
    CHECK (f.proceeds == 1);
    CHECK (f.prompts == 0);
}

TEST_CASE ("DiscardGuard: a dirty Song prompts and waits for the answer", "[discardguard]")
{
    Fake f;
    confirmDiscardChanges (true, f.hooks(), [&] { ++f.proceeds; });
    CHECK (f.prompts == 1);
    CHECK (f.proceeds == 0);          // nothing happens until the user answers
}

TEST_CASE ("DiscardGuard: Don't Save proceeds without saving", "[discardguard]")
{
    Fake f;
    confirmDiscardChanges (true, f.hooks(), [&] { ++f.proceeds; });
    f.pendingPrompt (DiscardChoice::DontSave);
    CHECK (f.proceeds == 1);
    CHECK (f.saves == 0);
}

TEST_CASE ("DiscardGuard: Cancel does nothing", "[discardguard]")
{
    Fake f;
    confirmDiscardChanges (true, f.hooks(), [&] { ++f.proceeds; });
    f.pendingPrompt (DiscardChoice::Cancel);
    CHECK (f.proceeds == 0);
    CHECK (f.saves == 0);
}

TEST_CASE ("DiscardGuard: Save proceeds only after the save reports success", "[discardguard]")
{
    Fake f;
    confirmDiscardChanges (true, f.hooks(), [&] { ++f.proceeds; });
    f.pendingPrompt (DiscardChoice::Save);
    CHECK (f.saves == 1);
    CHECK (f.proceeds == 0);          // the (possibly Save-As-chooser) save is still pending

    f.pendingSave (true);
    CHECK (f.proceeds == 1);
}

TEST_CASE ("DiscardGuard: a failed or cancelled save does not proceed", "[discardguard]")
{
    Fake f;
    confirmDiscardChanges (true, f.hooks(), [&] { ++f.proceeds; });
    f.pendingPrompt (DiscardChoice::Save);
    f.pendingSave (false);
    CHECK (f.proceeds == 0);
}
```

- [ ] **Step 2: Run** — compile error.
- [ ] **Step 3: Implement.** `DiscardGuard.h`:
```cpp
#pragma once

#include <functional>

namespace lotro
{

enum class DiscardChoice { Save, DontSave, Cancel };

// Both hooks are asynchronous (native dialogs / file choosers); each must
// call its continuation exactly once. `save` reports success -- false for a
// failed write or a cancelled Save As chooser.
struct DiscardGuardHooks
{
    std::function<void (std::function<void (DiscardChoice)>)> prompt;
    std::function<void (std::function<void (bool)>)>          save;
};

// Runs `onProceed` once it is safe to discard the open Song: immediately if
// it is clean; otherwise after the user chooses Don't Save, or Save and the
// save succeeds. Cancel (or a failed/cancelled save) never proceeds.
// A callback chain, never a modal loop.
void confirmDiscardChanges (bool dirty, const DiscardGuardHooks& hooks, std::function<void()> onProceed);

} // namespace lotro
```
`DiscardGuard.cpp`:
```cpp
#include "DiscardGuard.h"

namespace lotro
{

void confirmDiscardChanges (bool dirty, const DiscardGuardHooks& hooks, std::function<void()> onProceed)
{
    if (! dirty)
    {
        onProceed();
        return;
    }

    // Copy the hooks: the prompt answers later, after the caller's temporaries are gone.
    hooks.prompt ([hooks, onProceed] (DiscardChoice choice)
    {
        switch (choice)
        {
            case DiscardChoice::Cancel:   return;
            case DiscardChoice::DontSave: onProceed(); return;
            case DiscardChoice::Save:
                hooks.save ([onProceed] (bool saved) { if (saved) onProceed(); });
                return;
        }
    });
}

} // namespace lotro
```
- [ ] **Step 4: Run** — `ctest --test-dir build -R "DiscardGuard:" --output-on-failure` PASS (all test names must start `DiscardGuard:`).
- [ ] **Step 5: Commit** `feat(songsmith): add the async unsaved-changes guard` (+ trailer).

---

### Task 6: `documentReplaced()` session reset

**Files:** Modify `Source/UI/SongsmithMainComponent.{h,cpp}`, `Source/UI/TrackListComponent.{h,cpp}`, `Source/UI/PartStripComponent.{h,cpp}` (add `friend struct PartStripComponentTestAccess;`), `Tests/CMakeLists.txt`; Create `Tests/PartStripTestAccess.h`, `Tests/PartStripComponent_tests.cpp`; Test `Tests/SongsmithMainComponent_tests.cpp`, `Tests/TrackListComponent_tests.cpp`.

**Interfaces:**
- Produces:
  - `void SongsmithMainComponent::documentReplaced();` — closes the track editor, clears ghost / track-selection / part-selection / preview state, refits the timeline. Writes nothing to the tree.
  - `juce::int64 TrackListComponent::getSelectedTrackId() const noexcept;` `void TrackListComponent::clearSelection();`
  - `juce::int64 PartStripComponent::getSelectedPartId() const noexcept;` `void PartStripComponent::clearSelection();` (does NOT fire `onPartSelected`)
  - `SongsmithMainComponentTestAccess` gains `partStrip(c)`, `ghostedCount(c)`, `selectedPreviewPartId(c)`, `trackGhostToggled(c, id, v)` (test file only); `PartStripComponentTestAccess` (shared test header).

- [ ] **Step 1: Write the failing tests.**

`Tests/PartStripTestAccess.h` (single shared definition; `PartStripComponent.h` gets `friend struct PartStripComponentTestAccess;` in its private section, mirroring `TrackListComponent`'s friend):
```cpp
#pragma once

#include "UI/PartStripComponent.h"

namespace lotro
{
    struct PartStripComponentTestAccess
    {
        static void selectPart (PartStripComponent& c, juce::int64 partId) { c.selectPart (partId); }
    };
}
```

`Tests/PartStripComponent_tests.cpp` (register in `Tests/CMakeLists.txt`):
```cpp
#include "PartStripTestAccess.h"
#include "UI/SongDocument.h"

#include <catch2/catch_test_macros.hpp>

using namespace lotro;

TEST_CASE ("PartStripComponent: clearSelection forgets the selection without firing onPartSelected", "[session-reset]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    SongDocument doc;
    auto part = doc.addPart ("Lute of Ages", "Part 1");
    const auto partId = (juce::int64) part.getProperty (SongIDs::partId);

    PartStripComponent strip (doc);
    int fired = 0;
    strip.onPartSelected = [&] (juce::int64) { ++fired; };

    PartStripComponentTestAccess::selectPart (strip, partId);
    REQUIRE (strip.getSelectedPartId() == partId);
    REQUIRE (fired == 1);

    strip.clearSelection();
    CHECK (strip.getSelectedPartId() == -1);
    CHECK (fired == 1);                         // clearing is silent
}
```
In `Tests/TrackListComponent_tests.cpp` (uses its existing `Access`), append:
```cpp
TEST_CASE ("TrackListComponent: clearSelection forgets the selected track", "[session-reset]")
{
    SongDocument doc;
    auto track = doc.addTrack ("Track 1", (int) 0xFF7FA8D0, 0, 0);
    const auto id = (juce::int64) track.getProperty (SongIDs::trackId);

    TrackListComponent list (doc);
    Access::selectTrack (list, id);
    REQUIRE (list.getSelectedTrackId() == id);

    list.clearSelection();
    CHECK (list.getSelectedTrackId() == -1);
}
```
In `Tests/SongsmithMainComponent_tests.cpp` extend `SongsmithMainComponentTestAccess` (add `#include "PartStripTestAccess.h"`):
```cpp
        static PartStripComponent& partStrip (SongsmithMainComponent& c) { return c.partStrip; }
        static std::size_t ghostedCount (const SongsmithMainComponent& c) { return c.ghostedTrackIds.size(); }
        static juce::int64 selectedPreviewPartId (const SongsmithMainComponent& c) { return c.selectedPreviewPartId; }
        static void trackGhostToggled (SongsmithMainComponent& c, juce::int64 id, bool v) { c.trackGhostToggled (id, v); }
```
and append:
```cpp
TEST_CASE ("SongsmithMainComponent: documentReplaced closes the editor and clears UI state outside the tree", "[session-reset]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    SongDocument doc;
    auto track = doc.addTrack ("Track A", (int) 0xFFAABBCCu, 1, 1);
    const auto trackId = (juce::int64) track.getProperty (SongIDs::trackId);
    auto part = doc.addPart ("Lute of Ages", "Part 1");
    const auto partId = (juce::int64) part.getProperty (SongIDs::partId);

    SongsmithMainComponent main (doc);
    Access::trackDoubleClicked (main, trackId);
    Access::trackGhostToggled (main, trackId, true);
    PartStripComponentTestAccess::selectPart (Access::partStrip (main), partId);   // also previews it, via onPartSelected
    REQUIRE (main.isTrackEditorOpen());
    REQUIRE (Access::ghostedCount (main) == 1);
    REQUIRE (Access::selectedPreviewPartId (main) == partId);
    REQUIRE (Access::partStrip (main).getSelectedPartId() == partId);

    main.documentReplaced();

    CHECK_FALSE (main.isTrackEditorOpen());
    CHECK (Access::ghostedCount (main) == 0);
    CHECK (Access::selectedPreviewPartId (main) == -1);
    CHECK_FALSE (Access::hasWatchedPartNode (main));
    CHECK_FALSE (Access::hasPreviewNoteSource (main));
    CHECK (Access::partStrip (main).getSelectedPartId() == -1);
    CHECK (Access::trackList (main).getSelectedTrackId() == -1);
}

TEST_CASE ("SongsmithMainComponent: after a document swap an old id never re-selects the new Song's part", "[session-reset]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    SongDocument doc;
    auto part = doc.addPart ("Lute of Ages", "Old");
    SongsmithMainComponent main (doc);
    PartStripComponentTestAccess::selectPart (Access::partStrip (main), (juce::int64) part.getProperty (SongIDs::partId));

    // A different Song whose first part has the SAME id (ids are per-Song counters).
    SongDocument other;
    other.addPart ("Harp", "New");

    doc.replaceContents (other.getTree());
    main.documentReplaced();
    juce::MessageManager::getInstance()->runDispatchLoopUntil (300);   // let the async listener updates run

    CHECK (Access::selectedPreviewPartId (main) == -1);
    CHECK_FALSE (Access::hasPreviewNoteSource (main));
}

TEST_CASE ("SongsmithMainComponent: documentReplaced writes nothing to the tree", "[session-reset]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    SongDocument doc;
    doc.addTrack ("Track A", (int) 0xFFAABBCCu, 1, 1);
    SongsmithMainComponent main (doc);

    const auto before = doc.getTree().createCopy();
    main.documentReplaced();
    juce::MessageManager::getInstance()->runDispatchLoopUntil (200);

    CHECK (doc.getTree().isEquivalentTo (before));
}
```
(The track-list half of `documentReplaced`'s selection reset has no editor-level precondition because selecting a row needs a click; it is pinned by the `TrackListComponent: clearSelection` unit test above plus read-review of `documentReplaced`.)

- [ ] **Step 2: Run** — compile error (`documentReplaced`, getters missing).
- [ ] **Step 3: Implement.**
`TrackListComponent.h` (public): `juce::int64 getSelectedTrackId() const noexcept { return selectedTrackId; }` and `void clearSelection();`. `.cpp`:
```cpp
void TrackListComponent::clearSelection()
{
    selectTrack (-1);
}
```
(`selectTrack(-1)` already clears every row's selected flag and does not fire callbacks.)
`PartStripComponent.h` (public): `juce::int64 getSelectedPartId() const noexcept { return selectedPartId; }` and `void clearSelection();`. `.cpp`:
```cpp
void PartStripComponent::clearSelection()
{
    selectedPartId = -1;
    for (auto* slot : row.slots)
        slot->setSelected (false);
}
```
`SongsmithMainComponent.h` (public, next to `fitTrackTimelineToDocument`):
```cpp
    // Called by MainWindow after the document's contents were replaced
    // (New / Open / Close). Resets everything that lives outside the tree:
    // closes the track editor (its note source holds the OLD MIDI_TRACK
    // handle), clears ghost / track / part selection and the preview (ids
    // persist per Song, so a stale id could re-select a different Song's
    // part), and refits the timeline. Must not write to the tree.
    void documentReplaced();
```
`.cpp`:
```cpp
void SongsmithMainComponent::documentReplaced()
{
    trackEditorWindow.reset();
    ghostedTrackIds.clear();
    trackList.clearSelection();
    partStrip.clearSelection();
    selectPartForPreview (-1);
    trackList.fitTimelineToDocument();
}
```
- [ ] **Step 4: Run** — `ctest --test-dir build -R "SongsmithMainComponent:|TrackList" --output-on-failure` PASS; full suite PASS. If destroying `trackEditorWindow` directly (rather than through its `onClosed`) misbehaves, fix it there (e.g. clear `onClosed` first) and say so in the report.
- [ ] **Step 5: Commit** `feat(songsmith): reset UI state that lives outside the tree when the Song is replaced` (+ trailer).

---

### Task 7: MainWindow — menu, New / Open / Close / Save / Save As, title

> `MainWindow.cpp` is not compiled into `forge_tests`; this task is build-verified plus read-reviewed. Keep decisions in the tested helpers (Tasks 4, 5).

**Files:** Modify `Source/UI/MainWindow.{h,cpp}`, `CMakeLists.txt` (add `SongFile.cpp`, `SongSession.cpp`, `DiscardGuard.cpp` to `forge_ui` if not already added in Tasks 3–5).

**Interfaces:**
- Consumes: `SongSession`, `confirmDiscardChanges` / `DiscardGuardHooks` / `DiscardChoice`, `saveSongFile` / `loadSongFile`, `SongFileError`, `SongDocument::replaceContents` / `resetToEmpty`, `SongsmithMainComponent::documentReplaced`, `withExtensionIfMissing`, `defaultExportFile`.
- Produces (MainWindow, public): `void openSongFromPath (const juce::File&)` (NO guard — callers guard), `void requestOpenSong (const juce::File&)` (guarded; used by drops and later the command line), `void requestQuit()`.

- [ ] **Step 1: Header changes.** In `MainWindow.h`: remove `#include "Core/ConfigLoader.h"`; add `#include "DiscardGuard.h"`, `#include "SongSession.h"`, `<functional>`. Replace the `CommandId` enum's File items with:
```cpp
        FileNew = 1,
        FileOpenSong,
        FileClose,
        FileSave,
        FileSaveAs,
        FileImportMidi,
        FileExportAbc,
        FileExportMidi,
        FileQuit,
```
(delete `FileOpenMidi`, `FileOpenConfig`, `FileSaveAsJson/Toml/Xml`, `FileSaveAbc`; keep the rest). Add members after `songDocument`: `SongSession session { songDocument };` (declared **before** `body`). Delete `openConfigViaDialog`, `openConfigFromPath`, `saveConfigAs`. Add private methods: `void refreshTitle();`, `void resetToEmptySong();`, `void newOrCloseSong();`, `void openSongViaDialog();`, `void saveSong (std::function<void (bool)> done = {});`, `void saveSongAs (std::function<void (bool)> done = {});`, `void writeSongTo (const juce::File&, std::function<void (bool)> done);`, `DiscardGuardHooks guardHooks();`, `void guarded (std::function<void()> action);`, `void afterDocumentReplaced();`. Add `bool keyPressed (const juce::KeyPress&) override;`.

- [ ] **Step 2: Menu.** In `getMenuForIndex` (File), replace the whole File block with:
```cpp
        const auto item = [&m] (int id, const juce::String& text, const juce::String& shortcut, bool enabled)
        {
            juce::PopupMenu::Item i (text);
            i.itemID = id;
            i.isEnabled = enabled;
            i.shortcutKeyDescription = shortcut;
            m.addItem (i);
        };
        item (FileNew,      "New",         "Ctrl+N",       true);
        item (FileOpenSong, "Open...",     "Ctrl+O",       true);
        item (FileClose,    "Close",       "",             true);
        m.addSeparator();
        item (FileSave,     "Save",        "Ctrl+S",       session.isDirty() || session.isUntitled());
        item (FileSaveAs,   "Save As...",  "Ctrl+Shift+S", true);
        m.addSeparator();
        juce::PopupMenu importMenu;
        importMenu.addItem (FileImportMidi, "MIDI...");
        m.addSubMenu ("Import", importMenu);
        juce::PopupMenu exportMenu;
        // MIDI is enabled once the song has anything besides its conductor.
        exportMenu.addItem (FileExportMidi, "MIDI...", songDocument.getNumTracks() > 1);
        exportMenu.addItem (FileExportAbc,  "ABC...",  ! lastAbc.empty());
        m.addSubMenu ("Export", exportMenu);
        m.addSeparator();
        m.addItem (FileQuit, "Quit");
```
(Verify the `juce::PopupMenu::Item` field names against `JUCE/modules/juce_gui_basics/menus/juce_PopupMenu.h:~160-180`; the existing code only uses `addItem(id, text, enabled, ticked)`.)
In `menuItemSelected` replace the File cases:
```cpp
        case FileNew:
        case FileClose:       guarded ([this] { resetToEmptySong(); });                       return;
        case FileOpenSong:    guarded ([this] { openSongViaDialog(); });                       return;
        case FileSave:        saveSong();                                                      return;
        case FileSaveAs:      saveSongAs();                                                    return;
        case FileImportMidi:  openMidiViaDialog();                                            return;
        case FileExportAbc:   saveAbcAs();                                                    return;
        case FileExportMidi:  exportMidiAs();                                                 return;
        case FileQuit:        requestQuit();                                                   return;
```
(delete the Config/SaveAs cases.) Keep `openMidiViaDialog` / `openMidiFromPath` as they are (they are the Import ▸ MIDI implementation).

- [ ] **Step 3: Implement the flows** in `MainWindow.cpp` (anonymous-namespace helper plus members). Add includes `"SongFile.h"`, `"DiscardGuard.h"`, `"SongSession.h"`.
```cpp
namespace
{
    constexpr const char* songExtension = ".songsmith";

    void showError (const juce::String& title, const juce::String& message)
    {
        juce::NativeMessageBox::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, title, message);
    }
}

void MainWindow::refreshTitle()
{
    setName (session.displayTitle());
    menuItemsChanged();
}

DiscardGuardHooks MainWindow::guardHooks()
{
    return {
        // prompt: Save / Don't Save / Cancel
        [this] (std::function<void (DiscardChoice)> done)
        {
            juce::NativeMessageBox::showAsync (
                juce::MessageBoxOptions()
                    .withIconType (juce::MessageBoxIconType::QuestionIcon)
                    .withTitle ("Unsaved changes")
                    .withMessage ("Save changes to " + session.getName() + " before continuing?")
                    .withButton ("Save")
                    .withButton ("Don't Save")
                    .withButton ("Cancel")
                    .withAssociatedComponent (this),
                [done] (int button)
                {
                    done (button == 0 ? DiscardChoice::Save
                        : button == 1 ? DiscardChoice::DontSave
                                      : DiscardChoice::Cancel);
                });
        },
        // save: Save, or Save As for an untitled Song
        [this] (std::function<void (bool)> done) { saveSong (std::move (done)); }
    };
}

void MainWindow::guarded (std::function<void()> action)
{
    confirmDiscardChanges (session.isDirty(), guardHooks(), std::move (action));
}

void MainWindow::afterDocumentReplaced()
{
    body->getSongsmith().documentReplaced();
    body->getSongsmith().getDiagnostics().setDiagnostics ({});
    lastAbc.clear();
    body->getExportPanel().show ({}, {});
    body->setExportPanelVisible (false);
    menuItemsChanged();
}

void MainWindow::resetToEmptySong()
{
    songDocument.resetToEmpty();
    session.markNew();
    afterDocumentReplaced();
}

void MainWindow::openSongFromPath (const juce::File& file)
{
    try
    {
        songDocument.replaceContents (loadSongFile (file));   // validates fully first; document untouched on throw
    }
    catch (const SongFileError& e)
    {
        showError ("Could not open Song", juce::String (e.what()));
        return;
    }
    session.markClean (file);
    afterDocumentReplaced();
}

void MainWindow::requestOpenSong (const juce::File& file)
{
    guarded ([this, file] { openSongFromPath (file); });
}

void MainWindow::openSongViaDialog()
{
    fileChooser = std::make_unique<juce::FileChooser> ("Open Song", juce::File(), juce::String ("*") + songExtension);
    fileChooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [this] (const juce::FileChooser& fc)
        {
            const auto file = fc.getResult();
            if (file != juce::File())
                openSongFromPath (file);   // already behind the guard (File > Open runs guarded() first)
        });
}

void MainWindow::writeSongTo (const juce::File& file, std::function<void (bool)> done)
{
    const bool ok = saveSongFile (songDocument, file);
    if (ok)
        session.markClean (file);
    else
        showError ("Save failed", "Could not write: " + file.getFullPathName());
    if (done)
        done (ok);
}

void MainWindow::saveSong (std::function<void (bool)> done)
{
    if (session.isUntitled())
        saveSongAs (std::move (done));
    else
        writeSongTo (session.getFile(), std::move (done));
}

void MainWindow::saveSongAs (std::function<void (bool)> done)
{
    const auto defaultFile = defaultExportFile (session.getFile(), songExtension,
                                                juce::File::getSpecialLocation (juce::File::userDocumentsDirectory));
    fileChooser = std::make_unique<juce::FileChooser> ("Save Song", defaultFile, juce::String ("*") + songExtension);

    // No warnAboutOverwriting: the extension is appended AFTER the chooser
    // returns, so the native warning would check the wrong name. We ask ourselves.
    fileChooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles,
        [this, done] (const juce::FileChooser& fc)
        {
            auto file = fc.getResult();
            if (file == juce::File())
            {
                if (done) done (false);
                return;
            }
            file = withExtensionIfMissing (file, { songExtension }, songExtension);

            if (file.existsAsFile() && file != session.getFile())
            {
                juce::NativeMessageBox::showAsync (
                    juce::MessageBoxOptions()
                        .withIconType (juce::MessageBoxIconType::WarningIcon)
                        .withTitle ("Replace file?")
                        .withMessage (file.getFileName() + " already exists. Replace it?")
                        .withButton ("Replace")
                        .withButton ("Cancel")
                        .withAssociatedComponent (this),
                    [this, file, done] (int button)
                    {
                        if (button == 0) writeSongTo (file, done);
                        else if (done)   done (false);
                    });
                return;
            }
            writeSongTo (file, done);
        });
}

bool MainWindow::keyPressed (const juce::KeyPress& key)
{
    const auto cmd = juce::ModifierKeys::commandModifier;
    const auto cmdShift = juce::ModifierKeys::commandModifier | juce::ModifierKeys::shiftModifier;
    if (key == juce::KeyPress ('n', cmd, 0))      { menuItemSelected (FileNew, 0);      return true; }
    if (key == juce::KeyPress ('o', cmd, 0))      { menuItemSelected (FileOpenSong, 0); return true; }
    if (key == juce::KeyPress ('s', cmd, 0))      { menuItemSelected (FileSave, 0);     return true; }
    if (key == juce::KeyPress ('s', cmdShift, 0)) { menuItemSelected (FileSaveAs, 0);   return true; }
    return false;
}
```
In the constructor: `session.onChanged = [this] { refreshTitle(); };`, replace `juce::DocumentWindow ("Forge", …)` title with `session.displayTitle()` — note `session` is a member constructed before `body` but after `songDocument`, and DocumentWindow's base ctor runs first, so pass `"Songsmith"` there and call `refreshTitle()` at the end of the constructor body; also `setWantsKeyboardFocus (true);`. (In Task 8, `requestQuit` is added; stub nothing here — implement Task 8 immediately after or add `requestQuit` in this task if the build requires it.)
Remove from `MainWindow.cpp`: `openConfigViaDialog`, `openConfigFromPath`, `saveConfigAs`, and `#include "Core/ConfigWriter.h"` if nothing else uses it.

- [ ] **Step 4: Build and run the full suite** — `cmake --build build && ctest --test-dir build --output-on-failure` PASS. `build/forge_ui_artefacts/Debug/song-smith` must be built (do NOT run it). Read-review the diff against this task's code once more for async-lifetime hazards (every lambda that outlives the call captures `this` only while `MainWindow` is alive; `fileChooser` is a member so the chooser outlives its callback).
- [ ] **Step 5: Commit** `feat(songsmith): File menu with New / Open / Close / Save / Save As and an unsaved-changes guard` (+ trailer).

---

### Task 8: Quit routing, drag-and-drop, command-line open

**Files:** Modify `Source/UI/MainWindow.{h,cpp}`, `Source/UI/UiMain.cpp`.

**Interfaces:** Produces `void MainWindow::requestQuit();` — runs the guard, then `juce::JUCEApplication::quit()` only from `onProceed`. `UiApp::systemRequestedQuit()` and the close button route to it. `void MainWindow::openSongOnStartup (const juce::File&)`.

- [ ] **Step 1: Implement.** `MainWindow.cpp`:
```cpp
void MainWindow::closeButtonPressed()
{
    requestQuit();
}

void MainWindow::requestQuit()
{
    guarded ([] { juce::JUCEApplication::quit(); });
}

void MainWindow::openSongOnStartup (const juce::File& file)
{
    openSongFromPath (file);   // nothing is open yet, so no guard
}
```
`isInterestedInFileDrag` / `filesDropped` — remove the `.json/.toml/.xml` branches and accept `.songsmith`:
```cpp
bool MainWindow::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (const auto& f : files)
    {
        const auto ext = juce::File (f).getFileExtension().toLowerCase();
        if (ext == ".mid" || ext == ".midi" || ext == songExtension)
            return true;
    }
    return false;
}

void MainWindow::filesDropped (const juce::StringArray& files, int, int)
{
    for (const auto& f : files)
    {
        const auto file = juce::File (f);
        const auto ext = file.getFileExtension().toLowerCase();
        if (ext == ".mid" || ext == ".midi")  { openMidiFromPath (file);  return; }
        if (ext == songExtension)             { requestOpenSong (file);   return; }
    }
}
```
`UiMain.cpp`:
```cpp
    void initialise (const juce::String&) override
    {
        window = std::make_unique<MainWindow>();
        for (const auto& arg : getCommandLineParameterArray())
        {
            const auto file = juce::File::getCurrentWorkingDirectory().getChildFile (arg.unquoted());
            if (file.hasFileExtension (".songsmith") && file.existsAsFile())
            {
                window->openSongOnStartup (file);
                break;
            }
        }
    }
    void systemRequestedQuit() override
    {
        if (window != nullptr) window->requestQuit();   // prompts if dirty, then quit()s
        else quit();
    }
```
- [ ] **Step 2: Build + full suite** PASS (build-verified; do not run the GUI). Read-review: confirm `JUCEApplication::quit()` is only reachable from the guard's `onProceed`, and that Cancel leaves the window open.
- [ ] **Step 3: Commit** `feat(songsmith): route quit, close and .songsmith drops through the unsaved-changes guard` (+ trailer).

---

### Task 9: Export defaults and overwrite handling

**Files:** Modify `Source/UI/MainWindow.cpp`.

**Interfaces:** Consumes `defaultExportFile`, `withExtensionIfMissing` (Task 4).

- [ ] **Step 1: Implement.** Add a shared helper in the anonymous namespace and use it from both exports:
```cpp
    // Choose a destination, append the extension if missing, then confirm an
    // overwrite ourselves (the native warning would check the typed name, not
    // the name with the extension appended). `write` runs only once confirmed.
    void chooseAndConfirm (MainWindow* owner, std::unique_ptr<juce::FileChooser>& holder,
                           const juce::String& title, const juce::File& defaultFile,
                           const juce::String& wildcard, const juce::StringArray& accepted,
                           const juce::String& defaultExt, std::function<void (const juce::File&)> write)
    {
        holder = std::make_unique<juce::FileChooser> (title, defaultFile, wildcard);
        holder->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles,
            [owner, accepted, defaultExt, write] (const juce::FileChooser& fc)
            {
                auto file = fc.getResult();
                if (file == juce::File()) return;
                file = withExtensionIfMissing (file, accepted, defaultExt);

                if (! file.existsAsFile()) { write (file); return; }

                juce::NativeMessageBox::showAsync (
                    juce::MessageBoxOptions()
                        .withIconType (juce::MessageBoxIconType::WarningIcon)
                        .withTitle ("Replace file?")
                        .withMessage (file.getFileName() + " already exists. Replace it?")
                        .withButton ("Replace").withButton ("Cancel")
                        .withAssociatedComponent (owner),
                    [file, write] (int button) { if (button == 0) write (file); });
            });
    }
```
`saveAbcAs`: default `defaultExportFile (session.getFile(), ".abc", documentsDir)`; call `chooseAndConfirm (this, fileChooser, "Export ABC", defaultFile, "*.abc", { ".abc" }, ".abc", [this] (const juce::File& file) { /* existing replaceWithText + failure message box */ })`. `exportMidiAs`: default `defaultExportFile (session.getFile(), ".mid", documentsDir)`; `chooseAndConfirm (this, fileChooser, "Export MIDI", defaultFile, "*.mid;*.midi", { ".mid", ".midi" }, ".mid", [this] (const juce::File& file) { /* existing buildRawMidiFile → writeMidiBytes → replaceWithData with MidiExportError handling */ })`. Move the existing bodies into those lambdas unchanged. Delete the `inputMidiPath`-based defaults. Remove `warnAboutOverwriting` from both. (The MIDI bytes are still built **inside** the write step so a `MidiExportError` leaves any existing file intact.)
- [ ] **Step 2: Build + full suite** PASS; read-review that no remaining code reads `inputMidiPath` for a filename (`grep -n inputMidiPath Source/UI/MainWindow.cpp` → none).
- [ ] **Step 3: Commit** `fix(songsmith): export to the Song's name and confirm overwrites after the extension is appended` (+ trailer).

---

### Task 10: Integration round-trip, docs, Wine run

**Files:** Create `Tests/SongFileRoundTrip_tests.cpp` (register in `Tests/CMakeLists.txt`); Modify `docs/UI_GUIDE.md`, `docs/ARCHITECTURE.md`, `docs/TESTING.md`, `docs/DELTAS_FROM_SPEC.md`, `CLAUDE.md`.

- [ ] **Step 1: Write the integration tests** — `Tests/SongFileRoundTrip_tests.cpp` (copy `midiFixture` from `Tests/MidiFidelity_tests.cpp:~19-23`; the fixture list is the 9 tracked files):
```cpp
// SongFileRoundTrip: import -> arrange -> edit -> save -> load reproduces the
// Song exactly, and Songsmith's other outputs (MIDI export, ABC) are unchanged by it.

#include "UI/MidiExport.h"
#include "UI/SongDocument.h"
#include "UI/SongFile.h"
#include "UI/SongModelBridge.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <juce_core/juce_core.h>

using namespace lotro;

namespace
{
    juce::File midiFixture (const std::string& name)
    {
        return juce::File (__FILE__).getParentDirectory().getParentDirectory()
                   .getChildFile ("midi").getChildFile (name);
    }

    // Deletes a note, shifts another and adds a part, so the Song is not just a pristine import.
    void editSong (SongDocument& doc)
    {
        synthesiseDefaultParts (doc);
        for (int i = 0; i < doc.getNumTracks(); ++i)
        {
            auto notes = SongDocument::getNotesNode (doc.getTrack (i));
            if (notes.getNumChildren() >= 2)
            {
                doc.removeChild (notes, notes.getChild (0));
                doc.setProperty (notes.getChild (0), SongIDs::startTick,
                                 (int) notes.getChild (0).getProperty (SongIDs::startTick) + 10);
                break;
            }
        }
        doc.setProperty (doc.getTree(), SongIDs::title, "Round Trip");
    }
}

TEST_CASE ("SongFileRoundTrip: every tracked fixture survives save and load, edits included", "[songfile-roundtrip]")
{
    auto name = GENERATE (as<std::string>{},
        "Barnes Brothers Band - Pull The Wires.mid", "anymore.mid", "blue.mid",
        "hold.mid", "land.mid", "leah.mid", "nobody.mid", "right.mid", "tellit.mid");

    DYNAMIC_SECTION (name)
    {
        SongDocument original;
        Diagnostics diags;
        REQUIRE (importMidiFile (original, midiFixture (name), original.mintImportBatch(), diags));
        editSong (original);

        const auto file = juce::File::createTempFile (".songsmith");
        REQUIRE (saveSongFile (original, file));

        SongDocument loaded;
        loaded.replaceContents (loadSongFile (file));
        file.deleteFile();

        CHECK (loaded.getTree().isEquivalentTo (original.getTree()));
        CHECK (buildRawMidiFile (loaded) == buildRawMidiFile (original));   // Export > MIDI is unchanged by the round trip
    }
}

TEST_CASE ("SongFileRoundTrip: importing into a loaded Song uses fresh track ids and batch numbers", "[songfile-roundtrip]")
{
    SongDocument first;
    Diagnostics diags;
    REQUIRE (importMidiFile (first, midiFixture ("land.mid"), first.mintImportBatch(), diags));

    SongDocument loaded;
    loaded.replaceContents (readSongBytes (writeSongBytes (first.getTree())));

    const int tracksBefore = loaded.getNumTracks();
    REQUIRE (importMidiFile (loaded, midiFixture ("blue.mid"), loaded.mintImportBatch(), diags));
    REQUIRE (loaded.getNumTracks() > tracksBefore);

    std::set<juce::int64> ids;
    for (int i = 0; i < loaded.getNumTracks(); ++i)
        CHECK (ids.insert ((juce::int64) loaded.getTrack (i).getProperty (SongIDs::trackId)).second);   // no duplicate ids

    CHECK ((int) loaded.getTrack (loaded.getNumTracks() - 1).getProperty (SongIDs::importBatch) == 2);   // not a reused 1
}

TEST_CASE ("SongFileRoundTrip: undo history is not saved but redo-able edits after a load work normally", "[songfile-roundtrip]")
{
    SongDocument doc;
    doc.addPart ("Lute of Ages", "a");

    SongDocument loaded;
    loaded.replaceContents (readSongBytes (writeSongBytes (doc.getTree())));
    CHECK_FALSE (loaded.canUndo());

    loaded.addPart ("Harp", "b");
    CHECK (loaded.canUndo());
    loaded.undo();
    CHECK (loaded.getNumParts() == 1);
}
```
(Add `#include <set>`.) Run: `ctest --test-dir build -R "SongFileRoundTrip:" --output-on-failure` → PASS (these cover already-built code; they are integration pins, not new behavior. Rename the test cases so every name starts `SongFileRoundTrip:` — they do.)

- [ ] **Step 2: Docs.**
  - `docs/UI_GUIDE.md`: replace the File-menu description with the new menu (New / Open… / Close / Save / Save As… / Import ▸ MIDI… / Export ▸ MIDI…, ABC… / Quit, with Ctrl shortcuts), remove the Config items, add the title-bar `*` convention and the Save / Don't Save / Cancel prompt.
  - `docs/ARCHITECTURE.md`: add a "Song file" section — container layout, read order, `SongFileError` kinds, `replaceContents` in-place restore and why, `SongSession` dirty rules, `DiscardGuard`, `documentReplaced()` reset list, persisted counters (`nextTrackId`, `nextPartId`, `nextImportBatch`), export default names; fix the two stale items carried from the previous project: line ~26 "Only `Source/Core/MidiImporter.cpp` touches `juce::MidiFile`" (now also `JuceNoteReplica` test-only; reword accurately) and the `appendImportedSong` paragraph (~:663) that says the rescale Info fires when the import "touched at least one note" (it counts notes and event ticks).
  - `docs/TESTING.md`: new total test count and the new files' coverage.
  - `docs/DELTAS_FROM_SPEC.md`: add that the strict MIDI reader rejects files the CLI still converts (truncation, system-common bytes) with no notes-only fallback.
  - `CLAUDE.md`: status line — "Songsmith GUI through Phase 7 + polish; MIDI fidelity and the `.songsmith` Song file implemented"; add `docs/superpowers/specs/2026-10-03-songsmith-song-file-design.md` where specs are referenced if there is such a list; mention "Open Config" / "Save Config" are gone.
  Use the real test count from `ctest --test-dir build -N | tail -1`.
- [ ] **Step 3: Whole-suite and Wine runs.**
```bash
cmake --build build
ctest --test-dir build --output-on-failure           # expect all green; record the count
./build-windows.sh forge_tests                        # Wine run; covers Windows atomic replace via saveSongFile
```
If the Wine toolchain is not set up on this machine, say so in the report instead of skipping silently (`./setup-windows-toolchain.sh` is the one-time setup; do not run it unprompted).
- [ ] **Step 4: Commit** — two commits: `test: pin the Song file round trip across every tracked fixture` (the test file + CMake) and `docs: describe the Song file, the new File menu and the unsaved-changes guard` (docs + CLAUDE.md), each with the trailer.

---

## Self-Review notes (spec coverage)

- **File layer / container / read order / `SongFileError` / save atomicity** → Task 3. **`InvalidStructure` checks in `SongDocument`** → Task 1. **Persisted counters incl. `nextImportBatch`** → Task 1. **`replaceContents` in place, undo cleared, New = same path** → Task 2. **`SongSession` dirty rules, persistent handle, `displayTitle()`** → Task 4. **Filename defaults, extension-before-exists check** → Tasks 4, 9. **Async guard** → Task 5 (logic) + Tasks 7–8 (wiring). **Session reset list** → Task 6 (editor, ghost, selections, preview, timeline) + Task 7 `afterDocumentReplaced` (`lastAbc`, Diagnostics, export panel). **Menu / flows / Config removal / drops / command line / quit routing / title** → Tasks 7–8. **Round-trip across 9 tracked fixtures, export-equality, import-after-load** → Task 10. **Docs incl. DELTAS note** → Task 10.
- **Spec refinement to flag to the user:** the extra `crc32` header field (Task 3 Step 1 amends the spec).
- **Known unverifiable-by-test areas** (MainWindow is not in `forge_tests`): menu wiring, key shortcuts, native dialogs, quit routing. Mitigation: all decision logic lives in Tasks 4–5 helpers; Tasks 7–9 are build-verified and review-checked; the user's manual GUI pass is the gate (suggested checklist after merge: New/Open/Save round trip on `land.mid`, dirty `*` in the title, Cancel on each guarded action, quit with unsaved changes, drop a `.songsmith` and a `.mid`, Export ▸ MIDI/ABC default names, overwrite prompt, Open a damaged file).
