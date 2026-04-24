#include "Core/ConfigLoader.h"

#include <catch2/catch_test_macros.hpp>

TEST_CASE ("config-loader: XML minimal config parses", "[config-loader][xml]")
{
    const std::string text = R"(<?xml version="1.0"?>
<config>
  <input>song.mid</input>
  <instruments>
    <instrument x="1" name="LuteOfAges">
      <sources><source>0</source></sources>
    </instrument>
  </instruments>
</config>)";

    lotro::Config config;
    lotro::Diagnostics mig;
    const auto err = lotro::loadConfigFromString (text, lotro::ConfigFormat::Xml, config, mig);
    REQUIRE (err.empty());
    CHECK (config.input == "song.mid");
    REQUIRE (config.instruments.size() == 1);
    CHECK (config.instruments[0].x    == 1);
    CHECK (config.instruments[0].name == "LuteOfAges");
    REQUIRE (config.instruments[0].sources.size() == 1);
    CHECK (config.instruments[0].sources[0].midiTrackIndex == 0);
}

TEST_CASE ("config-loader: XML full config round-trips all fields", "[config-loader][xml]")
{
    const std::string text = R"(<?xml version="1.0"?>
<config>
  <input>in.mid</input>
  <output>out.abc</output>
  <title>A Song</title>
  <transcriber>Brian</transcriber>
  <tempo>140</tempo>
  <transpose>-2</transpose>
  <instruments>
    <instrument x="1" name="LuteOfAges" label="Lead">
      <sources>
        <source>0</source>
        <source midiTrack="2" transposeSemitones="-12" volumePercent="10" />
      </sources>
    </instrument>
    <instrument x="3" name="Drums">
      <sources><source midiTrack="9" /></sources>
      <drumMap>kit.json</drumMap>
    </instrument>
  </instruments>
</config>)";

    lotro::Config config;
    lotro::Diagnostics mig;
    const auto err = lotro::loadConfigFromString (text, lotro::ConfigFormat::Xml, config, mig);
    REQUIRE (err.empty());

    CHECK (config.input       == "in.mid");
    CHECK (config.output      == std::optional<std::string>{ "out.abc" });
    CHECK (config.title       == std::optional<std::string>{ "A Song" });
    CHECK (config.transpose   == -2);

    REQUIRE (config.instruments.size() == 2);

    const auto& lead = config.instruments[0];
    CHECK (lead.x     == 1);
    CHECK (lead.name  == "LuteOfAges");
    CHECK (lead.label == std::optional<std::string>{ "Lead" });
    REQUIRE (lead.sources.size() == 2);
    CHECK (lead.sources[0].midiTrackIndex    == 0);
    CHECK (lead.sources[0].transposeSemitones == 0);
    CHECK (lead.sources[0].volumePercent     == 0);
    CHECK (lead.sources[1].midiTrackIndex     == 2);
    CHECK (lead.sources[1].transposeSemitones == -12);
    CHECK (lead.sources[1].volumePercent      == 10);

    const auto& drums = config.instruments[1];
    CHECK (drums.name    == "Drums");
    REQUIRE (drums.sources.size() == 1);
    CHECK (drums.sources[0].midiTrackIndex == 9);
    CHECK (drums.drumMap == std::optional<std::string>{ "kit.json" });
}

TEST_CASE ("config-loader: XML malformed input returns error", "[config-loader][xml]")
{
    const std::string text = "<config><input>a.mid</inpu";
    lotro::Config config;
    lotro::Diagnostics mig;
    const auto err = lotro::loadConfigFromString (text, lotro::ConfigFormat::Xml, config, mig);
    CHECK_FALSE (err.empty());
}

TEST_CASE ("config-loader: XML non-numeric tempo returns descriptive error", "[config-loader][xml]")
{
    const std::string text = R"(<?xml version="1.0"?>
<config>
  <input>song.mid</input>
  <tempo>not-a-number</tempo>
  <instruments>
    <instrument x="1" name="LuteOfAges">
      <sources><source>0</source></sources>
    </instrument>
  </instruments>
</config>)";

    lotro::Config config;
    lotro::Diagnostics mig;
    const auto err = lotro::loadConfigFromString (text, lotro::ConfigFormat::Xml, config, mig);
    CHECK_FALSE (err.empty());
    CHECK (err.find ("tempo") != std::string::npos);
}

TEST_CASE ("config-loader: XML old instrument-level fields drop with warning", "[config-loader][xml][migration]")
{
    const std::string text = R"(<?xml version="1.0"?>
<config>
  <input>song.mid</input>
  <instruments>
    <instrument x="1" name="LuteOfAges">
      <sources><source>0</source></sources>
      <transposeSemitones>-12</transposeSemitones>
      <volumePercent>80</volumePercent>
    </instrument>
  </instruments>
</config>)";

    lotro::Config config;
    lotro::Diagnostics mig;
    const auto err = lotro::loadConfigFromString (text, lotro::ConfigFormat::Xml, config, mig);
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

TEST_CASE ("config-loader: XML empty source element returns error", "[config-loader][xml]")
{
    const std::string text = R"(<?xml version="1.0"?>
<config>
  <input>song.mid</input>
  <instruments>
    <instrument x="1" name="LuteOfAges">
      <sources><source /></sources>
    </instrument>
  </instruments>
</config>)";

    lotro::Config config;
    lotro::Diagnostics mig;
    const auto err = lotro::loadConfigFromString (text, lotro::ConfigFormat::Xml, config, mig);
    CHECK_FALSE (err.empty());
    CHECK (err.find ("midiTrack") != std::string::npos);
}
