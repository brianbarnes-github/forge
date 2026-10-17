#include "PlaybackTestSupport.h"
#include "UI/NoteEdit.h"
#include "UI/SongDocument.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>

using namespace lotro;
using namespace lotro::playbacktest;

namespace
{
    using Note = std::array<int, 4>;   // pitch, start, duration, velocity

    Note fieldsOf (const juce::ValueTree& n)
    {
        return { (int) n.getProperty (SongIDs::pitch), (int) n.getProperty (SongIDs::startTick),
                 (int) n.getProperty (SongIDs::durationTicks), (int) n.getProperty (SongIDs::velocity) };
    }

    std::vector<Note> fieldsOf (const std::vector<juce::ValueTree>& notes)
    {
        std::vector<Note> out;
        for (const auto& n : notes)
            out.push_back (fieldsOf (n));
        std::sort (out.begin(), out.end());
        return out;
    }

    std::vector<juce::ValueTree> allNotes (const juce::ValueTree& track)
    {
        std::vector<juce::ValueTree> out;
        const auto notes = SongDocument::getNotesNode (track);
        for (int i = 0; i < notes.getNumChildren(); ++i)
            out.push_back (notes.getChild (i));
        return out;
    }

    std::vector<Note> notesOf (const juce::ValueTree& track) { return fieldsOf (allNotes (track)); }

    juce::ValueTree noteAt (const juce::ValueTree& track, int index)
    {
        return SongDocument::getNotesNode (track).getChild (index);
    }
}

TEST_CASE ("note edit: split cuts each selected note the tick falls inside, in one undo step", "[note-edit]")
{
    SongDocument doc;
    auto track = doc.addTrack ("A", (int) 0xFFAABBCCu, 0, 0);
    addNote (track, 60, 0, 960, 100);     // spans 480
    addNote (track, 64, 240, 480, 90);    // spans 480
    addNote (track, 67, 480, 480, 80);    // starts at 480: not inside
    addNote (track, 72, 0, 960, 70);      // spans 480 but not selected
    noteAt (track, 0).setProperty (SongIDs::onOrder, 3, nullptr);
    noteAt (track, 0).setProperty (SongIDs::offOrder, 4, nullptr);
    noteAt (track, 0).setProperty (SongIDs::sectionId, 7, nullptr);
    noteAt (track, 0).setProperty (SongIDs::sourceEventIndex, 12, nullptr);
    const std::vector<juce::ValueTree> selected { noteAt (track, 0), noteAt (track, 1), noteAt (track, 2) };
    doc.getUndoManager().clearUndoHistory();

    CHECK (canSplitNotes (selected, 480));
    const auto result = splitNotes (doc, track, selected, 480);

    CHECK (notesOf (track) == std::vector<Note> { { 60, 0, 480, 100 }, { 60, 480, 480, 100 },
                                                  { 64, 240, 240, 90 }, { 64, 480, 240, 90 },
                                                  { 67, 480, 480, 80 }, { 72, 0, 960, 70 } });
    // Both halves of each split note, and the untouched selected note, stay selected.
    CHECK (fieldsOf (result) == std::vector<Note> { { 60, 0, 480, 100 }, { 60, 480, 480, 100 },
                                                    { 64, 240, 240, 90 }, { 64, 480, 240, 90 },
                                                    { 67, 480, 480, 80 } });

    // Both halves lose their recorded event order; the tail keeps the section and provenance.
    for (const auto& n : allNotes (track))
        if ((int) n.getProperty (SongIDs::pitch) == 60)
        {
            CHECK_FALSE (n.hasProperty (SongIDs::onOrder));
            CHECK_FALSE (n.hasProperty (SongIDs::offOrder));
            CHECK ((int) n.getProperty (SongIDs::sectionId) == 7);
            CHECK ((int) n.getProperty (SongIDs::sourceEventIndex) == 12);
        }

    doc.undo();
    CHECK (notesOf (track) == std::vector<Note> { { 60, 0, 960, 100 }, { 64, 240, 480, 90 },
                                                  { 67, 480, 480, 80 }, { 72, 0, 960, 70 } });
    CHECK_FALSE (doc.canUndo());
}

TEST_CASE ("note edit: a split at a note's start, its end or outside it does nothing and opens no undo step", "[note-edit]")
{
    SongDocument doc;
    auto track = doc.addTrack ("A", (int) 0xFFAABBCCu, 0, 0);
    addNote (track, 60, 480, 480, 100);
    const auto selected = allNotes (track);
    doc.getUndoManager().clearUndoHistory();

    for (const int tick : { 0, 480, 960, 2000 })
    {
        CHECK_FALSE (canSplitNotes (selected, tick));
        CHECK (splitNotes (doc, track, selected, tick).empty());
    }
    CHECK_FALSE (canSplitNotes ({}, 600));
    CHECK (notesOf (track) == std::vector<Note> { { 60, 480, 480, 100 } });
    CHECK_FALSE (doc.canUndo());
}

TEST_CASE ("note edit: join merges selected notes of one pitch from the first start to the latest end", "[note-edit]")
{
    SongDocument doc;
    auto track = doc.addTrack ("A", (int) 0xFFAABBCCu, 0, 0);
    addNote (track, 60, 480, 240, 90);
    addNote (track, 60, 0, 1000, 70);     // starts first: it survives, with its velocity
    addNote (track, 60, 1760, 240, 50);   // ends last
    addNote (track, 64, 0, 240, 100);     // a pitch with one selected note: left alone
    addNote (track, 67, 0, 240, 100);     // two of 67, the second unselected
    addNote (track, 67, 960, 240, 100);
    noteAt (track, 1).setProperty (SongIDs::onOrder, 3, nullptr);
    const std::vector<juce::ValueTree> selected { noteAt (track, 0), noteAt (track, 1), noteAt (track, 2),
                                                  noteAt (track, 3), noteAt (track, 4) };
    doc.getUndoManager().clearUndoHistory();

    CHECK (canJoinNotes (selected));
    const auto result = joinNotes (doc, track, selected);

    CHECK (notesOf (track) == std::vector<Note> { { 60, 0, 2000, 70 }, { 64, 0, 240, 100 },
                                                  { 67, 0, 240, 100 }, { 67, 960, 240, 100 } });
    CHECK_FALSE (noteAt (track, 0).hasProperty (SongIDs::onOrder));
    // The selection keeps the joined note and the untouched selected notes.
    CHECK (fieldsOf (result) == std::vector<Note> { { 60, 0, 2000, 70 }, { 64, 0, 240, 100 }, { 67, 0, 240, 100 } });

    doc.undo();
    CHECK (notesOf (track) == std::vector<Note> { { 60, 0, 1000, 70 }, { 60, 480, 240, 90 }, { 60, 1760, 240, 50 },
                                                  { 64, 0, 240, 100 }, { 67, 0, 240, 100 }, { 67, 960, 240, 100 } });
    CHECK_FALSE (doc.canUndo());
}

TEST_CASE ("note edit: join runs to the latest end even when an earlier note outlasts the last one", "[note-edit]")
{
    SongDocument doc;
    auto track = doc.addTrack ("A", (int) 0xFFAABBCCu, 0, 0);
    addNote (track, 60, 0, 1920, 100);
    addNote (track, 60, 480, 240, 60);
    addNote (track, 60, 2400, 240, 40);
    noteAt (track, 0).setProperty (SongIDs::onOrder, 3, nullptr);

    // A note swallowed whole still goes; the survivor's timing (and order) is untouched.
    CHECK (canJoinNotes ({ noteAt (track, 0), noteAt (track, 1) }));
    joinNotes (doc, track, { noteAt (track, 0), noteAt (track, 1) });
    CHECK (notesOf (track) == std::vector<Note> { { 60, 0, 1920, 100 }, { 60, 2400, 240, 40 } });
    CHECK ((int) noteAt (track, 0).getProperty (SongIDs::onOrder) == 3);

    joinNotes (doc, track, allNotes (track));
    CHECK (notesOf (track) == std::vector<Note> { { 60, 0, 2640, 100 } });
}

TEST_CASE ("note edit: join with no two selected notes of a pitch does nothing and opens no undo step", "[note-edit]")
{
    SongDocument doc;
    auto track = doc.addTrack ("A", (int) 0xFFAABBCCu, 0, 0);
    addNote (track, 60, 0, 240, 100);
    addNote (track, 64, 480, 240, 100);
    addNote (track, 60, 960, 240, 100);
    const std::vector<juce::ValueTree> selected { noteAt (track, 0), noteAt (track, 1) };
    doc.getUndoManager().clearUndoHistory();

    CHECK_FALSE (canJoinNotes (selected));
    CHECK (fieldsOf (joinNotes (doc, track, selected)) == fieldsOf (selected));
    CHECK (notesOf (track).size() == 3);
    CHECK_FALSE (doc.canUndo());
}

TEST_CASE ("note edit: legato ends each selected note where the next note on the track starts", "[note-edit]")
{
    SongDocument doc;
    auto track = doc.addTrack ("A", (int) 0xFFAABBCCu, 0, 0);
    addNote (track, 60, 0, 240, 100);     // chord at 0: both run to 960, not to each other
    addNote (track, 64, 0, 1200, 100);    // overlapping the next note: trimmed
    addNote (track, 67, 960, 240, 100);   // unselected, but still the "next note"
    addNote (track, 72, 1440, 240, 100);  // the last note on the track keeps its length
    noteAt (track, 0).setProperty (SongIDs::onOrder, 3, nullptr);
    const std::vector<juce::ValueTree> selected { noteAt (track, 0), noteAt (track, 1), noteAt (track, 3) };
    doc.getUndoManager().clearUndoHistory();

    CHECK (canLegatoNotes (track, selected));
    CHECK (legatoNotes (doc, track, selected));
    CHECK (notesOf (track) == std::vector<Note> { { 60, 0, 960, 100 }, { 64, 0, 960, 100 },
                                                  { 67, 960, 240, 100 }, { 72, 1440, 240, 100 } });
    CHECK_FALSE (noteAt (track, 0).hasProperty (SongIDs::onOrder));

    // Already legato: nothing left to change.
    CHECK_FALSE (canLegatoNotes (track, selected));

    doc.undo();
    CHECK (notesOf (track) == std::vector<Note> { { 60, 0, 240, 100 }, { 64, 0, 1200, 100 },
                                                  { 67, 960, 240, 100 }, { 72, 1440, 240, 100 } });
    CHECK_FALSE (doc.canUndo());
}

TEST_CASE ("note edit: legato with nothing to change does nothing and opens no undo step", "[note-edit]")
{
    SongDocument doc;
    auto track = doc.addTrack ("A", (int) 0xFFAABBCCu, 0, 0);
    addNote (track, 60, 0, 480, 100);
    addNote (track, 64, 0, 480, 100);
    addNote (track, 67, 480, 240, 100);
    doc.getUndoManager().clearUndoHistory();

    // The chord already ends at the next note; the last note has no next note.
    for (const auto& selected : { allNotes (track), std::vector<juce::ValueTree> { noteAt (track, 2) },
                                  std::vector<juce::ValueTree> {} })
    {
        CHECK_FALSE (canLegatoNotes (track, selected));
        CHECK_FALSE (legatoNotes (doc, track, selected));
    }
    CHECK_FALSE (doc.canUndo());
}
