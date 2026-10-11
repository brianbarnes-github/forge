#include "UI/GridLines.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>

namespace lotro
{

namespace
{
    constexpr GridLevel divisionLevels[] = {
        GridLevel::Whole,     GridLevel::Half,         GridLevel::Quarter,       GridLevel::Eighth,
        GridLevel::Sixteenth, GridLevel::ThirtySecond, GridLevel::SixtyFourth
    };

    // Adds the lines at multiples of `spacing` ticks inside [firstTick, lastTick].
    // emplace keeps whatever level a tick already has, so callers add coarse
    // levels first and a tick ends up at the coarsest level it belongs to.
    void addLines (std::map<int, GridLevel>& lines, double spacing, GridLevel level, int firstTick, int lastTick)
    {
        for (long long k = (long long) std::floor ((double) firstTick / spacing);; ++k)
        {
            const long long tick = std::llround ((double) k * spacing);
            if (tick > lastTick)
                break;
            if (tick >= firstTick)
                lines.emplace ((int) tick, level);
        }
    }
}

std::vector<GridLine> computeGridLines (int firstTick, int lastTick, double pixelsPerTick,
                                        int ticksPerQuarter, const std::vector<MeterChange>& meters)
{
    firstTick = std::max (firstTick, 0);
    if (ticksPerQuarter <= 0 || ! (pixelsPerTick > 0.0) || lastTick < firstTick)
        return {};

    std::map<int, GridLevel> lines;

    const auto segments = meterSegments (meters, ticksPerQuarter);
    for (size_t i = 0; i < segments.size(); ++i)
    {
        const auto& s = segments[i];
        const double end = i + 1 < segments.size() ? segments[i + 1].startTick : std::numeric_limits<double>::infinity();
        for (long long k = std::max (0LL, (long long) std::floor (((double) firstTick - s.startTick) / s.ticksPerBar));; ++k)
        {
            const double barTick = s.startTick + (double) k * s.ticksPerBar;
            if (barTick >= end - 1e-9 || barTick > (double) lastTick)
                break;
            const long long tick = std::llround (barTick);
            if (tick >= firstTick)
                lines.emplace ((int) tick, GridLevel::Bar);
        }
    }

    const double wholeTicks = 4.0 * (double) ticksPerQuarter;
    double spacing = wholeTicks;
    for (const auto level : divisionLevels)
    {
        if (spacing * pixelsPerTick < minGridLinePixels)
            break;   // every finer division is closer still
        addLines (lines, spacing, level, firstTick, lastTick);
        spacing /= 2.0;
    }

    std::vector<GridLine> result;
    result.reserve (lines.size());
    for (const auto& [tick, level] : lines)
        result.push_back ({ tick, level });
    return result;
}

std::vector<GridLine> computeGridLines (int firstTick, int lastTick, double pixelsPerTick,
                                        int ticksPerQuarter, RulerMeter meter)
{
    if (meter.numerator <= 0 || meter.denominator <= 0)
        meter = {};
    return computeGridLines (firstTick, lastTick, pixelsPerTick, ticksPerQuarter,
                             std::vector<MeterChange> { { 0, meter.numerator, meter.denominator } });
}

} // namespace lotro
