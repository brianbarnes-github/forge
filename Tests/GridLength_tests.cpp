// Verifies the MIDI editor's Options > Grid Length / Grid Division tick spans,
// pure and header-only.

#include "UI/GridLength.h"

#include <catch2/catch_test_macros.hpp>

using namespace lotro;

TEST_CASE ("GridLength: a straight length spans its note value, whole to 1/64, and Off is zero", "[grid-length]")
{
    constexpr int ppq = 480;
    CHECK (editorGridTicks (GridLength::Off, GridDivision::Straight, ppq) == 0);
    CHECK (editorGridTicks (GridLength::Whole, GridDivision::Straight, ppq) == 1920);
    CHECK (editorGridTicks (GridLength::Half, GridDivision::Straight, ppq) == 960);
    CHECK (editorGridTicks (GridLength::Quarter, GridDivision::Straight, ppq) == 480);
    CHECK (editorGridTicks (GridLength::Eighth, GridDivision::Straight, ppq) == 240);
    CHECK (editorGridTicks (GridLength::Sixteenth, GridDivision::Straight, ppq) == 120);
    CHECK (editorGridTicks (GridLength::ThirtySecond, GridDivision::Straight, ppq) == 60);
    CHECK (editorGridTicks (GridLength::SixtyFourth, GridDivision::Straight, ppq) == 30);
}

TEST_CASE ("GridLength: Triplet is two thirds of the length and Dotted one and a half; Off stays zero", "[grid-length]")
{
    constexpr int ppq = 480;
    CHECK (editorGridTicks (GridLength::Quarter, GridDivision::Triplet, ppq) == 320);
    CHECK (editorGridTicks (GridLength::Eighth, GridDivision::Triplet, ppq) == 160);
    CHECK (editorGridTicks (GridLength::SixtyFourth, GridDivision::Triplet, ppq) == 20);
    CHECK (editorGridTicks (GridLength::Quarter, GridDivision::Dotted, ppq) == 720);
    CHECK (editorGridTicks (GridLength::SixtyFourth, GridDivision::Dotted, ppq) == 45);
    CHECK (editorGridTicks (GridLength::Off, GridDivision::Triplet, ppq) == 0);
    CHECK (editorGridTicks (GridLength::Off, GridDivision::Dotted, ppq) == 0);
}

TEST_CASE ("GridLength: Swing spaces the grid as Straight for now", "[grid-length]")
{
    CHECK (editorGridTicks (GridLength::Eighth, GridDivision::Swing, 480) == 240);
    CHECK (editorGridTicks (GridLength::Off, GridDivision::Swing, 480) == 0);
}

TEST_CASE ("GridLength: a span that is not a whole number of ticks rounds, and is never below one tick", "[grid-length]")
{
    // 96 PPQ: 1/64 = 6, triplet 4, dotted 9. 100 PPQ: 1/64 = 6.25 -> 6, triplet 4.17 -> 4.
    CHECK (editorGridTicks (GridLength::SixtyFourth, GridDivision::Triplet, 96) == 4);
    CHECK (editorGridTicks (GridLength::SixtyFourth, GridDivision::Dotted, 96) == 9);
    CHECK (editorGridTicks (GridLength::SixtyFourth, GridDivision::Straight, 100) == 6);
    CHECK (editorGridTicks (GridLength::SixtyFourth, GridDivision::Triplet, 100) == 4);
    CHECK (editorGridTicks (GridLength::SixtyFourth, GridDivision::Triplet, 1) == 1);
    CHECK (editorGridTicks (GridLength::Quarter, GridDivision::Straight, 0) == 0);   // no PPQ, no grid
}

TEST_CASE ("GridLength: each main-window Grid Size is the matching length", "[grid-length]")
{
    CHECK (gridLengthForSize (GridSize::Off) == GridLength::Off);
    CHECK (gridLengthForSize (GridSize::Quarter) == GridLength::Quarter);
    CHECK (gridLengthForSize (GridSize::Eighth) == GridLength::Eighth);
    CHECK (gridLengthForSize (GridSize::Sixteenth) == GridLength::Sixteenth);
}
