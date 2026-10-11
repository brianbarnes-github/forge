#include "TempoGraphComponent.h"
#include "ConductorMarkers.h"
#include "SongsmithColours.h"

#include <algorithm>
#include <cmath>

namespace lotro
{
    namespace
    {
        constexpr int pointRadius   = 5;
        constexpr int plotMarginY   = 8;
        constexpr int hitRadius     = 6;   // px, Euclidean, around a point's centre
        constexpr int dragThreshold = 3;   // px of travel before a press becomes a drag

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
        setWantsKeyboardFocus (true);
    }

    void TempoGraphComponent::requestRepaint()
    {
        ++repaintRequests;
        repaint();
    }

    void TempoGraphComponent::setEvents (std::vector<TempoMeterEvent> newEvents)
    {
        // The dragged event vanished (undo / redo / New mid-drag): drop the drag, so the
        // coming mouse-up commits nothing.
        if (drag && std::none_of (newEvents.begin(), newEvents.end(), [this] (const TempoMeterEvent& e)
                                  { return e.kind == drag->event.kind && e.tick == drag->event.tick; }))
        {
            drag.reset();
            requestRepaint();
        }
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
        return bpmToY (drag ? drag->range : range, bpm, plot.getY(), plot.getHeight());
    }

    void TempoGraphComponent::paint (juce::Graphics& g)
    {
        ++paintCount;
        g.fillAll (juce::Colour (SongsmithColours::background));

        auto tempos = tempoEntries (events);
        if (! doc.hasTimeBase() || tempos.empty())
            return;

        // While dragging, the dragged point is drawn at the preview values (the document
        // is untouched until mouse-up).
        const bool previewing = drag.has_value() && drag->moved;
        if (previewing)
        {
            for (auto& t : tempos)
                if (t.tick == drag->event.tick)
                {
                    t.tick = drag->previewTick;
                    t.bpm  = drag->previewBpm;
                }
            std::stable_sort (tempos.begin(), tempos.end(),
                              [] (const TempoMeterEvent& a, const TempoMeterEvent& b) { return a.tick < b.tick; });
        }

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
            const bool dragged  = previewing && t.tick == drag->previewTick;
            const bool selected = dragged || std::any_of (selectedTempos.begin(), selectedTempos.end(),
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

        // The dragged value, beside the point.
        if (previewing)
        {
            const juce::Font font (juce::FontOptions (11.0f));
            g.setFont (font);
            const auto text = juce::String (formatMarkerBpm (drag->previewBpm)) + " BPM";
            const int  w = juce::GlyphArrangement::getStringWidthInt (font, text) + 4;
            const int  x = xForTick (drag->previewTick), y = yForBpm (drag->previewBpm);
            const int  left = x + pointRadius + 4 + w <= right ? x + pointRadius + 4 : x - pointRadius - 4 - w;
            g.setColour (juce::Colour (SongsmithColours::text));
            g.drawText (text, left, y + pointRadius + 1, w, 12, juce::Justification::centredLeft, false);
        }
    }

    // ---- editing ------------------------------------------------------------------

    int TempoGraphComponent::snappedTickForX (int x) const
    {
        const int ticksPerQuarter = (int) doc.getSourceMidiNode().getProperty (SongIDs::ticksPerQuarter, 480);
        return std::max (0, snapToBeat (meterSegments (doc.getMeterChanges(), ticksPerQuarter), view.tickForX (x)));
    }

    void TempoGraphComponent::mouseDown (const juce::MouseEvent& e)
    {
        drag.reset();
        const auto pos    = e.getPosition();
        const auto tempos = tempoEntries (events);

        std::vector<std::pair<int, int>> xy;
        xy.reserve (tempos.size());
        for (const auto& t : tempos)
            xy.emplace_back (xForTick (t.tick), yForBpm (t.bpm));

        if (const auto hit = nearestPoint (xy, pos.x, pos.y, hitRadius))
        {
            const auto& event = tempos[*hit];
            drag = Drag { event, pos, false, range, event.tick, event.bpm };
            if (onSelect)
                onSelect (TempoMeterKind::tempo, event.tick);
            return;
        }

        // Empty space: add a tempo at the nearest beat, or select the one already there.
        const int  tick    = snappedTickForX (pos.x);
        const auto current = listTempoMeterEvents (doc);
        if (std::any_of (current.begin(), current.end(), [tick] (const TempoMeterEvent& ev)
                         { return ev.kind == TempoMeterKind::tempo && ev.tick == tick; }))
        {
            if (onSelect)
                onSelect (TempoMeterKind::tempo, tick);
            return;
        }

        const auto plot   = plotArea();
        const auto result = setTempoAt (doc, tick, yToBpm (range, pos.y, plot.getY(), plot.getHeight()));
        if (onEdited)
            onEdited (result);
        if (! result && onSelect)
            onSelect (TempoMeterKind::tempo, tick);
    }

    void TempoGraphComponent::updatePreview (const juce::MouseEvent& e)
    {
        const auto pos = e.getPosition();
        const auto d   = pos - drag->down;
        if (std::abs (d.x) >= dragThreshold || std::abs (d.y) >= dragThreshold)
            drag->moved = true;
        if (! drag->moved)
            return;

        const auto plot = plotArea();
        // Within the threshold horizontally the stored tick is kept, so a vertical move
        // does not snap an off-beat tempo onto the beat; tick 0 never moves.
        drag->previewTick = drag->event.tick == 0 || std::abs (d.x) < dragThreshold
                              ? drag->event.tick
                              : snappedTickForX (pos.x);
        // Within the threshold vertically the stored BPM is kept, so a sideways move
        // does not round a fractional tempo to a whole BPM.
        drag->previewBpm  = std::abs (d.y) < dragThreshold
                              ? drag->event.bpm
                              : yToBpm (drag->range, pos.y, plot.getY(), plot.getHeight());
    }

    void TempoGraphComponent::mouseDrag (const juce::MouseEvent& e)
    {
        if (! drag)
            return;
        updatePreview (e);
        requestRepaint();
    }

    void TempoGraphComponent::mouseUp (const juce::MouseEvent& e)
    {
        if (! drag)
            return;
        updatePreview (e);   // a release outside the component may come without a drag there
        const Drag done = *drag;
        drag.reset();
        requestRepaint();
        if (! done.moved)
            return;          // a click: nothing is written
        const auto result = moveTempoTo (doc, done.event, done.previewTick, done.previewBpm);
        if (onEdited)
            onEdited (result);
        // The selection follows the point to its new tick (a rejected move keeps the old one).
        if (! result && done.previewTick != done.event.tick && onSelect)
            onSelect (TempoMeterKind::tempo, done.previewTick);
    }

    bool TempoGraphComponent::keyPressed (const juce::KeyPress&)
    {
        return false;   // Delete / undo bubble to the window
    }

    std::optional<std::pair<int, double>> TempoGraphComponent::dragPreviewForTesting() const
    {
        if (! drag)
            return std::nullopt;
        return std::make_pair (drag->previewTick, drag->previewBpm);
    }
}
