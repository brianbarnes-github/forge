#include "DynamicMapper.h"

#include <algorithm>

namespace lotro
{

const char* abcMarkingFor (DynamicMarking m) noexcept
{
    switch (m)
    {
        case DynamicMarking::ppp: return "ppp";
        case DynamicMarking::pp:  return "pp";
        case DynamicMarking::p:   return "p";
        case DynamicMarking::mp:  return "mp";
        case DynamicMarking::mf:  return "mf";
        case DynamicMarking::f:   return "f";
        case DynamicMarking::ff:  return "ff";
        case DynamicMarking::fff: return "fff";
    }
    return "mf";
}

DynamicMarking bucketForVelocity (int velocity) noexcept
{
    if (velocity <=  15) return DynamicMarking::ppp;
    if (velocity <=  32) return DynamicMarking::pp;
    if (velocity <=  49) return DynamicMarking::p;
    if (velocity <=  65) return DynamicMarking::mp;
    if (velocity <=  81) return DynamicMarking::mf;
    if (velocity <=  97) return DynamicMarking::f;
    if (velocity <= 113) return DynamicMarking::ff;
    return DynamicMarking::fff;
}

void applyDynamicMapper (Track& track, Diagnostics& diagnostics)
{
    (void) diagnostics;
    track.dynamicChanges.clear();

    if (track.notes.empty())
        return;

    auto previous          = DynamicMarking::mf;
    int  groupTick         = track.notes.front().startTick;
    int  groupMaxVelocity  = -1;

    const auto flushGroup = [&] ()
    {
        if (groupMaxVelocity < 0) return;
        const auto current = bucketForVelocity (groupMaxVelocity);
        if (current != previous)
        {
            DynamicChangeRef change;
            change.startTick = groupTick;
            change.marking   = (int) current;
            track.dynamicChanges.push_back (change);
            previous = current;
        }
    };

    for (const auto& note : track.notes)
    {
        if (note.startTick != groupTick)
        {
            flushGroup();
            groupTick        = note.startTick;
            groupMaxVelocity = note.velocity;
        }
        else
        {
            groupMaxVelocity = std::max (groupMaxVelocity, note.velocity);
        }
    }
    flushGroup();
}

} // namespace lotro
