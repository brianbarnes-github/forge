#pragma once

#include <juce_core/juce_core.h>

// Songsmith's theming palette (Songsmith Arch doc, "Theming" section) and the
// MIDI-track swatch colour cycle (A-R5). Header-only, plain constexpr ARGB
// values — deliberately NO juce_gui_basics include, since SongModelBridge.cpp
// (which assigns track swatches on import) is compiled into forge_tests,
// which links only juce_core/juce_data_structures, not juce_gui_basics.
namespace lotro::SongsmithColours
{
    constexpr juce::uint32 background   = 0xFF2B2B2B;
    constexpr juce::uint32 panelHeader  = 0xFF383838;
    constexpr juce::uint32 selectedRow  = 0xFF3D4A5A;
    constexpr juce::uint32 border       = 0xFF1A1A1A;
    constexpr juce::uint32 text         = 0xFFD0D0D0;
    constexpr juce::uint32 textMuted    = 0xFF888888;
    constexpr juce::uint32 accentAmber  = 0xFFE0B080;

    // 8-entry MIDI-track swatch cycle. The first four match the Songsmith UI
    // Guide mockup's track rows; the remaining four extend the cycle for
    // documents with more than four tracks.
    constexpr juce::uint32 trackSwatch[8] = {
        0xFF7FA8D0, 0xFFC89CC0, 0xFF9CC09C, 0xFFC0C09C,
        0xFFD08060, 0xFF80C0C0, 0xFFC0A0E0, 0xFFE0C080,
    };

    // Cycles trackSwatch by a document-wide track index (not a per-import
    // one), so a second MIDI import continues the cycle where the first left
    // off rather than restarting at entry 0.
    constexpr juce::uint32 trackColourForIndex (int index) noexcept
    {
        return trackSwatch[(index % 8 + 8) % 8];
    }
}
