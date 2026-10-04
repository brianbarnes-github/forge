#include "UI/Playback/TempoMap.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace lotro;
using Catch::Approx;

TEST_CASE ("TempoMap: an empty map is 120 BPM", "[playback][tempo]")
{
    const TempoMap map ({}, 480);
    CHECK (map.ticksToSeconds (480.0) == Approx (0.5));
    CHECK (map.secondsToTicks (0.5) == Approx (480.0));
}

TEST_CASE ("TempoMap: tempo before the first change is 120 BPM", "[playback][tempo]")
{
    const TempoMap map ({ { 480, 60.0 } }, 480);
    CHECK (map.ticksToSeconds (480.0) == Approx (0.5));          // 120 BPM up to tick 480
    CHECK (map.ticksToSeconds (960.0) == Approx (0.5 + 1.0));    // then 60 BPM
}

TEST_CASE ("TempoMap: mid-song change is integrated piecewise and inverts exactly", "[playback][tempo]")
{
    const TempoMap map ({ { 0, 120.0 }, { 480, 60.0 }, { 960, 240.0 } }, 480);
    CHECK (map.ticksToSeconds (960.0) == Approx (1.5));
    CHECK (map.ticksToSeconds (1440.0) == Approx (1.5 + 0.25));
    for (double tick : { 0.0, 100.0, 480.0, 700.0, 960.0, 1500.0 })
        CHECK (map.secondsToTicks (map.ticksToSeconds (tick)) == Approx (tick).margin (1e-6));
}

TEST_CASE ("TempoMap: seconds are independent of PPQ", "[playback][tempo]")
{
    const TempoMap low ({ { 0, 100.0 }, { 480, 150.0 } }, 480);
    const TempoMap high ({ { 0, 100.0 }, { 960, 150.0 } }, 960);   // same song at twice the PPQ
    CHECK (high.ticksToSeconds (1920.0) == Approx (low.ticksToSeconds (960.0)));
}

TEST_CASE ("TempoMap: bad data never produces a hang or NaN", "[playback][tempo]")
{
    const TempoMap map ({ { 0, 0.0 }, { 10, -5.0 } }, 0);   // bpm <= 0 and ppq 0
    CHECK (map.getTicksPerQuarter() == 480);
    CHECK (map.ticksToSeconds (480.0) == Approx (0.5));
    CHECK (map.ticksToSeconds (-10.0) == Approx (0.0));
}

TEST_CASE ("TempoMap: a later change at the same tick wins", "[playback][tempo]")
{
    const TempoMap map ({ { 0, 120.0 }, { 0, 60.0 } }, 480);
    CHECK (map.ticksToSeconds (480.0) == Approx (1.0));
}
