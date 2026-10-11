// TempoMeterPositionStrip: the Tempo/Meter window's "where in the song" strip.

#include "UI/MeterSegments.h"
#include "UI/TempoEdit.h"
#include "UI/TempoMeterPositionStrip.h"
#include "UI/SongsmithColours.h"

#include <catch2/catch_test_macros.hpp>

#include <juce_gui_basics/juce_gui_basics.h>

#include <algorithm>
#include <cmath>
#include <vector>

using namespace lotro;

namespace
{
    TempoMeterEvent tempoEvent (int tick)
    {
        TempoMeterEvent e;
        e.kind = TempoMeterKind::tempo;
        e.tick = tick;
        e.bpm = 100.0;
        return e;
    }

    TempoMeterEvent meterEvent (int tick)
    {
        TempoMeterEvent e;
        e.kind = TempoMeterKind::meter;
        e.tick = tick;
        e.numerator = 3;
        e.denominator = 4;
        return e;
    }

    std::vector<MeterSegment> fourFour() { return meterSegments ({ { 0, 4, 4 } }, 480); }

    bool near (juce::Colour a, juce::Colour b, int tol = 3)
    {
        return std::abs (a.getRed() - b.getRed()) <= tol && std::abs (a.getGreen() - b.getGreen()) <= tol
            && std::abs (a.getBlue() - b.getBlue()) <= tol;
    }

    // x positions of pixels in row `y` that are the event-marker green.
    std::vector<int> greenColumns (const juce::Image& image, int y)
    {
        std::vector<int> xs;
        for (int x = 0; x < image.getWidth(); ++x)
            if (near (image.getPixelAt (x, y), juce::Colour (SongsmithColours::eventMarker)))
                xs.push_back (x);
        return xs;
    }

    juce::Image render (PositionStrip& strip)
    {
        juce::Image image (juce::Image::ARGB, strip.getWidth(), strip.getHeight(), true, juce::SoftwareImageType());
        juce::Graphics g (image);
        strip.paintEntireComponent (g, false);
        return image;
    }
}

TEST_CASE ("PositionStrip: the end tick covers notes and events, with a four-bar minimum", "[position-strip]")
{
    const auto segs = fourFour();
    CHECK (stripEndTick (0, {}, segs, 480) == 4 * 1920);                          // empty song
    CHECK (stripEndTick (100, { tempoEvent (0) }, segs, 480) == 4 * 1920);
    CHECK (stripEndTick (20000, { tempoEvent (0) }, segs, 480) == 20000);         // a note past the end
    CHECK (stripEndTick (100, { tempoEvent (0), meterEvent (30000) }, segs, 480) == 30000);   // an event past it
    CHECK (stripEndTick (0, {}, {}, 480) == 4 * 4 * 480);                          // no segments: 4/4 assumed
}

TEST_CASE ("PositionStrip: tick to x is linear with exact endpoints and inverts", "[position-strip]")
{
    CHECK (stripTickToX (0, 7680, 8.0, 400.0) == 8.0);
    CHECK (stripTickToX (7680, 7680, 8.0, 400.0) == 408.0);
    CHECK (stripTickToX (3840, 7680, 8.0, 400.0) == 208.0);

    double previous = -1.0;
    for (int tick = 0; tick <= 7680; tick += 37)
    {
        const double x = stripTickToX (tick, 7680, 8.0, 400.0);
        CHECK (x > previous);
        previous = x;
        CHECK (std::abs (stripXToTick (x, 7680, 8.0, 400.0) - tick) <= 1);
    }
    CHECK (stripTickToX (5, 0, 8.0, 400.0) == 8.0);   // degenerate span does not divide by zero
}

TEST_CASE ("PositionStrip: bar ticks follow meter changes and thin out when crowded", "[position-strip]")
{
    // 4/4 for one bar (0..1920) then 3/4 (1440-tick bars) up to 5760.
    const auto segs = meterSegments ({ { 0, 4, 4 }, { 1920, 3, 4 } }, 480);
    const auto bars = stripBarTicks (segs, 5760, 1000.0);
    CHECK (bars == std::vector<int> { 0, 1920, 3360, 4800 });

    // 400 bars of 4/4 across 400 px: 1 px per bar, so only every Nth is kept (>= 4 px apart).
    const auto many = stripBarTicks (fourFour(), 400 * 1920, 400.0);
    REQUIRE (many.size() >= 2);
    CHECK (many.size() < 400);
    for (size_t i = 1; i < many.size(); ++i)
        CHECK ((many[i] - many[i - 1]) * 400.0 / (400 * 1920) >= 4.0);
    CHECK (many.front() == 0);
}

TEST_CASE ("PositionStrip: the selected event is a green line at its mapped x; none when unselected", "[position-strip]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    PositionStrip strip;
    strip.setSize (400, PositionStrip::height);
    const std::vector<TempoMeterEvent> events { tempoEvent (0), meterEvent (0), tempoEvent (3840) };
    const std::vector<MeterChange> meters { { 0, 4, 4 } };

    strip.update (events, meters, 480, 100, std::nullopt);
    CHECK (greenColumns (render (strip), 2).empty());

    strip.update (events, meters, 480, 100, 3840);
    REQUIRE (strip.endTick() == 4 * 1920);
    const auto expected = (double) PositionStrip::inset + 3840.0 / strip.endTick() * (400 - 2 * PositionStrip::inset);
    CHECK (std::abs (strip.xForTick (3840) - expected) < 0.01);

    const auto cols = greenColumns (render (strip), 2);
    REQUIRE (! cols.empty());
    const double centre = 0.5 * (cols.front() + cols.back() + 1);   // pixel columns cover [x, x+1)
    CHECK (std::abs (centre - expected) <= 1.0);
    CHECK (cols.back() - cols.front() <= 2);

    // A different tick lands somewhere else.
    strip.update (events, meters, 480, 100, 0);
    const auto cols0 = greenColumns (render (strip), 2);
    REQUIRE (! cols0.empty());
    CHECK (cols0.front() < cols.front() - 50);
}

TEST_CASE ("PositionStrip: tempo and meter events draw as coloured notches", "[position-strip]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    PositionStrip strip;
    strip.setSize (400, PositionStrip::height);
    strip.update ({ tempoEvent (1920), meterEvent (5760) }, { { 0, 4, 4 } }, 480, 100, std::nullopt);
    const auto image = render (strip);

    auto columnHas = [&] (float x, juce::uint32 colour)
    {
        for (int dx = -1; dx <= 1; ++dx)
            for (int y = 0; y < image.getHeight(); ++y)
                if (near (image.getPixelAt ((int) std::lround (x) + dx, y), juce::Colour (colour)))
                    return true;
        return false;
    };
    CHECK (columnHas (strip.xForTick (1920), SongsmithColours::conductorTempo));
    CHECK (columnHas (strip.xForTick (5760), SongsmithColours::conductorMeter));
    CHECK_FALSE (columnHas (strip.xForTick (1920), SongsmithColours::conductorMeter));
}
