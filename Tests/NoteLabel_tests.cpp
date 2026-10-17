#include "UI/NoteLabel.h"

#include <catch2/catch_test_macros.hpp>

using namespace lotro;

TEST_CASE ("noteLabelText: name, velocity, both on one line, or nothing", "[note-labels]")
{
    CHECK (noteLabelText ({}, 60, 96).isEmpty());
    CHECK (noteLabelText ({ true, false }, 60, 96) == "C4");
    CHECK (noteLabelText ({ false, true }, 60, 96) == "96");
    CHECK (noteLabelText ({ true, true }, 60, 96) == "C4 96");
    CHECK (noteLabelText ({ true, true }, 61, 1) == "C#4 1");
    CHECK (noteLabelText ({ true, false }, 0, 64) == "C-1");
}

TEST_CASE ("noteLabelsShownAt: labels show from 31.6 px per quarter up, hidden when zoomed out further", "[note-labels]")
{
    CHECK (noteLabelsShownAt (31.6));
    CHECK (noteLabelsShownAt (31.7));
    CHECK (noteLabelsShownAt (400.0));
    CHECK_FALSE (noteLabelsShownAt (31.5));
    CHECK_FALSE (noteLabelsShownAt (2.0));
}

TEST_CASE ("noteLabelX: inside a rectangle's left edge; just right of a diamond", "[note-labels]")
{
    const PianoRollNoteBounds note { 100, 40, 6, 14 };   // shorter than any label: it spills
    CHECK (noteLabelX (noteFootprint (note, NoteShape::rectangle)) == 100 + noteLabelInset);

    for (auto shape : { NoteShape::diamond, NoteShape::diamondLength })
    {
        const auto footprint = noteFootprint (note, shape);
        CHECK (noteLabelX (footprint) == footprint.body.getRight() + noteLabelInset);
    }
}

TEST_CASE ("noteLabelColour: dark on a light fill, light on a dark fill", "[note-labels]")
{
    const auto onLight = noteLabelColour (juce::Colour (0xFFF0E0B0u));
    const auto onDark = noteLabelColour (juce::Colour (0xFF203040u));
    CHECK (onLight.getPerceivedBrightness() < 0.3f);
    CHECK (onDark.getPerceivedBrightness() > 0.7f);
    CHECK (onLight.isOpaque());
    CHECK (onDark.isOpaque());
}
