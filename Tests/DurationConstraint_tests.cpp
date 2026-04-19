#include "Core/Constraints/DurationConstraint.h"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

namespace
{
    lotro::Song songWithMeter (int ppq = 480, int num = 4, int den = 4)
    {
        lotro::Song s;
        s.ticksPerQuarter = ppq;
        s.meterMap.push_back ({ 0, num, den });
        s.tempoMap.push_back ({ 0, 120.0 });
        return s;
    }

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

TEST_CASE ("duration: sub-grid duration is preserved exactly (no minimum floor)", "[duration]")
{
    auto song = songWithMeter();
    lotro::Track track;
    track.notes.push_back (make (0, 30));                  // 30 ticks = 1/64 at PPQ 480

    lotro::Diagnostics warnings;
    lotro::applyDurationConstraint (track, song, warnings);

    REQUIRE (track.notes.size() == 1);
    CHECK (track.notes.front().startTick     == 0);
    CHECK (track.notes.front().durationTicks == 30);
}

TEST_CASE ("duration: zero-duration note is dropped", "[duration]")
{
    auto song = songWithMeter();
    lotro::Track track;
    track.notes.push_back (make (0, 0));

    lotro::Diagnostics warnings;
    lotro::applyDurationConstraint (track, song, warnings);

    CHECK (track.notes.empty());
}

TEST_CASE ("duration: start tick is preserved exactly (no grid snap)", "[duration]")
{
    auto song = songWithMeter();
    lotro::Track track;
    track.notes.push_back (make (7, 480));                 // off-grid start, quarter duration

    lotro::Diagnostics warnings;
    lotro::applyDurationConstraint (track, song, warnings);

    REQUIRE (track.notes.size() == 1);
    CHECK (track.notes.front().startTick     == 7);
    CHECK (track.notes.front().durationTicks == 480);
}

TEST_CASE ("duration: long notes pass through unchanged (no bar splitting)", "[duration]")
{
    // In the held-note chord-emission model, a long note is emitted once
    // and its full duration controls how long the instrument rings.
    // No bar-boundary splitting is needed — bar lines aren't emitted anyway.
    auto song = songWithMeter();
    lotro::Track track;
    track.notes.push_back (make (0, 1920 * 3));            // 3 full bars

    lotro::Diagnostics warnings;
    lotro::applyDurationConstraint (track, song, warnings);

    REQUIRE (track.notes.size() == 1);
    CHECK (track.notes.front().startTick     == 0);
    CHECK (track.notes.front().durationTicks == 5760);
}

TEST_CASE ("duration: short in-range note is left alone after quantization", "[duration]")
{
    auto song = songWithMeter();
    lotro::Track track;
    track.notes.push_back (make (480, 240));               // starts on beat 1, 1/8 note duration

    lotro::Diagnostics warnings;
    lotro::applyDurationConstraint (track, song, warnings);

    REQUIRE (track.notes.size() == 1);
    CHECK (track.notes.front().startTick     == 480);
    CHECK (track.notes.front().durationTicks == 240);
}
