#pragma once

#include "UI/Playback/TempoMap.h"

#include <string>
#include <vector>

namespace lotro
{

class SongDocument;

// What the timing bar needs to know about the song: the tempo map (ticks <->
// clock time, which also carries the PPQ) and the first meter entry (the
// project's one-meter-timeline convention, as for the roll's gridlines).
struct RulerMeter
{
    int numerator = 4;
    int denominator = 4;
};

struct RulerGrid
{
    TempoMap tempo;
    RulerMeter meter;
};

struct RulerMark
{
    enum class Kind { Bar, Beat };

    Kind kind = Kind::Bar;
    double tick = 0.0;
    std::string label;       // "17", or "17.1" / "17.2" while beats are shown
    std::string timeLabel;   // clock time of a bar line; empty for a beat
};

// Minimum spacing, in pixels, between neighbouring labels / ticks.
constexpr double minBarLabelPixels = 48.0;           // between labelled bar lines
constexpr double minBeatPixels = 24.0;                // between beats, for beats to show
constexpr double minMillisecondLabelPixels = 80.0;   // between labelled bar lines, for m:ss.mmm

RulerGrid rulerGridFromDocument (const SongDocument& doc);

// "m:ss", or "m:ss.mmm" with milliseconds. Rounds to the displayed precision.
std::string formatClock (double seconds, bool withMilliseconds);

// The marks to draw for the ticks [firstTick, lastTick] at `pixelsPerTick`:
// bar lines (every 1, 2, 5, 10... bars so labels keep their spacing), each
// with its bar number and clock time; plus, once beats are far enough apart,
// a beat tick (labelled bar.beat, no clock time) between them.
std::vector<RulerMark> computeRulerMarks (double firstTick, double lastTick, double pixelsPerTick,
                                          const RulerGrid& grid);

} // namespace lotro
