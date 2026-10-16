#pragma once

#include "UI/Playback/LoopRange.h"
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
// instead. Pressing the marker's triangle clears the marker. A loop range is
// shaded through both rows with a bracket at each edge; dragging a bracket moves
// that edge and clicking one clears the range.
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
    int getContentLeft() const noexcept { return contentLeft; }
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

    // A left press on a loop edge's bracket grabs that edge: dragging reports the
    // tick under the pointer for it, and releasing without having moved 3 px
    // clears the range instead. The marker's triangle wins where they overlap.
    std::function<void (bool isStart, double tick)> onLoopEdgeDragged;
    std::function<void()> onClearLoop;
    // Fired by the press that grabs a bracket, before any onLoopEdgeDragged of its drag.
    std::function<void (bool isStart)> onLoopEdgePressed;

    // Where the loop range is (none = not set). Needs setMarks' xForTick to draw.
    void setLoopRange (std::function<std::optional<LoopRange>()> loopRangeIn)
    {
        loopRange = std::move (loopRangeIn);
        repaint();
    }

    // An edge's bracket (8 x 12 px in the top row; the start bracket extends
    // right from its edge, the end bracket left), in ruler coordinates; none
    // while no range is set.
    std::optional<juce::Rectangle<int>> loopHandleBounds (bool isStart) const;

    // Where the playhead is, for the copper cap in the top row (none = don't draw).
    void setPlayhead (std::function<std::optional<double>()> playheadIn)
    {
        playhead = std::move (playheadIn);
        lastCap = playheadCapBounds();
        repaint();
    }
    // The owner calls this when the playhead moves; repaints only the old and new cap.
    void playheadMoved();
    // The cap's triangle (10 x 7 px at the top), in ruler coordinates; none when unset or off-screen.
    std::optional<juce::Rectangle<int>> playheadCapBounds() const;

    void paint (juce::Graphics& g) override;
    void mouseDown (const juce::MouseEvent& e) override;
    void mouseDrag (const juce::MouseEvent& e) override;
    void mouseUp (const juce::MouseEvent& e) override;

private:
    void applyPointer (const juce::MouseEvent& e, bool pressed);

    std::function<double (int)> tickForX;
    std::function<int (double)> xForTick;
    std::function<RulerGrid()> grid;
    std::function<std::optional<double>()> marker;
    std::function<std::optional<LoopRange>()> loopRange;
    std::function<std::optional<double>()> playhead;
    std::optional<juce::Rectangle<int>> lastCap;   // the cap as last painted, for playheadMoved
    int contentLeft = 0;
    bool handleWasPressed = false;   // a press that cleared the marker: its drag must not set it again

    enum class LoopEdge { start, end };
    std::optional<LoopEdge> dragEdge;      // the loop edge grabbed by the current press
    bool pressedOnLoopHandle = false;      // so the press and its drag never set the marker
    bool loopDragMoved = false;            // the pointer has been 3 px or more from the press point
    juce::Point<int> loopPressPosition;
};

} // namespace lotro
