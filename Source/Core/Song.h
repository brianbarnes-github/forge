#pragma once

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
};

} // namespace lotro
