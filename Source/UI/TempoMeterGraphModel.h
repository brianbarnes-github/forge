#pragma once

// Pure (JUCE-free) geometry for the Tempo/Meter editor's tempo graph and meter
// strip: BPM <-> pixel mapping, tick snapping and point hit-testing.

#include "Core/Song.h" // MeterChange
#include "UI/MeterSegments.h"
#include "UI/TempoEdit.h"

#include <cstddef>
#include <optional>
#include <utility>
#include <vector>

namespace lotro
{

struct BpmRange
{
    double lo = 60.0;
    double hi = 180.0;
};

// Range for the tempo events in `events` (other kinds ignored): 75% of the lowest to
// 125% of the highest, widened symmetrically to at least 40 BPM tall, lo never below 1.
// Empty (no tempo events) -> {60, 180}.
BpmRange bpmRangeFor (const std::vector<TempoMeterEvent>& events);

// y grows downward: bpm == hi -> plotTop, bpm == lo -> plotTop + plotHeight (rounded).
int bpmToY (const BpmRange& range, double bpm, int plotTop, int plotHeight);

// Inverse of bpmToY, rounded to a whole BPM and clamped to [ceil(minBpm()), maxBpm].
// plotHeight <= 0 yields lo's clamp.
double yToBpm (const BpmRange& range, int y, int plotTop, int plotHeight);

// Nearest beat start of the segment containing `tick`; never < 0, never past the segment end.
int snapToBeat (const std::vector<MeterSegment>& segments, int tick);

// Nearest bar start of the meters in force WITHOUT the one at `fromTick`; tick >= 0.
int snapMeterTick (std::vector<MeterChange> meters, int ticksPerQuarter, int fromTick, int tick);

// Index of the point whose (x, y) is within radiusPx (Euclidean) of (x, y); nearest wins,
// ties go to the LATER index. nullopt when none.
std::optional<size_t> nearestPoint (const std::vector<std::pair<int, int>>& xy, int x, int y, int radiusPx);

} // namespace lotro
