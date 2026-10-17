#include "UI/NoteColours.h"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>

using namespace lotro;

namespace
{
    // True when every RGB channel of a and b is within tolerance and both are opaque.
    bool isNearArgb (std::uint32_t a, std::uint32_t b, int tolerance)
    {
        if ((a >> 24) != 0xFFu || (b >> 24) != 0xFFu)
            return false;

        for (int shift : { 16, 8, 0 })
            if (std::abs (static_cast<int> ((a >> shift) & 0xFFu) - static_cast<int> ((b >> shift) & 0xFFu)) > tolerance)
                return false;

        return true;
    }
}

TEST_CASE ("noteColourFor: Track is the track's colour unchanged", "[note-colours]")
{
    CHECK (noteColourFor (NoteColourMode::track, 60, 100, 3, 0xFF123456u) == 0xFF123456u);
}

TEST_CASE ("noteColourFor: Velocity runs the heat map, 0 blue to 127 red, stops evenly spaced", "[note-colours]")
{
    using M = NoteColourMode;
    CHECK (noteColourFor (M::velocity, 60, 0,   1, 0) == 0xFF2F5BD8u);
    CHECK (noteColourFor (M::velocity, 60, 127, 1, 0) == 0xFFE52C32u);
    // 127 * 4 / 8 = 63.5 sits on the middle stop; 63 and 64 straddle it.
    CHECK (noteColourFor (M::velocity, 60, 64,  1, 0) != noteColourFor (M::velocity, 60, 63, 1, 0));
    CHECK (noteColourFor (M::velocity, 60, -5,  1, 0) == 0xFF2F5BD8u);   // clamps
    CHECK (noteColourFor (M::velocity, 60, 200, 1, 0) == 0xFFE52C32u);
}

TEST_CASE ("noteColourFor: every velocity stop sits at 127 * k / 8", "[note-colours]")
{
    constexpr std::array<std::uint32_t, 9> stops { 0xFF2F5BD8u, 0xFF2D8FE0u, 0xFF2FC0C8u, 0xFF45C97Au, 0xFF9CCF3Eu,
                                                   0xFFE6CF35u, 0xFFF0A030u, 0xFFEC6A2Cu, 0xFFE52C32u };

    // Stops fall between integer velocities, so the nearest velocity is at most half a step
    // (1/16 of a stop gap) away; 8 per channel covers the steepest gap.
    for (int k = 0; k < 9; ++k)
    {
        const int velocity = static_cast<int> (std::lround (127.0 * k / 8.0));
        INFO ("stop " << k << " velocity " << velocity);
        CHECK (isNearArgb (noteColourFor (NoteColourMode::velocity, 60, velocity, 1, 0), stops[(size_t) k], 8));
    }
}

TEST_CASE ("noteColourFor: Channel uses the 16-colour table, 10 grey, out of range clamps", "[note-colours]")
{
    using M = NoteColourMode;
    CHECK (noteColourFor (M::channel, 60, 64, 1,  0) == 0xFFE5484Du);
    CHECK (noteColourFor (M::channel, 60, 64, 10, 0) == 0xFF9A9EA6u);
    CHECK (noteColourFor (M::channel, 60, 64, 16, 0) == 0xFFA0A4E0u);
    CHECK (noteColourFor (M::channel, 60, 64, 0,  0) == 0xFFE5484Du);
    CHECK (noteColourFor (M::channel, 60, 64, 17, 0) == 0xFFA0A4E0u);
}

TEST_CASE ("noteColourFor: every channel has its spec colour", "[note-colours]")
{
    constexpr std::array<std::uint32_t, 16> table { 0xFFE5484Du, 0xFFF08A3Cu, 0xFFE8CF3Au, 0xFF9CCF3Eu,
                                                    0xFF45C97Au, 0xFF3FB6C6u, 0xFF4F9CE8u, 0xFF4F6AE8u,
                                                    0xFF8A63E0u, 0xFF9A9EA6u, 0xFFD65FC8u, 0xFFE86A96u,
                                                    0xFFB5835Au, 0xFF7FB3A0u, 0xFFC9B27Au, 0xFFA0A4E0u };

    for (int channel = 1; channel <= 16; ++channel)
    {
        INFO ("channel " << channel);
        CHECK (noteColourFor (NoteColourMode::channel, 60, 64, channel, 0) == table[(size_t) (channel - 1)]);
    }
}

TEST_CASE ("noteColourFor: Pitch colours by pitch class, sharps halfway, every octave alike", "[note-colours]")
{
    using M = NoteColourMode;
    CHECK (noteColourFor (M::pitch, 60, 64, 1, 0) == 0xFFE5484Du);   // C4
    CHECK (noteColourFor (M::pitch, 0,  64, 1, 0) == 0xFFE5484Du);   // C-1
    CHECK (noteColourFor (M::pitch, 61, 64, 1, 0) == 0xFFEA6944u);   // C#
    CHECK (noteColourFor (M::pitch, 66, 64, 1, 0) == 0xFF4DBD98u);   // F#
    CHECK (noteColourFor (M::pitch, 71, 64, 1, 0) == 0xFFA46BE0u);   // B
}

TEST_CASE ("noteColourFor: every pitch class has its spec colour in every octave", "[note-colours]")
{
    constexpr std::array<std::uint32_t, 12> table { 0xFFE5484Du, 0xFFEA6944u, 0xFFF08A3Cu, 0xFFECAC3Bu,
                                                    0xFFE8CF3Au, 0xFF5CC46Au, 0xFF4DBD98u, 0xFF3FB6C6u,
                                                    0xFF4799D7u, 0xFF4F7CE8u, 0xFF7973E4u, 0xFFA46BE0u };

    for (int pitch = 0; pitch <= 127; ++pitch)
    {
        INFO ("pitch " << pitch);
        CHECK (noteColourFor (NoteColourMode::pitch, pitch, 64, 1, 0) == table[(size_t) (pitch % 12)]);
    }
}
