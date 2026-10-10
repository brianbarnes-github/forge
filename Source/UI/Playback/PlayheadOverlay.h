#pragma once

#include "UI/Playback/PlaybackController.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <functional>
#include <limits>
#include <optional>

namespace lotro
{

// Bookkeeping for which x strips of a PlayheadOverlay need repainting.
//
// The overlay paints at the LIVE transport position but is told about moves only
// by a 30 Hz timer, so any repaint of its area in between (a volume slider step
// repainting its row, say) can draw the line at a position no notification has
// seen yet. The tracker therefore remembers where the line was last actually
// drawn, not where the last notification said it was, so the next notification
// always erases what is really on screen.
class PlayheadDirtyTracker
{
public:
    static constexpr int lineWidth = 2;

    struct Dirty
    {
        std::array<int, 2> xs {};   // strips to repaint: the line's old x and its new x
        int count = 0;
    };

    // The playhead is now at x (in the overlay's coordinates).
    Dirty onPositionChanged (int x, int overlayWidth) noexcept;

    // paint() ran at the playhead's current x with this clip.
    void onPaint (int x, int overlayWidth, int overlayHeight, juce::Rectangle<int> clip) noexcept;

private:
    static constexpr int none = std::numeric_limits<int>::min();
    int drawnX = none;   // x of the line as last drawn and not since wiped
};

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
    PlayheadDirtyTracker dirtyTracker;
};

// An amber, mouse-transparent line at the shared start marker, running the
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

} // namespace lotro
