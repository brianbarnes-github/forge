#include "UI/Playback/TimelineRulerMarks.h"

#include "UI/Playback/PlaybackSnapshot.h"
#include "UI/SongDocument.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace lotro
{

namespace
{
    RulerMeter sanitised (RulerMeter meter)
    {
        if (meter.numerator <= 0 || meter.denominator <= 0)
            return {};
        return meter;
    }

    // Smallest of 1, 2, 5, 10, 20, 50... bars that keeps labelled bar lines
    // at least minBarLabelPixels apart.
    int barStepFor (double barPixels)
    {
        for (long long decade = 1;; decade *= 10)
            for (const int multiple : { 1, 2, 5 })
                if (barPixels * (double) (multiple * decade) >= minBarLabelPixels)
                    return (int) (multiple * decade);
    }
}

RulerGrid rulerGridFromDocument (const SongDocument& doc)
{
    RulerMeter meter;
    const auto meterMap = doc.getMeterMapNode();
    if (meterMap.isValid() && meterMap.getNumChildren() > 0)
    {
        const auto first = meterMap.getChild (0);
        meter.numerator = (int) first.getProperty (SongIDs::numerator, 4);
        meter.denominator = (int) first.getProperty (SongIDs::denominator, 4);
    }
    return { tempoMapFromDocument (doc), sanitised (meter) };
}

std::string formatClock (double seconds, bool withMilliseconds)
{
    seconds = std::max (0.0, seconds);
    char text[32];

    if (withMilliseconds)
    {
        const long long totalMs = std::llround (seconds * 1000.0);
        const long long totalSeconds = totalMs / 1000;
        std::snprintf (text, sizeof text, "%lld:%02lld.%03lld", totalSeconds / 60, totalSeconds % 60, totalMs % 1000);
    }
    else
    {
        const long long totalSeconds = std::llround (seconds);
        std::snprintf (text, sizeof text, "%lld:%02lld", totalSeconds / 60, totalSeconds % 60);
    }
    return text;
}

std::vector<RulerMark> computeRulerMarks (double firstTick, double lastTick, double pixelsPerTick,
                                          const RulerGrid& grid)
{
    std::vector<RulerMark> marks;
    if (pixelsPerTick <= 0.0 || lastTick < firstTick)
        return marks;

    const auto meter = sanitised (grid.meter);
    const double ticksPerBeat = (double) grid.tempo.getTicksPerQuarter() * 4.0 / (double) meter.denominator;
    const double ticksPerBar = ticksPerBeat * (double) meter.numerator;
    if (ticksPerBar <= 0.0)
        return marks;

    const double beatPixels = ticksPerBeat * pixelsPerTick;
    const int barStep = barStepFor (ticksPerBar * pixelsPerTick);
    const bool showBeats = barStep == 1 && beatPixels >= minBeatPixels;
    const bool showMilliseconds = ticksPerBar * pixelsPerTick * (double) barStep >= minMillisecondLabelPixels;

    const auto inRange = [&] (double tick) { return tick >= firstTick && tick <= lastTick; };

    const long long firstBar = std::max (0LL, (long long) std::floor (firstTick / ticksPerBar));
    for (long long bar = firstBar; (double) bar * ticksPerBar <= lastTick; ++bar)
    {
        const double barTick = (double) bar * ticksPerBar;

        if (bar % barStep == 0 && inRange (barTick))
        {
            RulerMark mark;
            mark.kind = RulerMark::Kind::Bar;
            mark.tick = barTick;
            mark.label = std::to_string (bar + 1) + (showBeats ? ".1" : "");
            mark.timeLabel = formatClock (grid.tempo.ticksToSeconds (barTick), showMilliseconds);
            marks.push_back (std::move (mark));
        }

        if (showBeats)
        {
            for (int beat = 1; beat < meter.numerator; ++beat)
            {
                const double beatTick = barTick + (double) beat * ticksPerBeat;
                if (inRange (beatTick))
                {
                    RulerMark mark;
                    mark.kind = RulerMark::Kind::Beat;
                    mark.tick = beatTick;
                    mark.label = std::to_string (bar + 1) + "." + std::to_string (beat + 1);
                    marks.push_back (std::move (mark));
                }
            }
        }
    }
    return marks;
}

} // namespace lotro
