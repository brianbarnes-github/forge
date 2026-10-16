#pragma once

#include "UI/Playback/PlaybackController.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <functional>
#include <optional>
#include <utility>
#include <vector>

namespace lotro
{

// Bookkeeping for which x strips of a PlayheadOverlay need repainting.
//
// The overlay paints at the LIVE transport position but is told about moves only
// by a 30 Hz timer, so any repaint of its area in between (a volume slider step
// repainting its row, say) can draw the line at a position no notification has
// seen yet, and usually only inside the repainted rows. The tracker therefore
// remembers every x where a line is actually on screen (and whether it was drawn
// the full height), not where the last notification said it was, so the next
// notification always erases everything that is really there.
class PlayheadDirtyTracker
{
public:
    static constexpr int lineWidth = 2;
    // The copper glow reaches this far either side of the line.
    static constexpr int paintHalfWidth = 4;

    // The strip to repaint for a line at x: the line plus its glow, with a 1 px margin.
    static juce::Rectangle<int> dirtyStrip (int x, int overlayHeight)
    {
        return { x - paintHalfWidth - 1, 0, lineWidth + 2 * paintHalfWidth + 2, overlayHeight };
    }

    struct Dirty
    {
        std::vector<int> xs;   // strips to repaint: every line still on screen, and the new x
    };

    // The playhead is now at x (in the overlay's coordinates).
    Dirty onPositionChanged (int x, int overlayWidth) const;

    // paint() ran at the playhead's current x with this clip.
    void onPaint (int x, int overlayWidth, int overlayHeight, juce::Rectangle<int> clip);

private:
    struct Drawn
    {
        int x = 0;
        bool full = false;   // drawn over the overlay's whole height
    };
    std::vector<Drawn> drawn;   // lines on screen and not since wiped
};

// A mouse-transparent copper line, with a soft glow, at the shared playhead. The owner supplies
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
    PlayheadDirtyTracker dirtyTracker;
};

// An ivory, mouse-transparent line at the shared start marker, running the
// full height of the canvas. Its triangle handle lives in the TimelineRuler
// above, which this line continues down from. Draws nothing while no marker is
// set. Same ownership and mapping contract as PlayheadOverlay; owners repaint()
// it when zoom/scroll change.
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

// A translucent ivory band over the loop range [start, end), with a faint line
// at each edge, running the full height of the canvas (the ruler above carries
// the edge handles). Draws nothing while no range is set, and draws whatever
// range the controller holds, clipped to this component (after an edit shortens
// the Song the range may run past its end). Mouse-transparent; same ownership
// and mapping contract as MarkerOverlay.
class LoopRangeOverlay : public juce::Component, private PlaybackController::Listener
{
public:
    LoopRangeOverlay (PlaybackController& controllerIn, std::function<int (double tick)> tickToXIn);
    ~LoopRangeOverlay() override;

    void paint (juce::Graphics& g) override;
    // The range's x span [first, second), unclipped; none while no range is set.
    std::optional<std::pair<int, int>> currentXs() const;

private:
    void playbackLoopChanged() override { repaint(); }

    PlaybackController& controller;
    std::function<int (double)> tickToX;
};

} // namespace lotro
