#pragma once

#include <juce_core/juce_core.h>

// The piano roll's own row-band and keyboard colours, in a header so the
// keyboard tests can name them. Not part of the general palette.
namespace lotro::PianoRollPalette
{
    constexpr juce::uint32 rowBandLight  = 0xFF1B1D21;
    constexpr juce::uint32 rowBandDark   = 0xFF17191C;
    // Drawn over rowBandLight for the active row (the semitone the last click
    // landed on), whatever its key colour, so sharps and naturals match.
    constexpr juce::uint32 activeRowTint = 0x14E8955A;

    constexpr juce::uint32 whiteKey          = 0xFFECE8E0;
    constexpr juce::uint32 whiteKeyShade     = 0xFFD4D0C8;   // right edge of a white key
    constexpr juce::uint32 blackKeyTop       = 0xFF1A1B1E;
    constexpr juce::uint32 blackKeyBottom    = 0xFF2A2C30;
    constexpr juce::uint32 blackKeyHighlight = 0x26FFFFFF;   // glossy 1 px top edge
    constexpr juce::uint32 whiteKeyDivider   = 0xFF9A968E;
    constexpr juce::uint32 whiteKeyLabel     = 0xFF5A5650;
    constexpr juce::uint32 whiteKeyHover     = 0xFFD8D3C9;
    constexpr juce::uint32 blackKeyHover     = 0xFF2D2F32;   // still dark (brightness < 0.2)
    constexpr juce::uint32 hoverLabel        = 0xFF8C8880;

    // The gutter in MIDI Numbers mode: the row bands above, with these.
    constexpr juce::uint32 numberLabel       = 0xFF7A7E85;
    constexpr juce::uint32 numberHover       = 0xFF2A2D32;   // still dark (brightness < 0.2)
    constexpr juce::uint32 numberHoverLabel  = 0xFFDADCE0;
    constexpr juce::uint32 numberDivider     = 0xFF3A3D43;   // under C and F, like the keys' full-width edge
}
