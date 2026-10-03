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
