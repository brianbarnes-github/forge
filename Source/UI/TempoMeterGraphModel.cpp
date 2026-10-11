#include "UI/TempoMeterGraphModel.h"

#include <algorithm>
#include <cmath>

namespace lotro
{

BpmRange bpmRangeFor (const std::vector<TempoMeterEvent>& events)
{
    double lo = 1e9, hi = -1e9;
    for (const auto& e : events)
        if (e.kind == TempoMeterKind::tempo)
        {
            lo = std::min (lo, e.bpm);
            hi = std::max (hi, e.bpm);
        }
    if (lo > hi)
        return {};

    lo *= 0.75;
    hi *= 1.25;
    if (hi - lo < 40.0)
    {
        const double mid = (lo + hi) / 2.0;
        lo = mid - 20.0;
        hi = mid + 20.0;
    }
    if (lo < 1.0)
    {
        hi += 1.0 - lo;
        lo = 1.0;
    }
    return { lo, hi };
}

int bpmToY (const BpmRange& range, double bpm, int plotTop, int plotHeight)
{
    const double span = range.hi - range.lo;
    if (span <= 0.0)
        return plotTop;
    return plotTop + (int) std::llround ((range.hi - bpm) / span * (double) plotHeight);
}

double yToBpm (const BpmRange& range, int y, int plotTop, int plotHeight)
{
    const double bpm = plotHeight <= 0
                         ? range.lo
                         : range.hi - (double) (y - plotTop) / (double) plotHeight * (range.hi - range.lo);
    return std::clamp (std::round (bpm), std::ceil (minBpm()), maxBpm);
}

int snapToBeat (const std::vector<MeterSegment>& segments, int tick)
{
    if (segments.empty())
        return std::max (0, tick);

    const auto& seg = segmentAt (segments, (double) tick);
    const double beat = seg.ticksPerBeat;
    if (beat <= 0.0)
        return std::max (0, tick);

    double snapped = seg.startTick + std::round (((double) tick - seg.startTick) / beat) * beat;

    const auto next = std::find_if (segments.begin(), segments.end(),
                                    [&] (const MeterSegment& s) { return s.startTick > seg.startTick; });
    if (next != segments.end())
        snapped = std::min (snapped, next->startTick - beat);
    snapped = std::max (snapped, seg.startTick);
    return std::max (0, (int) std::llround (snapped));
}

int snapMeterTick (std::vector<MeterChange> meters, int ticksPerQuarter, int fromTick, int tick)
{
    meters.erase (std::remove_if (meters.begin(), meters.end(),
                                  [&] (const MeterChange& c) { return c.tick == fromTick; }),
                  meters.end());
    return (int) std::llround (nearestBarStart (meterSegments (std::move (meters), ticksPerQuarter),
                                                (double) std::max (0, tick)));
}

std::optional<size_t> nearestPoint (const std::vector<std::pair<int, int>>& xy, int x, int y, int radiusPx)
{
    std::optional<size_t> best;
    long long bestD2 = 0;
    const long long r2 = (long long) radiusPx * radiusPx;
    for (size_t i = 0; i < xy.size(); ++i)
    {
        const long long dx = xy[i].first - x, dy = xy[i].second - y;
        const long long d2 = dx * dx + dy * dy;
        if (d2 <= r2 && (! best || d2 <= bestD2))
        {
            best   = i;
            bestD2 = d2;
        }
    }
    return best;
}

} // namespace lotro
