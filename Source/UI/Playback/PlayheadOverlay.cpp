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
    // A repaint wipes an old line only if it covers all of it, glow included;
    // the overlay is transparent, so whatever lies beneath is repainted in the
    // same pass.
    const juce::Rectangle<int> overlay (0, 0, overlayWidth, overlayHeight);
    drawn.erase (std::remove_if (drawn.begin(), drawn.end(), [&] (const Drawn& d)
    {
        return d.x != x && clip.contains (dirtyStrip (d.x, overlayHeight).getIntersection (overlay));
    }), drawn.end());

    // What paint() draws at x, glow included: a clip touching only the glow still leaves pixels behind.
    const auto painted = dirtyStrip (x, overlayHeight).getIntersection (overlay);
    if (x < 0 || x >= overlayWidth || ! clip.intersects (painted))
        return;
    const bool full = clip.contains (painted);
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
    const auto copper = juce::Colour (SongsmithColours::accentCopper);
    for (int d = PlayheadDirtyTracker::paintHalfWidth - 1; d >= 1; --d)
    {
        g.setColour (copper.withAlpha (0.30f * (1.0f - (float) d / (float) PlayheadDirtyTracker::paintHalfWidth)));
        g.fillRect (x - d, 0, 1, getHeight());
        g.fillRect (x + PlayheadDirtyTracker::lineWidth - 1 + d, 0, 1, getHeight());
    }
    g.setColour (copper);
    g.fillRect (x, 0, PlayheadDirtyTracker::lineWidth, getHeight());
}

void PlayheadOverlay::playbackPositionChanged()
{
    const auto dirty = dirtyTracker.onPositionChanged (currentX(), getWidth());
    // Out-of-range strips are clipped by JUCE, so an off-screen old line is harmless.
    for (const int dirtyX : dirty.xs)
        repaint (PlayheadDirtyTracker::dirtyStrip (dirtyX, getHeight()));
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
    g.setColour (juce::Colour (SongsmithColours::markerIvory));
    g.fillRect (*x, 0, 1, getHeight());
}

LoopRangeOverlay::LoopRangeOverlay (PlaybackController& controllerIn, std::function<int (double)> tickToXIn)
    : controller (controllerIn), tickToX (std::move (tickToXIn))
{
    setInterceptsMouseClicks (false, false);
    controller.addListener (this);
}

LoopRangeOverlay::~LoopRangeOverlay()
{
    controller.removeListener (this);
}

std::optional<std::pair<int, int>> LoopRangeOverlay::currentXs() const
{
    const auto range = controller.getLoopRange();
    if (! range.has_value() || ! tickToX)
        return std::nullopt;
    return std::make_pair (tickToX (range->startTick), tickToX (range->endTick));
}

void LoopRangeOverlay::paint (juce::Graphics& g)
{
    const auto xs = currentXs();
    if (! xs.has_value())
        return;
    const int first = juce::jmax (0, xs->first);
    const int last = juce::jmin (getWidth(), xs->second);   // exclusive
    if (first >= last)
        return;

    const auto ivory = juce::Colour (SongsmithColours::markerIvory);
    g.setColour (ivory.withAlpha (0.14f));
    g.fillRect (first, 0, last - first, getHeight());

    g.setColour (ivory.withAlpha (0.6f));
    if (xs->first >= 0 && xs->first < getWidth())
        g.fillRect (xs->first, 0, 1, getHeight());
    if (xs->second - 1 >= 0 && xs->second - 1 < getWidth())
        g.fillRect (xs->second - 1, 0, 1, getHeight());
}

} // namespace lotro
