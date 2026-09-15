// Verifies GridSize's tick conversion -- the toolbar grid-size selector's
// only piece of logic, pure and header-only.

#include "UI/GridSize.h"

#include <catch2/catch_test_macros.hpp>

using namespace lotro;

TEST_CASE ("GridSize: gridSizeToTicks maps each grid size to the right tick span, Off to zero", "[gridsize]")
{
    constexpr int ticksPerQuarter = 480;

    CHECK (gridSizeToTicks (GridSize::Off, ticksPerQuarter) == 0);
    CHECK (gridSizeToTicks (GridSize::Quarter, ticksPerQuarter) == 480);
    CHECK (gridSizeToTicks (GridSize::Eighth, ticksPerQuarter) == 240);
    CHECK (gridSizeToTicks (GridSize::Sixteenth, ticksPerQuarter) == 120);
}
