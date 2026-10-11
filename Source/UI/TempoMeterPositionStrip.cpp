#include "UI/TempoMeterPositionStrip.h"

#include "UI/SongDocument.h"
#include "UI/SongsmithColours.h"

#include <algorithm>
#include <cmath>

namespace lotro
{

int stripEndTick (int lastNoteEnd, const std::vector<TempoMeterEvent>& events,
                  const std::vector<MeterSegment>& segments, int ticksPerQuarter)
{
    const double barTicks = segments.empty() ? 4.0 * (double) std::max (ticksPerQuarter, 1)
                                             : segments.front().ticksPerBar;
    int end = (int) std::llround (4.0 * barTicks);
    end = std::max (end, lastNoteEnd);
    for (const auto& e : events)
        end = std::max (end, e.tick);
    return end;
}

double stripTickToX (int tick, int endTick, double left, double width)
{
    if (endTick <= 0)
        return left;
    return left + (double) tick / (double) endTick * width;
}

int stripXToTick (double x, int endTick, double left, double width)
{
    if (width <= 0.0)
        return 0;
    return (int) std::llround ((x - left) / width * (double) endTick);
}

std::vector<int> stripBarTicks (const std::vector<MeterSegment>& segments, int endTick,
                                double width, double minPixels)
{
    std::vector<int> bars;   // every bar start in [0, endTick]
    for (size_t i = 0; i < segments.size(); ++i)
    {
        const auto& seg = segments[i];
        if (seg.ticksPerBar <= 0.0)
            continue;
        const double limit = i + 1 < segments.size() ? std::min (segments[i + 1].startTick, (double) endTick + 1.0)
                                                     : (double) endTick + 1.0;
        for (double t = seg.startTick; t < limit; t += seg.ticksPerBar)
            bars.push_back ((int) std::llround (t));
    }
    if (bars.size() < 2 || endTick <= 0 || width <= 0.0)
        return bars;

    // Smallest 1-2-5 step whose bars (on average) clear minPixels.
    const double perBar = width / (double) bars.size();
    size_t stride = 1;
    for (size_t decade = 1;; decade *= 10)
    {
        bool found = false;
        for (const size_t m : { 1, 2, 5 })
        {
            stride = decade * m;
            if ((double) stride * perBar >= minPixels) { found = true; break; }
        }
        if (found)
            break;
    }

    std::vector<int> kept;
    for (size_t i = 0; i < bars.size(); i += stride)
        kept.push_back (bars[i]);
    return kept;
}

int lastNoteEndTick (const SongDocument& doc)
{
    int lastTick = 0;
    for (int t = 0; t < doc.getNumTracks(); ++t)
    {
        const auto notes = SongDocument::getNotesNode (doc.getTrack (t));
        for (int n = 0; n < notes.getNumChildren(); ++n)
        {
            const auto note = notes.getChild (n);
            lastTick = std::max (lastTick, (int) note.getProperty (SongIDs::startTick)
                                           + (int) note.getProperty (SongIDs::durationTicks));
        }
    }
    return lastTick;
}

PositionStrip::PositionStrip()
{
    setInterceptsMouseClicks (false, false);
    setOpaque (false);
}

void PositionStrip::update (std::vector<TempoMeterEvent> newEvents, const std::vector<MeterChange>& meterMap,
                            int ticksPerQuarter, int lastNoteEnd, std::optional<int> selectedTick)
{
    events = std::move (newEvents);
    segments = meterSegments (meterMap, ticksPerQuarter);
    selected = selectedTick;
    end = stripEndTick (lastNoteEnd, events, segments, ticksPerQuarter);
    repaint();
}

float PositionStrip::xForTick (int tick) const
{
    return (float) stripTickToX (tick, end, (double) inset, (double) (getWidth() - 2 * inset));
}

void PositionStrip::paint (juce::Graphics& g)
{
    const int barTop = 11, barHeight = 6;
    const double width = (double) (getWidth() - 2 * inset);

    g.setColour (juce::Colour (SongsmithColours::panelHeader));
    g.fillRect (inset, barTop, getWidth() - 2 * inset, barHeight);

    g.setColour (juce::Colour (SongsmithColours::textMuted).withAlpha (0.6f));
    for (const int tick : stripBarTicks (segments, end, width))
        g.fillRect ((int) std::lround (xForTick (tick)), barTop, 1, barHeight);

    for (const auto& e : events)
    {
        const bool tempo = e.kind == TempoMeterKind::tempo;
        g.setColour (juce::Colour (tempo ? SongsmithColours::conductorTempo : SongsmithColours::conductorMeter));
        const int x = (int) std::lround (xForTick (e.tick));
        g.fillRect (x - 1, tempo ? 4 : barTop + barHeight, 2, 7);
    }

    if (selected)
    {
        g.setColour (juce::Colour (SongsmithColours::eventMarker));
        g.fillRect ((int) std::lround (xForTick (*selected)) - 1, 0, 2, getHeight());
    }
}

} // namespace lotro
