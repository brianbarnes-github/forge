#include "UI/Playback/LoopRange.h"

#include <catch2/catch_test_macros.hpp>

using namespace lotro;

TEST_CASE ("normaliseLoopRange orders, clamps and rejects short ranges", "[loop]")
{
    auto r = normaliseLoopRange (960.0, 480.0, 4800.0, 24.0);
    REQUIRE (r.has_value());
    CHECK (r->startTick == 480.0);
    CHECK (r->endTick == 960.0);

    r = normaliseLoopRange (-100.0, 99999.0, 4800.0, 24.0);
    REQUIRE (r.has_value());
    CHECK (r->startTick == 0.0);
    CHECK (r->endTick == 4800.0);

    CHECK (! normaliseLoopRange (480.0, 480.0, 4800.0, 24.0).has_value());   // zero length
    CHECK (! normaliseLoopRange (480.0, 490.0, 4800.0, 24.0).has_value());   // shorter than minTicks
    CHECK (! normaliseLoopRange (5000.0, 6000.0, 4800.0, 24.0).has_value()); // both past the end -> empty after clamp
}

TEST_CASE ("snapLoopTick snaps to the nearest grid line unless unsnapped or grid off", "[loop]")
{
    CHECK (snapLoopTick (130.0, 120, false) == 120.0);
    CHECK (snapLoopTick (181.0, 120, false) == 240.0);
    CHECK (snapLoopTick (130.0, 120, true) == 130.0);   // Alt
    CHECK (snapLoopTick (130.0, 0, false) == 130.0);    // grid Off
}

TEST_CASE ("nextLoopStep splits the block at the loop end", "[loop]")
{
    const LoopSpan loop { 1.0, 2.0 };
    // Not crossing: whole block.
    auto s = nextLoopStep (1.2, 512, 48000.0, loop);
    CHECK (s.samples == 512);
    CHECK (! s.wraps);
    // Crossing: 10 ms (480 samples) before the end.
    s = nextLoopStep (1.99, 1000, 48000.0, loop);
    CHECK (s.samples == 480);
    CHECK (s.wraps);
    // At/after the end never wraps.
    s = nextLoopStep (2.0, 512, 48000.0, loop);
    CHECK (s.samples == 512);
    CHECK (! s.wraps);
    s = nextLoopStep (5.0, 512, 48000.0, loop);
    CHECK (! s.wraps);
    // Before the range: plays toward it, no wrap yet.
    s = nextLoopStep (0.5, 512, 48000.0, loop);
    CHECK (! s.wraps);
    // No loop.
    s = nextLoopStep (1.99, 1000, 48000.0, std::nullopt);
    CHECK (s.samples == 1000);
    CHECK (! s.wraps);
    // Progress is guaranteed: even a position a hair before the end renders >= 1 sample.
    s = nextLoopStep (1.9999999, 512, 48000.0, loop);
    CHECK (s.samples >= 1);
    CHECK (s.wraps);
}

TEST_CASE ("LoopSpanCell round-trips and clears", "[loop]")
{
    LoopSpanCell cell;
    CHECK (! cell.read().has_value());
    cell.publish (LoopSpan { 1.0, 3.0 });
    auto v = cell.read();
    REQUIRE (v.has_value());
    CHECK (v->startSeconds == 1.0);
    CHECK (v->endSeconds == 3.0);
    cell.publish (std::nullopt);
    CHECK (! cell.read().has_value());
}
