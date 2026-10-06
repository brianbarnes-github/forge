#include "UI/Playback/TimelineRulerMarks.h"
#include "UI/SongDocument.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace lotro;
using Catch::Approx;

namespace
{
    // 4/4, 480 PPQ, 120 BPM: a bar is 1920 ticks = 2 s, a beat is 480 ticks.
    RulerGrid fourFour() { return { TempoMap ({}, 480), { 4, 4 } }; }

    std::vector<RulerMark> barsOf (const std::vector<RulerMark>& marks)
    {
        std::vector<RulerMark> bars;
        for (const auto& m : marks)
            if (m.kind == RulerMark::Kind::Bar)
                bars.push_back (m);
        return bars;
    }
}

TEST_CASE ("formatClock: m:ss, or m:ss.mmm with milliseconds", "[ruler]")
{
    CHECK (formatClock (0.0, false) == "0:00");
    CHECK (formatClock (62.4, false) == "1:02");   // rounds to the nearest second
    CHECK (formatClock (62.5, false) == "1:03");
    CHECK (formatClock (62.5, true) == "1:02.500");
    CHECK (formatClock (3.25, true) == "0:03.250");
    // Rounding carries into the seconds and minutes.
    CHECK (formatClock (59.9996, true) == "1:00.000");
    CHECK (formatClock (59.6, false) == "1:00");
}

TEST_CASE ("ruler marks: bar lines carry the bar number and clock time", "[ruler]")
{
    // 0.03 px/tick: a bar is 57.6 px (>= 48, so every bar is labelled), beats
    // are 14.4 px (too close to show), and 57.6 < 80 so no milliseconds.
    const auto marks = computeRulerMarks (0.0, 6000.0, 0.03, fourFour());
    REQUIRE (marks.size() == 4);   // bars 1..4 at ticks 0, 1920, 3840, 5760
    CHECK (marks[0].kind == RulerMark::Kind::Bar);
    CHECK (marks[0].tick == Approx (0.0));
    CHECK (marks[0].label == "1");
    CHECK (marks[0].timeLabel == "0:00");
    CHECK (marks[1].tick == Approx (1920.0));
    CHECK (marks[1].label == "2");
    CHECK (marks[1].timeLabel == "0:02");
    CHECK (marks[3].label == "4");
    CHECK (marks[3].timeLabel == "0:06");
}

TEST_CASE ("ruler marks: zoomed out, bar lines thin to every 1, 2, 5, 10... bars", "[ruler]")
{
    // 0.01 px/tick: a bar is 19.2 px; 2 bars = 38.4 (< 48), 5 bars = 96 px.
    const auto marks = computeRulerMarks (0.0, 20000.0, 0.01, fourFour());
    REQUIRE (marks.size() >= 3);
    CHECK (marks[0].label == "1");
    CHECK (marks[1].label == "6");
    CHECK (marks[1].tick == Approx (5 * 1920.0));
    CHECK (marks[2].label == "11");
    // 96 px between labelled bars is room enough for milliseconds.
    CHECK (marks[1].timeLabel == "0:10.000");
}

TEST_CASE ("ruler marks: zoomed in far enough, beats appear as ticks labelled bar.beat", "[ruler]")
{
    // 0.06 px/tick: a beat is 28.8 px (>= 24), a bar is 115.2 px (>= 80: milliseconds).
    const auto marks = computeRulerMarks (0.0, 2500.0, 0.06, fourFour());
    REQUIRE (marks.size() == 6);   // 1.1 1.2 1.3 1.4 2.1 2.2
    const char* labels[] = { "1.1", "1.2", "1.3", "1.4", "2.1", "2.2" };
    const double ticks[] = { 0, 480, 960, 1440, 1920, 2400 };
    for (size_t i = 0; i < marks.size(); ++i)
    {
        CHECK (marks[i].label == labels[i]);
        CHECK (marks[i].tick == Approx (ticks[i]));
    }
    CHECK (marks[0].kind == RulerMark::Kind::Bar);
    CHECK (marks[1].kind == RulerMark::Kind::Beat);
    CHECK (marks[4].kind == RulerMark::Kind::Bar);

    // The clock time rides on the beat-1 lines only.
    CHECK (marks[0].timeLabel == "0:00.000");
    CHECK (marks[1].timeLabel.empty());
    CHECK (marks[4].timeLabel == "0:02.000");
}

TEST_CASE ("ruler marks: only marks inside the visible tick range are returned", "[ruler]")
{
    const auto marks = computeRulerMarks (2000.0, 4000.0, 0.03, fourFour());
    REQUIRE (marks.size() == 1);   // only bar 3 (tick 3840); bar 2 (1920) is off the left edge
    CHECK (marks[0].label == "3");
}

TEST_CASE ("ruler marks: clock times follow tempo changes", "[ruler]")
{
    // 120 BPM to tick 1920, then 60 BPM: bar 2 at 2 s, bar 3 (1920 ticks later at 1 s/beat) at 6 s.
    const RulerGrid grid { TempoMap ({ { 1920, 60.0 } }, 480), { 4, 4 } };
    const auto bars = barsOf (computeRulerMarks (0.0, 4000.0, 0.03, grid));
    REQUIRE (bars.size() == 3);
    CHECK (bars[1].timeLabel == "0:02");
    CHECK (bars[2].timeLabel == "0:06");
}

TEST_CASE ("ruler marks: a beat is one note of the meter's denominator", "[ruler]")
{
    // 6/8 at 480 PPQ: 240-tick beats, six to a bar, bar = 1440 ticks. 0.12 px/tick -> 28.8 px beats.
    const RulerGrid grid { TempoMap ({}, 480), { 6, 8 } };
    const auto marks = computeRulerMarks (0.0, 1500.0, 0.12, grid);
    REQUIRE (marks.size() == 7);   // 1.1 .. 1.6, 2.1
    CHECK (marks[5].label == "1.6");
    CHECK (marks[5].tick == Approx (5 * 240.0));
    CHECK (marks[6].label == "2.1");
    CHECK (marks[6].tick == Approx (1440.0));
}

TEST_CASE ("ruler marks: degenerate input yields no marks", "[ruler]")
{
    CHECK (computeRulerMarks (0.0, 1000.0, 0.0, fourFour()).empty());
    CHECK (computeRulerMarks (1000.0, 0.0, 0.05, fourFour()).empty());
    const RulerGrid broken { TempoMap ({}, 480), { 0, 0 } };
    CHECK (! computeRulerMarks (0.0, 4000.0, 0.03, broken).empty());   // falls back to 4/4
}

TEST_CASE ("rulerGridFromDocument: a new document is 4/4 at 120 BPM", "[ruler]")
{
    SongDocument doc;
    const auto grid = rulerGridFromDocument (doc);
    CHECK (grid.meter.numerator == 4);
    CHECK (grid.meter.denominator == 4);
    CHECK (grid.tempo.ticksToSeconds ((double) grid.tempo.getTicksPerQuarter()) == Approx (0.5));
}

#include "UI/Playback/TimelineRuler.h"
#include "UI/SongsmithColours.h"

namespace
{
    // A ruler at 0.1 px/tick over a 4/4, 480 PPQ, 120 BPM grid: bar lines every
    // 192 px (tick 1920 -> x 192), beats every 48 px (tick 480 -> x 48).
    struct RulerFixture
    {
        juce::ScopedJuceInitialiser_GUI juceInit;
        TimelineRuler ruler { [] (int x) { return (double) x * 10.0; } };
        juce::Image image { juce::Image::ARGB, 500, TimelineRuler::height, true, juce::SoftwareImageType() };

        RulerFixture()
        {
            ruler.setMarks ([] (double tick) { return (int) (tick / 10.0); },
                            [] { return RulerGrid { TempoMap ({}, 480), { 4, 4 } }; });
            ruler.setSize (500, TimelineRuler::height);
        }

        void paint()
        {
            juce::Graphics g (image);
            ruler.paintEntireComponent (g, false);
        }

        bool isBackground (int x, int y) const
        {
            return image.getPixelAt (x, y) == juce::Colour (SongsmithColours::background).brighter (0.1f);
        }
    };
}

TEST_CASE ("TimelineRuler: it is two rows tall", "[ruler]")
{
    CHECK (TimelineRuler::height == 28);
}

TEST_CASE ("TimelineRuler: a bar line spans both rows, a beat tick only the top row", "[ruler]")
{
    RulerFixture f;
    f.paint();

    const int bottomRow = TimelineRuler::height - 4;   // well inside the lower row
    const int topRow = 10;

    CHECK_FALSE (f.isBackground (192, bottomRow));   // bar 2's line reaches the clock row
    CHECK_FALSE (f.isBackground (192, topRow));
    CHECK_FALSE (f.isBackground (48, topRow));       // beat 1.2's tick
    CHECK (f.isBackground (48, bottomRow));          // ...stops short of the clock row
    CHECK (f.isBackground (100, bottomRow));         // nothing between marks
}

TEST_CASE ("TimelineRuler: marks left of the content edge are not drawn", "[ruler]")
{
    RulerFixture f;
    f.ruler.setContentLeft (100);
    f.paint();

    CHECK (f.isBackground (48, 10));                  // beat 1.2 sits left of the edge
    CHECK_FALSE (f.isBackground (192, 10));           // bar 2 is inside
}

TEST_CASE ("TimelineRuler: without marks configured it just paints its background", "[ruler]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    TimelineRuler ruler ([] (int x) { return (double) x; });
    ruler.setSize (200, TimelineRuler::height);
    juce::Image image (juce::Image::ARGB, 200, TimelineRuler::height, true, juce::SoftwareImageType());
    juce::Graphics g (image);
    CHECK_NOTHROW (ruler.paintEntireComponent (g, false));
}
