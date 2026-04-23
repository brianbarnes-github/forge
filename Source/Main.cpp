#include "Cli/CliOptions.h"
#include "Core/DrumMapLoader.h"
#include "Core/AutoInstrument.h"
#include "Core/Config.h"
#include "Core/ConfigLoader.h"
#include "Core/InstrumentAssembly.h"
#include "Core/Diagnostics.h"
#include "Core/MidiImporter.h"
#include "Core/AbcWriter.h"
#include "Core/Constraints/RangeConstraint.h"
#include "Core/Constraints/DurationConstraint.h"
#include "Core/Constraints/TempoCollapse.h"
#include "Core/Constraints/ChordConstraint.h"
#include "Core/Constraints/CollisionGuard.h"
#include "Core/Constraints/DynamicMapper.h"

#include <juce_core/juce_core.h>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace
{
    void printHelp()
    {
        std::cout << lotro::usageText().toStdString();
    }

    void printInstruments()
    {
        for (const auto& name : lotro::allInstrumentNames())
            std::cout << std::string (name) << "\n";
    }

    void printTracks (const lotro::Song& song)
    {
        std::cout << "Idx | Channel | Notes | Name\n";
        std::cout << "----+---------+-------+-----\n";
        for (size_t i = 0; i < song.tracks.size(); ++i)
        {
            const auto& t = song.tracks[i];
            std::printf ("%3zu | %7d | %5zu | %s\n",
                         i, t.sourceMidiChannel,
                         t.notes.size(),
                         t.name.c_str());
        }
    }

    lotro::Config synthesiseConfig (const lotro::CliOptions& opts,
                                    const lotro::Song&       raw)
    {
        lotro::Config cfg;
        cfg.input = opts.inputFile.getFullPathName().toStdString();
        if (opts.outputFile != juce::File())
            cfg.output = opts.outputFile.getFullPathName().toStdString();
        if (opts.tempoOverride.has_value())
            cfg.tempo = *opts.tempoOverride;
        cfg.transpose = opts.transposeSemitones;

        for (size_t i = 0; i < raw.tracks.size(); ++i)
        {
            lotro::ConfigInstrument inst;
            inst.x       = (int) (i + 1);
            inst.sources = { (int) i };

            const auto picked = lotro::pickInstrumentForTrack (raw.tracks[i]);
            inst.name = std::string (lotro::displayName (picked));

            auto overrideIt = opts.instrumentOverrides.find ((int) i);
            if (overrideIt != opts.instrumentOverrides.end())
                inst.name = std::string (lotro::displayName (overrideIt->second));

            cfg.instruments.push_back (inst);
        }
        return cfg;
    }

    void runPipeline (lotro::Song& song, lotro::Diagnostics& diagnostics)
    {
        for (size_t trackIdx = 0; trackIdx < song.tracks.size(); ++trackIdx)
        {
            auto& track = song.tracks[trackIdx];
            if (! track.enabled) continue;

            const size_t before = diagnostics.size();
            lotro::applyRangeConstraint    (track, diagnostics);
            lotro::applyChordConstraint    (track, diagnostics);
            lotro::applyDurationConstraint (track, song, diagnostics);
            lotro::applyTempoCollapse      (track, song, diagnostics);
            lotro::applyCollisionGuard     (track, diagnostics);
            lotro::applyDynamicMapper      (track, diagnostics);

            for (size_t i = before; i < diagnostics.size(); ++i)
                if (diagnostics[i].trackIndex < 0)
                    diagnostics[i].trackIndex = (int) trackIdx;
        }

        lotro::applyTempoCollapseToSongMaps (song);
    }
}

int main (int argc, char* argv[])
{
    juce::StringArray rawArgs;
    for (int i = 1; i < argc; ++i)
        rawArgs.add (juce::String::fromUTF8 (argv[i]));

    const auto parsed = lotro::parseCli (rawArgs);

    if (parsed.error.isNotEmpty())
    {
        std::cerr << parsed.error.toStdString() << "\n";
        return 2;
    }

    const auto& opts = *parsed.options;

    if (opts.help)           { printHelp();        return 0; }
    if (opts.listInstruments){ printInstruments(); return 0; }

    try
    {
        lotro::Diagnostics diagnostics;

        std::ifstream midiStream (opts.inputFile.getFullPathName().toStdString(),
                                  std::ios::binary);
        if (! midiStream)
        {
            std::cerr << "Error: could not open MIDI file: "
                      << opts.inputFile.getFullPathName().toStdString() << "\n";
            return 1;
        }

        const auto sourceName = opts.inputFile.getFileNameWithoutExtension().toStdString();
        auto song = lotro::importMidi (midiStream, sourceName, diagnostics);

        if (opts.drumMapFile != juce::File())
        {
            const auto err = lotro::loadDrumMapFromFile (
                opts.drumMapFile.getFullPathName().toStdString(),
                song.drumMap);
            if (! err.empty())
            {
                std::cerr << "Error: " << err << "\n";
                return 1;
            }
        }

        if (opts.listTracks)
        {
            printTracks (song);
            return 0;
        }

        lotro::Config cfg;
        if (opts.configFile != juce::File())
        {
            // CLI gave us a config file. Load it (auto-detect format unless
            // --config-format overrides), then layer CLI flag overrides on top.
            const auto format = opts.configFormat.isEmpty()
                ? lotro::ConfigFormat::Auto
                : (opts.configFormat == "json" ? lotro::ConfigFormat::Json
                 : opts.configFormat == "toml" ? lotro::ConfigFormat::Toml
                 :                               lotro::ConfigFormat::Xml);

            const auto loadErr = lotro::loadConfigFromFile (
                opts.configFile.getFullPathName().toStdString(),
                format,
                cfg);
            if (! loadErr.empty())
            {
                std::cerr << "config error: " << loadErr << "\n";
                return 2;
            }

            if (opts.tempoOverride.has_value())
                cfg.tempo = *opts.tempoOverride;
            if (opts.transposeSemitones != 0)
                cfg.transpose += opts.transposeSemitones;

            const auto validErr = lotro::validateConfig (cfg, (int) song.tracks.size());
            if (! validErr.empty())
            {
                std::cerr << "config error: " << validErr << "\n";
                return 2;
            }
        }
        else
        {
            cfg = synthesiseConfig (opts, song);
        }

        auto assembled = lotro::assembleInstruments (song, cfg, diagnostics);

        runPipeline (assembled, diagnostics);

        const auto abc = lotro::writeAbc (assembled);

        if (! opts.outputFile.replaceWithText (juce::String (abc)))
        {
            std::cerr << "Failed to write output file: "
                      << opts.outputFile.getFullPathName().toStdString() << "\n";
            return 1;
        }

        if (opts.verbose)
            for (const auto& d : diagnostics)
                std::cerr << lotro::formatDiagnostic (d) << "\n";

        std::cout << "Wrote " << opts.outputFile.getFullPathName().toStdString() << "\n";
        return 0;
    }
    catch (const lotro::MidiImportError& e)
    {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }
}
