#include "UI/Playback/TimelineRulerMarks.h"

#include "UI/Playback/PlaybackSnapshot.h"
#include "UI/SongDocument.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>

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
    RulerGrid grid;
    grid.tempo = tempoMapFromDocument (doc);
    grid.meters = doc.getMeterChanges();
    if (! grid.meters.empty())
    {
        grid.meter.numerator = grid.meters.front().numerator;
        grid.meter.denominator = grid.meters.front().denominator;
    }
    grid.meter = sanitised (grid.meter);
    return grid;
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

    auto changes = grid.meters;
    if (changes.empty())
    {
        const auto meter = sanitised (grid.meter);
        changes.push_back ({ 0, meter.numerator, meter.denominator });
    }
    const auto segments = meterSegments (std::move (changes), grid.tempo.getTicksPerQuarter());
    if (segments.empty())
        return marks;

    double narrowestBar = segments.front().ticksPerBar;
    double narrowestBeat = segments.front().ticksPerBeat;
    for (const auto& s : segments)
    {
        narrowestBar = std::min (narrowestBar, s.ticksPerBar);
        narrowestBeat = std::min (narrowestBeat, s.ticksPerBeat);
    }

    const int barStep = barStepFor (narrowestBar * pixelsPerTick);
    const bool showBeats = barStep == 1 && narrowestBeat * pixelsPerTick >= minBeatPixels;
    const bool showMilliseconds = narrowestBar * pixelsPerTick * (double) barStep >= minMillisecondLabelPixels;

    const auto inRange = [&] (double tick) { return tick >= firstTick && tick <= lastTick; };

    for (size_t i = 0; i < segments.size(); ++i)
    {
        const auto& s = segments[i];
        const double end = i + 1 < segments.size() ? segments[i + 1].startTick : std::numeric_limits<double>::infinity();
        if (end <= firstTick || s.startTick > lastTick)
            continue;

        const long long firstK = std::max (0LL, (long long) std::floor ((firstTick - s.startTick) / s.ticksPerBar));
        for (long long k = firstK;; ++k)
        {
            const double barTick = s.startTick + (double) k * s.ticksPerBar;
            if (barTick >= end - 1e-9 || barTick > lastTick)
                break;

            const long long bar = s.firstBar + k;
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
                for (int beat = 1; beat < s.numerator; ++beat)
                {
                    const double beatTick = barTick + (double) beat * s.ticksPerBeat;
                    if (beatTick < end - 1e-9 && inRange (beatTick))
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
    }
    return marks;
}

} // namespace lotro
