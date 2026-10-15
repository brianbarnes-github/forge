#pragma once

#include <optional>
#include <string>

namespace lotro
{

constexpr double kMinPlaybackRate = 0.25;
constexpr double kMaxPlaybackRate = 4.0;

// NaN -> 1.0; otherwise clamped to [kMinPlaybackRate, kMaxPlaybackRate].
double clampRate (double rate) noexcept;
// The knob works in log2 units (-2 .. 2) so 0.5 and 2.0 sit equally far from 1.0.
double rateToSlider (double rate) noexcept;
// Inverse of rateToSlider; within 0.02 slider (log2) units of the centre it returns exactly 1.0.
double sliderToRate (double slider) noexcept;
// "1.000": the readout.
std::string formatRate (double rate);
// The whole (trimmed) text must be a finite number; the result is clamped.
std::optional<double> parseRate (const std::string& text);

} // namespace lotro
