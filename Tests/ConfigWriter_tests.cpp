#include "Core/ConfigWriter.h"
#include "Core/ConfigLoader.h"

#include <catch2/catch_test_macros.hpp>

namespace
{
    lotro::Config richConfig()
    {
        lotro::Config c;
        c.input       = "in.mid";
        c.output      = std::string ("out.abc");
        c.title       = std::string ("A Song");
        c.transcriber = std::string ("Brian");
        c.tempo       = 140.0;
        c.transpose   = -2;
        {
            lotro::ConfigInstrument i;
            i.x                  = 1;
            i.name               = "LuteOfAges";
            i.label              = std::string ("Lead");
            i.sources            = { 0, 2 };
            i.transposeSemitones = -12;
            i.volumePercent      = 110;
            c.instruments.push_back (i);
        }
        {
            lotro::ConfigInstrument i;
            i.x       = 3;
            i.name    = "Drums";
            i.sources = { 9 };
            i.drumMap = std::string ("kit.json");
            c.instruments.push_back (i);
        }
        return c;
    }

    void checkConfigsEqual (const lotro::Config& a, const lotro::Config& b)
    {
        CHECK (a.input       == b.input);
        CHECK (a.output      == b.output);
        CHECK (a.title       == b.title);
        CHECK (a.transcriber == b.transcriber);
        CHECK (a.tempo       == b.tempo);
        CHECK (a.transpose   == b.transpose);
        REQUIRE (a.instruments.size() == b.instruments.size());
        for (size_t i = 0; i < a.instruments.size(); ++i)
        {
            const auto& ai = a.instruments[i];
            const auto& bi = b.instruments[i];
            CHECK (ai.x                  == bi.x);
            CHECK (ai.name               == bi.name);
            CHECK (ai.label              == bi.label);
            CHECK (ai.sources            == bi.sources);
            CHECK (ai.transposeSemitones == bi.transposeSemitones);
            CHECK (ai.volumePercent      == bi.volumePercent);
            CHECK (ai.drumMap            == bi.drumMap);
        }
    }
}

TEST_CASE ("config-writer: JSON round-trip preserves all fields", "[config-writer][json]")
{
    const auto original = richConfig();
    std::string text;
    REQUIRE (lotro::writeConfigToString (lotro::ConfigFormat::Json, original, text).empty());

    lotro::Config parsed;
    REQUIRE (lotro::loadConfigFromString (text, lotro::ConfigFormat::Json, parsed).empty());

    checkConfigsEqual (parsed, original);
}
