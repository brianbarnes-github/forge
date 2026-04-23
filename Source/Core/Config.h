#pragma once

#include "LotroInstrument.h"

#include <optional>
#include <string>
#include <vector>

namespace lotro
{

struct ConfigInstrument
{
    int                                   x = 0;
    std::string                           name;               // LOTRO enum identifier, e.g. "LuteOfAges"
    std::optional<std::string>            label;              // T: header suffix; fallback in assembler
    std::vector<int>                      sources;            // MIDI track indices (0-based)
    int                                   transposeSemitones = 0;
    int                                   volumePercent      = 100;
    std::optional<std::string>            drumMap;            // path to drum-map JSON; only valid when name == "Drums"
};

struct Config
{
    std::string                           input;
    std::optional<std::string>            output;
    std::optional<std::string>            title;
    std::optional<std::string>            transcriber;
    std::optional<double>                 tempo;
    int                                   transpose = 0;
    std::vector<ConfigInstrument>         instruments;
};

} // namespace lotro
