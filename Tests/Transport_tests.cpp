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
    t.advance (1.5, 10.0);
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
    t.advance (3.0, 10.0);
    t.stop();
    CHECK (! t.isPlaying());
    CHECK (t.getPositionSeconds() == Approx (2.0));
}

TEST_CASE ("Transport: reaching the end auto-stops with the playhead at the end; play then restarts from 0", "[playback][transport]")
{
    Transport t;
    t.play (4.0);
    t.advance (5.0, 4.0);
    CHECK (! t.isPlaying());
    CHECK (t.getPositionSeconds() == Approx (4.0));
    t.play (4.0);
    CHECK (t.isPlaying());
    CHECK (t.getPositionSeconds() == Approx (0.0));
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

TEST_CASE ("previousBarTick: steps to the start of the current bar, or the previous one when on a bar line", "[playback][transport]")
{
    // 480 PPQ, 4/4 -> 1920 ticks per bar
    CHECK (previousBarTick (2500.0, 480, 4, 4) == Approx (1920.0));
    CHECK (previousBarTick (3840.0, 480, 4, 4) == Approx (1920.0));
    CHECK (previousBarTick (100.0, 480, 4, 4) == Approx (0.0));
    CHECK (previousBarTick (0.0, 480, 4, 4) == Approx (0.0));
    CHECK (previousBarTick (1000.0, 480, 3, 4) == Approx (0.0));       // 1440 per bar
    CHECK (previousBarTick (1000.0, 480, 0, 0) == Approx (0.0));       // bad meter never divides by zero
}
