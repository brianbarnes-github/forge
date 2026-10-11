#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <optional>

namespace lotro
{
    // A mouse-transparent, one-pixel vertical line at an event's tick, running the
    // full height of the track list's note area. Paint-only: the owner supplies the
    // tick -> x mapping (this component's coordinates), sets the tick, and repaints
    // it when zoom/scroll change. Draws nothing while no tick is set.
    class EventMarkerOverlay : public juce::Component
    {
    public:
        explicit EventMarkerOverlay (std::function<int (int tick)> tickToXIn);

        void setTick (std::optional<int> tick);
        std::optional<int> getTick() const noexcept { return markerTick; }
        std::optional<int> currentX() const;

        void paint (juce::Graphics& g) override;

    private:
        std::function<int (int)> tickToX;
        std::optional<int>       markerTick;
    };
}
