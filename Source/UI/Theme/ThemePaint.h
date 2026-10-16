#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <optional>

// Stateless painting helpers shared by Songsmith's custom-painted components,
// so notes, headers and the logo look the same everywhere.
namespace lotro::ThemePaint
{
    // How far a note's glow and selection edge reach outside its bounds. Cull
    // and dirty rects for notes must be inflated by this.
    constexpr int noteOverhang = 2;
    // Notes shorter than this get no glow (zoomed-out rolls).
    constexpr int glowMinHeight = 6;
    // Notes under this in height or width are a flat fillRect (cheap; nothing else would show).
    constexpr int smallNoteSize = 4;

    struct NoteStyle
    {
        bool selected = false;
        std::optional<juce::Colour> border;   // replaces the default fill.brighter (0.4f) edge
    };

    // Gradient surfaceTop -> surfaceBottom, 1 px edgeHighlight on top, 1 px border on the bottom.
    void paintPanelGradient (juce::Graphics& g, juce::Rectangle<int> bounds);

    // A rounded, gradient-filled note with an edge and (if tall enough) a soft glow.
    void paintNoteBody (juce::Graphics& g, juce::Rectangle<int> rect, juce::Colour fill, const NoteStyle& style = {});

    // The anvil mark in unit space (x 0..1, y 0.10..0.70).
    juce::Path anvilPath();

    // The anvil in copper, fitted into `area`; with the "Songsmith" / "MIDI EDITOR" wordmark to its right.
    void paintLogo (juce::Graphics& g, juce::Rectangle<float> area, bool withWordmark);

    // A square ARGB image of the anvil on a transparent ground, for window icons.
    juce::Image renderAppIcon (int size);
}
