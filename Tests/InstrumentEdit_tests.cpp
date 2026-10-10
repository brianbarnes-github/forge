#include "PlaybackTestSupport.h"
#include "UI/InstrumentEdit.h"
#include "UI/MidiExport.h"
#include "UI/ProgramChanges.h"
#include "UI/SectionEdit.h"

#include <catch2/catch_test_macros.hpp>

using namespace lotro;
using namespace lotro::playbacktest;

namespace
{
    juce::int64 idOf (const juce::ValueTree& t) { return (juce::int64) t.getProperty (SongIDs::trackId); }

    // Piano (73) for the first bar, then 40: notes in both halves.
    juce::ValueTree twoInstrumentTrack (SongDocument& doc)
    {
        auto t = addTrack (doc);
        t.setProperty (SongIDs::sourceProgram, 73, nullptr);
        t.setProperty (SongIDs::endTick, 1920, nullptr);
        addEvent (t, 0, { 0xC0, 73 });
        addEvent (t, 960, { 0xC0, 40 });
        addNote (t, 60, 0, 480);
        addNote (t, 62, 960, 480);
        return t;
    }
}

TEST_CASE ("splitAtTicks: several ticks are one undo step", "[instrument][split]")
{
    SongDocument doc;
    auto t = addTrack (doc);
    addNote (t, 60, 0, 1800);

    splitAtTicks (doc, idOf (t), { 480, 960 });

    CHECK (sectionsOf (t).size() == 3);
    doc.undo();
    CHECK (sectionsOf (t).size() == 1);
    CHECK_FALSE (doc.canUndo());
}

TEST_CASE ("splitAtTicks: ticks outside the sections change nothing and open no transaction", "[instrument][split]")
{
    SongDocument doc;
    auto t = addTrack (doc);
    addNote (t, 60, 0, 480);

    splitAtTicks (doc, idOf (t), { 0, 480, 5000 });

    CHECK_FALSE (doc.canUndo());
}

TEST_CASE ("auto split: cuts the track at each instrument change, in one undo step", "[instrument][split]")
{
    SongDocument doc;
    auto t = twoInstrumentTrack (doc);
    REQUIRE (canAutoSplit (t));

    autoSplitOnInstrumentChange (doc, idOf (t));

    const auto s = sectionsOf (t);
    REQUIRE (s.size() == 2);
    CHECK (s[0].endTick == 960);
    CHECK (s[1].startTick == 960);
    doc.undo();
    CHECK (sectionsOf (t).size() == 1);
    CHECK_FALSE (doc.canUndo());
}

TEST_CASE ("auto split: one instrument is disabled and changes nothing", "[instrument][split]")
{
    SongDocument doc;
    auto t = addTrack (doc);
    t.setProperty (SongIDs::sourceProgram, 73, nullptr);
    addEvent (t, 0, { 0xC0, 73 });
    addNote (t, 60, 0, 480);
    CHECK_FALSE (canAutoSplit (t));

    autoSplitOnInstrumentChange (doc, idOf (t));

    CHECK_FALSE (doc.canUndo());
}

TEST_CASE ("auto split: a change after the last section creates no empty section", "[instrument][split]")
{
    SongDocument doc;
    auto t = addTrack (doc);
    t.setProperty (SongIDs::sourceProgram, 73, nullptr);
    addEvent (t, 0, { 0xC0, 73 });
    addEvent (t, 5000, { 0xC0, 40 });
    addNote (t, 60, 0, 480);

    autoSplitOnInstrumentChange (doc, idOf (t));

    CHECK (sectionsOf (t).size() == 1);
    CHECK_FALSE (doc.canUndo());
}

TEST_CASE ("set instrument: rewrites the first change, deletes the later ones, updates sourceProgram", "[instrument][set]")
{
    SongDocument doc;
    auto t = twoInstrumentTrack (doc);

    setTrackInstrument (doc, idOf (t), 24);

    const auto pcs = programChangesOf (t);
    REQUIRE (pcs.size() == 1);
    CHECK (pcs[0].tick == 0);
    CHECK (pcs[0].program == 24);
    CHECK ((int) t.getProperty (SongIDs::sourceProgram) == 24);
    doc.undo();
    CHECK (programChangesOf (t).size() == 2);
    CHECK ((int) t.getProperty (SongIDs::sourceProgram) == 73);
    CHECK_FALSE (doc.canUndo());
}

TEST_CASE ("set instrument: a track without a change gets one at tick 0 on its default channel", "[instrument][set]")
{
    SongDocument doc;
    auto t = addTrack (doc, "T", 3);
    t.setProperty (SongIDs::defaultChannel, 3, nullptr);
    t.setProperty (SongIDs::sourceProgram, 0, nullptr);
    addNote (t, 60, 0, 480);

    setTrackInstrument (doc, idOf (t), 40);

    const auto pcs = programChangesOf (t);
    REQUIRE (pcs.size() == 1);
    CHECK (pcs[0].tick == 0);
    CHECK (pcs[0].channel == 3);
    CHECK (pcs[0].program == 40);
}

TEST_CASE ("set instrument: picking what the track already plays changes nothing", "[instrument][set]")
{
    SongDocument doc;
    auto t = addTrack (doc);
    t.setProperty (SongIDs::sourceProgram, 5, nullptr);
    addNote (t, 60, 0, 480);

    setTrackInstrument (doc, idOf (t), 5);   // no change event, already program 5

    CHECK (programChangesOf (t).empty());
    CHECK_FALSE (doc.canUndo());
}

TEST_CASE ("set instrument: keeps the first change per channel", "[instrument][set]")
{
    SongDocument doc;
    auto t = addTrack (doc);
    t.setProperty (SongIDs::sourceProgram, 1, nullptr);
    addEvent (t, 0, { 0xC0, 1 });
    addEvent (t, 0, { 0xC1, 2 });
    addEvent (t, 480, { 0xC0, 3 });

    setTrackInstrument (doc, idOf (t), 9);

    const auto pcs = programChangesOf (t);
    REQUIRE (pcs.size() == 2);
    CHECK (pcs[0].program == 9);
    CHECK (pcs[1].program == 9);
}

TEST_CASE ("set instrument: the conductor and unknown tracks are ignored", "[instrument][set]")
{
    SongDocument doc;
    auto c = addTrack (doc);
    c.setProperty (SongIDs::isConductor, true, nullptr);

    setTrackInstrument (doc, idOf (c), 9);
    setTrackInstrument (doc, 9999, 9);

    CHECK_FALSE (doc.canUndo());
}

TEST_CASE ("set instrument: the inserted change exports before imported tick-0 notes", "[instrument][set]")
{
    SongDocument doc;
    auto t = addTrack (doc);
    t.setProperty (SongIDs::sourceProgram, 0, nullptr);
    addNote (t, 60, 0, 480);
    auto note = SongDocument::getNotesNode (t).getChild (0);
    note.setProperty (SongIDs::onOrder, 3, nullptr);       // as an import records them
    note.setProperty (SongIDs::offOrder, 4, nullptr);
    addEvent (t, 100, { 0xB0, 7, 100 });
    SongDocument::getEventsNode (t).getChild (0).setProperty (SongIDs::order, 9, nullptr);

    setTrackInstrument (doc, idOf (t), 40);

    int checked = 0;
    for (const auto& track : buildRawMidiFile (doc).tracks)
    {
        int program = -1, noteOn = -1, i = 0;
        for (const auto& e : track.events)
        {
            if ((e.bytes[0] & 0xF0) == 0xC0 && program < 0) program = i;
            if ((e.bytes[0] & 0xF0) == 0x90 && noteOn < 0) noteOn = i;
            ++i;
        }
        if (noteOn >= 0)
        {
            REQUIRE (program >= 0);
            CHECK (program < noteOn);
            ++checked;
        }
    }
    CHECK (checked == 1);
}

TEST_CASE ("set instrument: a first change after tick 0 moves to tick 0 so the whole track is one instrument", "[instrument][set]")
{
    SongDocument doc;
    auto t = addTrack (doc);
    t.setProperty (SongIDs::sourceProgram, 0, nullptr);
    addNote (t, 60, 0, 480);
    addEvent (t, 960, { 0xC0, 40 });

    setTrackInstrument (doc, idOf (t), 24);

    const auto pcs = programChangesOf (t);
    REQUIRE (pcs.size() == 1);
    CHECK (pcs[0].tick == 0);
    CHECK (pcs[0].program == 24);
    doc.undo();
    CHECK (programChangesOf (t)[0].tick == 960);
    CHECK_FALSE (doc.canUndo());
}
