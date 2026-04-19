#pragma once

#include "Core/LotroInstrument.h"

#include <juce_core/juce_core.h>
#include <map>
#include <optional>

namespace lotro
{

struct CliOptions
{
    juce::File                         inputFile;
    juce::File                         outputFile;
    juce::File                         drumMapFile;        // --drum-map PATH (optional)
    std::map<int, LotroInstrument>     instrumentOverrides;
    std::optional<double>              tempoOverride;
    int                                transposeSemitones = 0;
    bool                               listTracks         = false;
    bool                               listInstruments    = false;
    bool                               help               = false;
    bool                               verbose            = false;
};

struct CliParseResult
{
    std::optional<CliOptions> options;
    juce::String              error;
};

CliParseResult parseCli (const juce::StringArray& rawArgs);

juce::String usageText();

} // namespace lotro
