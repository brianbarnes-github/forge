// Verifies SongDocument's ValueTree schema construction, synthetic id
// minting (trackId/partId must never be positional indices), query
// helpers, and undo/redo transaction boundaries.

#include "UI/SongDocument.h"
#include "UI/SongFileError.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <optional>
#include <vector>

using namespace lotro;

TEST_CASE ("SongDocument: constructs an empty SONG tree with sane default properties", "[songdocument]")
{
    SongDocument doc;
    auto tree = doc.getTree();

    CHECK (tree.hasType (SongIDs::SONG));

    // title/transcriber/tempoBpm mirror forge_core::Config's std::optional
    // override fields — they must stay ABSENT (not defaulted) so Phase 2
    // can tell "no override" from "explicitly set to empty/some value".
    CHECK (! tree.hasProperty (SongIDs::title));
    CHECK (! tree.hasProperty (SongIDs::transcriber));
    CHECK (! tree.hasProperty (SongIDs::tempoBpm));

    CHECK ((int) tree.getProperty (SongIDs::globalTranspose) == 0);
    CHECK (tree.getProperty (SongIDs::inputMidiPath).toString() == "");

    auto sourceMidi = doc.getSourceMidiNode();
    REQUIRE (sourceMidi.isValid());
    CHECK (sourceMidi.hasType (SongIDs::SOURCE_MIDI));
    CHECK ((int) sourceMidi.getProperty (SongIDs::ticksPerQuarter) == 480);
    CHECK (doc.getNumTracks() == 1);

    auto parts = doc.getPartsNode();
    REQUIRE (parts.isValid());
    CHECK (parts.hasType (SongIDs::PARTS));
    CHECK (doc.getNumParts() == 0);
}

TEST_CASE ("SongDocument: addTrack mints a monotonically increasing trackId that survives removal, never a positional index", "[songdocument]")
{
    SongDocument doc;

    auto a = doc.addTrack ("Track A", 0xff0000, 0, 1);
    auto b = doc.addTrack ("Track B", 0x00ff00, 1, 1);
    auto c = doc.addTrack ("Track C", 0x0000ff, 2, 1);

    auto idA = (juce::int64) a.getProperty (SongIDs::trackId);
    auto idB = (juce::int64) b.getProperty (SongIDs::trackId);
    auto idC = (juce::int64) c.getProperty (SongIDs::trackId);

    // All distinct, monotonically increasing.
    CHECK (idA < idB);
    CHECK (idB < idC);

    // Remove the middle track — a positional-index scheme would now let
    // "position 1" (0-based) silently refer to what was track C.
    doc.removeTrack (idB);
    REQUIRE (doc.getNumTracks() == 3);

    // The track now sitting at position 1 is still identified by idC, not
    // by the stale idB — proves lookups are id-based, not index-based.
    CHECK (doc.getTrack (2).getProperty (SongIDs::trackId).toString()
           == juce::String (idC));
    CHECK (! doc.findTrackById (idB).isValid());

    // A newly minted id must not reuse idB (the just-freed slot) — it must
    // continue the monotonic counter, proving the id source is not
    // "next available array position."
    auto d = doc.addTrack ("Track D", 0xffffff, 3, 1);
    auto idD = (juce::int64) d.getProperty (SongIDs::trackId);
    CHECK (idD != idB);
    CHECK (idD > idC);
}

TEST_CASE ("SongDocument: findTrackById locates the correct track regardless of insertion order", "[songdocument]")
{
    SongDocument doc;
    auto a = doc.addTrack ("A", 0, 0, 0);
    auto b = doc.addTrack ("B", 0, 1, 0);

    auto idA = (juce::int64) a.getProperty (SongIDs::trackId);
    auto idB = (juce::int64) b.getProperty (SongIDs::trackId);

    CHECK (doc.findTrackById (idA).getProperty (SongIDs::name).toString() == "A");
    CHECK (doc.findTrackById (idB).getProperty (SongIDs::name).toString() == "B");
    CHECK (! doc.findTrackById (999).isValid());
}

TEST_CASE ("SongDocument: partId is minted from a counter independent of trackId", "[songdocument]")
{
    SongDocument doc;

    // Mint a track first so the trackId counter has already advanced past 1.
    auto track = doc.addTrack ("A", 0, 0, 0);
    auto part  = doc.addPart ("LuteOfAges", "Lead");

    auto trackIdVal = (juce::int64) track.getProperty (SongIDs::trackId);
    auto partIdVal  = (juce::int64) part.getProperty (SongIDs::partId);

    // If the two synthetic ids shared one counter, partId would be
    // trackIdVal + 1. Independent counters mean partId starts at its own
    // baseline (1) regardless of how far the trackId counter has moved.
    CHECK (partIdVal == 1);
    CHECK (trackIdVal == 2);
}

TEST_CASE ("SongDocument: addPart mints x as a 1-based positional index, matching synthesiseConfig's convention", "[songdocument]")
{
    // Config.cpp's validateConfig requires x >= 1 and unique per part; a
    // hardcoded x == 0 (the pre-fix behaviour) fails validation on the very
    // first part a document produces.
    SongDocument doc;

    auto p1 = doc.addPart ("LuteOfAges", "Lead");
    auto p2 = doc.addPart ("Drums", "Percussion");

    CHECK ((int) p1.getProperty (SongIDs::x) == 1);
    CHECK ((int) p2.getProperty (SongIDs::x) == 2);
}

TEST_CASE ("SongDocument: title/transcriber/tempoBpm stay absent until explicitly set, so Phase 2 can distinguish \"no override\" from \"cleared\"", "[songdocument]")
{
    SongDocument doc;
    REQUIRE (! doc.getTree().hasProperty (SongIDs::title));
    REQUIRE (! doc.getTree().hasProperty (SongIDs::tempoBpm));

    doc.setProperty (doc.getTree(), SongIDs::tempoBpm, 93.75);
    CHECK (doc.getTree().hasProperty (SongIDs::tempoBpm));
    CHECK ((double) doc.getTree().getProperty (SongIDs::tempoBpm) == 93.75);
}

TEST_CASE ("SongDocument: undoing a mint does not roll back the id counter, so the next mint cannot reissue a colliding id", "[songdocument]")
{
    // Regression test for the nullptr-UndoManager id-counter invariant.
    // Flipping mintTrackId()/mintPartId()'s nullptr to &undoManager makes
    // this fail: undo() would roll the counter back, and the next
    // addTrack() would re-mint idA, colliding with the (still-undone-but-
    // referenceable) first track's id.
    SongDocument doc;

    auto idA = (juce::int64) doc.addTrack ("A", 0, 0, 0).getProperty (SongIDs::trackId);
    doc.undo();
    REQUIRE (doc.getNumTracks() == 1);

    auto idB = (juce::int64) doc.addTrack ("B", 0, 0, 0).getProperty (SongIDs::trackId);
    CHECK (idB != idA);
}

TEST_CASE ("SongDocument: addPart mints x as (max existing x) + 1, so an add/remove/add sequence never collides", "[songdocument]")
{
    // Regression for I4: getNumParts() + 1 mints a duplicate x once a part
    // in the middle has been removed (add A/B/C -> x 1/2/3; remove B;
    // add D -> getNumParts() + 1 == 3, colliding with C). validateConfig
    // rejects a document with two parts sharing one x, so this must never
    // happen.
    SongDocument doc;

    auto a = doc.addPart ("LuteOfAges", "A");
    auto b = doc.addPart ("LuteOfAges", "B");
    auto c = doc.addPart ("LuteOfAges", "C");
    CHECK ((int) a.getProperty (SongIDs::x) == 1);
    CHECK ((int) b.getProperty (SongIDs::x) == 2);
    CHECK ((int) c.getProperty (SongIDs::x) == 3);

    doc.removePart ((juce::int64) b.getProperty (SongIDs::partId));
    REQUIRE (doc.getNumParts() == 2);

    auto d = doc.addPart ("LuteOfAges", "D");
    CHECK ((int) d.getProperty (SongIDs::x) == 4);

    // All remaining x values are unique.
    std::vector<int> xs;
    for (int i = 0; i < doc.getNumParts(); ++i)
        xs.push_back ((int) doc.getPart (i).getProperty (SongIDs::x));
    std::sort (xs.begin(), xs.end());
    CHECK (std::adjacent_find (xs.begin(), xs.end()) == xs.end());
}

TEST_CASE ("SongDocument: removeTrack cascades to remove every ASSIGNMENT referencing that track, in one undo transaction", "[songdocument]")
{
    SongDocument doc;
    auto track = doc.addTrack ("A", 0, 0, 0);
    auto trackId = (juce::int64) track.getProperty (SongIDs::trackId);

    auto partOne = doc.addPart ("LuteOfAges", "One");
    auto partTwo = doc.addPart ("Drums", "Two");
    doc.addAssignment (partOne, trackId, 0, 0, "octaveShift");
    doc.addAssignment (partTwo, trackId, 0, 0, "octaveShift");

    REQUIRE (SongDocument::getNumAssignments (partOne) == 1);
    REQUIRE (SongDocument::getNumAssignments (partTwo) == 1);

    doc.removeTrack (trackId);

    CHECK (! doc.findTrackById (trackId).isValid());
    CHECK (SongDocument::getNumAssignments (partOne) == 0);
    CHECK (SongDocument::getNumAssignments (partTwo) == 0);

    // One undo() restores the track AND both assignments — proves the
    // cascade shares removeTrack's single transaction rather than opening
    // separate ones.
    doc.undo();
    CHECK (doc.findTrackById (trackId).isValid());
    CHECK (SongDocument::getNumAssignments (partOne) == 1);
    CHECK (SongDocument::getNumAssignments (partTwo) == 1);
}

TEST_CASE ("SongDocument: findPartById locates the correct part", "[songdocument]")
{
    SongDocument doc;
    auto p1 = doc.addPart ("LuteOfAges", "Lead");
    auto p2 = doc.addPart ("Drums", "Percussion");

    auto id1 = (juce::int64) p1.getProperty (SongIDs::partId);
    auto id2 = (juce::int64) p2.getProperty (SongIDs::partId);

    CHECK (doc.findPartById (id1).getProperty (SongIDs::label).toString() == "Lead");
    CHECK (doc.findPartById (id2).getProperty (SongIDs::label).toString() == "Percussion");
    CHECK (! doc.findPartById (999).isValid());
}

TEST_CASE ("SongDocument: addAssignment stores a trackId reference resolved by id, not by the assignment's own position", "[songdocument]")
{
    SongDocument doc;
    auto trackX = doc.addTrack ("X", 0, 0, 0);
    auto trackY = doc.addTrack ("Y", 0, 1, 0);
    auto idX = (juce::int64) trackX.getProperty (SongIDs::trackId);
    auto idY = (juce::int64) trackY.getProperty (SongIDs::trackId);

    auto part = doc.addPart ("LuteOfAges", "Lead");

    // Insert in reverse order: assignment at position 0 references the
    // *second*-created track (idY), proving the reference is the trackId
    // property, not "assignment index == track index".
    doc.addAssignment (part, idY, 0, 100, "octaveShift");
    doc.addAssignment (part, idX, -12, 80, "octaveShift");

    REQUIRE (SongDocument::getNumAssignments (part) == 2);
    auto firstAssignment  = SongDocument::getAssignment (part, 0);
    auto secondAssignment = SongDocument::getAssignment (part, 1);

    CHECK ((juce::int64) firstAssignment.getProperty (SongIDs::trackId) == idY);
    CHECK ((juce::int64) secondAssignment.getProperty (SongIDs::trackId) == idX);
    CHECK ((int) secondAssignment.getProperty (SongIDs::transposeSemitones) == -12);
    CHECK ((int) firstAssignment.getProperty (SongIDs::volumePercent) == 100);
    CHECK (secondAssignment.getProperty (SongIDs::rangePolicy).toString() == "octaveShift");
}

TEST_CASE ("SongDocument: removeAssignment removes only the targeted assignment", "[songdocument]")
{
    SongDocument doc;
    auto track = doc.addTrack ("A", 0, 0, 0);
    auto trackId = (juce::int64) track.getProperty (SongIDs::trackId);
    auto part = doc.addPart ("LuteOfAges", "Lead");

    doc.addAssignment (part, trackId, 0, 100, "octaveShift");
    auto toRemove = doc.addAssignment (part, trackId, 5, 90, "octaveShift");
    doc.addAssignment (part, trackId, 0, 50, "octaveShift");

    REQUIRE (SongDocument::getNumAssignments (part) == 3);
    doc.removeAssignment (part, toRemove);
    REQUIRE (SongDocument::getNumAssignments (part) == 2);

    CHECK ((int) SongDocument::getAssignment (part, 0).getProperty (SongIDs::transposeSemitones) == 0);
    CHECK ((int) SongDocument::getAssignment (part, 1).getProperty (SongIDs::transposeSemitones) == 0);
}

TEST_CASE ("SongDocument: addTrack is undoable as a single transaction", "[songdocument]")
{
    SongDocument doc;
    REQUIRE (! doc.canUndo());

    doc.addTrack ("A", 0, 0, 0);
    REQUIRE (doc.getNumTracks() == 2);
    REQUIRE (doc.canUndo());

    doc.undo();
    CHECK (doc.getNumTracks() == 1);
    CHECK (doc.canRedo());

    doc.redo();
    CHECK (doc.getNumTracks() == 2);
}

TEST_CASE ("SongDocument: two separate addTrack calls are two separate undo steps", "[songdocument]")
{
    SongDocument doc;
    doc.addTrack ("A", 0, 0, 0);
    doc.addTrack ("B", 0, 1, 0);
    REQUIRE (doc.getNumTracks() == 3);

    // One undo() call reverts only the most recent gesture (the second
    // addTrack), not both — proves each addTrack begins its own
    // transaction rather than coalescing into the first.
    doc.undo();
    CHECK (doc.getNumTracks() == 2);
    CHECK (doc.getTrack (1).getProperty (SongIDs::name).toString() == "A");

    doc.undo();
    CHECK (doc.getNumTracks() == 1);
}

TEST_CASE ("SongDocument: setProperty routes through the UndoManager and is undoable", "[songdocument]")
{
    SongDocument doc;
    doc.setProperty (doc.getTree(), SongIDs::title, juce::String ("My Song"));

    CHECK (doc.getTree().getProperty (SongIDs::title).toString() == "My Song");
    REQUIRE (doc.canUndo());

    doc.undo();
    CHECK (doc.getTree().getProperty (SongIDs::title).toString() == "");
}

TEST_CASE ("SongDocument: appendChildBulk bypasses undo entirely, for future non-undoable MIDI import", "[songdocument]")
{
    SongDocument doc;

    juce::ValueTree bulkTrack (SongIDs::MIDI_TRACK);
    bulkTrack.setProperty (SongIDs::trackId, (juce::int64) 42, nullptr);
    bulkTrack.setProperty (SongIDs::name, "Imported", nullptr);

    SongDocument::appendChildBulk (doc.getSourceMidiNode(), bulkTrack);

    REQUIRE (doc.getNumTracks() == 2);
    CHECK (! doc.canUndo());
    CHECK (doc.findTrackById (42).getProperty (SongIDs::name).toString() == "Imported");
}

TEST_CASE ("SongDocument: addTrackBulk bypasses undo entirely, for non-undoable MIDI import", "[songdocument]")
{
    SongDocument doc;

    doc.addTrackBulk ("Imported", 0, 0, 1);

    REQUIRE (doc.getNumTracks() == 2);
    CHECK (! doc.canUndo());
}

TEST_CASE ("SongDocument: addPart/addAssignment's newTransaction=false batches into the caller's already-open transaction", "[songdocument]")
{
    SongDocument doc;
    auto track = doc.addTrack ("A", 0, 0, 0);
    auto trackId = (juce::int64) track.getProperty (SongIDs::trackId);

    doc.getUndoManager().beginNewTransaction();
    auto part = doc.addPart ("LuteOfAges", "Lead", false);
    doc.addAssignment (part, trackId, 0, 0, "octaveShift", false);

    REQUIRE (doc.getNumParts() == 1);
    REQUIRE (SongDocument::getNumAssignments (doc.getPart (0)) == 1);

    // Both the addTrack (its own transaction) and the addPart+addAssignment
    // pair (one shared transaction, since both passed newTransaction=false)
    // must be exactly two undo steps, not three — a bug that let
    // newTransaction=false still call beginNewTransaction() would split the
    // part and its assignment into two separate undo steps instead of one.
    doc.undo();
    CHECK (doc.getNumParts() == 0);
    CHECK (doc.getNumTracks() == 2);

    doc.undo();
    CHECK (doc.getNumTracks() == 1);
    CHECK (! doc.canUndo());
}

TEST_CASE ("SongDocument: findAssignment locates the assignment referencing a given trackId on a part, or an invalid tree if none", "[songdocument]")
{
    SongDocument doc;
    auto trackA = doc.addTrack ("A", 0, 0, 0);
    auto trackB = doc.addTrack ("B", 0, 1, 0);
    auto idA = (juce::int64) trackA.getProperty (SongIDs::trackId);
    auto idB = (juce::int64) trackB.getProperty (SongIDs::trackId);

    auto part = doc.addPart ("LuteOfAges", "Lead");
    doc.addAssignment (part, idA, 0, 100, "octaveShift");

    auto found = SongDocument::findAssignment (part, idA);
    REQUIRE (found.isValid());
    CHECK ((int) found.getProperty (SongIDs::volumePercent) == 100);

    CHECK (! SongDocument::findAssignment (part, idB).isValid());
}

TEST_CASE ("SongDocument: assignTrackToPart succeeds once, undoably, and rejects a missing part, a missing track, and a duplicate source", "[songdocument]")
{
    SongDocument doc;
    auto track = doc.addTrack ("A", 0, 0, 0);
    // Only tracks holding a note are assignable.
    juce::ValueTree note (SongIDs::NOTE);
    note.setProperty (SongIDs::pitch, 60, nullptr);
    SongDocument::appendChildBulk (SongDocument::getNotesNode (track), note);
    auto trackId = (juce::int64) track.getProperty (SongIDs::trackId);
    auto part = doc.addPart ("LuteOfAges", "Lead");
    auto partId = (juce::int64) part.getProperty (SongIDs::partId);

    // Outcome 1: missing part id -> false, no mutation, canUndo() unchanged.
    const bool canUndoBefore1 = doc.canUndo();
    CHECK_FALSE (doc.assignTrackToPart (999, trackId));
    CHECK (SongDocument::getNumAssignments (part) == 0);
    CHECK (doc.canUndo() == canUndoBefore1);

    // Outcome 2: missing track id -> false, no mutation.
    const bool canUndoBefore2 = doc.canUndo();
    CHECK_FALSE (doc.assignTrackToPart (partId, 999));
    CHECK (SongDocument::getNumAssignments (part) == 0);
    CHECK (doc.canUndo() == canUndoBefore2);

    // Outcome 3: success -> true, one undoable assignment created.
    CHECK (doc.assignTrackToPart (partId, trackId));
    REQUIRE (SongDocument::getNumAssignments (part) == 1);
    auto assignment = SongDocument::getAssignment (part, 0);
    CHECK ((juce::int64) assignment.getProperty (SongIDs::trackId) == trackId);
    CHECK ((int) assignment.getProperty (SongIDs::transposeSemitones) == 0);
    CHECK ((int) assignment.getProperty (SongIDs::volumePercent) == 0);
    CHECK (assignment.getProperty (SongIDs::rangePolicy).toString() == "octaveShift");
    REQUIRE (doc.canUndo());

    // Outcome 4: duplicate source (same trackId already assigned to this
    // part) -> false, no second assignment added, canUndo() unchanged (still
    // true from outcome 3, but no NEW undo step was pushed).
    doc.undo();
    doc.redo();
    REQUIRE (SongDocument::getNumAssignments (part) == 1);
    const bool canUndoBefore4 = doc.canUndo();
    CHECK_FALSE (doc.assignTrackToPart (partId, trackId));
    CHECK (SongDocument::getNumAssignments (part) == 1);
    CHECK (doc.canUndo() == canUndoBefore4);

    // The one successful assignment is undoable back to zero.
    doc.undo();
    CHECK (SongDocument::getNumAssignments (part) == 0);
}

TEST_CASE ("SongDocument: addChild/removeChild default to opening their own transaction", "[songdocument]")
{
    SongDocument doc;
    auto track = doc.addTrack ("Track A", 0xFF0000, 0, 1);

    juce::ValueTree note (SongIDs::NOTE);
    note.setProperty (SongIDs::pitch, 60, nullptr);

    doc.addChild (SongDocument::getNotesNode (track), note);
    REQUIRE (SongDocument::getNotesNode (track).getNumChildren() == 1);

    doc.removeChild (SongDocument::getNotesNode (track), note);
    REQUIRE (SongDocument::getNotesNode (track).getNumChildren() == 0);

    doc.undo(); // undoes removeChild only (its own transaction)
    CHECK (SongDocument::getNotesNode (track).getNumChildren() == 1);

    doc.undo(); // undoes addChild (its own, separate, earlier transaction)
    CHECK (SongDocument::getNotesNode (track).getNumChildren() == 0);
}

TEST_CASE ("SongDocument: addChild/removeChild's newTransaction=false batches into the caller's already-open transaction", "[songdocument]")
{
    SongDocument doc;
    auto track = doc.addTrack ("Track A", 0xFF0000, 0, 1);

    juce::ValueTree noteA (SongIDs::NOTE);
    noteA.setProperty (SongIDs::pitch, 60, nullptr);
    juce::ValueTree noteB (SongIDs::NOTE);
    noteB.setProperty (SongIDs::pitch, 64, nullptr);

    doc.getUndoManager().beginNewTransaction();
    doc.addChild (SongDocument::getNotesNode (track), noteA, false);
    doc.addChild (SongDocument::getNotesNode (track), noteB, false);
    REQUIRE (SongDocument::getNotesNode (track).getNumChildren() == 2);

    doc.undo();
    CHECK (SongDocument::getNotesNode (track).getNumChildren() == 0); // one undo() reverts BOTH adds

    doc.redo();
    REQUIRE (SongDocument::getNotesNode (track).getNumChildren() == 2);

    doc.getUndoManager().beginNewTransaction();
    doc.removeChild (SongDocument::getNotesNode (track), SongDocument::getNotesNode (track).getChild (0), false);
    doc.removeChild (SongDocument::getNotesNode (track), SongDocument::getNotesNode (track).getChild (0), false);
    CHECK (SongDocument::getNotesNode (track).getNumChildren() == 0);

    doc.undo();
    CHECK (SongDocument::getNotesNode (track).getNumChildren() == 2); // one undo() restores both removed children
}

TEST_CASE ("SongDocument: new tracks get empty NOTES and EVENTS containers and endTick 0", "[songdocument][fidelity]")
{
    SongDocument doc;
    auto undoable = doc.addTrack ("A", 0, 0, 1);
    auto bulk = doc.addTrackBulk ("B", 0, 0, 1);

    for (auto track : { undoable, bulk })
    {
        auto notes = SongDocument::getNotesNode (track);
        auto events = SongDocument::getEventsNode (track);
        REQUIRE (notes.isValid());
        REQUIRE (events.isValid());
        CHECK (notes.hasType (SongIDs::NOTES));
        CHECK (events.hasType (SongIDs::EVENTS));
        CHECK (notes.getNumChildren() == 0);
        CHECK (events.getNumChildren() == 0);
        CHECK ((int) track.getProperty (SongIDs::endTick) == 0);
    }
}

TEST_CASE ("SongDocument: isAssignableTrack needs at least one note and no conductor flag", "[songdocument][fidelity]")
{
    SongDocument doc;
    auto track = doc.addTrack ("A", 0, 0, 1);
    CHECK_FALSE (SongDocument::isAssignableTrack (track));
    CHECK (doc.getNumAssignableTracks() == 0);

    SongDocument::appendChildBulk (SongDocument::getNotesNode (track), juce::ValueTree (SongIDs::NOTE));
    CHECK (SongDocument::isAssignableTrack (track));
    CHECK (doc.getNumAssignableTracks() == 1);

    track.setProperty (SongIDs::isConductor, true, nullptr);
    CHECK_FALSE (SongDocument::isAssignableTrack (track));
    CHECK_FALSE (SongDocument::isAssignableTrack ({}));
}

TEST_CASE ("SongDocument: removeProperty is undoable and joins an open transaction when asked", "[songdocument][fidelity]")
{
    SongDocument doc;
    auto track = doc.addTrack ("A", 0, 0, 1);
    juce::ValueTree note (SongIDs::NOTE);
    note.setProperty (SongIDs::onOrder, 3, nullptr);
    SongDocument::appendChildBulk (SongDocument::getNotesNode (track), note);

    doc.setProperty (note, SongIDs::startTick, 10);
    doc.removeProperty (note, SongIDs::onOrder, false);
    CHECK_FALSE (note.hasProperty (SongIDs::onOrder));

    doc.undo(); // one transaction: both the setProperty and the removeProperty
    CHECK ((int) note.getProperty (SongIDs::onOrder) == 3);
    CHECK_FALSE (note.hasProperty (SongIDs::startTick));
}

TEST_CASE ("SongDocument: a fresh document has exactly one empty conductor track at child 0", "[songdocument][fidelity]")
{
    SongDocument doc;
    REQUIRE (doc.getNumTracks() == 1);
    auto conductor = doc.getConductorTrack();
    REQUIRE (conductor.isValid());
    CHECK (conductor == doc.getTrack (0));
    CHECK ((bool) conductor.getProperty (SongIDs::isConductor));
    CHECK ((int) conductor.getProperty (SongIDs::importBatch) == 0);
    CHECK ((juce::int64) conductor.getProperty (SongIDs::trackId) > 0);
    CHECK (SongDocument::getNotesNode (conductor).getNumChildren() == 0);
    CHECK (SongDocument::getEventsNode (conductor).getNumChildren() == 0);
    CHECK_FALSE (doc.canUndo());
    CHECK (doc.getNumAssignableTracks() == 0);
}

TEST_CASE ("SongDocument: removeTrack refuses the conductor and opens no transaction", "[songdocument][fidelity]")
{
    SongDocument doc;
    const auto conductorId = (juce::int64) doc.getConductorTrack().getProperty (SongIDs::trackId);
    doc.removeTrack (conductorId);
    CHECK (doc.getConductorTrack().isValid());
    CHECK_FALSE (doc.canUndo());
}

TEST_CASE ("SongDocument: assignTrackToPart refuses the conductor and note-less tracks", "[songdocument][fidelity]")
{
    SongDocument doc;
    auto part = doc.addPart ("Lute of Ages", "");
    const auto partId = (juce::int64) part.getProperty (SongIDs::partId);
    auto noteless = doc.addTrack ("Lyrics", 0, 0, 1);

    CHECK_FALSE (doc.assignTrackToPart (partId, (juce::int64) doc.getConductorTrack().getProperty (SongIDs::trackId)));
    CHECK_FALSE (doc.assignTrackToPart (partId, (juce::int64) noteless.getProperty (SongIDs::trackId)));
    CHECK (SongDocument::getNumAssignments (part) == 0);
}

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
    // (SongDocument is not movable, so the caller owns it and we fill it in.)
    void arrange (SongDocument& doc)
    {
        auto track = doc.addTrack ("Lead", 0xFF112233, 1, doc.mintImportBatch());
        juce::ValueTree note (SongIDs::NOTE);
        note.setProperty (SongIDs::pitch, 60, nullptr);
        note.setProperty (SongIDs::startTick, 0, nullptr);
        note.setProperty (SongIDs::durationTicks, 120, nullptr);
        note.setProperty (SongIDs::velocity, 90, nullptr);
        SongDocument::getNotesNode (track).addChild (note, -1, nullptr);
        auto part = doc.addPart ("Lute of Ages", "Part 1");
        doc.addAssignment (part, (juce::int64) track.getProperty (SongIDs::trackId), 0, 0, "octaveShift");
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

    SongDocument arranged;
    arrange (arranged);
    CHECK_FALSE (SongDocument::validateLoaded (arranged.getTree()).has_value());
}

TEST_CASE ("SongDocument: validateLoaded rejects structurally invalid trees", "[songdocument]")
{
    SongDocument goodDoc;
    arrange (goodDoc);
    const auto good = goodDoc.getTree();

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
        SongDocument doc;
        arrange (doc);
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
