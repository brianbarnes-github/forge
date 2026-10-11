#include "EventMarkerOverlay.h"
#include "SongsmithColours.h"

namespace lotro
{
    EventMarkerOverlay::EventMarkerOverlay (std::function<int (int)> tickToXIn)
        : tickToX (std::move (tickToXIn))
    {
        setInterceptsMouseClicks (false, false);
    }

    void EventMarkerOverlay::setTick (std::optional<int> tick)
    {
        if (markerTick == tick)
            return;
        markerTick = tick;
        repaint();
    }

    std::optional<int> EventMarkerOverlay::currentX() const
    {
        if (! markerTick.has_value() || ! tickToX)
            return std::nullopt;
        return tickToX (*markerTick);
    }

    void EventMarkerOverlay::paint (juce::Graphics& g)
    {
        const auto x = currentX();
        if (! x.has_value() || *x < 0 || *x >= getWidth())
            return;
        g.setColour (juce::Colour (SongsmithColours::eventMarker));
        g.fillRect (*x, 0, 1, getHeight());
    }
}
