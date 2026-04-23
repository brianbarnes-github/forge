#include "Core/ConfigLoader.h"

#include <catch2/catch_test_macros.hpp>

TEST_CASE ("config-loader: TOML minimal config parses", "[config-loader][toml]")
{
    const std::string text = R"(
input = "song.mid"

[[instruments]]
x = 1
name = "LuteOfAges"
sources = [0]
)";

    lotro::Config config;
    const auto err = lotro::loadConfigFromString (text, lotro::ConfigFormat::Toml, config);
    REQUIRE (err.empty());
    CHECK (config.input == "song.mid");
    REQUIRE (config.instruments.size() == 1);
    CHECK (config.instruments[0].x       == 1);
    CHECK (config.instruments[0].name    == "LuteOfAges");
    CHECK (config.instruments[0].sources == std::vector<int>{ 0 });
}

TEST_CASE ("config-loader: TOML full config round-trips all fields", "[config-loader][toml]")
{
    const std::string text = R"(
input = "in.mid"
output = "out.abc"
title = "A Song"
transcriber = "Brian"
tempo = 140
transpose = -2

[[instruments]]
x = 1
name = "LuteOfAges"
label = "Lead"
sources = [0, 2]
transposeSemitones = -12
volumePercent = 110

[[instruments]]
x = 3
name = "Drums"
sources = [9]
drumMap = "kit.json"
)";

    lotro::Config config;
    const auto err = lotro::loadConfigFromString (text, lotro::ConfigFormat::Toml, config);
    REQUIRE (err.empty());

    CHECK (config.input       == "in.mid");
    CHECK (config.output      == std::optional<std::string>{ "out.abc" });
    CHECK (config.title       == std::optional<std::string>{ "A Song" });
    CHECK (config.tempo       == std::optional<double>{ 140.0 });
    CHECK (config.transpose   == -2);

    REQUIRE (config.instruments.size() == 2);

    const auto& lead = config.instruments[0];
    CHECK (lead.x                  == 1);
    CHECK (lead.label              == std::optional<std::string>{ "Lead" });
    CHECK (lead.sources            == std::vector<int>{ 0, 2 });
    CHECK (lead.transposeSemitones == -12);
    CHECK (lead.volumePercent      == 110);

    const auto& drums = config.instruments[1];
    CHECK (drums.name    == "Drums");
    CHECK (drums.drumMap == std::optional<std::string>{ "kit.json" });
}

TEST_CASE ("config-loader: TOML malformed input returns error", "[config-loader][toml]")
{
    const std::string text = "input = \"a.mid\"  \n[[";
    lotro::Config config;
    const auto err = lotro::loadConfigFromString (text, lotro::ConfigFormat::Toml, config);
    CHECK_FALSE (err.empty());
}
