#include "TimelineViewState.h"

namespace lotro
{
    void TimelineViewState::setPixelsPerTick (double pixelsPerTickIn) noexcept
    {
        pixelsPerTick = juce::jlimit (minPixelsPerTick, maxPixelsPerTick, pixelsPerTickIn);
    }

    void TimelineViewState::setScrollOffsetTicks (double ticks) noexcept
    {
        scrollOffsetTicks = juce::jmax (0.0, ticks);
    }

    void TimelineViewState::zoomBy (double factor, int anchorX) noexcept
    {
        const double anchorTick = tickForX (anchorX);
        setPixelsPerTick (pixelsPerTick * factor);
        setScrollOffsetTicks (anchorTick - (double) anchorX / pixelsPerTick);
    }

    void TimelineViewState::scrollByPixels (int deltaX) noexcept
    {
        setScrollOffsetTicks (scrollOffsetTicks + (double) deltaX / pixelsPerTick);
    }

    int TimelineViewState::xForTick (int tick) const noexcept
    {
        return juce::roundToInt (((double) tick - scrollOffsetTicks) * pixelsPerTick);
    }

    int TimelineViewState::tickForX (int x) const noexcept
    {
        return juce::roundToInt (scrollOffsetTicks + (double) x / pixelsPerTick);
    }
}
