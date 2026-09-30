#pragma once

#include <juce_core/juce_core.h>

// Songsmith's theming palette (Songsmith Arch doc, "Theming" section) and the
// MIDI-track colours by GM instrument family. Header-only, plain constexpr ARGB
// values — deliberately NO juce_gui_basics include, since SongModelBridge.cpp
// (which assigns track colours on import) is compiled into forge_tests. As
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

    // Phase 7 -- source-role selection highlight (roll editing).
    constexpr juce::uint32 selectionHighlight = 0xFFFFFFFF;

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

    // Track colour by instrument family. The 16 General MIDI families are
    // the 8-program blocks of the GM program table (program / 8); Drums is a
    // 17th, keyed off channel 10 rather than the program number.
    enum class GmFamily
    {
        Piano, ChromaticPercussion, Organ, Guitar, Bass, Strings, Ensemble, Brass,
        Reed, Pipe, SynthLead, SynthPad, SynthEffects, Ethnic, Percussive, SoundEffects,
        Drums,
    };
    constexpr int numGmFamilies = 17;

    constexpr GmFamily gmFamilyFor (int program, int midiChannel) noexcept
    {
        if (midiChannel == 10)
            return GmFamily::Drums;
        const int clamped = program < 0 ? 0 : (program > 127 ? 127 : program);
        return static_cast<GmFamily> (clamped / 8);
    }

    // Indexed by GmFamily. Families that tend to share a score (Strings vs
    // Ensemble, Reed vs Pipe, Guitar vs Bass) are pushed apart in hue.
    constexpr juce::uint32 familyBaseColour[numGmFamilies] = {
        0xFFE0D2A8, // Piano               — ivory
        0xFFE0A0C0, // ChromaticPercussion — pink
        0xFFB88850, // Organ               — brown
        0xFFE8A060, // Guitar              — orange
        0xFFB86048, // Bass                — rust
        0xFF80C878, // Strings             — green
        0xFF60B8B0, // Ensemble            — teal
        0xFFE8C840, // Brass               — gold
        0xFF6890E0, // Reed                — blue
        0xFF70D8E8, // Pipe                — cyan
        0xFFB880F0, // SynthLead           — violet
        0xFF8C84C8, // SynthPad            — slate lavender
        0xFFD870D0, // SynthEffects        — magenta
        0xFFA8C850, // Ethnic              — olive
        0xFFE88080, // Percussive          — salmon
        0xFF989898, // SoundEffects        — grey
        0xFFE04848, // Drums               — red
    };

    // Same-family tracks cycle through these shades (base, darker, lighter,
    // much darker) so their chips/ghosts stay distinguishable.
    constexpr int numFamilyShades = 4;

    namespace detail
    {
        constexpr juce::uint32 scaleChannels (juce::uint32 argb, int towards, int percent) noexcept
        {
            juce::uint32 out = argb & 0xFF000000u;
            for (int shift = 0; shift <= 16; shift += 8)
            {
                const int c = (int) ((argb >> shift) & 0xFFu);
                const int scaled = c + (towards - c) * percent / 100;
                out |= (juce::uint32) scaled << shift;
            }
            return out;
        }
    }

    constexpr juce::uint32 trackColourFor (GmFamily family, int indexWithinFamily) noexcept
    {
        const auto base = familyBaseColour[static_cast<int> (family)];
        switch ((indexWithinFamily % numFamilyShades + numFamilyShades) % numFamilyShades)
        {
            case 1:  return detail::scaleChannels (base, 0,   25);
            case 2:  return detail::scaleChannels (base, 255, 35);
            case 3:  return detail::scaleChannels (base, 0,   45);
            default: return base;
        }
    }

}
