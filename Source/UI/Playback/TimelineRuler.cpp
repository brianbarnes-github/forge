#include "UI/Playback/TimelineRuler.h"

#include "UI/SongsmithColours.h"
#include "UI/Theme/ThemePaint.h"

namespace lotro
{

std::optional<juce::Rectangle<int>> TimelineRuler::markerHandleBounds() const
{
    if (! marker || ! xForTick)
        return std::nullopt;
    const auto tick = marker();
    if (! tick.has_value())
        return std::nullopt;
    constexpr int handleWidth = 16, handleHeight = 12;
    return juce::Rectangle<int> (xForTick (*tick) - handleWidth / 2, 0, handleWidth, handleHeight);
}

namespace
{
    constexpr int loopHandleWidth = 8, loopHandleHeight = 12;
    constexpr int loopClickSlop = 3;   // a bracket press that never moves this far is a click (clear)
}

std::optional<juce::Rectangle<int>> TimelineRuler::loopHandleBounds (bool isStart) const
{
    if (! loopRange || ! xForTick)
        return std::nullopt;
    const auto range = loopRange();
    if (! range.has_value())
        return std::nullopt;
    if (isStart)
        return juce::Rectangle<int> (xForTick (range->startTick), 0, loopHandleWidth, loopHandleHeight);
    return juce::Rectangle<int> (xForTick (range->endTick) - loopHandleWidth, 0, loopHandleWidth, loopHandleHeight);
}

void TimelineRuler::mouseDrag (const juce::MouseEvent& e)
{
    if (pressedOnLoopHandle)
    {
        if (e.getPosition().getDistanceFrom (loopPressPosition) >= loopClickSlop)
            loopDragMoved = true;
        if (dragEdge.has_value() && tickForX && onLoopEdgeDragged)
            onLoopEdgeDragged (*dragEdge == LoopEdge::start, tickForX (e.x));
        return;
    }
    applyPointer (e, /*pressed*/ false);
}

void TimelineRuler::mouseUp (const juce::MouseEvent& e)
{
    const bool loopClick = pressedOnLoopHandle && ! loopDragMoved
                           && e.getPosition().getDistanceFrom (loopPressPosition) < loopClickSlop;
    handleWasPressed = false;
    pressedOnLoopHandle = false;
    loopDragMoved = false;
    dragEdge.reset();
    if (loopClick && onClearLoop)
        onClearLoop();
}

void TimelineRuler::mouseDown (const juce::MouseEvent& e)
{
    handleWasPressed = false;
    pressedOnLoopHandle = false;
    loopDragMoved = false;
    dragEdge.reset();
    if (! e.mods.isRightButtonDown() && ! e.mods.isPopupMenu())
    {
        for (const auto edge : { LoopEdge::start, LoopEdge::end })
        {
            const auto bracket = loopHandleBounds (edge == LoopEdge::start);
            // Not where the info column hides it: brackets are only drawn right of contentLeft.
            if (bracket.has_value() && bracket->contains (e.getPosition()) && e.x >= contentLeft)
            {
                pressedOnLoopHandle = true;
                dragEdge = edge;
                loopPressPosition = e.getPosition();
                if (onLoopEdgePressed)
                    onLoopEdgePressed (edge == LoopEdge::start);
                return;
            }
        }
        // A loop bracket wins where it overlaps the marker's triangle (checked above); the rest of the triangle clears the marker.
        const auto handle = markerHandleBounds();
        // Not where the info column hides it: the triangle is only drawn right of contentLeft.
        if (handle.has_value() && handle->contains (e.getPosition()) && e.x >= contentLeft)
        {
            handleWasPressed = true;
            if (onClearMarker)
                onClearMarker();
            return;
        }
    }
    applyPointer (e, /*pressed*/ true);
}

void TimelineRuler::applyPointer (const juce::MouseEvent& e, bool)
{
    if (handleWasPressed || pressedOnLoopHandle || ! tickForX)
        return;

    const double tick = juce::jmax (0.0, tickForX (e.x));
    if (e.mods.isRightButtonDown() || e.mods.isPopupMenu())
    {
        if (onSeek)
            onSeek (tick);
    }
    else if (onSetMarker)
    {
        onSetMarker (tick);
    }
}

void TimelineRuler::paint (juce::Graphics& g)
{
    ThemePaint::paintPanelGradient (g, getLocalBounds());

    if (xForTick && grid && tickForX)
    {
        const double firstTick = tickForX (contentLeft);
        const double lastTick = tickForX (getWidth());
        const int visiblePixels = getWidth() - contentLeft;
        const double pixelsPerTick = lastTick > firstTick ? (double) visiblePixels / (lastTick - firstTick) : 0.0;

        g.saveState();
        g.reduceClipRegion (contentLeft, 0, juce::jmax (0, visiblePixels), getHeight());
        g.setFont (juce::Font (juce::FontOptions (9.0f)));

        // The loop range's shading sits under the marks, through both rows.
        if (const auto range = loopRange ? loopRange() : std::optional<LoopRange>(); range.has_value())
        {
            const int first = xForTick (range->startTick);
            const int last = xForTick (range->endTick);   // exclusive
            if (first < last)
            {
                g.setColour (juce::Colour (SongsmithColours::markerIvory).withAlpha (0.14f));
                g.fillRect (first, 0, last - first, getHeight());
            }
        }

        for (const auto& mark : computeRulerMarks (firstTick, lastTick, pixelsPerTick, grid()))
        {
            const int x = xForTick (mark.tick);
            const bool isBar = mark.kind == RulerMark::Kind::Bar;

            // A bar line crosses both rows; a beat tick is a short stub in the
            // lower half of the top row.
            g.setColour (juce::Colour (isBar ? SongsmithColours::text : SongsmithColours::textMuted).withAlpha (isBar ? 0.8f : 0.6f));
            g.fillRect (x, isBar ? 0 : rowHeight / 2, 1, isBar ? getHeight() - 1 : rowHeight / 2);

            g.setColour (juce::Colour (isBar ? SongsmithColours::text : SongsmithColours::textMuted));
            g.drawText (mark.label, x + 3, 0, 56, rowHeight - 1, juce::Justification::centredLeft, false);
            if (! mark.timeLabel.empty())
            {
                g.setColour (juce::Colour (SongsmithColours::textMuted));
                g.drawText (mark.timeLabel, x + 3, rowHeight, 64, rowHeight - 1, juce::Justification::centredLeft, false);
            }
        }

        // Each edge: a faint line through both rows (continuing LoopRangeOverlay's
        // below) and a bracket in the top row opening into the range. Drawn before
        // the marker, which stays on top.
        const auto ivory = juce::Colour (SongsmithColours::markerIvory);
        for (const bool isStart : { true, false })
        {
            const auto bracket = loopHandleBounds (isStart);
            if (! bracket.has_value())
                continue;
            constexpr int stroke = 2;
            const int uprightX = isStart ? bracket->getX() : bracket->getRight() - stroke;
            g.setColour (ivory.withAlpha (0.6f));
            g.fillRect (isStart ? bracket->getX() : bracket->getRight() - 1, bracket->getBottom(),
                        1, getHeight() - bracket->getBottom());
            g.setColour (ivory);
            g.fillRect (uprightX, bracket->getY(), stroke, bracket->getHeight());
            g.fillRect (bracket->getX(), bracket->getY(), bracket->getWidth(), stroke);
            g.fillRect (bracket->getX(), bracket->getBottom() - stroke, bracket->getWidth(), stroke);
        }

        if (const auto handle = markerHandleBounds())
        {
            const int x = handle->getCentreX();
            g.setColour (juce::Colour (SongsmithColours::markerIvory));
            g.fillRect (x, handle->getBottom(), 1, getHeight() - handle->getBottom());
            juce::Path flag;
            flag.addTriangle ((float) handle->getX(), 0.0f, (float) handle->getRight(), 0.0f,
                              (float) x + 0.5f, (float) handle->getBottom());
            g.fillPath (flag);
        }

        // The playhead's cap, on top of everything else.
        if (const auto cap = playheadCapBounds())
        {
            juce::Path triangle;
            triangle.addTriangle ((float) cap->getX(), 0.0f, (float) cap->getRight(), 0.0f,
                                  (float) cap->getCentreX() + 0.5f, (float) cap->getBottom());
            g.setColour (juce::Colour (SongsmithColours::accentCopper));
            g.fillPath (triangle);
        }
        lastCap = playheadCapBounds();   // a full repaint (zoom, scroll) resyncs playheadMoved's bookkeeping

        g.restoreState();
    }
}

std::optional<juce::Rectangle<int>> TimelineRuler::playheadCapBounds() const
{
    if (! playhead || ! xForTick)
        return std::nullopt;
    const auto tick = playhead();
    if (! tick.has_value())
        return std::nullopt;
    const int x = xForTick (*tick);
    if (x < contentLeft || x >= getWidth())
        return std::nullopt;
    constexpr int capWidth = 10, capHeight = 7;
    return juce::Rectangle<int> (x - capWidth / 2, 0, capWidth, capHeight);
}

void TimelineRuler::playheadMoved()
{
    const auto cap = playheadCapBounds();
    if (cap == lastCap)
        return;
    if (lastCap) repaint (lastCap->expanded (1));
    if (cap)     repaint (cap->expanded (1));
    lastCap = cap;
}

} // namespace lotro
