#pragma once

#include <juce_core/juce_core.h>

// Arithmetic shared by every "Ctrl+wheel resizes the rows" surface (the main
// track list and the Source piano roll), so the two cannot drift apart.
namespace lotro::wheelresize
{

// Adds `delta` to the unrounded `exact` value and clamps it to [lo, hi]. The
// unrounded value accumulates, so a touchpad's sub-pixel deltas add up to a
// whole pixel eventually instead of each rounding to nothing; hitting a limit
// discards the excess, so reversing starts moving at once.
inline double accumulateClamped (double exact, double delta, double lo, double hi) noexcept
{
    return juce::jlimit (lo, hi, exact + delta);
}

// The scroll position that keeps the same fractional position of the content
// under the pointer after the content's extent changes from `oldExtent` to
// `newExtent`. `pointerInViewport` is the pointer's offset inside the viewport;
// the result is clamped to [0, maxScroll].
inline int anchoredScroll (int oldScroll, int pointerInViewport, int oldExtent, int newExtent, int maxScroll) noexcept
{
    const double fraction = oldExtent > 0 ? (double) (oldScroll + pointerInViewport) / (double) oldExtent : 0.0;
    return juce::jlimit (0, juce::jmax (0, maxScroll),
                         juce::roundToInt (fraction * (double) newExtent) - pointerInViewport);
}

} // namespace lotro::wheelresize
