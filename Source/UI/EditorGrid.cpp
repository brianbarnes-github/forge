#include "EditorGrid.h"

#include "GridSnap.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace lotro
{

namespace
{
    // One bar of a swung grid: its start, its end (the next bar's start, which a
    // meter change can pull in) and its lines, sorted.
    struct Bar
    {
        double begin = 0.0;
        double end = 0.0;
        std::vector<long long> lines;
    };

    Bar barContaining (const EditorGrid& grid, double tick)
    {
        const double t = std::max (0.0, tick);
        const auto& seg = segmentAt (grid.bars, t);
        const auto next = std::find_if (grid.bars.begin(), grid.bars.end(),
                                        [&seg] (const MeterSegment& s) { return s.startTick > seg.startTick; });
        const double segEnd = next == grid.bars.end() ? std::numeric_limits<double>::infinity() : next->startTick;

        Bar bar;
        bar.begin = seg.startTick + std::floor ((t - seg.startTick) / seg.ticksPerBar) * seg.ticksPerBar;
        bar.end = std::min (bar.begin + seg.ticksPerBar, segEnd);

        const double s = grid.spacingTicks;
        for (int j = 0; bar.begin + j * s < bar.end; ++j)
        {
            double line = bar.begin + j * s;
            if (j % 2 != 0)
            {
                line += grid.swing * s / 2.0;
                if (line >= bar.end)
                    continue;
            }
            const long long rounded = std::llround (line);
            if (bar.lines.empty() || rounded > bar.lines.back())
                bar.lines.push_back (rounded);
        }
        return bar;
    }

    // The last swung line at or below `tick` (below it when !inclusive), looking
    // back across bars: a fractional bar start can round its first line past a
    // tick that is still inside the bar. -1 when there is none.
    double lastLineUpTo (const EditorGrid& grid, double tick, bool inclusive)
    {
        Bar bar = barContaining (grid, tick);
        for (;;)
        {
            double best = -1.0;
            for (const long long line : bar.lines)
                if (inclusive ? (double) line <= tick : (double) line < tick)
                    best = (double) line;
            if (best >= 0.0 || bar.begin <= 0.0)
                return best;
            bar = barContaining (grid, bar.begin - 0.5);
        }
    }

    // The first swung line above `tick`, looking forward across bars (the same
    // rounding can leave a bar with no line past a tick near its end).
    double firstLineAbove (const EditorGrid& grid, double tick)
    {
        Bar bar = barContaining (grid, tick);
        for (;;)
        {
            for (const long long line : bar.lines)
                if ((double) line > tick)
                    return (double) line;
            bar = barContaining (grid, bar.end);
        }
    }

    bool isSwung (const EditorGrid& grid) noexcept
    {
        return grid.swung && ! grid.bars.empty();
    }
}

EditorGrid EditorGrid::straight (int spacing)
{
    EditorGrid grid;
    grid.spacingTicks = spacing;
    return grid;
}

EditorGrid makeEditorGrid (GridLength length, GridDivision division, int swingPercent, int ticksPerQuarter,
                           const std::vector<MeterChange>& meters)
{
    EditorGrid grid = EditorGrid::straight (editorGridTicks (length, division, ticksPerQuarter));
    grid.swung = division == GridDivision::Swing && grid.spacingTicks > 0;
    if (grid.swung)
    {
        grid.bars = meterSegments (meters, ticksPerQuarter);
        grid.swing = clampSwingPercent (swingPercent) / 100.0;
        if (grid.bars.empty())
            grid.swung = false;
    }
    return grid;
}

double editorGridLineAtOrBefore (const EditorGrid& grid, double tick)
{
    if (grid.spacingTicks <= 0)
        return tick;
    if (! isSwung (grid))
        return std::max (0.0, std::floor (tick / grid.spacingTicks) * grid.spacingTicks);

    return std::max (0.0, lastLineUpTo (grid, tick, true));
}

double editorGridLineAfter (const EditorGrid& grid, double tick)
{
    if (grid.spacingTicks <= 0)
        return tick;
    if (! isSwung (grid))
        return stepTickByGrid (tick, grid.spacingTicks, 1);

    return firstLineAbove (grid, tick);
}

double snapToEditorGrid (const EditorGrid& grid, double tick)
{
    if (grid.spacingTicks <= 0)
        return tick;
    if (! isSwung (grid))
        return snapTickToGrid (tick, grid.spacingTicks);

    const double before = editorGridLineAtOrBefore (grid, tick);
    const double after = editorGridLineAfter (grid, tick);
    return tick - before < after - tick ? before : after;
}

double stepEditorGrid (const EditorGrid& grid, double tick, int direction)
{
    if (grid.spacingTicks <= 0 || direction == 0)
        return tick;
    if (! isSwung (grid))
        return stepTickByGrid (tick, grid.spacingTicks, direction);
    if (direction > 0)
        return editorGridLineAfter (grid, tick);
    if (tick <= 0.0)
        return 0.0;

    return std::max (0.0, lastLineUpTo (grid, tick, false));
}

std::vector<int> editorGridLinesIn (const EditorGrid& grid, int firstTick, int lastTick)
{
    std::vector<int> out;
    if (grid.spacingTicks <= 0 || lastTick < firstTick)
        return out;

    if (! isSwung (grid))
    {
        const long long s = grid.spacingTicks;
        const long long first = std::max (0, firstTick);
        for (long long t = (first + s - 1) / s * s; t <= lastTick; t += s)
            out.push_back ((int) t);
        return out;
    }

    double begin = barContaining (grid, firstTick).begin;
    while (begin <= lastTick)
    {
        const Bar bar = barContaining (grid, begin);
        for (const long long line : bar.lines)
            if (line >= firstTick && line <= lastTick)
                out.push_back ((int) line);
        begin = bar.end;
    }
    return out;
}

double editorGridMinGap (const EditorGrid& grid)
{
    if (grid.spacingTicks <= 0)
        return 0.0;
    const double s = grid.spacingTicks;
    return isSwung (grid) ? s * (1.0 - std::abs (grid.swing) / 2.0) : s;
}

std::string swingPercentText (int percent)
{
    return (percent > 0 ? "+" : "") + std::to_string (percent) + "%";
}

} // namespace lotro
