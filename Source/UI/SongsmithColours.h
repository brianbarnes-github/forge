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
    constexpr juce::uint32 background    = 0xFF17191C;
    constexpr juce::uint32 panelHeader   = 0xFF24272B;
    // Header / transport-strip / button surfaces: a subtle top-to-bottom
    // gradient with a 1 px highlight on top (ThemePaint::paintPanelGradient).
    constexpr juce::uint32 surfaceTop    = 0xFF2C2F34;
    constexpr juce::uint32 surfaceBottom = 0xFF1C1E22;
    constexpr juce::uint32 edgeHighlight = 0x0FFFFFFF;
    constexpr juce::uint32 selectedRow   = 0xFF2E3238;
    constexpr juce::uint32 border        = 0xFF0E0F11;
    constexpr juce::uint32 text          = 0xFFDADCE0;
    constexpr juce::uint32 textMuted     = 0xFF8B8F96;
    constexpr juce::uint32 accentCopper  = 0xFFE8955A;

    // The start marker and loop range: ivory, so the copper playhead stays distinct.
    constexpr juce::uint32 markerIvory   = 0xFFE6DFD2;

    // 1px line between MIDI-source track rows — lighter than `background`
    // (unlike `border`, which is darker and vanishes against it).
    constexpr juce::uint32 trackDivider   = 0xFF2E3136;
    constexpr juce::uint32 columnDivider  = 0xFF4A4E55;
    // Instrument-name band above each track's notes (the Reaper-style label bar).
    constexpr juce::uint32 instrumentBand = 0xFF25282C;

    // Phase 7 -- source-role selection highlight (roll editing).
    constexpr juce::uint32 selectionHighlight = 0xFFFFFFFF;

    // Conductor row tempo / meter markers (opaque). conductorMeter intentionally
    // shares previewNoteNormal's value.
    constexpr juce::uint32 conductorTempo       = accentCopper;
    constexpr juce::uint32 conductorMeter       = 0xFF5CC3DE;

    // The Tempo and Meter window's selected-event line across the track list.
    // Green, so it differs from the copper playhead / conductor tempo, the
    // ivory start marker and the cyan conductor meter / preview notes.
    constexpr juce::uint32 eventMarker          = 0xFF5FD38B;

    // Phase 6 — preview roll overlays (Songsmith UI Guide mockup's "LOTRO
    // preview" pane). Colour is driven entirely by NoteState, not by the
    // note's original source track: the mockup renders every preview note
    // in one instrument-accent colour regardless of source track, and the
    // session lead decided during Phase 6 not to deviate from that (see
    // the plan's "Multi-track-per-part preview legibility" deferred
    // decision, resolved 2026-09-14).
    constexpr juce::uint32 previewNoteNormal    = 0xFF5CC3DE;
    constexpr juce::uint32 previewNoteBorder    = 0xFF8FD8EC;
    // The source roll's header label ("MIDI SOURCE ...").
    constexpr juce::uint32 sourceHeaderLabel    = previewNoteNormal;
    constexpr juce::uint32 outOfRangeFill       = 0xFFC06060;
    constexpr juce::uint32 outOfRangeBorder     = 0xFFE08080;
    constexpr juce::uint32 rangeBandFill        = 0x0F5CC3DE;
    constexpr juce::uint32 rangeBandBorder      = 0xFF3F8AA0;
    constexpr juce::uint32 outOfRangeZoneFillHi = 0x14C06060;
    constexpr juce::uint32 outOfRangeZoneFillLo = 0x0AC06060;

    // Track sections on the main canvas's note strips.
    constexpr juce::uint32 sectionFill          = 0x22FFFFFF;
    constexpr juce::uint32 sectionSelectedFill  = 0x44FFD27F;
    constexpr juce::uint32 mergeTarget          = 0xFF7FD2FF;
    constexpr juce::uint32 mergeTargetFill      = 0x337FD2FF;
    constexpr juce::uint32 sectionEdge          = 0xAAFFFFFF;
    constexpr juce::uint32 sectionSelectedEdge  = 0xFFFFD27F;

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
        0xFFF0E0B0, // Piano               — ivory
        0xFFF0A8CC, // ChromaticPercussion — pink
        0xFFCC9450, // Organ               — brown
        0xFFF8A860, // Guitar              — orange
        0xFFCC6448, // Bass                — rust
        0xFF84DC78, // Strings             — green
        0xFF5CCCC2, // Ensemble            — teal
        0xFFF8D840, // Brass               — gold
        0xFF6898F4, // Reed                — blue
        0xFF70E8F8, // Pipe                — cyan
        0xFFC484FF, // SynthLead           — violet
        0xFF948CDC, // SynthPad            — slate lavender
        0xFFEC74E4, // SynthEffects        — magenta
        0xFFB4DC50, // Ethnic              — olive
        0xFFF88484, // Percussive          — salmon
        0xFFA4A4A4, // SoundEffects        — grey
        0xFFF44C4C, // Drums               — red
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
