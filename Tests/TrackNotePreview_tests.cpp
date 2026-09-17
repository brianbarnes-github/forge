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

TEST_CASE ("TrackNotePreview: paints a note as a bar at its mapped tick position", "[track-note-preview]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    juce::ValueTree track (SongIDs::MIDI_TRACK);
    juce::ValueTree note (SongIDs::NOTE);
    note.setProperty (SongIDs::pitch, 60, nullptr);
    note.setProperty (SongIDs::startTick, 0, nullptr);
    note.setProperty (SongIDs::durationTicks, 480, nullptr);
    track.appendChild (note, nullptr);

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
    CHECK (pixel == juce::Colour (SongsmithColours::accentAmber));
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
