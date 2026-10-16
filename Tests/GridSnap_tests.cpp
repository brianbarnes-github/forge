#include "UI/GridSnap.h"

#include <catch2/catch_test_macros.hpp>

using namespace lotro;

TEST_CASE ("GridSnap: a tick snaps to the nearest multiple of the grid, never below 0; no grid leaves it", "[grid-snap]")
{
    CHECK (snapTickToGrid (1000.0, 480) == 960.0);
    CHECK (snapTickToGrid (1300.0, 480) == 1440.0);
    CHECK (snapTickToGrid (960.0, 480) == 960.0);
    CHECK (snapTickToGrid (100.0, 480) == 0.0);
    CHECK (snapTickToGrid (-50.0, 480) == 0.0);
    CHECK (snapTickToGrid (1000.0, 0) == 1000.0);    // grid Off
    CHECK (snapTickToGrid (1000.0, -1) == 1000.0);
}

TEST_CASE ("GridSnap: stepping goes to the next or previous grid line, from on or between lines, stopping at 0", "[grid-snap]")
{
    CHECK (stepTickByGrid (960.0, 480, +1) == 1440.0);   // on a line: one grid on
    CHECK (stepTickByGrid (960.0, 480, -1) == 480.0);
    CHECK (stepTickByGrid (1000.0, 480, +1) == 1440.0);  // between lines: the next line, not +480
    CHECK (stepTickByGrid (1000.0, 480, -1) == 960.0);
    CHECK (stepTickByGrid (0.0, 480, -1) == 0.0);        // the start is the floor
    CHECK (stepTickByGrid (100.0, 480, -1) == 0.0);
    CHECK (stepTickByGrid (0.0, 480, +1) == 480.0);
    CHECK (stepTickByGrid (1000.0, 0, +1) == 1000.0);    // grid Off: nowhere to step
}
