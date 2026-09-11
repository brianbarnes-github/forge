// Verifies PianoRollGeometry's pixel<->model coordinate math: tick<->x and
// pitch<->y round-trips at several zoom/scroll values, edge cases, and
// fitToContent placing a note source's full extent inside a given viewport.

#include "UI/PianoRollGeometry.h"

#include <catch2/catch_test_macros.hpp>

using namespace lotro;

TEST_CASE ("PianoRollGeometry: xForTick/tickForX round-trip at 1 pixel per tick, no scroll", "[piano-roll]")
{
    PianoRollGeometry geometry;
    geometry.setTicksPerQuarter (480);
    geometry.setPixelsPerQuarterNote (480.0);   // 1 px/tick
    geometry.setVisibleTickRange ({ 0.0, 1920.0 });

    const int gutter = geometry.getKeyboardGutterWidth();

    CHECK (geometry.xForTick (0) == gutter);
    CHECK (geometry.xForTick (240) == gutter + 240);
    CHECK (geometry.tickForX (gutter + 240) == 240);
    CHECK (geometry.tickForX (geometry.xForTick (960)) == 960);
}

TEST_CASE ("PianoRollGeometry: xForTick/tickForX round-trip under zoom and a scrolled window", "[piano-roll]")
{
    PianoRollGeometry geometry;
    geometry.setTicksPerQuarter (960);
    geometry.setPixelsPerQuarterNote (30.0);        // zoomed out: 32 ticks/px
    geometry.setVisibleTickRange ({ 4800.0, 24000.0 }); // scrolled well past tick 0

    for (int tick : { 4800, 5760, 9600, 19200 })
        CHECK (geometry.tickForX (geometry.xForTick (tick)) == tick);
}

TEST_CASE ("PianoRollGeometry: tick 0 maps exactly to the gutter edge when visible range starts at 0", "[piano-roll]")
{
    PianoRollGeometry geometry;
    geometry.setTicksPerQuarter (480);
    geometry.setPixelsPerQuarterNote (100.0);
    geometry.setVisibleTickRange ({ 0.0, 4800.0 });

    CHECK (geometry.xForTick (0) == geometry.getKeyboardGutterWidth());
}

TEST_CASE ("PianoRollGeometry: yForPitch/pitchForY round-trip at several top-pitch values", "[piano-roll]")
{
    PianoRollGeometry geometry;
    const int rowHeight = geometry.getRowHeight();

    geometry.setTopPitch (84);   // C6, per the mockup
    for (int pitch : { 84, 72, 60, 48, 36 })
        CHECK (geometry.pitchForY (geometry.yForPitch (pitch)) == pitch);

    // Higher pitch draws at a smaller y (nearer the top of the visible area).
    CHECK (geometry.yForPitch (84) < geometry.yForPitch (72));
    CHECK (geometry.yForPitch (84) == 0);
    CHECK (geometry.yForPitch (72) == 12 * rowHeight);
}

TEST_CASE ("PianoRollGeometry: pitchForY below the visible top returns a pitch above topPitch, not garbage", "[piano-roll]")
{
    PianoRollGeometry geometry;
    geometry.setTopPitch (60);

    // A y coordinate above the component (negative) corresponds to a pitch
    // higher than topPitch — still a well-defined answer, not a crash/clamp.
    CHECK (geometry.pitchForY (-geometry.getRowHeight()) == 61);
}

TEST_CASE ("PianoRollGeometry: noteBounds places a note's rectangle from its tick/pitch", "[piano-roll]")
{
    PianoRollGeometry geometry;
    geometry.setTicksPerQuarter (480);
    geometry.setPixelsPerQuarterNote (480.0);   // 1 px/tick
    geometry.setVisibleTickRange ({ 0.0, 1920.0 });
    geometry.setTopPitch (72);

    PianoRollNote note;
    note.pitch = 60;
    note.startTick = 240;
    note.durationTicks = 480;

    auto bounds = geometry.noteBounds (note);
    CHECK (bounds.x == geometry.getKeyboardGutterWidth() + 240);
    CHECK (bounds.width == 480);
    CHECK (bounds.y == geometry.yForPitch (60));
    CHECK (bounds.height == geometry.getRowHeight());
}

TEST_CASE ("PianoRollGeometry: fitToContent places the whole tick/pitch range inside the viewport", "[piano-roll]")
{
    // A track spanning ticks [0, 3840) (4 quarters at 960 tpq) and pitches
    // [55, 68) (13 semitones) — deliberately not a round/trivial span so a
    // fitToContent that just hardcodes a zoom factor would fail this.
    const juce::Range<int> tickRange (0, 3840);
    const juce::Range<int> pitchRange (55, 68);
    const int ticksPerQuarter = 960;
    const int viewportWidth = 800;
    const int viewportHeight = 300;

    auto geometry = PianoRollGeometry::fitToContent (tickRange, pitchRange, ticksPerQuarter,
                                                       viewportWidth, viewportHeight);

    const int gutter = geometry.getKeyboardGutterWidth();
    const int xStart = geometry.xForTick (tickRange.getStart());
    const int xEnd = geometry.xForTick (tickRange.getEnd());
    CHECK (xStart >= gutter);
    CHECK (xStart <= viewportWidth);
    CHECK (xEnd >= gutter);
    CHECK (xEnd <= viewportWidth);

    // Highest pitch (67, the last one inside the half-open [55,68) range)
    // must land at or above the top of the viewport; lowest pitch (55) must
    // land within the viewport's height.
    const int yHighest = geometry.yForPitch (pitchRange.getEnd() - 1);
    const int yLowest = geometry.yForPitch (pitchRange.getStart());
    CHECK (yHighest >= 0);
    CHECK (yHighest <= viewportHeight);
    CHECK (yLowest >= 0);
    CHECK (yLowest <= viewportHeight);
    CHECK (yLowest > yHighest);
}

TEST_CASE ("PianoRollGeometry: fitToContent on an empty source doesn't divide by zero or crash", "[piano-roll]")
{
    const juce::Range<int> emptyTicks (0, 0);
    const juce::Range<int> emptyPitches (0, 0);

    auto geometry = PianoRollGeometry::fitToContent (emptyTicks, emptyPitches, 480, 800, 300);

    CHECK (geometry.getPixelsPerQuarterNote() > 0.0);
    CHECK (geometry.xForTick (0) == geometry.getKeyboardGutterWidth());
}
