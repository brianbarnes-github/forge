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
