#include "Cli/CliOptions.h"
#include "Core/DrumMapLoader.h"
#include "Core/AutoInstrument.h"
#include "Core/Config.h"
#include "Core/ConfigLoader.h"
#include "Core/InstrumentAssembly.h"
#include "Core/Diagnostics.h"
#include "Core/MidiImporter.h"
#include "Core/AbcWriter.h"
#include "Core/Pipeline.h"
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

        // Resolve the effective input and output paths. In config mode we may
        // need to load the config before we know the MIDI path, so we handle
        // config loading up front when --config is present.
        juce::File effectiveInput  = opts.inputFile;
        juce::File effectiveOutput = opts.outputFile;
        lotro::Config cfg;

        if (opts.configFile != juce::File())
        {
            // Reject --instrument in config mode (spec).
            if (! opts.instrumentOverrides.empty())
            {
                std::cerr << "config error: --instrument is not allowed with --config "
                          << "(use the config's instruments[].name to assign instruments)\n";
                return 2;
            }

            // Load the config (auto-detect format unless --config-format overrides).
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

            // Positional INPUT.mid overrides config's input; if absent, fall
            // back to cfg.input. Relative paths in the config are resolved
            // against the config file's directory (per spec).
            const auto configDir = opts.configFile.getParentDirectory();
            if (effectiveInput == juce::File() && ! cfg.input.empty())
                effectiveInput = configDir.getChildFile (cfg.input);

            // Same for output.
            if (effectiveOutput == juce::File() && cfg.output.has_value())
                effectiveOutput = configDir.getChildFile (*cfg.output);

            // Last resort: derive output from effective input stem.
            if (effectiveOutput == juce::File())
                effectiveOutput = effectiveInput.withFileExtension (".abc");
        }

        std::ifstream midiStream (effectiveInput.getFullPathName().toStdString(),
                                  std::ios::binary);
        if (! midiStream)
        {
            std::cerr << "Error: could not open MIDI file: "
                      << effectiveInput.getFullPathName().toStdString() << "\n";
            return 1;
        }

        const auto sourceName = effectiveInput.getFileNameWithoutExtension().toStdString();
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

        if (opts.configFile != juce::File())
        {
            const auto validErr = lotro::validateConfig (cfg, (int) song.tracks.size());
            if (! validErr.empty())
            {
                std::cerr << "config error: " << validErr << "\n";
                return 2;
            }
        }
        else
        {
            cfg = lotro::synthesiseConfig (
                song,
                opts.inputFile.getFullPathName().toStdString(),
                opts.outputFile != juce::File()
                    ? opts.outputFile.getFullPathName().toStdString()
                    : std::string{},
                opts.tempoOverride,
                opts.transposeSemitones,
                opts.instrumentOverrides);
        }

        auto assembled = lotro::assembleInstruments (song, cfg, diagnostics);

        lotro::runPipeline (assembled, diagnostics);

        const auto abc = lotro::writeAbc (assembled);

        if (! effectiveOutput.replaceWithText (juce::String (abc)))
        {
            std::cerr << "Failed to write output file: "
                      << effectiveOutput.getFullPathName().toStdString() << "\n";
            return 1;
        }

        if (opts.verbose)
            for (const auto& d : diagnostics)
                std::cerr << lotro::formatDiagnostic (d) << "\n";

        std::cout << "Wrote " << effectiveOutput.getFullPathName().toStdString() << "\n";
        return 0;
    }
    catch (const lotro::MidiImportError& e)
    {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }
}
