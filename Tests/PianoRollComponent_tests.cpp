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

TEST_CASE ("PianoRollComponent: an upward-folding note's ghost and range band render on-canvas, not off-canvas/flooded (C1)", "[piano-roll]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    // Reviewer's exact repro: pitch 20 folds to 44 (inside LuteOfAges's
    // range), with setPreviewRangeBand({36, 73}) called after
    // setNoteSource. Before the fix, topPitch was derived from prePitch
    // alone (20), so the ghost (drawn at postPitch 44) and the whole range
    // band landed off the top of the canvas (negative y), and the
    // below-range wash flooded the entire visible clip instead.
    PreviewNote foldingNote;
    foldingNote.prePitch = 20;
    foldingNote.postPitch = 44;
    foldingNote.startTick = 0;
    foldingNote.durationTicks = ticksPerQuarter;
    foldingNote.state = NoteState::WillFold;

    PreviewNoteSource source ({ foldingNote });

    PianoRollComponent roll (PianoRollComponent::Role::Preview);
    roll.setBounds (0, 0, viewportWidth, viewportHeight);
    roll.setNoteSource (&source, ticksPerQuarter, {});
    roll.setPreviewRangeBand ({ 36, 73 });

    // Independently derived by hand, not by calling the component's own
    // fitToContent: the effective pitch extent must cover both the note's
    // post-fold pitch (44) and the band (36..73), so topPitch = 72.
    const int expectedTopPitch = 72;
    const auto sourceGeometry = PianoRollGeometry::fitToContent (source.getTickRange(), source.getPitchRange(),
                                                                   ticksPerQuarter, viewportWidth, viewportHeight);
    const auto noteBounds = sourceGeometry.noteBounds (source.getNote (0));
    const int rowHeight = sourceGeometry.getRowHeight();
    const int sampleX = noteBounds.x + noteBounds.width / 2;
    const int ghostY = (expectedTopPitch - 44) * rowHeight;
    const int insideBandY = (expectedTopPitch - 50) * rowHeight; // pitch 50: inside the band (36..73)
    const int belowBandY = (expectedTopPitch - 30) * rowHeight;  // pitch 30: below the band's floor (36)

    const int imageHeight = (expectedTopPitch - 20 + 2) * rowHeight; // covers the whole effective pitch span
    juce::Image image (juce::Image::ARGB, viewportWidth, imageHeight, true);
    juce::Graphics g (image);
    Access::paintCanvas (roll, g, { 0, 0, viewportWidth, imageHeight });

    // The ghost's top-edge outline is the last thing drawn at this pixel;
    // deriving the expected colour from the pixel one row beneath (which
    // has no ghost stroke, only the row band + range band tint already
    // painted there) and blending the ghost colour onto it via JUCE's own
    // Graphics compositing proves the ghost was actually painted on-canvas,
    // without hard-coding PianoRollComponent's private fill constants.
    const auto underlyingBeneathGhost = image.getPixelAt (sampleX, ghostY + 1);
    juce::Image reference (juce::Image::ARGB, 1, 1, true);
    {
        juce::Graphics rg (reference);
        rg.fillAll (underlyingBeneathGhost);
        rg.setColour (juce::Colour (SongsmithColours::accentAmber).withAlpha (0.7f));
        rg.fillRect (0, 0, 1, 1);
    }
    CHECK (image.getPixelAt (sampleX, ghostY) == reference.getPixelAt (0, 0));

    // The band's interior tint and the below-range wash must be visibly
    // distinct colours on-canvas — under the pre-fix bug, both coordinates
    // fell inside one giant "clip.withTop(negative bandBottom)" fill, so
    // this pixel pair would have been identical (the whole clip flooded
    // with the same wash).
    CHECK (image.getPixelAt (sampleX, insideBandY) != image.getPixelAt (sampleX, belowBandY));
}

TEST_CASE ("PianoRollComponent: a WillFold note's ghost still paints when a partial repaint clips out the solid note but not the ghost (I1)", "[piano-roll]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    PreviewNote foldingNote;
    foldingNote.prePitch = 80;
    foldingNote.postPitch = 40;
    foldingNote.startTick = 0;
    foldingNote.durationTicks = ticksPerQuarter;
    foldingNote.state = NoteState::WillFold;

    PreviewNoteSource source ({ foldingNote });

    PianoRollComponent roll (PianoRollComponent::Role::Preview);
    roll.setBounds (0, 0, viewportWidth, viewportHeight);
    roll.setNoteSource (&source, ticksPerQuarter, {});

    const auto geometry = PianoRollGeometry::fitToContent (source.getTickRange(), source.getPitchRange(),
                                                             ticksPerQuarter, viewportWidth, viewportHeight);
    const auto noteBounds = geometry.noteBounds (source.getNote (0));
    const int rowHeight = geometry.getRowHeight();
    const int ghostY = geometry.yForPitch (40);
    const int sampleX = noteBounds.x + noteBounds.width / 2;

    // A clip strip covering only the ghost's row — far from the solid
    // note's own row (drawn at prePitch 80) — simulating a Viewport scroll
    // repaint that exposes only a newly-visible strip.
    const juce::Rectangle<int> partialClip (0, ghostY, viewportWidth, rowHeight);

    juce::Image image (juce::Image::ARGB, viewportWidth, ghostY + rowHeight + 1, true);
    juce::Graphics g (image);
    Access::paintCanvas (roll, g, partialClip);

    // Same independent-composition approach as the C1 test above: the
    // expected pixel is the row's own underlying colour (sampled one row
    // beneath, where no ghost stroke lands) with the ghost colour blended
    // on top via JUCE's own compositing — proving the ghost was actually
    // painted despite the clip excluding the note's solid rect.
    const auto underlyingBeneathGhost = image.getPixelAt (sampleX, ghostY + 1);
    juce::Image reference (juce::Image::ARGB, 1, 1, true);
    {
        juce::Graphics rg (reference);
        rg.fillAll (underlyingBeneathGhost);
        rg.setColour (juce::Colour (SongsmithColours::accentAmber).withAlpha (0.7f));
        rg.fillRect (0, 0, 1, 1);
    }

    CHECK (image.getPixelAt (sampleX, ghostY) == reference.getPixelAt (0, 0));
}
