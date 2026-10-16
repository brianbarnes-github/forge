#include "UI/EditorGrid.h"
#include "UI/GridSnap.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>

using namespace lotro;
using Catch::Approx;

namespace
{
    constexpr int ppq = 480;
    using Ticks = std::vector<int>;

    EditorGrid make (GridLength length, GridDivision division, int percent,
                     int quarter = ppq, const std::vector<MeterChange>& meters = {})
    {
        return makeEditorGrid (length, division, percent, quarter, meters);
    }

    EditorGrid eighthSwing (int percent) { return make (GridLength::Eighth, GridDivision::Swing, percent); }

    Ticks linesOf (const EditorGrid& g, int first, int last) { return editorGridLinesIn (g, first, last); }
}

TEST_CASE ("EditorGrid: non-Swing grids match GridSnap exactly", "[editor-grid][swing]")
{
    for (const int s : { 1, 7, 160, 240, 480 })
    {
        const auto grid = EditorGrid::straight (s);
        for (int t = -100; t <= 3000; t += 7)
        {
            const double tick = t;
            INFO ("s=" << s << " t=" << t);
            CHECK (snapToEditorGrid (grid, tick) == snapTickToGrid (tick, s));
            CHECK (stepEditorGrid (grid, tick, 1) == stepTickByGrid (tick, s, 1));
            CHECK (stepEditorGrid (grid, tick, -1) == stepTickByGrid (tick, s, -1));
            CHECK (editorGridLineAfter (grid, tick) == stepTickByGrid (tick, s, 1));
            CHECK (editorGridLineAtOrBefore (grid, tick) == std::max (0.0, std::floor (tick / s) * s));
        }
    }

    const auto triplet = make (GridLength::Eighth, GridDivision::Triplet, 100);
    CHECK (triplet.spacingTicks == 160);
    CHECK (! triplet.swung);
}

TEST_CASE ("EditorGrid: off leaves every tick", "[editor-grid][swing]")
{
    for (const int quarter : { ppq, 0 })
    {
        const auto grid = quarter == ppq ? make (GridLength::Off, GridDivision::Swing, 50)
                                         : make (GridLength::Eighth, GridDivision::Swing, 50, 0);
        CHECK (grid.spacingTicks == 0);
        CHECK (snapToEditorGrid (grid, 1000.0) == 1000.0);
        CHECK (stepEditorGrid (grid, 1000.0, 1) == 1000.0);
        CHECK (stepEditorGrid (grid, 1000.0, -1) == 1000.0);
        CHECK (editorGridLineAfter (grid, 1000.0) == 1000.0);
        CHECK (editorGridLineAtOrBefore (grid, 1000.0) == 1000.0);
        CHECK (linesOf (grid, 0, 2000).empty());
        CHECK (editorGridMinGap (grid) == 0.0);
    }
}

TEST_CASE ("EditorGrid: Swing at 0% in 4/4 equals Straight", "[editor-grid][swing]")
{
    Ticks expected;
    for (int t = 0; t <= 3840; t += 240)
        expected.push_back (t);
    CHECK (linesOf (eighthSwing (0), 0, 3840) == expected);
}

TEST_CASE ("EditorGrid: +100% and -100% at 1/8", "[editor-grid][swing]")
{
    CHECK (linesOf (eighthSwing (100), 0, 1920) == Ticks { 0, 360, 480, 840, 960, 1320, 1440, 1800, 1920 });

    const auto back = eighthSwing (-100);
    CHECK (editorGridLineAfter (back, 0.0) == 120.0);
    CHECK (editorGridLineAfter (back, 480.0) == 600.0);
}

TEST_CASE ("EditorGrid: +67% gives a 2:1 pair", "[editor-grid][swing]")
{
    const auto grid = eighthSwing (67);
    CHECK (editorGridLineAfter (grid, 0.0) == 320.0);
    CHECK (editorGridLineAfter (grid, 320.0) == 480.0);
}

TEST_CASE ("EditorGrid: pairs restart at each bar line", "[editor-grid][swing]")
{
    SECTION ("3/4 at 1/4")
    {
        const auto g = make (GridLength::Quarter, GridDivision::Swing, 100, ppq, { { 0, 3, 4 } });
        CHECK (linesOf (g, 0, 2880) == Ticks { 0, 720, 960, 1440, 2160, 2400, 2880 });
    }
    SECTION ("5/8 at 1/4")
    {
        const auto g = make (GridLength::Quarter, GridDivision::Swing, 100, ppq, { { 0, 5, 8 } });
        CHECK (linesOf (g, 0, 2400) == Ticks { 0, 720, 960, 1200, 1920, 2160, 2400 });
    }
    SECTION ("3/4 then 4/4 from tick 1440")
    {
        const auto g = make (GridLength::Quarter, GridDivision::Swing, 100, ppq,
                             { { 0, 3, 4 }, { 1440, 4, 4 } });
        CHECK (linesOf (g, 0, 3360) == Ticks { 0, 720, 960, 1440, 2160, 2400, 3120, 3360 });
    }
}

TEST_CASE ("EditorGrid: an odd line at or past the bar end is dropped", "[editor-grid][swing]")
{
    const std::vector<MeterChange> fiveSixteenths { { 0, 5, 16 } };
    CHECK (linesOf (make (GridLength::Quarter, GridDivision::Swing, 50, ppq, fiveSixteenths), 0, 1200)
           == Ticks { 0, 600, 1200 });
    CHECK (linesOf (make (GridLength::Quarter, GridDivision::Swing, 25, ppq, fiveSixteenths), 0, 1200)
           == Ticks { 0, 540, 600, 1140, 1200 });
}

TEST_CASE ("EditorGrid: rounding at odd PPQ", "[editor-grid][swing]")
{
    const auto grid = make (GridLength::Eighth, GridDivision::Swing, 100, 97);
    CHECK (grid.spacingTicks == 49);
    CHECK (editorGridLineAfter (grid, 0.0) == 74.0);
}

TEST_CASE ("EditorGrid: snap, step and the cell edges on a swung grid", "[editor-grid][swing]")
{
    const auto g = eighthSwing (100);

    CHECK (snapToEditorGrid (g, 500.0) == 480.0);
    CHECK (snapToEditorGrid (g, 700.0) == 840.0);
    CHECK (snapToEditorGrid (g, 180.0) == 360.0);   // tie goes to the later line
    CHECK (snapToEditorGrid (g, 1900.0) == 1920.0);
    CHECK (snapToEditorGrid (g, -50.0) == 0.0);

    CHECK (stepEditorGrid (g, 400.0, 1) == 480.0);
    CHECK (stepEditorGrid (g, 400.0, -1) == 360.0);
    CHECK (stepEditorGrid (g, 360.0, -1) == 0.0);
    CHECK (stepEditorGrid (g, 1800.0, 1) == 1920.0);
    CHECK (stepEditorGrid (g, 1920.0, -1) == 1800.0);
    CHECK (stepEditorGrid (g, 0.0, -1) == 0.0);

    CHECK (editorGridLineAtOrBefore (g, 359.0) == 0.0);
    CHECK (editorGridLineAtOrBefore (g, 360.0) == 360.0);
    CHECK (editorGridLineAtOrBefore (g, -5.0) == 0.0);
    CHECK (editorGridLineAfter (g, 360.0) == 480.0);
    CHECK (editorGridLineAfter (g, -5.0) == 0.0);
}

TEST_CASE ("EditorGrid: min gap and strength clamp", "[editor-grid][swing]")
{
    CHECK (editorGridMinGap (eighthSwing (100)) == Approx (120.0));
    CHECK (editorGridMinGap (eighthSwing (-67)) == Approx (159.6));
    CHECK (editorGridMinGap (make (GridLength::Eighth, GridDivision::Straight, 100)) == Approx (240.0));
    CHECK (eighthSwing (250).swing == 1.0);
    CHECK (clampSwingPercent (-300) == -100);
}

TEST_CASE ("EditorGrid: swing readout text", "[editor-grid][swing]")
{
    CHECK (swingPercentText (67) == "+67%");
    CHECK (swingPercentText (0) == "0%");
    CHECK (swingPercentText (-25) == "-25%");
    CHECK (swingPercentText (100) == "+100%");
}
