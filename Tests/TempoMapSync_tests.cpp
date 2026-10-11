#include "UI/TempoMapSync.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace lotro;
using Catch::Approx;

namespace
{
    RawMidiEvent tempoEvent (int tick, std::uint32_t us)
    {
        return { tick, { 0xFF, 0x51, (std::uint8_t) (us >> 16), (std::uint8_t) (us >> 8), (std::uint8_t) us } };
    }
    RawMidiEvent meterEvent (int tick, std::uint8_t nn, std::uint8_t dd)
    {
        return { tick, { 0xFF, 0x58, nn, dd, 24, 8 } };
    }
}

TEST_CASE ("deriveMaps: no events gives the 120 BPM, 4/4 defaults", "[tempo-sync]")
{
    const auto maps = deriveMaps ({});
    REQUIRE (maps.tempo.size() == 1);
    CHECK (maps.tempo[0].tick == 0);
    CHECK (maps.tempo[0].bpm == Approx (120.0));
    REQUIRE (maps.meter.size() == 1);
    CHECK (maps.meter[0].numerator == 4);
    CHECK (maps.meter[0].denominator == 4);
}

TEST_CASE ("deriveMaps: tempo and meter events become entries in event order", "[tempo-sync]")
{
    const auto maps = deriveMaps ({ tempoEvent (0, 500000), meterEvent (0, 3, 2),
                                    tempoEvent (960, 600000), meterEvent (1440, 6, 3) });
    REQUIRE (maps.tempo.size() == 2);
    CHECK (maps.tempo[0].bpm == Approx (120.0));
    CHECK (maps.tempo[1].tick == 960);
    CHECK (maps.tempo[1].bpm == Approx (100.0));
    REQUIRE (maps.meter.size() == 2);
    CHECK (maps.meter[0].numerator == 3);
    CHECK (maps.meter[0].denominator == 4);
    CHECK (maps.meter[1].tick == 1440);
    CHECK (maps.meter[1].numerator == 6);
    CHECK (maps.meter[1].denominator == 8);
}

TEST_CASE ("deriveMaps: other events, short events, zero tempo and absurd denominators are ignored", "[tempo-sync]")
{
    const auto maps = deriveMaps ({ { 0, { 0xFF, 0x59, 0x00, 0x00 } },        // key signature
                                    { 0, { 0xB0, 7, 100 } },                  // controller
                                    { 0, { 0xFF, 0x51, 0x07, 0xA1 } },        // tempo cut short
                                    tempoEvent (0, 0),                        // zero microseconds
                                    { 0, { 0xFF, 0x58, 4, 2 } },              // meter cut short
                                    meterEvent (0, 4, 16) });                 // 2^16 denominator
    CHECK (maps.tempo.size() == 1);     // only the default
    CHECK (maps.tempo[0].bpm == Approx (120.0));
    CHECK (maps.meter.size() == 1);
    CHECK (maps.meter[0].denominator == 4);
}

TEST_CASE ("bpmFromMicroseconds matches 60 / seconds-per-quarter exactly", "[tempo-sync]")
{
    CHECK (bpmFromMicroseconds (500000) == 60.0 / (500000 / 1000000.0));
    CHECK (bpmFromMicroseconds (597015) == 60.0 / (597015 / 1000000.0));
}
