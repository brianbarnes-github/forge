// Tests/TrackNotePreview_tests.cpp
#include "UI/TrackNotePreview.h"
#include "UI/SongDocument.h"
#include "UI/SongsmithColours.h"

#include <catch2/catch_test_macros.hpp>

using namespace lotro;

namespace
{
    constexpr int previewWidth = 300;
    constexpr int previewHeight = 34;
}

TEST_CASE ("TrackNotePreview: paints a note as a bar at its mapped tick position, in the track's colour", "[track-note-preview]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    constexpr juce::uint32 trackColour = 0xFF80C878;
    juce::ValueTree track (SongIDs::MIDI_TRACK);
    track.setProperty (SongIDs::colorArgb, (int) trackColour, nullptr);
    juce::ValueTree note (SongIDs::NOTE);
    note.setProperty (SongIDs::pitch, 60, nullptr);
    note.setProperty (SongIDs::startTick, 0, nullptr);
    note.setProperty (SongIDs::durationTicks, 480, nullptr);
    juce::ValueTree notes (SongIDs::NOTES);
    notes.appendChild (note, nullptr);
    track.appendChild (notes, nullptr);

    TimelineViewState viewState;
    viewState.setPixelsPerTick (0.1);
    viewState.setScrollOffsetTicks (0.0);

    TrackNotePreview preview (track, viewState);
    preview.setBounds (0, 0, previewWidth, previewHeight);

    juce::Image image (juce::Image::ARGB, previewWidth, previewHeight, true, juce::SoftwareImageType());
    juce::Graphics g (image);
    preview.paint (g);

    const auto expectedX = viewState.xForTick (0);
    const auto pixel = image.getPixelAt (expectedX + 1, previewHeight - 2);
    CHECK (pixel == juce::Colour (trackColour));
}

TEST_CASE ("TrackNotePreview: an empty track paints only the background, no note bars", "[track-note-preview]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    juce::ValueTree track (SongIDs::MIDI_TRACK);
    TimelineViewState viewState;
    viewState.setPixelsPerTick (0.1);

    TrackNotePreview preview (track, viewState);
    preview.setBounds (0, 0, previewWidth, previewHeight);

    juce::Image image (juce::Image::ARGB, previewWidth, previewHeight, true, juce::SoftwareImageType());
    juce::Graphics g (image);
    preview.paint (g);

    CHECK (image.getPixelAt (previewWidth / 2, previewHeight / 2) == juce::Colour (SongsmithColours::background));
}

TEST_CASE ("TrackNotePreview: toggleGhostIfHit flips visibility only inside the toggle bounds and fires onGhostToggled", "[track-note-preview]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    juce::ValueTree track (SongIDs::MIDI_TRACK);
    TimelineViewState viewState;

    TrackNotePreview preview (track, viewState);
    preview.setBounds (0, 0, previewWidth, previewHeight);

    bool fired = false;
    bool firedState = false;
    preview.onGhostToggled = [&] (bool visible) { fired = true; firedState = visible; };

    CHECK_FALSE (preview.toggleGhostIfHit ({ 0, 0 }));
    CHECK_FALSE (fired);
    CHECK_FALSE (preview.isGhostVisible());

    const auto toggle = preview.ghostToggleBounds();
    CHECK (preview.toggleGhostIfHit (toggle.getCentre()));
    CHECK (fired);
    CHECK (firedState);
    CHECK (preview.isGhostVisible());
}

TEST_CASE ("TrackNotePreview: draws ghost toggle as filled amber when visible, outlined muted when invisible", "[track-note-preview]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    juce::ValueTree track (SongIDs::MIDI_TRACK);
    TimelineViewState viewState;
    viewState.setPixelsPerTick (0.1);

    TrackNotePreview preview (track, viewState);
    preview.setBounds (0, 0, previewWidth, previewHeight);

    const auto toggleBounds = preview.ghostToggleBounds();
    const auto toggleCentre = toggleBounds.getCentre();

    // When invisible (default), draw outlined toggle
    juce::Image imageInvisible (juce::Image::ARGB, previewWidth, previewHeight, true, juce::SoftwareImageType());
    juce::Graphics gInvisible (imageInvisible);
    preview.paint (gInvisible);
    auto pixelAtCentreInvisible = imageInvisible.getPixelAt (toggleCentre.x, toggleCentre.y);
    CHECK (pixelAtCentreInvisible != juce::Colour (SongsmithColours::accentAmber));

    // When visible, draw filled toggle
    preview.setGhostVisible (true);
    juce::Image imageVisible (juce::Image::ARGB, previewWidth, previewHeight, true, juce::SoftwareImageType());
    juce::Graphics gVisible (imageVisible);
    preview.paint (gVisible);
    auto pixelAtCentreVisible = imageVisible.getPixelAt (toggleCentre.x, toggleCentre.y);
    CHECK (pixelAtCentreVisible == juce::Colour (SongsmithColours::accentAmber));
}

namespace
{
    // The pixel at the grid line for `tick` differs from the empty pixel beside it.
    bool gridLineDrawnAt (juce::Image& image, const TimelineViewState& viewState, int tick)
    {
        const int x = viewState.xForTick (tick);
        return image.getPixelAt (x, previewHeight / 2) != image.getPixelAt (x + 1, previewHeight / 2);
    }
}

TEST_CASE ("TrackNotePreview: grid lines appear for each note value once it is at least 8 px wide", "[track-note-preview]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    juce::ValueTree track (SongIDs::MIDI_TRACK);
    TimelineViewState viewState;
    viewState.setPixelsPerTick (0.1);   // 480-tick quarter = 48 px; 1/16 = 12 px; 1/32 = 6 px

    TrackNotePreview preview (track, viewState);
    preview.setBounds (0, 0, previewWidth, previewHeight);

    juce::Image image (juce::Image::ARGB, previewWidth, previewHeight, true, juce::SoftwareImageType());
    juce::Graphics g (image);
    preview.paint (g);

    CHECK (gridLineDrawnAt (image, viewState, 480));        // quarter
    CHECK (gridLineDrawnAt (image, viewState, 240));        // eighth
    CHECK (gridLineDrawnAt (image, viewState, 120));        // sixteenth
    CHECK_FALSE (gridLineDrawnAt (image, viewState, 60));   // thirty-second: too close
}

TEST_CASE ("TrackNotePreview: zoomed far in the grid goes down to 1/64", "[track-note-preview]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    juce::ValueTree track (SongIDs::MIDI_TRACK);
    TimelineViewState viewState;
    viewState.setPixelsPerTick (1.0);   // 1/64 = 30 ticks = 30 px

    TrackNotePreview preview (track, viewState);
    preview.setBounds (0, 0, previewWidth, previewHeight);

    juce::Image image (juce::Image::ARGB, previewWidth, previewHeight, true, juce::SoftwareImageType());
    juce::Graphics g (image);
    preview.paint (g);

    CHECK (gridLineDrawnAt (image, viewState, 30));
    CHECK (gridLineDrawnAt (image, viewState, 60));
}

TEST_CASE ("TrackNotePreview: a finer division is fainter than a bar line", "[track-note-preview]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    juce::ValueTree track (SongIDs::MIDI_TRACK);
    TimelineViewState viewState;
    viewState.setPixelsPerTick (0.1);

    TrackNotePreview preview (track, viewState);
    preview.setBounds (0, 0, previewWidth, previewHeight);

    juce::Image image (juce::Image::ARGB, previewWidth, previewHeight, true, juce::SoftwareImageType());
    juce::Graphics g (image);
    preview.paint (g);

    const auto barPixel = image.getPixelAt (viewState.xForTick (1920), previewHeight / 2);
    const auto sixteenthPixel = image.getPixelAt (viewState.xForTick (120), previewHeight / 2);
    CHECK (barPixel.getBrightness() > sixteenthPixel.getBrightness());
}
