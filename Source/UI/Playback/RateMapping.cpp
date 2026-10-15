#include "UI/Playback/RateMapping.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace lotro
{

namespace
{
    constexpr double kSnapWindow = 0.02;   // slider (log2) units
}

double clampRate (double rate) noexcept
{
    if (std::isnan (rate))
        return 1.0;
    return std::clamp (rate, kMinPlaybackRate, kMaxPlaybackRate);
}

double rateToSlider (double rate) noexcept { return std::log2 (clampRate (rate)); }

double sliderToRate (double slider) noexcept
{
    if (std::abs (slider) < kSnapWindow)
        return 1.0;
    return clampRate (std::exp2 (slider));
}

std::string formatRate (double rate)
{
    char buffer[32];
    std::snprintf (buffer, sizeof buffer, "%.3f", rate);
    return buffer;
}

std::optional<double> parseRate (const std::string& text)
{
    const auto first = text.find_first_not_of (" \t");
    if (first == std::string::npos)
        return std::nullopt;
    const auto last = text.find_last_not_of (" \t");
    const std::string trimmed = text.substr (first, last - first + 1);
    char* end = nullptr;
    const double value = std::strtod (trimmed.c_str(), &end);
    if (end != trimmed.c_str() + trimmed.size() || ! std::isfinite (value))
        return std::nullopt;
    return clampRate (value);
}

} // namespace lotro
