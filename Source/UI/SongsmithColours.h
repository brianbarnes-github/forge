#pragma once

#include <juce_core/juce_core.h>

// Songsmith's theming palette (Songsmith Arch doc, "Theming" section) and the
// MIDI-track swatch colour cycle (A-R5). Header-only, plain constexpr ARGB
// values — deliberately NO juce_gui_basics include, since SongModelBridge.cpp
// (which assigns track swatches on import) is compiled into forge_tests. As
// of Phase 5 forge_tests also links juce_gui_basics (for TrackListComponent/
// TrackRowComponent tests), but SongModelBridge.cpp itself still doesn't
// need it, so this header stays juce_core-only on principle.
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

    // Phase 6 — preview roll overlays (Songsmith UI Guide mockup's "LOTRO
    // preview" pane). Colour is driven entirely by NoteState, not by the
    // note's original source track: the mockup renders every preview note
    // in one instrument-accent colour regardless of source track, and the
    // session lead decided during Phase 6 not to deviate from that (see
    // the plan's "Multi-track-per-part preview legibility" deferred
    // decision, resolved 2026-09-14).
    constexpr juce::uint32 previewNoteNormal    = 0xFF7FA8D0;
    constexpr juce::uint32 previewNoteBorder    = 0xFFA0C0E0;
    constexpr juce::uint32 outOfRangeFill       = 0xFFC06060;
    constexpr juce::uint32 outOfRangeBorder     = 0xFFE08080;
    constexpr juce::uint32 rangeBandFill        = 0x0F7FA8D0;
    constexpr juce::uint32 rangeBandBorder      = 0xFF5A7A9A;
    constexpr juce::uint32 outOfRangeZoneFillHi = 0x14C06060;
    constexpr juce::uint32 outOfRangeZoneFillLo = 0x0AC06060;

    // Cycles trackSwatch by a document-wide track index (not a per-import
    // one), so a second MIDI import continues the cycle where the first left
    // off rather than restarting at entry 0.
    constexpr juce::uint32 trackColourForIndex (int index) noexcept
    {
        return trackSwatch[(index % 8 + 8) % 8];
    }
}
