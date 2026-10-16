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

namespace
{
    const std::vector<MeterChange> fourFour { { 0, 4, 4 } };

    std::vector<int> ticksOf (const std::vector<GridLine>& all)
    {
        std::vector<int> out;
        for (const auto& l : all)
            out.push_back (l.tick);
        return out;
    }

    GridLevel levelAt (const std::vector<GridLine>& all, int tick)
    {
        for (const auto& l : all)
            if (l.tick == tick)
                return l.level;
        FAIL ("no line at " << tick);
        return GridLevel::Bar;
    }
}

TEST_CASE ("grid lines: an editor grid of a triplet length draws its own lines, and the note values it lands on, nothing finer", "[grid-lines][grid-length]")
{
    // 1/8 triplet = 160 ticks. It lands on every quarter (480) but not on the straight eighths (240).
    const auto lines = computeGridLines (0, 960, 1.0, ppq, fourFour, EditorGridLines { 160, GridLevel::Eighth });
    CHECK (ticksOf (lines) == std::vector<int> { 0, 160, 320, 480, 640, 800, 960 });
    CHECK (levelAt (lines, 0) == GridLevel::Bar);
    CHECK (levelAt (lines, 160) == GridLevel::Eighth);
    CHECK (levelAt (lines, 480) == GridLevel::Quarter);
    CHECK (levelAt (lines, 960) == GridLevel::Half);
    CHECK_FALSE (hasLevel (lines, GridLevel::Sixteenth));
}

TEST_CASE ("grid lines: a straight editor grid draws the note values down to its length and none finer", "[grid-lines][grid-length]")
{
    const auto lines = computeGridLines (0, 1920, 2.5, ppq, fourFour, EditorGridLines { 120, GridLevel::Sixteenth });
    CHECK (hasLevel (lines, GridLevel::Sixteenth));
    CHECK (hasLevel (lines, GridLevel::Eighth));
    CHECK_FALSE (hasLevel (lines, GridLevel::ThirtySecond));
    CHECK_FALSE (hasLevel (lines, GridLevel::SixtyFourth));
    CHECK (tickCount (lines, GridLevel::Sixteenth) == 8);   // 120, 360, ... 1800
}

TEST_CASE ("grid lines: a dotted editor grid keeps the bar lines and drops the note values it does not land on", "[grid-lines][grid-length]")
{
    // Dotted quarter = 720 ticks: no straight value from the whole note down is a multiple of it.
    const auto lines = computeGridLines (0, 3840, 1.0, ppq, fourFour, EditorGridLines { 720, GridLevel::Quarter });
    CHECK (ticksOf (lines) == std::vector<int> { 0, 720, 1440, 1920, 2160, 2880, 3600, 3840 });
    CHECK (levelAt (lines, 720) == GridLevel::Quarter);
    CHECK (levelAt (lines, 1920) == GridLevel::Bar);
}

TEST_CASE ("grid lines: an editor grid closer than minGridLinePixels is not drawn, while coarser values still are", "[grid-lines][grid-length]")
{
    // 1/64 triplet = 20 ticks: 4 px at 0.2 px/tick. The 1/32 (60 ticks, 12 px) is a multiple of it.
    const auto lines = computeGridLines (0, 480, 0.2, ppq, fourFour, EditorGridLines { 20, GridLevel::SixtyFourth });
    CHECK_FALSE (hasLevel (lines, GridLevel::SixtyFourth));
    CHECK (hasLevel (lines, GridLevel::ThirtySecond));
    const auto ticks = ticksOf (lines);
    CHECK (std::find (ticks.begin(), ticks.end(), 20) == ticks.end());
}

TEST_CASE ("grid lines: an editor grid of 0 ticks (grid Off) is the plain zoom-following grid", "[grid-lines][grid-length]")
{
    const auto plain = computeGridLines (0, 1920, 2.5, ppq, fourFour);
    const auto off = computeGridLines (0, 1920, 2.5, ppq, fourFour, EditorGridLines {});
    REQUIRE (plain.size() == off.size());
    for (size_t i = 0; i < plain.size(); ++i)
    {
        CHECK (plain[i].tick == off[i].tick);
        CHECK (plain[i].level == off[i].level);
    }
}

TEST_CASE ("grid lines: each Grid Length draws at its own note value's level", "[grid-lines][grid-length]")
{
    CHECK (gridLevelForLength (GridLength::Whole) == GridLevel::Whole);
    CHECK (gridLevelForLength (GridLength::Half) == GridLevel::Half);
    CHECK (gridLevelForLength (GridLength::Quarter) == GridLevel::Quarter);
    CHECK (gridLevelForLength (GridLength::Eighth) == GridLevel::Eighth);
    CHECK (gridLevelForLength (GridLength::Sixteenth) == GridLevel::Sixteenth);
    CHECK (gridLevelForLength (GridLength::ThirtySecond) == GridLevel::ThirtySecond);
    CHECK (gridLevelForLength (GridLength::SixtyFourth) == GridLevel::SixtyFourth);
}

TEST_CASE ("GridLines: a swung editor grid draws bar lines and the swung lines only", "[grid-lines][swing]")
{
    const auto lines = computeGridLines (0, 1920, 0.2, ppq, fourFour, EditorGridLines { 240, GridLevel::Eighth, true, 1.0 });
    CHECK (ticksOf (lines) == std::vector<int> { 0, 360, 480, 840, 960, 1320, 1440, 1800, 1920 });
    CHECK (levelAt (lines, 0) == GridLevel::Bar);
    CHECK (levelAt (lines, 1920) == GridLevel::Bar);
    CHECK (levelAt (lines, 960) == GridLevel::Eighth);   // no Half / Quarter levels
    CHECK (levelAt (lines, 360) == GridLevel::Eighth);
}

TEST_CASE ("GridLines: swung lines need their smallest gap to be minGridLinePixels", "[grid-lines][swing]")
{
    const EditorGridLines swung { 240, GridLevel::Eighth, true, 1.0 };   // smallest gap 120 ticks
    CHECK (ticksOf (computeGridLines (0, 1920, 0.05, ppq, fourFour, swung)) == std::vector<int> { 0, 1920 });   // 6 px
    CHECK (ticksOf (computeGridLines (0, 1920, 0.07, ppq, fourFour, swung)).size() == 9);                      // 8.4 px
}

TEST_CASE ("GridLines: swung pairs restart at a meter change", "[grid-lines][swing]")
{
    const auto lines = computeGridLines (0, 3360, 0.2, ppq, std::vector<MeterChange> { { 0, 3, 4 }, { 1440, 4, 4 } },
                                         EditorGridLines { 480, GridLevel::Quarter, true, 1.0 });
    CHECK (ticksOf (lines) == std::vector<int> { 0, 720, 960, 1440, 2160, 2400, 3120, 3360 });
}
