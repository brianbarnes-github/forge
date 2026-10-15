#include "UI/MeterSegments.h"
#include "UI/Playback/TransportText.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace lotro;
using Catch::Approx;

namespace
{
    constexpr int ppq = 480;
    std::vector<MeterSegment> fourFour() { return meterSegments ({ { 0, 4, 4 } }, ppq); }
    // 4/4 for 2 bars (ticks 0..3840), then 6/8 (bars 3+; 240-tick beats, 1440-tick bars).
    std::vector<MeterSegment> mixed() { return meterSegments ({ { 0, 4, 4 }, { 3840, 6, 8 } }, ppq); }
}

TEST_CASE ("TransportText: positions format as bar.beat.tick (1-based bar and beat)", "[playback][text]")
{
    const auto s = fourFour();
    CHECK (formatPosition (s, 0) == "1.1.0");
    CHECK (formatPosition (s, 480) == "1.2.0");
    CHECK (formatPosition (s, 1920) == "2.1.0");
    CHECK (formatPosition (s, 1920 + 480 * 2 + 17) == "2.3.17");
}

TEST_CASE ("TransportText: positions parse, with short forms", "[playback][text]")
{
    const auto s = fourFour();
    CHECK (parsePosition ("1.1.0", s).tick == Approx (0.0));
    CHECK (parsePosition ("2.1.0", s).tick == Approx (1920.0));
    CHECK (parsePosition ("2", s).tick == Approx (1920.0));
    CHECK (parsePosition (" 2.3 ", s).tick == Approx (1920.0 + 960.0));
    CHECK (parsePosition ("2.3.17", s).tick == Approx (1920.0 + 960.0 + 17.0));
    CHECK (! parsePosition ("2.3.17", s).error.has_value());
}

TEST_CASE ("TransportText: bad positions report a typed error", "[playback][text]")
{
    const auto s = fourFour();
    CHECK (parsePosition ("", s).error == PositionParseError::Empty);
    CHECK (parsePosition ("a.b", s).error == PositionParseError::BadFormat);
    CHECK (parsePosition ("1.2.3.4", s).error == PositionParseError::BadFormat);
    CHECK (parsePosition ("1.-1.0", s).error == PositionParseError::BadFormat);
    CHECK (parsePosition ("0.1.0", s).error == PositionParseError::BarOutOfRange);
    CHECK (parsePosition ("1.5.0", s).error == PositionParseError::BeatOutOfRange);
    CHECK (parsePosition ("1.1.480", s).error == PositionParseError::TickOutOfRange);
}

TEST_CASE ("TransportText: positions round-trip across a meter change", "[playback][text]")
{
    const auto s = mixed();
    CHECK (formatPosition (s, 3840) == "3.1.0");
    CHECK (formatPosition (s, 3840 + 240 * 2) == "3.3.0");        // 6/8: 240-tick beats
    CHECK (parsePosition ("3.3.0", s).tick == Approx (3840.0 + 480.0));
    CHECK (parsePosition ("4.1.0", s).tick == Approx (3840.0 + 1440.0));
    CHECK (parsePosition ("3.7.0", s).error == PositionParseError::BeatOutOfRange);   // 6/8 has 6 beats
    for (double tick : { 0.0, 480.0, 3839.0, 3840.0, 4321.0, 7000.0 })
        CHECK (parsePosition (formatPosition (s, tick), s).tick == Approx (tick).margin (0.5));
}

TEST_CASE ("TransportText: beats and ticks convert through meter changes", "[playback][text]")
{
    const auto s = mixed();
    CHECK (beatsToTicks (s, 0.0, 4.0) == Approx (1920.0));
    CHECK (ticksToBeats (s, 0.0, 1920.0) == Approx (4.0));
    // Two beats of 4/4 (960 ticks) up to tick 3840 + 3 beats of 6/8 (3 * 240).
    CHECK (beatsToTicks (s, 3840.0 - 960.0, 5.0) == Approx (3840.0 + 720.0));
    CHECK (ticksToBeats (s, 3840.0 - 960.0, 3840.0 + 720.0) == Approx (5.0));
    CHECK (beatsToTicks (s, 100.0, 0.0) == Approx (100.0));
}

TEST_CASE ("TransportText: beat lengths format and parse", "[playback][text]")
{
    CHECK (formatBeats (88.5) == "88.5");
    CHECK (formatBeats (4.0) == "4");
    CHECK (formatBeats (1.0 / 3.0) == "0.33");
    CHECK (parseBeats ("88.5").value() == Approx (88.5));
    CHECK (! parseBeats ("0").has_value());
    CHECK (! parseBeats ("-2").has_value());
    CHECK (! parseBeats ("abc").has_value());
}

TEST_CASE ("TransportText: meter and tempo text", "[playback][text]")
{
    CHECK (formatMeterText (6, 8) == "6/8");
    CHECK (parseMeterText ("6/8").value() == std::make_pair (6, 8));
    CHECK (parseMeterText (" 3 / 4 ").value() == std::make_pair (3, 4));
    CHECK (! parseMeterText ("6").has_value());
    CHECK (! parseMeterText ("a/b").has_value());
    CHECK (! parseMeterText ("0/4").has_value());

    CHECK (parseTempoText ("120").value() == Approx (120.0));
    CHECK (parseTempoText ("=93.75").value() == Approx (93.75));
    CHECK (parseTempoText ("\xe2\x99\xa9= 93.75").value() == Approx (93.75));
    CHECK (! parseTempoText ("").has_value());
    CHECK (! parseTempoText ("fast").has_value());
    CHECK (! parseTempoText ("0").has_value());
}
