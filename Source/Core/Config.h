#pragma once

#include "LotroInstrument.h"

#include <optional>
#include <string>
#include <vector>

namespace lotro
{

// A single MIDI track feeding an instrument, with its own per-source
// transpose and volume adjustment. Multiple sources can feed one instrument
// (the assembler merges their notes); the same midiTrackIndex can legally
// appear in multiple instruments' sources.
struct ConfigSource
{
    int midiTrackIndex      = -1;   // required; 0-based index into raw Song.tracks
    int transposeSemitones  = 0;    // additive with Config.transpose
    int volumePercent       = 0;    // 0 = no change, +N louder, -N quieter; > -100
};

struct ConfigInstrument
{
    int                                   x = 0;
    std::string                           name;                    // LOTRO enum identifier, e.g. "LuteOfAges"
    std::optional<std::string>            label;                   // T: header suffix; fallback in assembler
    std::vector<ConfigSource>             sources;                 // list of source tracks feeding this instrument
    std::optional<std::string>            drumMap;                 // path to drum-map JSON; only valid when name == "Drums"
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

// Validates a Config against a MIDI file with `midiTrackCount` tracks.
// Returns an empty string on success; otherwise a human-readable error
// message suitable for stderr. All rules are checked eagerly — the
// message reports the first failure.
std::string validateConfig (const Config& config, int midiTrackCount);

} // namespace lotro
