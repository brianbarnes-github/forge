#include "TempoGraphComponent.h"
#include "ConductorMarkers.h"
#include "SongsmithColours.h"

#include <algorithm>
#include <cmath>

namespace lotro
{
    namespace
    {
        constexpr int pointRadius = 5;
        constexpr int plotMarginY = 8;

        std::vector<TempoMeterEvent> tempoEntries (const std::vector<TempoMeterEvent>& in)
        {
            std::vector<TempoMeterEvent> out;
            for (const auto& e : in)
                if (e.kind == TempoMeterKind::tempo)
                    out.push_back (e);
            return out;
        }

        bool sameEvents (const std::vector<TempoMeterEvent>& a, const std::vector<TempoMeterEvent>& b)
        {
            if (a.size() != b.size())
                return false;
            for (size_t i = 0; i < a.size(); ++i)
                if (a[i].kind != b[i].kind || a[i].tick != b[i].tick || a[i].bpm != b[i].bpm
                    || a[i].numerator != b[i].numerator || a[i].denominator != b[i].denominator)
                    return false;
            return true;
        }

        // Smallest "round" step whose spacing is at least 28 px (else the largest).
        double gridStepFor (const BpmRange& range, int plotHeight)
        {
            const double span = range.hi - range.lo;
            const double steps[] = { 10, 20, 25, 50, 100, 200 };
            for (const double s : steps)
                if (plotHeight <= 0 || span <= 0.0 || s * plotHeight / span >= 28.0)
                    return s;
            return 200;
        }
    }

    TempoGraphComponent::TempoGraphComponent (SongDocument& document, const TimelineViewState& viewIn)
        : doc (document), view (viewIn)
    {
        setOpaque (true);
    }

    void TempoGraphComponent::requestRepaint()
    {
        ++repaintRequests;
        repaint();
    }

    void TempoGraphComponent::setEvents (std::vector<TempoMeterEvent> newEvents)
    {
        if (sameEvents (events, newEvents))
            return;
        events = std::move (newEvents);
        range  = bpmRangeFor (events);
        requestRepaint();
    }

    void TempoGraphComponent::setSelected (std::vector<TempoMeterEvent> selected)
    {
        auto tempos = tempoEntries (selected);
        if (sameEvents (selectedTempos, tempos))
            return;
        selectedTempos = std::move (tempos);
        requestRepaint();
    }

    int TempoGraphComponent::xForTick (int tick) const { return view.xForTick (tick); }

    juce::Rectangle<int> TempoGraphComponent::plotArea() const
    {
        return getLocalBounds().reduced (0, plotMarginY);
    }

    int TempoGraphComponent::yForBpm (double bpm) const
    {
        const auto plot = plotArea();
        return bpmToY (range, bpm, plot.getY(), plot.getHeight());
    }

    void TempoGraphComponent::paint (juce::Graphics& g)
    {
        ++paintCount;
        g.fillAll (juce::Colour (SongsmithColours::background));

        const auto tempos = tempoEntries (events);
        if (! doc.hasTimeBase() || tempos.empty())
            return;

        const auto plot = plotArea();
        const auto clip = g.getClipBounds();
        const int  right = getWidth();

        // Gridlines at round BPM values, labelled at the left edge.
        {
            const double step = gridStepFor (range, plot.getHeight());
            const juce::Font font (juce::FontOptions (10.0f));
            g.setFont (font);
            for (double bpm = std::ceil (range.lo / step) * step; bpm <= range.hi; bpm += step)
            {
                const int y = yForBpm (bpm);
                if (y < clip.getY() - 12 || y > clip.getBottom() + 12)
                    continue;
                g.setColour (juce::Colour (SongsmithColours::trackDivider));
                g.fillRect (0, y, right, 1);
                g.setColour (juce::Colour (SongsmithColours::textMuted));
                const auto text = juce::String (formatMarkerBpm (bpm));
                g.drawText (text, 2, y - 11, juce::GlyphArrangement::getStringWidthInt (font, text) + 2, 10,
                            juce::Justification::centredLeft, false);
            }
        }

        // The stepped line: hold the value to the next point, then step vertically.
        g.setColour (juce::Colour (SongsmithColours::conductorTempo));
        for (size_t i = 0; i < tempos.size(); ++i)
        {
            const int x0 = xForTick (tempos[i].tick);
            const int y0 = yForBpm (tempos[i].bpm);
            const bool last = i + 1 == tempos.size();
            const int x1 = last ? std::max (right, x0) : xForTick (tempos[i + 1].tick);
            const int y1 = last ? y0 : yForBpm (tempos[i + 1].bpm);
            if (std::max (x0, x1) < clip.getX() || std::min (x0, x1) > clip.getRight())
                continue;
            g.fillRect (std::min (x0, x1), y0, std::abs (x1 - x0) + 1, 1);
            g.fillRect (x1, std::min (y0, y1), 1, std::abs (y1 - y0) + 1);
        }

        // Points; selected ones are filled.
        for (const auto& t : tempos)
        {
            const int x = xForTick (t.tick), y = yForBpm (t.bpm);
            if (x + pointRadius < clip.getX() || x - pointRadius > clip.getRight())
                continue;
            const bool selected = std::any_of (selectedTempos.begin(), selectedTempos.end(),
                                               [&] (const TempoMeterEvent& s) { return s.tick == t.tick; });
            const auto circle = juce::Rectangle<float> ((float) (x - pointRadius), (float) (y - pointRadius),
                                                        (float) (2 * pointRadius), (float) (2 * pointRadius));
            if (selected)
            {
                g.setColour (juce::Colour (SongsmithColours::eventMarker));
                g.fillEllipse (circle);
            }
            g.setColour (juce::Colour (SongsmithColours::conductorTempo));
            g.drawEllipse (circle, 1.5f);
        }

        // BPM labels above the points, placed by the same rule as the Conductor row.
        {
            const juce::Font font (juce::FontOptions (10.0f));
            g.setFont (font);
            const auto placed = placeMarkers (conductorMarkers (tempos),
                                              [this] (int tick) { return xForTick (tick); },
                                              [&font] (const std::string& s)
                                              { return juce::GlyphArrangement::getStringWidthInt (font, juce::String::fromUTF8 (s.c_str())); },
                                              3, 4, clip.getRight() + 64);
            g.setColour (juce::Colour (SongsmithColours::text));
            for (size_t i = 0; i < placed.size(); ++i)
            {
                if (! placed[i].labelShown)
                    continue;
                const int y = yForBpm (tempos[i].bpm);
                g.drawText (juce::String::fromUTF8 (placed[i].marker.label.c_str()),
                            placed[i].x + 3, y - pointRadius - 12, placed[i].labelWidth + 2, 11,
                            juce::Justification::centredLeft, false);
            }
        }
    }
}
