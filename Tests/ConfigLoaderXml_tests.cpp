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
    const auto err = lotro::loadConfigFromString (text, lotro::ConfigFormat::Xml, config);
    REQUIRE (err.empty());
    CHECK (config.input == "song.mid");
    REQUIRE (config.instruments.size() == 1);
    CHECK (config.instruments[0].x    == 1);
    CHECK (config.instruments[0].name == "LuteOfAges");
    CHECK (config.instruments[0].sources == std::vector<int>{ 0 });
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
      <sources><source>0</source><source>2</source></sources>
      <transposeSemitones>-12</transposeSemitones>
      <volumePercent>110</volumePercent>
    </instrument>
    <instrument x="3" name="Drums">
      <sources><source>9</source></sources>
      <drumMap>kit.json</drumMap>
    </instrument>
  </instruments>
</config>)";

    lotro::Config config;
    const auto err = lotro::loadConfigFromString (text, lotro::ConfigFormat::Xml, config);
    REQUIRE (err.empty());

    CHECK (config.input       == "in.mid");
    CHECK (config.output      == std::optional<std::string>{ "out.abc" });
    CHECK (config.title       == std::optional<std::string>{ "A Song" });
    CHECK (config.transpose   == -2);

    REQUIRE (config.instruments.size() == 2);

    const auto& lead = config.instruments[0];
    CHECK (lead.x                  == 1);
    CHECK (lead.name               == "LuteOfAges");
    CHECK (lead.label              == std::optional<std::string>{ "Lead" });
    CHECK (lead.sources            == std::vector<int>{ 0, 2 });
    CHECK (lead.transposeSemitones == -12);
    CHECK (lead.volumePercent      == 110);

    const auto& drums = config.instruments[1];
    CHECK (drums.name    == "Drums");
    CHECK (drums.drumMap == std::optional<std::string>{ "kit.json" });
}

TEST_CASE ("config-loader: XML malformed input returns error", "[config-loader][xml]")
{
    const std::string text = "<config><input>a.mid</inpu";
    lotro::Config config;
    const auto err = lotro::loadConfigFromString (text, lotro::ConfigFormat::Xml, config);
    CHECK_FALSE (err.empty());
}
