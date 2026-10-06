#pragma once

#include "UI/Playback/TimelineRulerMarks.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <optional>

namespace lotro
{

// The timing bar above a note canvas, in two rows: bar numbers (and, once
// zoomed in, beat ticks labelled bar.beat) on top, the clock time of each bar
// line below. A left press or drag sets the start marker to the exact tick under
// the pointer (the marker's line and triangle are drawn here, continuing into
// the canvas below via MarkerOverlay); a right press or drag moves the playhead
// instead. Pressing the marker's triangle clears the marker.
class TimelineRuler : public juce::Component, public juce::SettableTooltipClient
{
public:
    static constexpr int rowHeight = 14;
    static constexpr int height = 2 * rowHeight;

    explicit TimelineRuler (std::function<double (int x)> tickForXIn) : tickForX (std::move (tickForXIn))
    {
        setOpaque (true);
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
        setTooltip ("Click or drag to set the start marker; right-click to move the playhead");
    }

    std::function<void (double tick)> onSeek;

    // Turns on the bar/beat/clock marks. `xForTick` is the inverse of the
    // constructor's tickForX (ruler-local x); `grid` supplies the tempo map and
    // meter afresh on every paint, so edits show without any invalidation.
    void setMarks (std::function<int (double tick)> xForTickIn, std::function<RulerGrid()> gridIn)
    {
        xForTick = std::move (xForTickIn);
        grid = std::move (gridIn);
        repaint();
    }

    // Marks left of this x are not drawn (the main view's track-info column /
    // the editor's keyboard gutter sit under part of the ruler).
    void setContentLeft (int x)
    {
        contentLeft = x;
        repaint();
    }

    std::function<void (double tick)> onSetMarker;
    std::function<void()> onClearMarker;

    // Where the start marker is (none = not set). Needs setMarks' xForTick to draw.
    void setMarker (std::function<std::optional<double>()> markerIn)
    {
        marker = std::move (markerIn);
        repaint();
    }

    // The marker's triangle, in ruler coordinates; none while no marker is set.
    std::optional<juce::Rectangle<int>> markerHandleBounds() const;

    void paint (juce::Graphics& g) override;
    void mouseDown (const juce::MouseEvent& e) override;
    void mouseDrag (const juce::MouseEvent& e) override { applyPointer (e, /*pressed*/ false); }
    void mouseUp (const juce::MouseEvent&) override { handleWasPressed = false; }

private:
    void applyPointer (const juce::MouseEvent& e, bool pressed);

    std::function<double (int)> tickForX;
    std::function<int (double)> xForTick;
    std::function<RulerGrid()> grid;
    std::function<std::optional<double>()> marker;
    int contentLeft = 0;
    bool handleWasPressed = false;   // a press that cleared the marker: its drag must not set it again
};

} // namespace lotro
