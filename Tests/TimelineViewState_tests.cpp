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

TEST_CASE ("TimelineViewState: fitToWidth scales so totalTicks exactly fills widthPixels and resets scroll",
           "[timeline-view-state]")
{
    TimelineViewState view;
    view.setPixelsPerTick (0.1);
    view.setScrollOffsetTicks (500.0);

    view.fitToWidth (1000.0, 200);

    CHECK (view.getPixelsPerTick() == Catch::Approx (0.2));
    CHECK (view.getScrollOffsetTicks() == Catch::Approx (0.0));
    CHECK (view.xForTick (0) == 0);
    CHECK (view.xForTick (1000) == 200);
}

TEST_CASE ("TimelineViewState: fitToWidth clamps to the same sane pixels-per-tick range as zoomBy",
           "[timeline-view-state]")
{
    TimelineViewState tiny;
    tiny.fitToWidth (1.0, 10000); // would need 10000 px/tick without clamping
    CHECK (tiny.getPixelsPerTick() <= 2.5);

    TimelineViewState huge;
    huge.fitToWidth (10000000.0, 10); // would need 0.000001 px/tick without clamping
    CHECK (huge.getPixelsPerTick() >= 0.001);
}

TEST_CASE ("TimelineViewState: fitToWidth is a no-op for a zero or negative tick range",
           "[timeline-view-state]")
{
    TimelineViewState view;
    view.setPixelsPerTick (0.4);
    view.setScrollOffsetTicks (25.0);

    view.fitToWidth (0.0, 200);
    CHECK (view.getPixelsPerTick() == Catch::Approx (0.4));
    CHECK (view.getScrollOffsetTicks() == Catch::Approx (25.0));

    view.fitToWidth (-100.0, 200);
    CHECK (view.getPixelsPerTick() == Catch::Approx (0.4));
    CHECK (view.getScrollOffsetTicks() == Catch::Approx (25.0));
}
