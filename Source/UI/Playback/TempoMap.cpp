#include "UI/Playback/TempoMap.h"

#include <algorithm>

namespace lotro
{

TempoMap::TempoMap (std::vector<TempoPoint> points, int ticksPerQuarterIn)
    : ticksPerQuarter (ticksPerQuarterIn > 0 ? ticksPerQuarterIn : 480)
{
    points.erase (std::remove_if (points.begin(), points.end(),
                                  [] (const TempoPoint& p) { return ! (p.bpm > 0.0) || p.tick < 0; }),
                  points.end());
    std::stable_sort (points.begin(), points.end(),
                      [] (const TempoPoint& a, const TempoPoint& b) { return a.tick < b.tick; });
    if (points.empty() || points.front().tick > 0)
        points.insert (points.begin(), TempoPoint { 0, defaultBpm });

    double seconds = 0.0;
    for (size_t i = 0; i < points.size(); ++i)
    {
        if (i > 0)
            seconds += (double) (points[i].tick - points[i - 1].tick) * segments.back().secondsPerTick;
        segments.push_back ({ (double) points[i].tick, seconds, 60.0 / (points[i].bpm * (double) ticksPerQuarter) });
    }
}

double TempoMap::ticksToSeconds (double tick) const
{
    tick = std::max (0.0, tick);
    auto it = std::upper_bound (segments.begin(), segments.end(), tick,
                                [] (double t, const Segment& s) { return t < s.startTick; });
    const Segment& seg = *(it - 1);
    return seg.startSeconds + (tick - seg.startTick) * seg.secondsPerTick;
}

double TempoMap::secondsToTicks (double seconds) const
{
    seconds = std::max (0.0, seconds);
    auto it = std::upper_bound (segments.begin(), segments.end(), seconds,
                                [] (double s, const Segment& seg) { return s < seg.startSeconds; });
    const Segment& seg = *(it - 1);
    return seg.startTick + (seconds - seg.startSeconds) / seg.secondsPerTick;
}

} // namespace lotro
