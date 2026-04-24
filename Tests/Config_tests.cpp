#include "Core/Config.h"

#include <catch2/catch_test_macros.hpp>

namespace
{
    lotro::Config minimalValid (int x = 1, const std::string& name = "LuteOfAges")
    {
        lotro::Config c;
        c.input = "song.mid";
        lotro::ConfigInstrument inst;
        inst.x       = x;
        inst.name    = name;
        inst.sources = { 0 };
        c.instruments.push_back (inst);
        return c;
    }
}

TEST_CASE ("config: minimal valid config passes", "[config][validate]")
{
    auto config = minimalValid();
    CHECK (lotro::validateConfig (config, /*midiTrackCount=*/1).empty());
}

TEST_CASE ("config: empty instruments array fails", "[config][validate]")
{
    lotro::Config c;
    c.input = "song.mid";
    const auto err = lotro::validateConfig (c, 1);
    CHECK_FALSE (err.empty());
    CHECK (err.find ("instruments") != std::string::npos);
}

TEST_CASE ("config: empty input path fails", "[config][validate]")
{
    auto config = minimalValid();
    config.input.clear();
    const auto err = lotro::validateConfig (config, 1);
    CHECK_FALSE (err.empty());
    CHECK (err.find ("input") != std::string::npos);
}

TEST_CASE ("config: duplicate x values fail", "[config][validate]")
{
    auto config = minimalValid();
    lotro::ConfigInstrument dup;
    dup.x       = 1;
    dup.name    = "Harp";
    dup.sources = { 1 };
    config.instruments.push_back (dup);
    const auto err = lotro::validateConfig (config, 2);
    CHECK_FALSE (err.empty());
    CHECK (err.find ("duplicate") != std::string::npos);
}

TEST_CASE ("config: x < 1 fails", "[config][validate]")
{
    auto config = minimalValid();
    config.instruments[0].x = 0;
    const auto err = lotro::validateConfig (config, 1);
    CHECK_FALSE (err.empty());
    CHECK (err.find ("x") != std::string::npos);
}

TEST_CASE ("config: unknown instrument name fails", "[config][validate]")
{
    auto config = minimalValid();
    config.instruments[0].name = "NotARealInstrument";
    const auto err = lotro::validateConfig (config, 1);
    CHECK_FALSE (err.empty());
    CHECK (err.find ("NotARealInstrument") != std::string::npos);
}

TEST_CASE ("config: empty sources fails", "[config][validate]")
{
    auto config = minimalValid();
    config.instruments[0].sources.clear();
    const auto err = lotro::validateConfig (config, 1);
    CHECK_FALSE (err.empty());
    CHECK (err.find ("sources") != std::string::npos);
}

TEST_CASE ("config: source index negative fails", "[config][validate]")
{
    auto config = minimalValid();
    config.instruments[0].sources = { -1 };
    const auto err = lotro::validateConfig (config, 1);
    CHECK_FALSE (err.empty());
    CHECK (err.find ("source") != std::string::npos);
}

TEST_CASE ("config: source index out of range fails", "[config][validate]")
{
    auto config = minimalValid();
    config.instruments[0].sources = { 5 };
    const auto err = lotro::validateConfig (config, 1);
    CHECK_FALSE (err.empty());
    CHECK (err.find ("source") != std::string::npos);
}

TEST_CASE ("config: volumePercent 0 (no change) passes", "[config][validate]")
{
    auto config = minimalValid();
    config.instruments[0].volumePercent = 0;
    CHECK (lotro::validateConfig (config, 1).empty());
}

TEST_CASE ("config: volumePercent <= -100 fails (would silence the note)", "[config][validate]")
{
    auto config = minimalValid();
    config.instruments[0].volumePercent = -100;
    const auto err = lotro::validateConfig (config, 1);
    CHECK_FALSE (err.empty());
    CHECK (err.find ("volumePercent") != std::string::npos);
}

TEST_CASE ("config: drumMap on non-Drums instrument fails", "[config][validate]")
{
    auto config = minimalValid();
    config.instruments[0].drumMap = "kit.json";
    const auto err = lotro::validateConfig (config, 1);
    CHECK_FALSE (err.empty());
    CHECK (err.find ("drumMap") != std::string::npos);
}

TEST_CASE ("config: drumMap on Drums instrument passes", "[config][validate]")
{
    auto config = minimalValid (1, "Drums");
    config.instruments[0].drumMap = "kit.json";
    CHECK (lotro::validateConfig (config, 1).empty());
}

TEST_CASE ("config: gaps in x are allowed", "[config][validate]")
{
    lotro::Config c;
    c.input = "song.mid";
    for (int x : {1, 3, 7})
    {
        lotro::ConfigInstrument inst;
        inst.x       = x;
        inst.name    = "LuteOfAges";
        inst.sources = { 0 };
        c.instruments.push_back (inst);
    }
    CHECK (lotro::validateConfig (c, 1).empty());
}
