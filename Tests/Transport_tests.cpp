#include "UI/Playback/Transport.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace lotro;
using Catch::Approx;

TEST_CASE ("Transport: play on a song with no length is a no-op", "[playback][transport]")
{
    Transport t;
    t.play (0.0);
    CHECK (! t.isPlaying());
    CHECK (t.getPositionSeconds() == Approx (0.0));
}

TEST_CASE ("Transport: play starts from the current position; pause keeps it", "[playback][transport]")
{
    Transport t;
    t.seek (2.0);
    t.play (10.0);
    CHECK (t.isPlaying());
    t.advance (t.getPositionSeconds(), 1.5, 10.0);
    CHECK (t.getPositionSeconds() == Approx (3.5));
    t.pause();
    CHECK (! t.isPlaying());
    CHECK (t.getPositionSeconds() == Approx (3.5));
}

TEST_CASE ("Transport: stop returns to where play started", "[playback][transport]")
{
    Transport t;
    t.seek (2.0);
    t.play (10.0);
    t.advance (t.getPositionSeconds(), 3.0, 10.0);
    t.stop();
    CHECK (! t.isPlaying());
    CHECK (t.getPositionSeconds() == Approx (2.0));
}

TEST_CASE ("Transport: reaching the end auto-stops with the playhead at the end; play then restarts from 0", "[playback][transport]")
{
    Transport t;
    t.play (4.0);
    t.advance (t.getPositionSeconds(), 5.0, 4.0);
    CHECK (! t.isPlaying());
    CHECK (t.getPositionSeconds() == Approx (4.0));
    t.play (4.0);
    CHECK (t.isPlaying());
    CHECK (t.getPositionSeconds() == Approx (0.0));
}

TEST_CASE ("Transport: advance from a stale position is dropped after a seek", "[playback][transport]")
{
    Transport t;
    t.play (10.0);
    const double blockStart = t.getPositionSeconds();
    t.seek (4.0);
    CHECK (! t.advance (blockStart, 0.5, 10.0));
    CHECK (t.getPositionSeconds() == Approx (4.0));
    CHECK (t.isPlaying());
    CHECK (t.advance (4.0, 0.5, 10.0));
    CHECK (t.getPositionSeconds() == Approx (4.5));
}

TEST_CASE ("Transport: seeks bump the generation and clamp at zero", "[playback][transport]")
{
    Transport t;
    const auto g0 = t.getSeekGeneration();
    t.seek (-3.0);
    CHECK (t.getPositionSeconds() == Approx (0.0));
    CHECK (t.getSeekGeneration() != g0);
    t.goToEnd (7.0);
    CHECK (t.getPositionSeconds() == Approx (7.0));
    t.goToStart();
    CHECK (t.getPositionSeconds() == Approx (0.0));
}

TEST_CASE ("Transport::advanceTo commits only from the expected position and stops at the end", "[playback][transport]")
{
    Transport t;
    t.play (2.0);
    CHECK (t.advanceTo (0.0, 0.5, 2.0));
    CHECK (t.getPositionSeconds() == Approx (0.5));
    CHECK (t.isPlaying());

    CHECK (! t.advanceTo (0.0, 0.7, 2.0));       // stale start: dropped
    CHECK (t.getPositionSeconds() == Approx (0.5));
    CHECK (t.isPlaying());

    CHECK (t.advanceTo (0.5, 3.0, 2.0));         // clamped to the end, which stops
    CHECK (t.getPositionSeconds() == Approx (2.0));
    CHECK (! t.isPlaying());
}

TEST_CASE ("Transport::advanceTo may move the playhead backwards (a loop wrap) and keeps playing", "[playback][transport]")
{
    Transport t;
    t.play (2.0);
    REQUIRE (t.advanceTo (0.0, 0.5, 2.0));
    const unsigned generation = t.getSeekGeneration();
    CHECK (t.advanceTo (0.5, 0.45, 2.0));
    CHECK (t.getPositionSeconds() == Approx (0.45));
    CHECK (t.isPlaying());
    CHECK (t.getSeekGeneration() == generation);   // not a seek
}
