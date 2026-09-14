// Verifies PianoRollComponent's Preview-role note border colours: a Normal
// note borders with SongsmithColours::previewNoteBorder, and a
// WillFold/Dropped note borders with SongsmithColours::outOfRangeBorder —
// not the generic fill.brighter(0.4f) used by Role::Source.

#include "UI/PianoRollComponent.h"
#include "UI/PreviewNoteDiff.h"
#include "UI/PreviewNoteSource.h"
#include "UI/SongsmithColours.h"

#include <catch2/catch_test_macros.hpp>

using namespace lotro;

namespace lotro
{
    // Grants PianoRollComponent_tests.cpp access to the private paintCanvas(),
    // which stays private on the class itself.
    struct PianoRollComponentTestAccess
    {
        static void paintCanvas (const PianoRollComponent& c, juce::Graphics& g, juce::Rectangle<int> clip)
        {
            c.paintCanvas (g, clip);
        }
    };
}

namespace
{
    using Access = PianoRollComponentTestAccess;
    constexpr int viewportWidth = 400;
    constexpr int viewportHeight = 200;
    constexpr int ticksPerQuarter = 480;
}

TEST_CASE ("PianoRollComponent: Preview-role notes border with the state-specific preview colours", "[piano-roll]")
{
    // PianoRollComponent is a real juce::Component (owns a Viewport/Canvas);
    // constructing one lazily creates the Desktop/LookAndFeel singletons,
    // which need a matching teardown or they report as leaks (same
    // rationale as TrackListComponent_tests.cpp's real-import test).
    juce::ScopedJuceInitialiser_GUI juceInit;

    PreviewNote normalNote;
    normalNote.prePitch = 60;
    normalNote.startTick = 0;
    normalNote.durationTicks = ticksPerQuarter;
    normalNote.state = NoteState::Normal;

    PreviewNote droppedNote;
    droppedNote.prePitch = 64;
    droppedNote.startTick = ticksPerQuarter;
    droppedNote.durationTicks = ticksPerQuarter;
    droppedNote.state = NoteState::Dropped;

    PreviewNoteSource source ({ normalNote, droppedNote });

    PianoRollComponent roll (PianoRollComponent::Role::Preview);
    roll.setBounds (0, 0, viewportWidth, viewportHeight);
    roll.setNoteSource (&source, ticksPerQuarter, {});

    const auto geometry = PianoRollGeometry::fitToContent (source.getTickRange(), source.getPitchRange(),
                                                             ticksPerQuarter, viewportWidth, viewportHeight);
    const auto normalBounds = geometry.noteBounds (source.getNote (0));
    const auto droppedBounds = geometry.noteBounds (source.getNote (1));

    juce::Image image (juce::Image::ARGB, viewportWidth, viewportHeight, true);
    juce::Graphics g (image);
    Access::paintCanvas (roll, g, { 0, 0, viewportWidth, viewportHeight });

    // drawNotes runs last in paintCanvas, so the top-border pixel (drawn
    // after the fill, at rect.y) is exactly the border colour, no blending
    // with row bands/gridlines painted earlier.
    const auto normalBorderPixel = image.getPixelAt (normalBounds.x + normalBounds.width / 2, normalBounds.y);
    const auto droppedBorderPixel = image.getPixelAt (droppedBounds.x + droppedBounds.width / 2, droppedBounds.y);

    CHECK (normalBorderPixel == juce::Colour (SongsmithColours::previewNoteBorder));
    CHECK (droppedBorderPixel == juce::Colour (SongsmithColours::outOfRangeBorder));
}
