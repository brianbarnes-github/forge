#include "UI/TimelineViewState.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

using namespace lotro;

TEST_CASE ("TimelineViewState: xForTick/tickForX round-trip at default zoom", "[timeline-view-state]")
{
    TimelineViewState view;
    view.setPixelsPerTick (0.1);
    view.setScrollOffsetTicks (0.0);

    CHECK (view.xForTick (0) == 0);
    CHECK (view.xForTick (100) == 10);
    CHECK (view.tickForX (10) == 100);
}

TEST_CASE ("TimelineViewState: scrollByPixels shifts the tick origin and never goes negative", "[timeline-view-state]")
{
    TimelineViewState view;
    view.setPixelsPerTick (0.1);
    view.setScrollOffsetTicks (50.0);

    view.scrollByPixels (10); // +10px at 0.1 px/tick == +100 ticks
    CHECK (view.getScrollOffsetTicks() == Catch::Approx (150.0));

    view.scrollByPixels (-10000);
    CHECK (view.getScrollOffsetTicks() == Catch::Approx (0.0));
}

TEST_CASE ("TimelineViewState: zoomBy keeps the tick under the anchor pixel fixed", "[timeline-view-state]")
{
    TimelineViewState view;
    view.setPixelsPerTick (0.1);
    view.setScrollOffsetTicks (0.0);

    const int anchorX = 40;
    const int anchorTickBefore = view.tickForX (anchorX);

    view.zoomBy (2.0, anchorX);

    CHECK (view.getPixelsPerTick() == Catch::Approx (0.2));
    CHECK (view.tickForX (anchorX) == anchorTickBefore);
}

TEST_CASE ("TimelineViewState: zoomBy clamps to sane min/max pixels-per-tick", "[timeline-view-state]")
{
    TimelineViewState view;
    view.setPixelsPerTick (0.1);

    for (int i = 0; i < 100; ++i)
        view.zoomBy (0.5, 0);
    CHECK (view.getPixelsPerTick() >= 0.001);

    view.setPixelsPerTick (0.1);
    for (int i = 0; i < 100; ++i)
        view.zoomBy (2.0, 0);
    CHECK (view.getPixelsPerTick() <= 2.5);
}
