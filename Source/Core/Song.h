#pragma once

#include "DrumMap.h"
#include "Track.h"

#include <string>
#include <vector>

namespace lotro
{

struct TempoChange
{
    int    tick = 0;
    double bpm  = 120.0;
};

struct MeterChange
{
    int tick        = 0;
    int numerator   = 4;
    int denominator = 4;
};

struct Song
{
    int                      ticksPerQuarter = 480;
    std::vector<Track>       tracks;
    std::vector<TempoChange> tempoMap;
    std::vector<MeterChange> meterMap;
    std::string              title;
    std::string              transcriber = "LotroAbcConverter v0.1";

    // Maps GM drum pitches to LOTRO ABC drum-slot letters. Defaults to the
    // spec §2.6 set; users can load a drum_map.json to override per-song.
    DrumMap                  drumMap     = defaultDrumMap();
};

} // namespace lotro
