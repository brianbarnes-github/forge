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

PlayheadDirtyTracker::Dirty PlayheadDirtyTracker::onPositionChanged (int x, int overlayWidth) noexcept
{
    Dirty dirty;
    const bool visible = x >= 0 && x < overlayWidth;
    if (drawnX == x || (drawnX == none && ! visible))
        return dirty;   // what is on screen already matches
    if (drawnX != none)
        dirty.xs[(size_t) dirty.count++] = drawnX;   // erase the line that is really there
    dirty.xs[(size_t) dirty.count++] = x;
    return dirty;
}

void PlayheadDirtyTracker::onPaint (int x, int overlayWidth, int overlayHeight, juce::Rectangle<int> clip) noexcept
{
    // A repaint wipes the old line only if it covers all of it; the overlay is
    // transparent, so whatever lies beneath is repainted in the same pass.
    if (drawnX != none && drawnX != x
        && clip.contains (juce::Rectangle<int> (drawnX, 0, lineWidth, overlayHeight)))
        drawnX = none;

    if (x >= 0 && x < overlayWidth && clip.intersects (juce::Rectangle<int> (x, 0, lineWidth, overlayHeight)))
        drawnX = x;
}

void PlayheadOverlay::paint (juce::Graphics& g)
{
    const int x = currentX();
    dirtyTracker.onPaint (x, getWidth(), getHeight(), g.getClipBounds());
    if (x < 0 || x >= getWidth())
        return;
    g.setColour (juce::Colours::white.withAlpha (0.9f));
    g.fillRect (x, 0, PlayheadDirtyTracker::lineWidth, getHeight());
}

void PlayheadOverlay::playbackPositionChanged()
{
    const auto dirty = dirtyTracker.onPositionChanged (currentX(), getWidth());
    // The line covers x..x+1; a 4 px strip from x-1 covers it with a margin.
    // Out-of-range strips are clipped by JUCE, so an off-screen old line is harmless.
    for (int i = 0; i < dirty.count; ++i)
        repaint (juce::jmax (0, dirty.xs[(size_t) i] - 1), 0, 4, getHeight());
}

MarkerOverlay::MarkerOverlay (PlaybackController& controllerIn, std::function<int (double)> tickToXIn)
    : controller (controllerIn), tickToX (std::move (tickToXIn))
{
    setInterceptsMouseClicks (false, false);
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

void MarkerOverlay::paint (juce::Graphics& g)
{
    const auto x = currentX();
    if (! x.has_value() || *x < 0 || *x >= getWidth())
        return;
    g.setColour (juce::Colour (SongsmithColours::accentAmber));
    g.fillRect (*x, 0, 1, getHeight());
}

} // namespace lotro
