// Tests/SongsmithColours_tests.cpp
#include "UI/SongsmithColours.h"
#include "UI/Theme/PianoRollPalette.h"

#include <juce_graphics/juce_graphics.h>
#include <catch2/catch_test_macros.hpp>

#include <set>

using namespace lotro;
using namespace lotro::SongsmithColours;

TEST_CASE ("SongsmithColours: every GM program maps to its 8-program family block", "[songsmith-colours]")
{
    CHECK (gmFamilyFor (0,   1) == GmFamily::Piano);
    CHECK (gmFamilyFor (7,   1) == GmFamily::Piano);
    CHECK (gmFamilyFor (8,   1) == GmFamily::ChromaticPercussion);
    CHECK (gmFamilyFor (24,  1) == GmFamily::Guitar);
    CHECK (gmFamilyFor (40,  1) == GmFamily::Strings);   // Violin
    CHECK (gmFamilyFor (48,  1) == GmFamily::Ensemble);  // String Ensemble 1
    CHECK (gmFamilyFor (56,  1) == GmFamily::Brass);     // Trumpet
    CHECK (gmFamilyFor (71,  1) == GmFamily::Reed);      // Clarinet
    CHECK (gmFamilyFor (73,  1) == GmFamily::Pipe);      // Flute
    CHECK (gmFamilyFor (118, 1) == GmFamily::Percussive);// Synth Drum
    CHECK (gmFamilyFor (127, 1) == GmFamily::SoundEffects);

    for (int program = 0; program < 128; ++program)
        CHECK (static_cast<int> (gmFamilyFor (program, 1)) == program / 8);
}

TEST_CASE ("SongsmithColours: channel 10 is Drums regardless of program", "[songsmith-colours]")
{
    CHECK (gmFamilyFor (0,  10) == GmFamily::Drums);
    CHECK (gmFamilyFor (40, 10) == GmFamily::Drums);
}

TEST_CASE ("SongsmithColours: out-of-range programs clamp instead of indexing past the table", "[songsmith-colours]")
{
    CHECK (gmFamilyFor (-5,  1) == GmFamily::Piano);
    CHECK (gmFamilyFor (400, 1) == GmFamily::SoundEffects);
}

TEST_CASE ("SongsmithColours: all 17 family base colours are distinct", "[songsmith-colours]")
{
    std::set<juce::uint32> seen;
    for (int f = 0; f < numGmFamilies; ++f)
        seen.insert (trackColourFor (static_cast<GmFamily> (f), 0));
    CHECK ((int) seen.size() == numGmFamilies);
}

TEST_CASE ("SongsmithColours: tracks within one family share a hue but get distinct shades", "[songsmith-colours]")
{
    const auto base = juce::Colour (trackColourFor (GmFamily::Strings, 0));

    std::set<juce::uint32> shades;
    for (int n = 0; n < numFamilyShades; ++n)
    {
        const auto shade = juce::Colour (trackColourFor (GmFamily::Strings, n));
        shades.insert (shade.getARGB());
        CHECK (std::abs (shade.getHue() - base.getHue()) < 0.02f);
        CHECK (shade.getAlpha() == 0xFF);
    }
    CHECK ((int) shades.size() == numFamilyShades);

    // The shade cycle wraps rather than running off the end.
    CHECK (trackColourFor (GmFamily::Strings, numFamilyShades) == trackColourFor (GmFamily::Strings, 0));
}

TEST_CASE ("SongsmithColours: the restyle palette", "[theme]")
{
    CHECK (SongsmithColours::background    == 0xFF17191Cu);
    CHECK (SongsmithColours::panelHeader   == 0xFF24272Bu);
    CHECK (SongsmithColours::surfaceTop    == 0xFF2C2F34u);
    CHECK (SongsmithColours::surfaceBottom == 0xFF1C1E22u);
    CHECK (SongsmithColours::edgeHighlight == 0x0FFFFFFFu);
    CHECK (SongsmithColours::border        == 0xFF0E0F11u);
    CHECK (SongsmithColours::text          == 0xFFDADCE0u);
    CHECK (SongsmithColours::textMuted     == 0xFF8B8F96u);
    CHECK (SongsmithColours::accentCopper  == 0xFFE8955Au);
    CHECK (SongsmithColours::conductorTempo == SongsmithColours::accentCopper);
    CHECK (SongsmithColours::previewNoteNormal == 0xFF5CC3DEu);
    CHECK (SongsmithColours::conductorMeter == SongsmithColours::previewNoteNormal);
    CHECK (SongsmithColours::previewNoteBorder == 0xFF8FD8ECu);
    CHECK (SongsmithColours::selectedRow   == 0xFF2E3238u);
    CHECK (SongsmithColours::markerIvory   == 0xFFE6DFD2u);
    CHECK (SongsmithColours::sourceHeaderLabel == SongsmithColours::previewNoteNormal);
}

TEST_CASE ("SongsmithColours: the playhead copper differs from the marker ivory", "[theme]")
{
    CHECK (SongsmithColours::accentCopper != SongsmithColours::markerIvory);
}

TEST_CASE ("PianoRollPalette: keys keep light/dark contrast", "[theme]")
{
    const auto brightness = [] (juce::uint32 argb)
    {
        return (((argb >> 16) & 0xFF) + ((argb >> 8) & 0xFF) + (argb & 0xFF)) / 3;
    };
    CHECK (brightness (PianoRollPalette::whiteKey) > 200);
    CHECK (brightness (PianoRollPalette::whiteKeyShade) < brightness (PianoRollPalette::whiteKey));
    CHECK (brightness (PianoRollPalette::blackKeyTop) < 50);
    CHECK (brightness (PianoRollPalette::blackKeyBottom) > brightness (PianoRollPalette::blackKeyTop));
}
