#pragma once

// Pure bar arithmetic over a list of meter changes (no JUCE). The ruler, the
// grid lines, Rewind One Bar and the tempo/meter editor all turn the meter map
// into bars through this one place.

#include <vector>

namespace lotro
{

struct MeterChange
{
    int tick        = 0;
    int numerator   = 4;
    int denominator = 4;
};

struct MeterSegment
{
    double    startTick    = 0.0;
    double    ticksPerBar  = 1920.0;
    double    ticksPerBeat = 480.0;
    int       numerator    = 4;
    int       denominator  = 4;
    long long firstBar     = 0;   // 0-based number of the bar that starts at startTick
};

// Sorted, de-duplicated, sanitised segments; the first starts at tick 0. Empty
// when ticksPerQuarter <= 0.
std::vector<MeterSegment> meterSegments (std::vector<MeterChange> changes, int ticksPerQuarter);

// The segment containing `tick` (the first for tick < 0). `segments` must not be empty.
const MeterSegment& segmentAt (const std::vector<MeterSegment>& segments, double tick);

// The bar line nearest `tick`, never past the end of its segment. 0 when `segments` is empty.
double nearestBarStart (const std::vector<MeterSegment>& segments, double tick);

// Start of the bar containing `tick`, or of the previous bar when `tick` is
// exactly on a bar line. 0 when `segments` is empty.
double previousBarStart (const std::vector<MeterSegment>& segments, double tick);

} // namespace lotro
