#include "UI/Playback/RateMapping.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>

using namespace lotro;
using Catch::Approx;

TEST_CASE ("RateMapping: 1.000 sits at the slider centre and 0.5 / 2.0 are symmetric", "[playback][rate]")
{
    CHECK (rateToSlider (1.0) == Approx (0.0).margin (1e-12));
    CHECK (rateToSlider (2.0) == Approx (1.0));
    CHECK (rateToSlider (0.5) == Approx (-1.0));
    CHECK (rateToSlider (kMinPlaybackRate) == Approx (-2.0));
    CHECK (rateToSlider (kMaxPlaybackRate) == Approx (2.0));
}

TEST_CASE ("RateMapping: the slider snaps to exactly 1.0 near the centre and round-trips elsewhere", "[playback][rate]")
{
    CHECK (sliderToRate (0.0) == 1.0);
    CHECK (sliderToRate (0.015) == 1.0);
    CHECK (sliderToRate (-0.015) == 1.0);
    CHECK (sliderToRate (0.05) != 1.0);
    for (double r : { 0.25, 0.5, 0.8, 1.25, 2.0, 4.0 })
        CHECK (sliderToRate (rateToSlider (r)) == Approx (r));
}

TEST_CASE ("RateMapping: out-of-range and non-finite rates clamp", "[playback][rate]")
{
    CHECK (clampRate (0.0) == kMinPlaybackRate);
    CHECK (clampRate (-3.0) == kMinPlaybackRate);
    CHECK (clampRate (100.0) == kMaxPlaybackRate);
    CHECK (clampRate (std::nan ("")) == 1.0);
    CHECK (sliderToRate (50.0) == kMaxPlaybackRate);
}

TEST_CASE ("RateMapping: text is three decimals and parses back, rejecting junk", "[playback][rate]")
{
    CHECK (formatRate (1.0) == "1.000");
    CHECK (formatRate (0.25) == "0.250");
    CHECK (parseRate ("1.5").value() == Approx (1.5));
    CHECK (parseRate ("  0.75 ").value() == Approx (0.75));
    CHECK (parseRate ("99").value() == kMaxPlaybackRate);
    CHECK (! parseRate ("").has_value());
    CHECK (! parseRate ("fast").has_value());
    CHECK (! parseRate ("1.2x").has_value());
}
