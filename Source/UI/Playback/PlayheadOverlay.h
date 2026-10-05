#pragma once

#include "UI/Playback/PlaybackController.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <optional>

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

// A mouse-transparent amber line (with a flag at the top) at the shared start
// marker. Draws nothing while no marker is set. Same ownership and mapping
// contract as PlayheadOverlay; owners repaint() it when zoom/scroll change.
class MarkerOverlay : public juce::Component, private PlaybackController::Listener
{
public:
    MarkerOverlay (PlaybackController& controllerIn, std::function<int (double tick)> tickToXIn);
    ~MarkerOverlay() override;

    void paint (juce::Graphics& g) override;
    std::optional<int> currentX() const;

private:
    void playbackMarkerChanged() override { repaint(); }

    PlaybackController& controller;
    std::function<int (double)> tickToX;
};

} // namespace lotro
