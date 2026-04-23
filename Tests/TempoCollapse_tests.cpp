#include "Core/Constraints/TempoCollapse.h"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

namespace
{
    lotro::Note make (int start, int duration, int pitch = 60)
    {
        lotro::Note n;
        n.pitch         = pitch;
        n.startTick     = start;
        n.durationTicks = duration;
        n.velocity      = 100;
        return n;
    }
}

TEST_CASE ("tempo: single tempo leaves durations unchanged", "[tempo]")
{
    lotro::Song song;
    song.ticksPerQuarter = 480;
    song.tempoMap.push_back ({ 0, 120.0 });

    lotro::Track track;
    track.notes.push_back (make (   0, 480));
    track.notes.push_back (make (2000, 240));

    lotro::Diagnostics warnings;
    lotro::applyTempoCollapse (track, song, warnings);

    CHECK (track.notes[0].durationTicks == 480);
    CHECK (track.notes[1].durationTicks == 240);
}

TEST_CASE ("tempo: doubling mid-song halves durations in the second half", "[tempo]")
{
    lotro::Song song;
    song.ticksPerQuarter = 480;
    song.tempoMap.push_back ({    0, 120.0 });
    song.tempoMap.push_back ({ 1920, 240.0 });

    lotro::Track track;
    track.notes.push_back (make (   0, 480));              // first half, unchanged
    track.notes.push_back (make (1920, 480));              // second half, halved
    track.notes.push_back (make (3840, 240));              // second half, halved

    lotro::Diagnostics warnings;
    lotro::applyTempoCollapse (track, song, warnings);

    CHECK (track.notes[0].durationTicks == 480);
    CHECK (track.notes[1].durationTicks == 240);
    CHECK (track.notes[2].durationTicks == 120);

    // Start ticks compress under the doubled tempo: note at original tick
    // 3840 (= 1920 + 1920 slow ticks) sits at stream tick 1920 + 1920*0.5
    // = 2880 after collapse.
    CHECK (track.notes[0].startTick ==    0);
    CHECK (track.notes[1].startTick == 1920);
    CHECK (track.notes[2].startTick == 2880);
}

TEST_CASE ("tempo: rest gaps between notes are stretched in a slow section", "[tempo]")
{
    // Tempo halves at tick 480. A note starting at original tick 1920 is
    // 1440 slow ticks past the change, which should land at stream tick
    // 480 + 1440*2 = 3360 in main-tempo space.
    lotro::Song song;
    song.ticksPerQuarter = 480;
    song.tempoMap.push_back ({   0, 120.0 });
    song.tempoMap.push_back ({ 480,  60.0 });

    lotro::Track track;
    track.notes.push_back (make (   0, 480));
    track.notes.push_back (make (1920, 480));

    lotro::Diagnostics warnings;
    lotro::applyTempoCollapse (track, song, warnings);

    CHECK (track.notes[0].startTick     ==    0);
    CHECK (track.notes[0].durationTicks ==  480);
    CHECK (track.notes[1].startTick     == 3360);
    CHECK (track.notes[1].durationTicks ==  960);
}

TEST_CASE ("tempo: meter-map ticks rescale alongside note ticks", "[tempo][meter]")
{
    // Tempo halves at tick 1920. A meter change originally at tick 3840
    // (1920 slow ticks past the tempo change) lands at stream tick
    // 1920 + 1920*2 = 5760 after collapse.
    lotro::Song song;
    song.ticksPerQuarter = 480;
    song.tempoMap.push_back ({    0, 120.0 });
    song.tempoMap.push_back ({ 1920,  60.0 });
    song.meterMap.push_back ({    0, 4, 4 });
    song.meterMap.push_back ({ 3840, 3, 4 });

    lotro::applyTempoCollapseToMeterMap (song);

    REQUIRE (song.meterMap.size() == 2);
    CHECK (song.meterMap[0].tick ==    0);
    CHECK (song.meterMap[1].tick == 5760);
}

TEST_CASE ("tempo: song tempoMap is not mutated by collapse", "[tempo]")
{
    lotro::Song song;
    song.ticksPerQuarter = 480;
    song.tempoMap.push_back ({    0, 120.0 });
    song.tempoMap.push_back ({ 1920, 240.0 });

    lotro::Track track;
    track.notes.push_back (make (1920, 480));

    lotro::Diagnostics warnings;
    lotro::applyTempoCollapse (track, song, warnings);

    REQUIRE (song.tempoMap.size() == 2);
    CHECK (song.tempoMap[0].tick ==    0);
    CHECK (song.tempoMap[0].bpm  == 120.0);
    CHECK (song.tempoMap[1].tick == 1920);
    CHECK (song.tempoMap[1].bpm  == 240.0);
}
