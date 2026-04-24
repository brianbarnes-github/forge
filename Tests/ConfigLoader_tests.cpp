#include "Core/ConfigLoader.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdio>
#include <fstream>
#include <string>

TEST_CASE ("config-loader: JSON minimal config parses (bare-integer sources)", "[config-loader][json]")
{
    const std::string text = R"({
        "input": "song.mid",
        "instruments": [
            { "x": 1, "name": "LuteOfAges", "sources": [0] }
        ]
    })";

    lotro::Config config;
    lotro::Diagnostics mig;
    const auto err = lotro::loadConfigFromString (text, lotro::ConfigFormat::Json, config, mig);
    REQUIRE (err.empty());
    CHECK (config.input == "song.mid");
    REQUIRE (config.instruments.size() == 1);
    CHECK (config.instruments[0].x       == 1);
    CHECK (config.instruments[0].name    == "LuteOfAges");
    REQUIRE (config.instruments[0].sources.size() == 1);
    CHECK (config.instruments[0].sources[0].midiTrackIndex == 0);
}

TEST_CASE ("config-loader: JSON full config round-trips all fields", "[config-loader][json]")
{
    const std::string text = R"({
        "input": "in.mid",
        "output": "out.abc",
        "title": "A Song",
        "transcriber": "Brian",
        "tempo": 140,
        "transpose": -2,
        "instruments": [
            {
                "x": 1,
                "name": "LuteOfAges",
                "label": "Lead",
                "sources": [0, { "midiTrack": 2, "transposeSemitones": -12, "volumePercent": 10 }]
            },
            {
                "x": 3,
                "name": "Drums",
                "sources": [9],
                "drumMap": "kit.json"
            }
        ]
    })";

    lotro::Config config;
    lotro::Diagnostics mig;
    const auto err = lotro::loadConfigFromString (text, lotro::ConfigFormat::Json, config, mig);
    REQUIRE (err.empty());

    CHECK (config.input       == "in.mid");
    CHECK (config.output      == std::optional<std::string>{ "out.abc" });
    CHECK (config.title       == std::optional<std::string>{ "A Song" });
    CHECK (config.transcriber == std::optional<std::string>{ "Brian" });
    CHECK (config.tempo       == std::optional<double>{ 140.0 });
    CHECK (config.transpose   == -2);

    REQUIRE (config.instruments.size() == 2);

    const auto& lead = config.instruments[0];
    CHECK (lead.x     == 1);
    CHECK (lead.name  == "LuteOfAges");
    CHECK (lead.label == std::optional<std::string>{ "Lead" });
    CHECK (lead.sources.size() == 2);
    CHECK (lead.sources[0].midiTrackIndex    == 0);
    CHECK (lead.sources[0].transposeSemitones == 0);
    CHECK (lead.sources[0].volumePercent     == 0);
    CHECK (lead.sources[1].midiTrackIndex     == 2);
    CHECK (lead.sources[1].transposeSemitones == -12);
    CHECK (lead.sources[1].volumePercent      == 10);
    CHECK_FALSE (lead.drumMap.has_value());

    const auto& drums = config.instruments[1];
    CHECK (drums.x       == 3);
    CHECK (drums.name    == "Drums");
    REQUIRE (drums.sources.size() == 1);
    CHECK (drums.sources[0].midiTrackIndex == 9);
    CHECK (drums.drumMap == std::optional<std::string>{ "kit.json" });
}

TEST_CASE ("config-loader: JSON malformed input returns error", "[config-loader][json]")
{
    const std::string text = "{ \"input\": \"a.mid\",  ";
    lotro::Config config;
    lotro::Diagnostics mig;
    const auto err = lotro::loadConfigFromString (text, lotro::ConfigFormat::Json, config, mig);
    CHECK_FALSE (err.empty());
}

TEST_CASE ("config-loader: JSON missing 'instruments' produces empty array", "[config-loader][json]")
{
    const std::string text = R"({ "input": "song.mid" })";
    lotro::Config config;
    lotro::Diagnostics mig;
    const auto err = lotro::loadConfigFromString (text, lotro::ConfigFormat::Json, config, mig);
    CHECK (err.empty());
    CHECK (config.instruments.empty());
}

TEST_CASE ("config-loader: auto format detection picks JSON from .json extension", "[config-loader][json]")
{
    const auto tmpPath = std::string ("/tmp/lotro-config-loader-test.json");
    {
        std::ofstream out (tmpPath);
        out << R"({
            "input": "song.mid",
            "instruments": [ { "x": 1, "name": "Harp", "sources": [0] } ]
        })";
    }

    lotro::Config config;
    lotro::Diagnostics mig;
    const auto err = lotro::loadConfigFromFile (tmpPath, lotro::ConfigFormat::Auto, config, mig);
    REQUIRE (err.empty());
    CHECK (config.instruments.size() == 1);
    CHECK (config.instruments[0].name == "Harp");

    std::remove (tmpPath.c_str());
}

TEST_CASE ("config-loader: JSON old instrument-level fields drop with warning", "[config-loader][json][migration]")
{
    const std::string text = R"({
        "input": "song.mid",
        "instruments": [
            { "x": 1, "name": "LuteOfAges", "sources": [0],
              "transposeSemitones": -12, "volumePercent": 80 }
        ]
    })";

    lotro::Config config;
    lotro::Diagnostics mig;
    const auto err = lotro::loadConfigFromString (text, lotro::ConfigFormat::Json, config, mig);
    REQUIRE (err.empty());
    REQUIRE (config.instruments.size() == 1);
    CHECK (config.instruments[0].sources.size() == 1);
    CHECK (config.instruments[0].sources[0].midiTrackIndex == 0);
    // Two warnings: one for transposeSemitones, one for volumePercent
    int warnings = 0;
    for (const auto& d : mig)
        if (d.severity == lotro::Severity::Warning
            && d.source == "ConfigLoader")
            ++warnings;
    CHECK (warnings == 2);
}
