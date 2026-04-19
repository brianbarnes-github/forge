#pragma once

#include "../Diagnostics.h"
#include "../Track.h"

namespace lotro
{

enum class DynamicMarking { ppp, pp, p, mp, mf, f, ff, fff };

struct DynamicChange
{
    int            startTick = 0;
    DynamicMarking marking   = DynamicMarking::mf;
};

const char* abcMarkingFor (DynamicMarking m) noexcept;

DynamicMarking bucketForVelocity (int velocity) noexcept;

void applyDynamicMapper (Track& track, Diagnostics& diagnostics);

} // namespace lotro
