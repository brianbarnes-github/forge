#include "Core/Constraints/CollisionGuard.h"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

namespace
{
    lotro::Note make (int pitch, int start, int duration)
    {
        lotro::Note n;
        n.pitch         = pitch;
        n.startTick     = start;
        n.durationTicks = duration;
        n.velocity      = 100;
        return n;
    }
}

TEST_CASE ("collision: two overlapping notes at the same pitch - first is trimmed", "[collision]")
{
    lotro::Track track;
    track.notes.push_back (make (60,   0, 480));
    track.notes.push_back (make (60, 240, 480));

    lotro::Diagnostics warnings;
    lotro::applyCollisionGuard (track, warnings);

    REQUIRE (track.notes.size() == 2);
    CHECK (track.notes[0].durationTicks == 239);
    CHECK (track.notes[1].durationTicks == 480);
    CHECK (warnings.empty());
}

TEST_CASE ("collision: two overlapping notes at different pitches are untouched", "[collision]")
{
    lotro::Track track;
    track.notes.push_back (make (60,   0, 480));
    track.notes.push_back (make (64, 240, 480));

    lotro::Diagnostics warnings;
    lotro::applyCollisionGuard (track, warnings);

    REQUIRE (track.notes.size() == 2);
    CHECK (track.notes[0].durationTicks == 480);
    CHECK (track.notes[1].durationTicks == 480);
}

TEST_CASE ("collision: same-tick same-pitch drops the earlier note", "[collision]")
{
    lotro::Track track;
    track.notes.push_back (make (60, 100, 480));
    track.notes.push_back (make (60, 100, 240));

    lotro::Diagnostics warnings;
    lotro::applyCollisionGuard (track, warnings);

    REQUIRE (track.notes.size() == 1);
    CHECK (warnings.size() == 1);
}
