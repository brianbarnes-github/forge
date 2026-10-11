#include "MeterStripComponent.h"
#include "ConductorMarkers.h"
#include "SongsmithColours.h"
#include "TempoMeterGraphModel.h"

#include <algorithm>
#include <cstdlib>

namespace lotro
{
    namespace
    {
        constexpr int edgeHitRadius = 6;    // px either side of a block's left edge
        constexpr int grabZone      = 40;   // px from a block's left edge that start a drag
        constexpr int dragThreshold = 3;    // px of horizontal travel before a press becomes a drag
        constexpr int edgeWidth     = 2;
        constexpr int labelInset    = edgeWidth + 3;   // label start, right of the edge line

        constexpr float fillAlphaEven     = 0.12f;
        constexpr float fillAlphaOdd      = 0.22f;
        constexpr float fillAlphaSelected = 0.40f;

        std::vector<TempoMeterEvent> meterEntries (const std::vector<TempoMeterEvent>& in)
        {
            std::vector<TempoMeterEvent> out;
            for (const auto& e : in)
                if (e.kind == TempoMeterKind::meter)
                    out.push_back (e);
            std::stable_sort (out.begin(), out.end(),
                              [] (const TempoMeterEvent& a, const TempoMeterEvent& b) { return a.tick < b.tick; });
            return out;
        }

        bool sameMeters (const std::vector<TempoMeterEvent>& a, const std::vector<TempoMeterEvent>& b)
        {
            if (a.size() != b.size())
                return false;
            for (size_t i = 0; i < a.size(); ++i)
                if (a[i].kind != b[i].kind || a[i].tick != b[i].tick
                    || a[i].numerator != b[i].numerator || a[i].denominator != b[i].denominator)
                    return false;
            return true;
        }

        bool hasMeterAt (const std::vector<TempoMeterEvent>& listing, int tick)
        {
            return std::any_of (listing.begin(), listing.end(), [tick] (const TempoMeterEvent& e)
                                { return e.kind == TempoMeterKind::meter && e.tick == tick; });
        }

        // The meter in force at `tick` (the last change at or before it); 4/4 when none.
        MeterChange meterInForce (const std::vector<MeterChange>& meters, int tick)
        {
            MeterChange found;
            int foundTick = -1;
            for (const auto& m : meters)
                if (m.tick <= tick && m.tick >= foundTick)
                {
                    found     = m;
                    foundTick = m.tick;
                }
            return found;
        }

        juce::Font labelFont() { return juce::Font (juce::FontOptions (11.0f)); }

        juce::String labelText (const TempoMeterEvent& e)
        {
            return juce::String::fromUTF8 (markerLabel (e).c_str());
        }
    }

    MeterStripComponent::MeterStripComponent (SongDocument& document, const TimelineViewState& viewIn)
        : doc (document), view (viewIn)
    {
        setOpaque (true);
        setWantsKeyboardFocus (true);
    }

    void MeterStripComponent::requestRepaint()
    {
        ++repaintRequests;
        repaint();
    }

    void MeterStripComponent::setEvents (std::vector<TempoMeterEvent> newEvents)
    {
        auto newMeters = meterEntries (newEvents);
        // The dragged meter vanished (undo / redo / New mid-drag): drop the drag, so the
        // coming mouse-up commits nothing.
        if (drag && ! hasMeterAt (newMeters, drag->event.tick))
        {
            drag.reset();
            requestRepaint();
        }
        if (sameMeters (events, newMeters))
            return;
        events = std::move (newMeters);
        requestRepaint();
    }

    void MeterStripComponent::setSelected (std::vector<TempoMeterEvent> selected)
    {
        auto meters = meterEntries (selected);
        if (sameMeters (selectedMeters, meters))
            return;
        selectedMeters = std::move (meters);
        requestRepaint();
    }

    int MeterStripComponent::xForTick (int tick) const { return view.xForTick (tick); }

    int MeterStripComponent::ticksPerQuarter() const
    {
        return (int) doc.getSourceMidiNode().getProperty (SongIDs::ticksPerQuarter, 480);
    }

    void MeterStripComponent::paint (juce::Graphics& g)
    {
        ++paintCount;
        g.fillAll (juce::Colour (SongsmithColours::background));
        if (! doc.hasTimeBase() || events.empty())
            return;

        const auto clip   = g.getClipBounds();
        const int  right  = getWidth();
        const int  height = getHeight();
        const auto meterColour = juce::Colour (SongsmithColours::conductorMeter);

        // Blocks: from each meter to the next (the last to the right edge), alternating shade.
        for (size_t i = 0; i < events.size(); ++i)
        {
            const int x0 = xForTick (events[i].tick);
            const int x1 = i + 1 < events.size() ? xForTick (events[i + 1].tick) : std::max (right, x0);
            if (x1 < clip.getX() || x0 > clip.getRight())
                continue;
            const bool selected = hasMeterAt (selectedMeters, events[i].tick);
            const float alpha = selected ? fillAlphaSelected : (i % 2 == 0 ? fillAlphaEven : fillAlphaOdd);
            g.setColour (meterColour.withAlpha (alpha));
            g.fillRect (x0, 0, x1 - x0, height);
            if (selected)
            {
                g.setColour (juce::Colour (SongsmithColours::eventMarker));
                g.fillRect (x0, 0, x1 - x0, 2);
            }
            g.setColour (meterColour);
            g.fillRect (x0, 0, edgeWidth, height);
        }

        // Labels inside each block's left end, hidden by the Conductor row's overlap rule.
        const auto font = labelFont();
        g.setFont (font);
        const auto placed = placeMarkers (conductorMarkers (events),
                                          [this] (int tick) { return xForTick (tick); },
                                          [&font] (const std::string& s)
                                          { return juce::GlyphArrangement::getStringWidthInt (font, juce::String::fromUTF8 (s.c_str())); },
                                          labelInset, 4, clip.getRight() + 64);
        g.setColour (juce::Colour (SongsmithColours::text));
        for (const auto& p : placed)
            if (p.labelShown)
                g.drawText (juce::String::fromUTF8 (p.marker.label.c_str()),
                            p.x + labelInset, 0, p.labelWidth + 2, height, juce::Justification::centredLeft, false);

        // The drag preview: a ghost line at the bar the block would land on, and its label.
        if (drag && drag->moved && drag->previewTick != drag->event.tick)
        {
            const auto ghost = juce::Colour (SongsmithColours::eventMarker);
            const int  x = xForTick (drag->previewTick);
            g.setColour (ghost);
            g.fillRect (x, 0, edgeWidth, height);
            const auto text = labelText (drag->event);
            const int  w = juce::GlyphArrangement::getStringWidthInt (font, text) + 2;
            const int  left = x + labelInset + w <= right ? x + labelInset : x - 3 - w;
            g.drawText (text, left, 0, w, height, juce::Justification::centredLeft, false);
        }
    }

    // ---- editing ------------------------------------------------------------------

    std::optional<MeterStripComponent::Hit> MeterStripComponent::hitAt (int x) const
    {
        // A block's left edge (+-6 px) wins over the body it overlaps; nearest edge, the
        // later block on a tie.
        std::optional<size_t> edge;
        int bestD = 0;
        for (size_t i = 0; i < events.size(); ++i)
        {
            const int d = std::abs (x - xForTick (events[i].tick));
            if (d <= edgeHitRadius && (! edge || d <= bestD))
            {
                edge  = i;
                bestD = d;
            }
        }
        if (edge)
            return Hit { *edge, true };

        std::optional<size_t> inside;
        for (size_t i = 0; i < events.size(); ++i)
            if (xForTick (events[i].tick) <= x)
                inside = i;
        if (! inside)
            return std::nullopt;

        // The grab zone is the block's first 40 px, never reaching into the next block.
        const int x0 = xForTick (events[*inside].tick);
        int grabEnd = x0 + grabZone;
        if (*inside + 1 < events.size())
            grabEnd = std::min (grabEnd, xForTick (events[*inside + 1].tick));
        return Hit { *inside, x < grabEnd };
    }

    void MeterStripComponent::mouseDown (const juce::MouseEvent& e)
    {
        drag.reset();
        if (! e.mods.isLeftButtonDown())
            return;
        const auto hit = hitAt (e.getPosition().x);
        if (! hit)
            return;

        const auto& event = events[hit->index];
        if (hit->grab && event.tick != 0)   // the tick-0 meter cannot move: it only selects
            drag = Drag { event, e.getPosition().x, false, event.tick };
        if (onSelect)
            onSelect (TempoMeterKind::meter, event.tick);
    }

    void MeterStripComponent::updatePreview (const juce::MouseEvent& e)
    {
        const int dx = e.getPosition().x - drag->downX;
        if (std::abs (dx) >= dragThreshold)
            drag->moved = true;
        if (! drag->moved)
            return;
        // Back within the threshold the stored tick is kept: an off-bar meter is not
        // quantised by a drag that returns to its start.
        drag->previewTick = std::abs (dx) < dragThreshold
                              ? drag->event.tick
                              : snapMeterTick (doc.getMeterChanges(), ticksPerQuarter(), drag->event.tick,
                                               view.tickForX (e.getPosition().x));
    }

    void MeterStripComponent::mouseDrag (const juce::MouseEvent& e)
    {
        if (! drag)
            return;
        updatePreview (e);
        requestRepaint();
    }

    void MeterStripComponent::mouseUp (const juce::MouseEvent& e)
    {
        if (! drag)
            return;
        updatePreview (e);   // a release outside the component may come without a drag there
        const Drag done = *drag;
        drag.reset();
        requestRepaint();
        if (! done.moved || done.previewTick == done.event.tick)
            return;          // a click, or back where it started: nothing is written
        // The meter vanished before the refresh reached us (undo mid-drag): write nothing.
        if (! doc.hasTimeBase() || ! hasMeterAt (listTempoMeterEvents (doc), done.event.tick))
            return;

        const auto result = moveMeterTo (doc, done.event, done.previewTick);
        if (onEdited)
            onEdited (result);
        // The selection follows the block to its new bar (a rejected move keeps the old one).
        if (! result && onSelect)
            onSelect (TempoMeterKind::meter, done.previewTick);
    }

    void MeterStripComponent::mouseDoubleClick (const juce::MouseEvent& e)
    {
        drag.reset();   // a double-click is never a drag
        if (! e.mods.isLeftButtonDown())
            return;

        const auto meters = doc.getMeterChanges();
        const int  tick   = snapMeterTick (meters, ticksPerQuarter(), -1, view.tickForX (e.getPosition().x));
        if (hasMeterAt (listTempoMeterEvents (doc), tick))
        {
            if (onSelect)
                onSelect (TempoMeterKind::meter, tick);
            return;
        }

        const auto inForce = meterInForce (meters, tick);
        const auto result  = setMeterAt (doc, tick, inForce.numerator, inForce.denominator);
        if (onEdited)
            onEdited (result);
        if (! result && onSelect)
            onSelect (TempoMeterKind::meter, tick);
    }

    bool MeterStripComponent::keyPressed (const juce::KeyPress&)
    {
        return false;   // Delete / undo bubble to the window
    }

    std::optional<int> MeterStripComponent::dragPreviewForTesting() const
    {
        if (! drag)
            return std::nullopt;
        return drag->previewTick;
    }
}
