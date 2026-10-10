#include "PlaybackTestSupport.h"
#include "UI/ProgramChanges.h"
#include "UI/TimelineViewState.h"

#include <catch2/catch_test_macros.hpp>

using namespace lotro;
using namespace lotro::playbacktest;

TEST_CASE ("program changes: parsed in tick order with channel and program", "[instrument]")
{
    SongDocument doc;
    auto t = addTrack (doc, "T", 2);
    addEvent (t, 960, { 0xC1, 40 });
    addEvent (t, 0, { 0xC1, 73 });
    addEvent (t, 10, { 0xB1, 7, 100 });   // a controller is not a program change

    const auto pcs = programChangesOf (t);

    REQUIRE (pcs.size() == 2);
    CHECK (pcs[0].tick == 0);
    CHECK (pcs[0].program == 73);
    CHECK (pcs[0].channel == 2);
    CHECK (pcs[1].tick == 960);
    CHECK (pcs[1].program == 40);
}

TEST_CASE ("program changes: the conductor has none", "[instrument]")
{
    SongDocument doc;
    auto t = addTrack (doc);
    t.setProperty (SongIDs::isConductor, true, nullptr);
    addEvent (t, 0, { 0xC0, 5 });
    CHECK (programChangesOf (t).empty());
    CHECK (instrumentSegmentsOf (t).empty());
}

TEST_CASE ("instrument segments: one per distinct instrument, to the next change", "[instrument]")
{
    SongDocument doc;
    auto t = addTrack (doc);
    t.setProperty (SongIDs::sourceProgram, 73, nullptr);
    t.setProperty (SongIDs::endTick, 1920, nullptr);
    addEvent (t, 0, { 0xC0, 73 });
    addEvent (t, 480, { 0xC0, 73 });   // same program again: merged
    addEvent (t, 960, { 0xC0, 40 });

    const auto s = instrumentSegmentsOf (t);

    REQUIRE (s.size() == 2);
    CHECK (s[0].startTick == 0);
    CHECK (s[0].endTick == 960);
    CHECK (s[0].program == 73);
    CHECK (s[1].startTick == 960);
    CHECK (s[1].endTick == 1920);
    CHECK (s[1].program == 40);
}

TEST_CASE ("instrument segments: the span before a late first change uses sourceProgram", "[instrument]")
{
    SongDocument doc;
    auto t = addTrack (doc);
    t.setProperty (SongIDs::sourceProgram, 73, nullptr);
    t.setProperty (SongIDs::endTick, 1000, nullptr);
    addEvent (t, 480, { 0xC0, 40 });

    const auto s = instrumentSegmentsOf (t);

    REQUIRE (s.size() == 2);
    CHECK (s[0].startTick == 0);
    CHECK (s[0].endTick == 480);
    CHECK (s[0].program == 73);
    CHECK (s[1].program == 40);
}

TEST_CASE ("instrument segments: a track with no program change is one sourceProgram segment", "[instrument]")
{
    SongDocument doc;
    auto t = addTrack (doc);
    t.setProperty (SongIDs::sourceProgram, 5, nullptr);
    addNote (t, 60, 0, 480);

    const auto s = instrumentSegmentsOf (t);

    REQUIRE (s.size() == 1);
    CHECK (s[0].program == 5);
    CHECK (s[0].endTick >= 480);
}

TEST_CASE ("instrument segments: two changes on one tick keep the later one", "[instrument]")
{
    SongDocument doc;
    auto t = addTrack (doc);
    t.setProperty (SongIDs::sourceProgram, 1, nullptr);
    t.setProperty (SongIDs::endTick, 1000, nullptr);
    addEvent (t, 0, { 0xC0, 1 });
    addEvent (t, 0, { 0xC0, 9 });

    const auto s = instrumentSegmentsOf (t);

    REQUIRE (s.size() == 1);
    CHECK (s[0].program == 9);
}

TEST_CASE ("band segments: ticks map to pixels through the shared view state", "[instrument]")
{
    TimelineViewState view;
    view.setPixelsPerTick (0.1);
    const std::vector<InstrumentSegment> segs { { 0, 960, 73 }, { 960, 1920, 40 } };

    const auto b = bandSegments (segs, view);

    REQUIRE (b.size() == 2);
    CHECK (b[0].x0 == 0);
    CHECK (b[0].x1 == 96);
    CHECK (b[1].x0 == 96);
    CHECK (b[1].x1 == 192);
    CHECK (b[1].program == 40);
}
