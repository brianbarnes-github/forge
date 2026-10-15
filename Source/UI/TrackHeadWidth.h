#pragma once

#include <algorithm>

// The main screen's track-head column width bounds: the user drags the divider
// between the heads and the note strips within [minimum, maximum]. The minimum is
// the original fixed width; the maximum doubles it. Header-only, no JUCE dependency.
namespace lotro::headwidth
{

constexpr int minimum = 200;
constexpr int maximum = 400;

constexpr int clamp (int width) noexcept
{
    return std::clamp (width, minimum, maximum);
}

} // namespace lotro::headwidth
