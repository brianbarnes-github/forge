#include "UI/Playback/PlayheadOverlay.h"

#include "UI/SongsmithColours.h"

#include <algorithm>
#include <vector>

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

PlayheadDirtyTracker::Dirty PlayheadDirtyTracker::onPositionChanged (int x, int overlayWidth) const
{
    Dirty dirty;
    bool alreadyDrawn = false;   // the whole line is already on screen at x
    for (const auto& d : drawn)
    {
        if (d.x == x && d.full)
            alreadyDrawn = true;
        else
            dirty.xs.push_back (d.x);   // erase the line that is really there
    }
    if (x >= 0 && x < overlayWidth && ! alreadyDrawn)
        dirty.xs.push_back (x);
    return dirty;
}

void PlayheadDirtyTracker::onPaint (int x, int overlayWidth, int overlayHeight, juce::Rectangle<int> clip)
{
    // A repaint wipes an old line only if it covers all of it; the overlay is
    // transparent, so whatever lies beneath is repainted in the same pass.
    drawn.erase (std::remove_if (drawn.begin(), drawn.end(), [&] (const Drawn& d)
    {
        return d.x != x && clip.contains (juce::Rectangle<int> (d.x, 0, lineWidth, overlayHeight));
    }), drawn.end());

    const juce::Rectangle<int> line (x, 0, lineWidth, overlayHeight);
    if (x < 0 || x >= overlayWidth || ! clip.intersects (line))
        return;
    const bool full = clip.contains (line);
    if (auto it = std::find_if (drawn.begin(), drawn.end(), [x] (const Drawn& d) { return d.x == x; }); it != drawn.end())
        it->full = it->full || full;
    else
        drawn.push_back ({ x, full });
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
    for (const int dirtyX : dirty.xs)
        repaint (juce::jmax (0, dirtyX - 1), 0, 4, getHeight());
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
