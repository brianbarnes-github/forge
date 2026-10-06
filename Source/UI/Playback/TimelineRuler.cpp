#include "UI/Playback/TimelineRuler.h"

#include "UI/SongsmithColours.h"

namespace lotro
{

std::optional<juce::Rectangle<int>> TimelineRuler::markerHandleBounds() const
{
    if (! marker || ! xForTick)
        return std::nullopt;
    const auto tick = marker();
    if (! tick.has_value())
        return std::nullopt;
    constexpr int handleWidth = 16, handleHeight = 12;
    return juce::Rectangle<int> (xForTick (*tick) - handleWidth / 2, 0, handleWidth, handleHeight);
}

void TimelineRuler::mouseDown (const juce::MouseEvent& e)
{
    handleWasPressed = false;
    if (! e.mods.isRightButtonDown() && ! e.mods.isPopupMenu())
    {
        const auto handle = markerHandleBounds();
        if (handle.has_value() && handle->contains (e.getPosition()))
        {
            handleWasPressed = true;
            if (onClearMarker)
                onClearMarker();
            return;
        }
    }
    applyPointer (e, /*pressed*/ true);
}

void TimelineRuler::applyPointer (const juce::MouseEvent& e, bool)
{
    if (handleWasPressed || ! tickForX)
        return;

    const double tick = juce::jmax (0.0, tickForX (e.x));
    if (e.mods.isRightButtonDown() || e.mods.isPopupMenu())
    {
        if (onSeek)
            onSeek (tick);
    }
    else if (onSetMarker)
    {
        onSetMarker (tick);
    }
}

void TimelineRuler::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (SongsmithColours::background).brighter (0.1f));

    if (xForTick && grid && tickForX)
    {
        const double firstTick = tickForX (contentLeft);
        const double lastTick = tickForX (getWidth());
        const int visiblePixels = getWidth() - contentLeft;
        const double pixelsPerTick = lastTick > firstTick ? (double) visiblePixels / (lastTick - firstTick) : 0.0;

        g.saveState();
        g.reduceClipRegion (contentLeft, 0, juce::jmax (0, visiblePixels), getHeight());
        g.setFont (juce::Font (juce::FontOptions (9.0f)));

        for (const auto& mark : computeRulerMarks (firstTick, lastTick, pixelsPerTick, grid()))
        {
            const int x = xForTick (mark.tick);
            const bool isBar = mark.kind == RulerMark::Kind::Bar;

            // A bar line crosses both rows; a beat tick is a short stub in the
            // lower half of the top row.
            g.setColour (juce::Colour (isBar ? SongsmithColours::text : SongsmithColours::textMuted).withAlpha (isBar ? 0.8f : 0.6f));
            g.fillRect (x, isBar ? 0 : rowHeight / 2, 1, isBar ? getHeight() - 1 : rowHeight / 2);

            g.setColour (juce::Colour (isBar ? SongsmithColours::text : SongsmithColours::textMuted));
            g.drawText (mark.label, x + 3, 0, 56, rowHeight - 1, juce::Justification::centredLeft, false);
            if (! mark.timeLabel.empty())
            {
                g.setColour (juce::Colour (SongsmithColours::textMuted));
                g.drawText (mark.timeLabel, x + 3, rowHeight, 64, rowHeight - 1, juce::Justification::centredLeft, false);
            }
        }

        if (const auto handle = markerHandleBounds())
        {
            const int x = handle->getCentreX();
            g.setColour (juce::Colour (SongsmithColours::accentAmber));
            g.fillRect (x, handle->getBottom(), 1, getHeight() - handle->getBottom());
            juce::Path flag;
            flag.addTriangle ((float) handle->getX(), 0.0f, (float) handle->getRight(), 0.0f,
                              (float) x + 0.5f, (float) handle->getBottom());
            g.fillPath (flag);
        }

        g.restoreState();
    }

    g.setColour (juce::Colours::black.withAlpha (0.5f));
    g.drawHorizontalLine (getHeight() - 1, 0.0f, (float) getWidth());
}

} // namespace lotro
