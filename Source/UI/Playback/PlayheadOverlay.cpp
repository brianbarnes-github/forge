#include "UI/Playback/PlayheadOverlay.h"

namespace lotro
{

PlayheadOverlay::PlayheadOverlay (PlaybackController& controllerIn, std::function<int (double)> tickToXIn)
    : controller (controllerIn), tickToX (std::move (tickToXIn))
{
    setInterceptsMouseClicks (false, false);
    controller.addListener (this);
}

PlayheadOverlay::~PlayheadOverlay()
{
    controller.removeListener (this);
}

void PlayheadOverlay::paint (juce::Graphics& g)
{
    const int x = currentX();
    lastX = x;
    if (x < 0 || x >= getWidth())
        return;
    g.setColour (juce::Colours::white.withAlpha (0.9f));
    g.fillRect (x, 0, 2, getHeight());
}

void PlayheadOverlay::playbackPositionChanged()
{
    const int x = currentX();
    if (x == lastX)
        return;
    // The line covers x..x+1; a 4 px strip from x-1 covers it with a margin.
    // Out-of-range strips are clipped by JUCE, so an off-screen old line is harmless.
    repaint (juce::jmax (0, lastX - 1), 0, 4, getHeight());   // erase the old line
    repaint (juce::jmax (0, x - 1), 0, 4, getHeight());       // draw the new one
    lastX = x;
}

} // namespace lotro
