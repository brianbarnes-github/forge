#include "UI/GridLines.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>

using namespace lotro;

namespace
{
    constexpr int ppq = 480;   // whole = 1920, 1/4 = 480, 1/64 = 30

    std::vector<GridLine> linesOf (const std::vector<GridLine>& all, GridLevel level)
    {
        std::vector<GridLine> out;
        for (const auto& l : all)
            if (l.level == level)
                out.push_back (l);
        return out;
    }

    bool hasLevel (const std::vector<GridLine>& all, GridLevel level)
    {
        return ! linesOf (all, level).empty();
    }

    int tickCount (const std::vector<GridLine>& all, GridLevel level)
    {
        return (int) linesOf (all, level).size();
    }
}

TEST_CASE ("grid lines: zoomed far out show only bar lines", "[grid-lines]")
{
    // 0.001 px/tick: a bar (1920 ticks) is 1.92 px; even a whole note is under 8 px.
    const auto lines = computeGridLines (0, 19200, 0.001, ppq, { 4, 4 });
    CHECK (tickCount (lines, GridLevel::Bar) == 11);   // ticks 0..19200 step 1920
    CHECK_FALSE (hasLevel (lines, GridLevel::Whole));
    CHECK_FALSE (hasLevel (lines, GridLevel::Quarter));
}

TEST_CASE ("grid lines: a division appears once its lines are minGridLinePixels apart", "[grid-lines]")
{
    // 1/4 = 480 ticks. At 0.0166 px/tick that is 7.97 px (hidden); at 0.017 it is 8.16 px (shown).
    CHECK_FALSE (hasLevel (computeGridLines (0, 4000, 0.0166, ppq, { 4, 4 }), GridLevel::Quarter));
    CHECK (hasLevel (computeGridLines (0, 4000, 0.017, ppq, { 4, 4 }), GridLevel::Quarter));
}

TEST_CASE ("grid lines: fully zoomed in shows every level down to 1/64", "[grid-lines]")
{
    // 1/64 = 30 ticks; at 2.5 px/tick that is 75 px.
    const auto lines = computeGridLines (0, 1920, 2.5, ppq, { 4, 4 });
    CHECK (hasLevel (lines, GridLevel::SixtyFourth));
    CHECK (hasLevel (lines, GridLevel::ThirtySecond));
    CHECK (hasLevel (lines, GridLevel::Sixteenth));
    CHECK (hasLevel (lines, GridLevel::Eighth));
    CHECK (hasLevel (lines, GridLevel::Quarter));
    CHECK (hasLevel (lines, GridLevel::Half));
}

TEST_CASE ("grid lines: each tick is drawn once, at the coarsest level it belongs to", "[grid-lines]")
{
    const auto lines = computeGridLines (0, 1920, 2.5, ppq, { 4, 4 });

    for (size_t i = 1; i < lines.size(); ++i)
        CHECK (lines[i - 1].tick < lines[i].tick);   // sorted, no duplicates

    auto levelAt = [&] (int tick)
    {
        const auto it = std::find_if (lines.begin(), lines.end(), [tick] (const GridLine& l) { return l.tick == tick; });
        REQUIRE (it != lines.end());
        return it->level;
    };

    CHECK (levelAt (0) == GridLevel::Bar);          // bar beats whole note
    CHECK (levelAt (1920) == GridLevel::Bar);
    CHECK (levelAt (960) == GridLevel::Half);
    CHECK (levelAt (480) == GridLevel::Quarter);
    CHECK (levelAt (1440) == GridLevel::Quarter);
    CHECK (levelAt (240) == GridLevel::Eighth);
    CHECK (levelAt (120) == GridLevel::Sixteenth);
    CHECK (levelAt (60) == GridLevel::ThirtySecond);
    CHECK (levelAt (30) == GridLevel::SixtyFourth);
    CHECK (lines.size() == 1920 / 30 + 1);          // every 1/64 position, 0..1920 inclusive
}

TEST_CASE ("grid lines: bar lines follow the meter, divisions follow the whole note", "[grid-lines]")
{
    // 3/4: a bar is 1440 ticks, which is not a multiple of the whole note (1920).
    const auto lines = computeGridLines (0, 2880, 0.1, ppq, { 3, 4 });
    const auto bars = linesOf (lines, GridLevel::Bar);
    REQUIRE (bars.size() == 3);
    CHECK (bars[0].tick == 0);
    CHECK (bars[1].tick == 1440);
    CHECK (bars[2].tick == 2880);

    const auto wholes = linesOf (lines, GridLevel::Whole);   // 1920 only; 0 and 2880 are bars, 3840 is out of range
    REQUIRE (wholes.size() == 1);
    CHECK (wholes[0].tick == 1920);
}

TEST_CASE ("grid lines: 6/8 bar is six eighths", "[grid-lines]")
{
    const auto lines = computeGridLines (0, 1440, 0.001, ppq, { 6, 8 });
    const auto bars = linesOf (lines, GridLevel::Bar);
    REQUIRE (bars.size() == 2);
    CHECK (bars[1].tick == 1440);   // 6 * 240
}

TEST_CASE ("grid lines: only the requested tick range is returned", "[grid-lines]")
{
    const auto lines = computeGridLines (1000, 2000, 2.5, ppq, { 4, 4 });
    REQUIRE_FALSE (lines.empty());
    CHECK (lines.front().tick >= 1000);
    CHECK (lines.back().tick <= 2000);
    CHECK (lines.front().tick == 1020);   // first multiple of 30 at or after 1000
}

TEST_CASE ("grid lines: a negative first tick starts at 0", "[grid-lines]")
{
    const auto lines = computeGridLines (-500, 500, 2.5, ppq, { 4, 4 });
    REQUIRE_FALSE (lines.empty());
    CHECK (lines.front().tick == 0);
}

TEST_CASE ("grid lines: an invalid meter falls back to 4/4 and a bad ppq or range yields nothing", "[grid-lines]")
{
    const auto fallback = computeGridLines (0, 1920, 0.001, ppq, { 0, 0 });
    CHECK (tickCount (fallback, GridLevel::Bar) == 2);

    CHECK (computeGridLines (0, 1920, 0.1, 0, { 4, 4 }).empty());
    CHECK (computeGridLines (0, 1920, 0.0, ppq, { 4, 4 }).empty());
    CHECK (computeGridLines (2000, 1000, 0.1, ppq, { 4, 4 }).empty());
}

TEST_CASE ("grid lines: a ppq the 1/64 note does not divide evenly rounds to whole ticks", "[grid-lines]")
{
    // 100 PPQ: 1/64 = 6.25 ticks. Positions are round (k * 6.25), still sorted and unique.
    const auto lines = computeGridLines (0, 400, 2.5, 100, { 4, 4 });
    for (size_t i = 1; i < lines.size(); ++i)
        CHECK (lines[i - 1].tick < lines[i].tick);
    CHECK (hasLevel (lines, GridLevel::SixtyFourth));
}

TEST_CASE ("computeGridLines: bar lines follow a meter change", "[grid][tempo-sync]")
{
    const int ppq = 480;
    const auto lines = computeGridLines (0, 6720, 0.001, ppq, std::vector<MeterChange> { { 0, 4, 4 }, { 3840, 3, 4 } });
    std::vector<int> bars;
    for (const auto& l : lines)
        if (l.level == GridLevel::Bar)
            bars.push_back (l.tick);
    CHECK (bars == std::vector<int> { 0, 1920, 3840, 5280, 6720 });   // 4/4 bars, then 1440-tick bars
}

TEST_CASE ("computeGridLines: one meter through the list overload equals the single-meter overload", "[grid][tempo-sync]")
{
    const int ppq = 480;
    const auto a = computeGridLines (0, 4000, 0.05, ppq, RulerMeter { 6, 8 });
    const auto b = computeGridLines (0, 4000, 0.05, ppq, std::vector<MeterChange> { { 0, 6, 8 } });
    REQUIRE (a.size() == b.size());
    for (size_t i = 0; i < a.size(); ++i)
    {
        CHECK (a[i].tick == b[i].tick);
        CHECK (a[i].level == b[i].level);
    }
}
