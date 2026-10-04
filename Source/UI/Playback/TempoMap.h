#pragma once

#include <vector>

namespace lotro
{

struct TempoPoint
{
    int tick = 0;
    double bpm = 120.0;
};

// Piecewise-linear tick <-> seconds conversion over a song's tempo changes.
// Seconds are the playback time domain: unlike ticks they do not change when
// an import rescales the document's PPQ, and they do not depend on the
// device sample rate.
class TempoMap
{
public:
    static constexpr double defaultBpm = 120.0;

    TempoMap() : TempoMap ({}, 480) {}
    TempoMap (std::vector<TempoPoint> points, int ticksPerQuarterIn);

    double ticksToSeconds (double tick) const;
    double secondsToTicks (double seconds) const;
    int getTicksPerQuarter() const noexcept { return ticksPerQuarter; }

private:
    struct Segment
    {
        double startTick;
        double startSeconds;
        double secondsPerTick;
    };

    std::vector<Segment> segments;   // never empty; first starts at tick 0
    int ticksPerQuarter;
};

} // namespace lotro
