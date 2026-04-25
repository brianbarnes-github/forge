#include "Core/AbcWriter.h"
#include "Core/Song.h"

#include <catch2/catch_test_macros.hpp>

namespace
{
    lotro::Note note (int pitch, int startTick, int dur, int vel = 100)
    {
        lotro::Note n;
        n.pitch = pitch; n.startTick = startTick;
        n.durationTicks = dur; n.velocity = vel;
        return n;
    }
}

TEST_CASE ("abc-writer: parts emit in ascending x order regardless of vector order", "[abc-writer][config]")
{
    lotro::Song song;
    song.title            = "x-order";
    song.ticksPerQuarter  = 480;
    song.meterMap.push_back ({ 0, 4, 4 });
    song.tempoMap.push_back ({ 0, 120.0 });

    for (int x : {3, 1, 2})
    {
        lotro::Track t;
        t.x    = x;
        t.name = "part" + std::to_string (x);
        t.instrument = lotro::LotroInstrument::LuteOfAges;
        t.notes.push_back (note (60, 0, 480));
        song.tracks.push_back (t);
    }

    const auto abc = lotro::writeAbc (song);

    const auto x1 = abc.find ("X:1");
    const auto x2 = abc.find ("X:2");
    const auto x3 = abc.find ("X:3");
    REQUIRE (x1 != std::string::npos);
    REQUIRE (x2 != std::string::npos);
    REQUIRE (x3 != std::string::npos);
    CHECK (x1 < x2);
    CHECK (x2 < x3);
}

TEST_CASE ("abc-writer: gaps in x are preserved as-written", "[abc-writer][config]")
{
    lotro::Song song;
    song.ticksPerQuarter = 480;
    song.meterMap.push_back ({ 0, 4, 4 });
    song.tempoMap.push_back ({ 0, 120.0 });

    for (int x : {1, 3, 7})
    {
        lotro::Track t;
        t.x    = x;
        t.name = "p" + std::to_string (x);
        t.instrument = lotro::LotroInstrument::LuteOfAges;
        t.notes.push_back (note (60, 0, 480));
        song.tracks.push_back (t);
    }

    const auto abc = lotro::writeAbc (song);
    CHECK (abc.find ("X:1") != std::string::npos);
    CHECK (abc.find ("X:2") == std::string::npos);
    CHECK (abc.find ("X:3") != std::string::npos);
    CHECK (abc.find ("X:7") != std::string::npos);
}

TEST_CASE ("abc-writer: track with x == 0 falls back to sequential auto-index", "[abc-writer][config]")
{
    lotro::Song song;
    song.ticksPerQuarter = 480;
    song.meterMap.push_back ({ 0, 4, 4 });
    song.tempoMap.push_back ({ 0, 120.0 });

    for (int i = 0; i < 2; ++i)
    {
        lotro::Track t;
        t.name = "track" + std::to_string (i);
        t.instrument = lotro::LotroInstrument::LuteOfAges;
        t.notes.push_back (note (60, 0, 480));
        song.tracks.push_back (t);
    }

    const auto abc = lotro::writeAbc (song);
    CHECK (abc.find ("X:1") != std::string::npos);
    CHECK (abc.find ("X:2") != std::string::npos);
}

TEST_CASE ("abc-writer: per-track drumMap overrides song.drumMap", "[abc-writer][config]")
{
    lotro::Song song;
    song.ticksPerQuarter = 480;
    song.meterMap.push_back ({ 0, 4, 4 });
    song.tempoMap.push_back ({ 0, 120.0 });
    song.drumMap = lotro::defaultDrumMap();

    lotro::Track t;
    t.x    = 1;
    t.name = "kit";
    t.instrument = lotro::LotroInstrument::Drums;

    lotro::DrumMap perTrack;
    perTrack.set (38, "Z");
    t.drumMap = perTrack;

    auto n = note (38, 0, 480);
    n.isDrum = true;
    t.notes.push_back (n);

    song.tracks.push_back (t);

    const auto abc = lotro::writeAbc (song);
    // Per-track map maps pitch 38 → "Z"; song-level default would give "F".
    // Verify the per-track override wins. Look for tokens inside chord
    // brackets so we don't false-match letters in the banner / title.
    CHECK (abc.find ("[Z") != std::string::npos);
    CHECK (abc.find ("[F") == std::string::npos);
}
