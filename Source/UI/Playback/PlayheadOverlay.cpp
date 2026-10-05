#include "UI/Playback/PlayheadOverlay.h"

#include "UI/SongsmithColours.h"

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

MarkerOverlay::MarkerOverlay (PlaybackController& controllerIn, std::function<int (double)> tickToXIn)
    : controller (controllerIn), tickToX (std::move (tickToXIn))
{
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
    controller.addListener (this);
}

MarkerOverlay::~MarkerOverlay()
{
    controller.removeListener (this);
}

std::optional<int> MarkerOverlay::currentX() const
{
    const auto tick = controller.getMarkerTick();
    if (! tick.has_value() || ! tickToX)
        return std::nullopt;
    return tickToX (*tick);
}

std::optional<juce::Rectangle<int>> MarkerOverlay::handleBounds() const
{
    const auto x = currentX();
    if (! x.has_value())
        return std::nullopt;
    constexpr int handleWidth = 16, handleHeight = 12;
    return juce::Rectangle<int> (*x - handleWidth / 2, 0, handleWidth, handleHeight);
}

bool MarkerOverlay::hitTest (int x, int y)
{
    const auto handle = handleBounds();
    return handle.has_value() && handle->contains (x, y);
}

void MarkerOverlay::mouseDown (const juce::MouseEvent&)
{
    controller.clearMarker();
}

void MarkerOverlay::paint (juce::Graphics& g)
{
    const auto handle = handleBounds();
    const auto x = currentX();
    if (! handle.has_value() || *x + handle->getWidth() / 2 < 0 || *x - handle->getWidth() / 2 >= getWidth())
        return;
    g.setColour (juce::Colour (SongsmithColours::accentAmber));
    if (*x >= 0 && *x < getWidth())
        g.fillRect (*x, handle->getBottom(), 1, getHeight() - handle->getBottom());
    juce::Path flag;
    flag.addTriangle ((float) handle->getX(), 0.0f, (float) handle->getRight(), 0.0f,
                      (float) *x + 0.5f, (float) handle->getBottom());
    g.fillPath (flag);
}

} // namespace lotro
