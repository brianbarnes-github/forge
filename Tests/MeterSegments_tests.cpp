#include "UI/MeterSegments.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace lotro;
using Catch::Approx;

TEST_CASE ("meterSegments: no changes is one 4/4 segment from tick 0", "[meter-segments]")
{
    const auto s = meterSegments ({}, 480);
    REQUIRE (s.size() == 1);
    CHECK (s[0].startTick == Approx (0.0));
    CHECK (s[0].ticksPerBar == Approx (1920.0));
    CHECK (s[0].ticksPerBeat == Approx (480.0));
    CHECK (s[0].firstBar == 0);
    CHECK (meterSegments ({}, 0).empty());
}

TEST_CASE ("meterSegments: later segments continue the bar count, a partial bar counts as a bar", "[meter-segments]")
{
    const auto exact = meterSegments ({ { 0, 4, 4 }, { 1920, 3, 4 } }, 480);
    REQUIRE (exact.size() == 2);
    CHECK (exact[1].firstBar == 1);
    CHECK (exact[1].ticksPerBar == Approx (1440.0));

    const auto partial = meterSegments ({ { 0, 4, 4 }, { 2400, 6, 8 } }, 480);   // 1.25 bars of 4/4 first
    CHECK (partial[1].firstBar == 2);
    CHECK (partial[1].ticksPerBeat == Approx (240.0));
    CHECK (partial[1].ticksPerBar == Approx (1440.0));
}

TEST_CASE ("meterSegments: unsorted input, duplicate ticks, bad values and a first entry after tick 0", "[meter-segments]")
{
    const auto sorted = meterSegments ({ { 1920, 3, 4 }, { 0, 4, 4 } }, 480);
    REQUIRE (sorted.size() == 2);
    CHECK (sorted[0].numerator == 4);
    CHECK (sorted[1].numerator == 3);

    const auto dup = meterSegments ({ { 0, 4, 4 }, { 1920, 3, 4 }, { 1920, 5, 4 } }, 480);
    REQUIRE (dup.size() == 2);
    CHECK (dup[1].numerator == 5);   // the later of two on one tick wins

    const auto bad = meterSegments ({ { 0, 0, 0 } }, 480);
    CHECK (bad[0].numerator == 4);
    CHECK (bad[0].denominator == 4);

    const auto late = meterSegments ({ { 960, 3, 4 } }, 480);   // applies from the start
    REQUIRE (late.size() == 1);
    CHECK (late[0].startTick == Approx (0.0));
    CHECK (late[0].numerator == 3);
}

TEST_CASE ("segmentAt and nearestBarStart", "[meter-segments]")
{
    const auto s = meterSegments ({ { 0, 4, 4 }, { 1920, 3, 4 } }, 480);
    CHECK (segmentAt (s, 0.0).numerator == 4);
    CHECK (segmentAt (s, 1919.0).numerator == 4);
    CHECK (segmentAt (s, 1920.0).numerator == 3);
    CHECK (segmentAt (s, -50.0).numerator == 4);

    CHECK (nearestBarStart (s, 100.0) == Approx (0.0));
    CHECK (nearestBarStart (s, 1100.0) == Approx (1920.0));            // nearer the next bar line
    CHECK (nearestBarStart (s, 1920.0 + 1440.0 * 0.6) == Approx (1920.0 + 1440.0));
    CHECK (nearestBarStart (s, 1900.0) == Approx (1920.0));            // never past a segment's end
    CHECK (nearestBarStart (s, -10.0) == Approx (0.0));
}

TEST_CASE ("previousBarStart: one meter matches previousBarTick, and it crosses a meter change", "[meter-segments]")
{
    const auto one = meterSegments ({ { 0, 4, 4 } }, 480);
    CHECK (previousBarStart (one, 2500.0) == Approx (1920.0));
    CHECK (previousBarStart (one, 3840.0) == Approx (1920.0));   // exactly on a bar line: the previous bar
    CHECK (previousBarStart (one, 100.0) == Approx (0.0));
    CHECK (previousBarStart (one, 0.0) == Approx (0.0));

    const auto two = meterSegments ({ { 0, 4, 4 }, { 1920, 3, 4 } }, 480);
    CHECK (previousBarStart (two, 1920.0) == Approx (0.0));              // on the change: the last bar of 4/4
    CHECK (previousBarStart (two, 2000.0) == Approx (1920.0));
    CHECK (previousBarStart (two, 3360.0 + 10.0) == Approx (3360.0));    // 1920 + 1440
    CHECK (previousBarStart (two, 3360.0) == Approx (1920.0));
    CHECK (previousBarStart ({}, 500.0) == Approx (0.0));                // no segments: start
}
