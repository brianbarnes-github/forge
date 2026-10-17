#include "NoteColours.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>

namespace lotro
{

namespace
{
    // Blue through cyan, green, yellow and orange to red, evenly spaced over 0..127.
    constexpr std::array<std::uint32_t, 9> velocityStops { 0xFF2F5BD8u, 0xFF2D8FE0u, 0xFF2FC0C8u, 0xFF45C97Au,
                                                           0xFF9CCF3Eu, 0xFFE6CF35u, 0xFFF0A030u, 0xFFEC6A2Cu,
                                                           0xFFE52C32u };

    // Channels 1..16; 10 (drums) is grey.
    constexpr std::array<std::uint32_t, 16> channelColours { 0xFFE5484Du, 0xFFF08A3Cu, 0xFFE8CF3Au, 0xFF9CCF3Eu,
                                                             0xFF45C97Au, 0xFF3FB6C6u, 0xFF4F9CE8u, 0xFF4F6AE8u,
                                                             0xFF8A63E0u, 0xFF9A9EA6u, 0xFFD65FC8u, 0xFFE86A96u,
                                                             0xFFB5835Au, 0xFF7FB3A0u, 0xFFC9B27Au, 0xFFA0A4E0u };

    // Pitch classes C..B; each sharp is halfway between its two naturals.
    constexpr std::array<std::uint32_t, 12> pitchClassColours { 0xFFE5484Du, 0xFFEA6944u, 0xFFF08A3Cu, 0xFFECAC3Bu,
                                                                0xFFE8CF3Au, 0xFF5CC46Au, 0xFF4DBD98u, 0xFF3FB6C6u,
                                                                0xFF4799D7u, 0xFF4F7CE8u, 0xFF7973E4u, 0xFFA46BE0u };

    std::uint32_t lerpArgb (std::uint32_t from, std::uint32_t to, double t)
    {
        std::uint32_t result = 0xFF000000u;
        for (int shift : { 16, 8, 0 })
        {
            const double a = (double) ((from >> shift) & 0xFFu);
            const double b = (double) ((to >> shift) & 0xFFu);
            result |= (std::uint32_t) std::lround (a + (b - a) * t) << shift;
        }
        return result;
    }

    std::uint32_t velocityColour (int velocity)
    {
        const double t = std::clamp (velocity, 0, 127) / 127.0 * (double) (velocityStops.size() - 1);
        const auto lower = std::min ((std::size_t) t, velocityStops.size() - 2);
        return lerpArgb (velocityStops[lower], velocityStops[lower + 1], t - (double) lower);
    }
}

std::uint32_t noteColourFor (NoteColourMode mode, int pitch, int velocity, int channel, std::uint32_t trackArgb)
{
    switch (mode)
    {
        case NoteColourMode::velocity: return velocityColour (velocity);
        case NoteColourMode::channel:  return channelColours[(std::size_t) (std::clamp (channel, 1, 16) - 1)];
        case NoteColourMode::pitch:    return pitchClassColours[(std::size_t) (((pitch % 12) + 12) % 12)];
        case NoteColourMode::track:    break;
    }
    return trackArgb;
}

} // namespace lotro
