#pragma once

#include "UI/Playback/PlaybackController.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

namespace lotro
{

// A mouse-transparent vertical line at the shared playhead. The owner supplies
// the tick -> x mapping in this component's own coordinates, because each
// canvas has independent zoom/scroll.
// Lifetime: holds a reference to the controller, so it must be destroyed before it.
class PlayheadOverlay : public juce::Component, private PlaybackController::Listener
{
public:
    PlayheadOverlay (PlaybackController& controllerIn, std::function<int (double tick)> tickToXIn);
    ~PlayheadOverlay() override;

    void paint (juce::Graphics& g) override;
    int currentX() const { return tickToX ? tickToX (controller.getPositionTicks()) : 0; }

private:
    void playbackPositionChanged() override;

    PlaybackController& controller;
    std::function<int (double)> tickToX;
    int lastX = -1;
};

} // namespace lotro
