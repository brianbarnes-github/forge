#include "Core/Pipeline.h"
#include "Core/Song.h"
#include "Core/LotroInstrument.h"

#include <catch2/catch_test_macros.hpp>

namespace
{
    lotro::Note note (int pitch, int startTick, int dur, int vel = 100)
    {
        lotro::Note n;
        n.pitch = pitch; n.startTick = startTick;
        n.durationTicks = dur; n.velocity = vel;
        return n;
    }

    lotro::Song threeTrackRaw()
    {
        lotro::Song s;
        s.ticksPerQuarter = 480;
        s.tempoMap.push_back ({ 0, 120.0 });
        s.meterMap.push_back ({ 0, 4, 4 });

        for (int i = 0; i < 3; ++i)
        {
            lotro::Track t;
            t.name = "MidiTrack" + std::to_string (i);
            t.notes.push_back (note (60 + i, 0, 480));
            s.tracks.push_back (t);
        }
        return s;
    }
}

TEST_CASE ("pipeline: synthesiseConfig produces one instrument per raw track", "[pipeline][synth]")
{
    const auto raw = threeTrackRaw();
    const auto cfg = lotro::synthesiseConfig (raw, "in.mid", "out.abc",
                                              std::nullopt, 0, {});
    CHECK (cfg.input  == "in.mid");
    CHECK (cfg.output == std::optional<std::string>{ "out.abc" });
    CHECK (cfg.transpose == 0);
    REQUIRE (cfg.instruments.size() == 3);
    for (int i = 0; i < 3; ++i)
    {
        CHECK (cfg.instruments[(size_t) i].x       == i + 1);
        CHECK (cfg.instruments[(size_t) i].sources == std::vector<int>{ i });
        lotro::LotroInstrument parsed;
        CHECK (lotro::parseName (cfg.instruments[(size_t) i].name, parsed).empty());
    }
}

TEST_CASE ("pipeline: synthesiseConfig honours instrumentOverrides", "[pipeline][synth]")
{
    const auto raw = threeTrackRaw();
    std::map<int, lotro::LotroInstrument> overrides;
    overrides[1] = lotro::LotroInstrument::Theorbo;
    const auto cfg = lotro::synthesiseConfig (raw, "in.mid", "",
                                              std::nullopt, 0, overrides);
    REQUIRE (cfg.instruments.size() == 3);
    CHECK (cfg.instruments[1].name == "Theorbo");
}

TEST_CASE ("pipeline: synthesiseConfig propagates tempo and transpose", "[pipeline][synth]")
{
    const auto raw = threeTrackRaw();
    const auto cfg = lotro::synthesiseConfig (raw, "in.mid", "",
                                              std::optional<double>{ 140.0 }, -5, {});
    CHECK (cfg.tempo     == std::optional<double>{ 140.0 });
    CHECK (cfg.transpose == -5);
}
