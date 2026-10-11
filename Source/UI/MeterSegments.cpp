#include "UI/MeterSegments.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace lotro
{

std::vector<MeterSegment> meterSegments (std::vector<MeterChange> changes, int ticksPerQuarter)
{
    if (ticksPerQuarter <= 0)
        return {};

    std::stable_sort (changes.begin(), changes.end(),
                      [] (const MeterChange& a, const MeterChange& b) { return a.tick < b.tick; });

    std::vector<MeterChange> unique;
    for (auto change : changes)
    {
        change.tick = std::max (0, change.tick);
        if (change.numerator <= 0 || change.denominator <= 0)
        {
            change.numerator = 4;
            change.denominator = 4;
        }
        if (! unique.empty() && unique.back().tick == change.tick)
            unique.back() = change;
        else
            unique.push_back (change);
    }
    if (unique.empty())
        unique.push_back ({});
    unique.front().tick = 0;

    std::vector<MeterSegment> out;
    for (const auto& change : unique)
    {
        MeterSegment s;
        s.startTick    = (double) change.tick;
        s.numerator    = change.numerator;
        s.denominator  = change.denominator;
        s.ticksPerBeat = (double) ticksPerQuarter * 4.0 / (double) change.denominator;
        s.ticksPerBar  = s.ticksPerBeat * (double) change.numerator;
        if (! out.empty())
        {
            const auto& previous = out.back();
            s.firstBar = previous.firstBar
                       + (long long) std::ceil ((s.startTick - previous.startTick) / previous.ticksPerBar - 1e-9);
        }
        out.push_back (s);
    }
    return out;
}

const MeterSegment& segmentAt (const std::vector<MeterSegment>& segments, double tick)
{
    size_t index = 0;
    for (size_t i = 1; i < segments.size(); ++i)
        if (segments[i].startTick <= tick)
            index = i;
    return segments[index];
}

double nearestBarStart (const std::vector<MeterSegment>& segments, double tick)
{
    if (segments.empty())
        return 0.0;

    const auto& segment = segmentAt (segments, tick);
    const double bars = std::max (0.0, std::round ((tick - segment.startTick) / segment.ticksPerBar));
    double candidate = segment.startTick + bars * segment.ticksPerBar;

    const size_t index = (size_t) (&segment - segments.data());
    if (index + 1 < segments.size() && candidate >= segments[index + 1].startTick - 1e-9)
        candidate = segments[index + 1].startTick;
    return candidate;
}

double previousBarStart (const std::vector<MeterSegment>& segments, double tick)
{
    if (segments.empty())
        return 0.0;

    size_t index = (size_t) (&segmentAt (segments, tick) - segments.data());
    for (;; --index)
    {
        const auto& s = segments[index];
        const double end = index + 1 < segments.size() ? segments[index + 1].startTick
                                                        : std::numeric_limits<double>::infinity();
        const double bars = std::ceil ((std::min (tick, end) - s.startTick) / s.ticksPerBar - 1e-9);
        if (bars >= 1.0)
            return std::max (0.0, s.startTick + (bars - 1.0) * s.ticksPerBar);
        if (index == 0)
            return 0.0;
    }
}

} // namespace lotro
