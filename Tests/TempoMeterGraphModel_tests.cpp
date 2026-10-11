// TempoMeterGraphModel: BPM/pixel mapping, tick snapping and hit-testing for the editor graph.

#include "UI/TempoMeterGraphModel.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <vector>

using namespace lotro;
using Catch::Approx;

namespace
{
TempoMeterEvent tempoAt (int tick, double bpm)
{
    TempoMeterEvent e;
    e.kind = TempoMeterKind::tempo;
    e.tick = tick;
    e.bpm  = bpm;
    return e;
}

TempoMeterEvent meterAt (int tick)
{
    TempoMeterEvent e;
    e.kind = TempoMeterKind::meter;
    e.tick = tick;
    return e;
}
} // namespace

TEST_CASE ("bpmRangeFor: empty gives the default range", "[tempo-graph-model]")
{
    const auto r = bpmRangeFor ({});
    CHECK (r.lo == Approx (60.0));
    CHECK (r.hi == Approx (180.0));
}

TEST_CASE ("bpmRangeFor: padding around the tempos", "[tempo-graph-model]")
{
    auto r = bpmRangeFor ({ tempoAt (0, 120.0) });
    CHECK (r.lo == Approx (90.0));
    CHECK (r.hi == Approx (150.0));

    r = bpmRangeFor ({ tempoAt (0, 200.0) });
    CHECK (r.lo == Approx (150.0));
    CHECK (r.hi == Approx (250.0));
}

TEST_CASE ("bpmRangeFor: narrow ranges widen symmetrically to 40 tall", "[tempo-graph-model]")
{
    // 50 -> 37.5..62.5 (25 tall), centred on 50 -> 30..70
    auto r = bpmRangeFor ({ tempoAt (0, 50.0) });
    CHECK (r.lo == Approx (30.0));
    CHECK (r.hi == Approx (70.0));

    r = bpmRangeFor ({ tempoAt (0, 50.0), tempoAt (480, 52.0) });
    CHECK (r.hi - r.lo == Approx (40.0));
    CHECK (r.lo < 50.0);
    CHECK (r.hi > 52.0);
}

TEST_CASE ("bpmRangeFor: lo never below 1, height preserved", "[tempo-graph-model]")
{
    const auto r = bpmRangeFor ({ tempoAt (0, 10.0), tempoAt (480, 11.0) });
    CHECK (r.lo == Approx (1.0));
    CHECK (r.hi - r.lo == Approx (40.0));
    CHECK (r.hi > 11.0);
}

TEST_CASE ("bpmRangeFor: meter events are ignored", "[tempo-graph-model]")
{
    CHECK (bpmRangeFor ({ meterAt (0) }).lo == Approx (60.0));
    const auto r = bpmRangeFor ({ meterAt (0), tempoAt (0, 120.0), meterAt (1920) });
    CHECK (r.lo == Approx (90.0));
    CHECK (r.hi == Approx (150.0));
}

TEST_CASE ("bpmToY and yToBpm map the plot", "[tempo-graph-model]")
{
    const BpmRange r { 60.0, 180.0 };
    CHECK (bpmToY (r, 180.0, 10, 120) == 10);
    CHECK (bpmToY (r, 60.0, 10, 120) == 130);
    CHECK (bpmToY (r, 120.0, 10, 120) == 70);
    CHECK (yToBpm (r, 70, 10, 120) == Approx (120.0));
    CHECK (yToBpm (r, 69, 10, 120) == std::round (yToBpm (r, 69, 10, 120)));
    CHECK (yToBpm (r, 69, 10, 120) == Approx (121.0));

    // a 3.33 BPM-per-pixel scale: 1000 - 1/300*1000 = 996.67 -> 997
    const BpmRange wide { 0.0, 1000.0 };
    CHECK (yToBpm (wide, 1, 0, 300) == Approx (997.0));

    CHECK (yToBpm (r, -5000, 10, 120) == Approx (1000.0));
    CHECK (yToBpm (r, 5000, 10, 120) == Approx (std::ceil (minBpm())));
}

TEST_CASE ("yToBpm: degenerate plot height returns lo's clamp", "[tempo-graph-model]")
{
    CHECK (yToBpm ({ 60.4, 180.0 }, 50, 10, 0) == Approx (60.0));
    CHECK (yToBpm ({ 70.0, 180.0 }, 50, 10, -5) == Approx (70.0));
    CHECK (yToBpm ({ 0.2, 180.0 }, 50, 10, 0) == Approx (std::ceil (minBpm())));
}

TEST_CASE ("snapToBeat: 4/4", "[tempo-graph-model]")
{
    const auto segs = meterSegments ({ { 0, 4, 4 } }, 480);
    CHECK (snapToBeat (segs, 0) == 0);
    CHECK (snapToBeat (segs, 239) == 0);
    CHECK (snapToBeat (segs, 240) == 480);
    CHECK (snapToBeat (segs, 700) == 480);
    CHECK (snapToBeat (segs, 1000) == 960);
    CHECK (snapToBeat (segs, -50) == 0);
}

TEST_CASE ("snapToBeat: beats are measured from the segment start", "[tempo-graph-model]")
{
    const auto a = meterSegments ({ { 0, 4, 4 }, { 1920, 3, 4 } }, 480);
    CHECK (snapToBeat (a, 1930) == 1920);

    // 6/8 at ppq 480: beat = 240, starting at 1920
    const auto b = meterSegments ({ { 0, 4, 4 }, { 1920, 6, 8 } }, 480);
    CHECK (snapToBeat (b, 2050) == 2160);

    // segment starting off the 480 grid: beats are 1000, 1480, 1960...
    const auto c = meterSegments ({ { 0, 4, 4 }, { 1000, 4, 4 } }, 480);
    CHECK (snapToBeat (c, 1500) == 1480);
}

TEST_CASE ("snapToBeat: never past the segment end", "[tempo-graph-model]")
{
    // second segment starts at 1900; 1890 would round to 1920, past the end -> last beat of the
    // first segment (beats 0, 480, 960, 1440) = 1440
    const auto segs = meterSegments ({ { 0, 4, 4 }, { 1900, 3, 4 } }, 480);
    CHECK (snapToBeat (segs, 1890) == 1440);
    CHECK (snapToBeat (segs, 1900) == 1900);

    // segment boundary exactly on a beat: 1900 rounds to 1920 (the next segment), clamps to 1440
    const auto onBeat = meterSegments ({ { 0, 4, 4 }, { 1920, 3, 4 } }, 480);
    CHECK (snapToBeat (onBeat, 1900) == 1440);
}

TEST_CASE ("snapMeterTick: bars of the meters without the moved one", "[tempo-graph-model]")
{
    const std::vector<MeterChange> m { { 0, 4, 4 }, { 3840, 3, 4 } };
    CHECK (snapMeterTick (m, 480, 3840, 3000) == 3840); // 4/4 bars 1920 (1080 away) vs 3840 (840 away)
    CHECK (snapMeterTick (m, 480, 3840, 2000) == 1920);
    CHECK (snapMeterTick (m, 480, 3840, -300) == 0);
    // with the 3/4 meter in force the nearest bar to 5000 would be 5280; without it, 4/4 gives 5760
    CHECK (snapMeterTick (m, 480, 3840, 5000) == 5760);
}

TEST_CASE ("nearestPoint: radius and tie rules", "[tempo-graph-model]")
{
    CHECK_FALSE (nearestPoint ({}, 0, 0, 6).has_value());

    const std::vector<std::pair<int, int>> one { { 6, 0 } };
    CHECK (nearestPoint (one, 0, 0, 6) == 0u);
    CHECK_FALSE (nearestPoint ({ { 7, 0 } }, 0, 0, 6).has_value());
    CHECK_FALSE (nearestPoint ({ { 5, 5 } }, 0, 0, 6).has_value()); // Euclidean 7.07

    CHECK (nearestPoint ({ { 3, 0 }, { -3, 0 } }, 0, 0, 6) == 1u); // tie -> later
    CHECK (nearestPoint ({ { 3, 0 }, { 0, 1 }, { 2, 2 } }, 0, 0, 6) == 1u); // nearest beats later
}
