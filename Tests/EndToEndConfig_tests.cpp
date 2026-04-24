#include "Cli/CliOptions.h"
#include "Core/AbcWriter.h"
#include "Core/Config.h"
#include "Core/ConfigLoader.h"
#include "Core/InstrumentAssembly.h"
#include "Core/MidiImporter.h"
#include "Core/Constraints/ChordConstraint.h"
#include "Core/Constraints/CollisionGuard.h"
#include "Core/Constraints/DurationConstraint.h"
#include "Core/Constraints/DynamicMapper.h"
#include "Core/Constraints/RangeConstraint.h"
#include "Core/Constraints/TempoCollapse.h"

#include <catch2/catch_test_macros.hpp>

#include <juce_core/juce_core.h>

#include <fstream>
#include <string>

namespace
{
    juce::File projectRoot()
    {
        return juce::File (__FILE__).getParentDirectory().getParentDirectory();
    }

    bool contains (const std::string& hay, const std::string& needle)
    {
        return hay.find (needle) != std::string::npos;
    }

    int countOccurrences (const std::string& hay, const std::string& needle)
    {
        int n = 0;
        size_t p = 0;
        while ((p = hay.find (needle, p)) != std::string::npos) { ++n; p += needle.size(); }
        return n;
    }

    void runPipeline (lotro::Song& song, lotro::Diagnostics& diag)
    {
        for (auto& t : song.tracks)
        {
            if (! t.enabled) continue;
            lotro::applyRangeConstraint    (t, diag);
            lotro::applyChordConstraint    (t, diag);
            lotro::applyDurationConstraint (t, song, diag);
            lotro::applyTempoCollapse      (t, song, diag);
            lotro::applyCollisionGuard     (t, diag);
            lotro::applyDynamicMapper      (t, diag);
        }
        lotro::applyTempoCollapseToSongMaps (song);
    }
}

TEST_CASE ("end-to-end-config: a JSON config selects a subset of BBB tracks via assembleInstruments", "[e2e][config]")
{
    const auto midi = projectRoot().getChildFile ("midi").getChildFile ("Barnes Brothers Band - Pull The Wires.mid");
    REQUIRE (midi.existsAsFile());

    // Load + import the MIDI.
    std::ifstream input (midi.getFullPathName().toStdString(), std::ios::binary);
    REQUIRE (input);
    lotro::Diagnostics diag;
    auto raw = lotro::importMidi (input,
                                  midi.getFileNameWithoutExtension().toStdString(),
                                  diag);
    REQUIRE (raw.tracks.size() >= 2);

    // Build a config picking only the first two tracks, with explicit x.
    lotro::Config cfg;
    cfg.input = midi.getFullPathName().toStdString();
    {
        lotro::ConfigInstrument inst;
        inst.x       = 1;
        inst.name    = "LuteOfAges";
        inst.label   = std::string ("Lute v1");
        inst.sources = { { 0, 0, 0 } };
        cfg.instruments.push_back (inst);
    }
    {
        lotro::ConfigInstrument inst;
        inst.x       = 2;
        inst.name    = "Theorbo";
        inst.label   = std::string ("Bass");
        inst.sources = { { 1, 0, 0 } };
        cfg.instruments.push_back (inst);
    }

    REQUIRE (lotro::validateConfig (cfg, (int) raw.tracks.size()).empty());

    // Run the production-shaped pipeline: assemble + constraints + writer.
    auto song = lotro::assembleInstruments (raw, cfg, diag);
    runPipeline (song, diag);
    const auto abc = lotro::writeAbc (song);

    // Headers and structural sanity.
    CHECK (contains (abc, "L:1/8"));
    CHECK (contains (abc, "K:C"));
    CHECK (contains (abc, "Q:"));

    // Exactly two parts; X: indices match the config.
    CHECK (countOccurrences (abc, "\nX:") == 2);
    CHECK (contains (abc, "X:1"));
    CHECK (contains (abc, "X:2"));

    // Labels appear in the T: header.
    CHECK (contains (abc, "T:" + midi.getFileNameWithoutExtension().toStdString() + " - Lute v1"));
    CHECK (contains (abc, "T:" + midi.getFileNameWithoutExtension().toStdString() + " - Bass"));

    // Diagnostics flag the dropped MIDI tracks (everything but 0 and 1).
    int dropped = 0;
    for (const auto& d : diag)
        if (d.source == "InstrumentAssembly" && d.severity == lotro::Severity::Info)
            ++dropped;
    CHECK (dropped == (int) raw.tracks.size() - 2);
}
