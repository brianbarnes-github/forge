#include "UI/SongDocument.h"
#include "UI/TrackRowComponent.h"
#include "UI/TimelineViewState.h"

#include <catch2/catch_test_macros.hpp>

using namespace lotro;

TEST_CASE ("TrackRowComponent: double-click fires onTrackDoubleClicked with the row's trackId", "[track-row]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    SongDocument doc;
    auto track = doc.addTrack ("Track A", 0xFFAABBCC, 0, 0);
    const auto trackId = (juce::int64) track.getProperty (SongIDs::trackId);

    TimelineViewState viewState;
    TrackRowComponent row (track, 1, viewState);

    juce::int64 firedId = -1;
    row.onTrackDoubleClicked = [&] (juce::int64 id) { firedId = id; };

    row.mouseDoubleClick (juce::MouseEvent (juce::Desktop::getInstance().getMainMouseSource(),
                                             juce::Point<float> (5.0f, 5.0f), juce::ModifierKeys(),
                                             0.0f, 0.0f, 0.0f, 0.0f, 0.0f, &row, &row,
                                             juce::Time::getCurrentTime(), juce::Point<float> (5.0f, 5.0f),
                                             juce::Time::getCurrentTime(), 2, false));

    CHECK (firedId == trackId);
}

namespace
{
    // Mirrors the MouseEvent construction the double-click test above uses,
    // but targeted at an arbitrary component and click count so the
    // preview-forwarding tests can aim at the embedded TrackNotePreview.
    juce::MouseEvent eventAt (juce::Component& target, juce::Point<int> localPos, int numClicks)
    {
        const auto pos = localPos.toFloat();
        return juce::MouseEvent (juce::Desktop::getInstance().getMainMouseSource(),
                                  pos, juce::ModifierKeys(),
                                  0.0f, 0.0f, 0.0f, 0.0f, 0.0f, &target, &target,
                                  juce::Time::getCurrentTime(), pos,
                                  juce::Time::getCurrentTime(), numClicks, false);
    }
}

TEST_CASE ("TrackRowComponent: clicks on the note preview outside its ghost toggle still reach the row", "[track-row]")
{
    // The preview is a hit-testable child covering the right 160px of the
    // row, and JUCE does not forward a child's unhandled mouse events to its
    // parent -- so without explicit forwarding that whole strip is dead to
    // the row's own select/double-click-to-edit gestures.
    juce::ScopedJuceInitialiser_GUI juceInit;

    SongDocument doc;
    auto track = doc.addTrack ("Track A", 0xFFAABBCC, 0, 0);
    const auto trackId = (juce::int64) track.getProperty (SongIDs::trackId);

    TimelineViewState viewState;
    TrackRowComponent row (track, 1, viewState);
    row.setBounds (0, 0, 300, TrackRowComponent::rowHeight);

    auto& preview = row.notePreviewForTesting();
    const juce::Point<int> awayFromToggle { 20, 20 };
    REQUIRE_FALSE (preview.ghostToggleBounds().contains (awayFromToggle));

    juce::int64 selectedId = -1;
    juce::int64 doubleClickedId = -1;
    bool ghostFired = false;
    row.onTrackSelected = [&] (juce::int64 id) { selectedId = id; };
    row.onTrackDoubleClicked = [&] (juce::int64 id) { doubleClickedId = id; };
    row.onGhostToggled = [&] (juce::int64, bool) { ghostFired = true; };

    preview.mouseDown (eventAt (preview, awayFromToggle, 1));
    CHECK (selectedId == trackId);

    preview.mouseDoubleClick (eventAt (preview, awayFromToggle, 2));
    CHECK (doubleClickedId == trackId);

    // A miss on the toggle must not also report a ghost change.
    CHECK_FALSE (ghostFired);
    CHECK_FALSE (preview.isGhostVisible());
}

TEST_CASE ("TrackRowComponent: a click on the ghost toggle toggles the ghost and does not select or open the row", "[track-row]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    SongDocument doc;
    auto track = doc.addTrack ("Track A", 0xFFAABBCC, 0, 0);

    TimelineViewState viewState;
    TrackRowComponent row (track, 1, viewState);
    row.setBounds (0, 0, 300, TrackRowComponent::rowHeight);

    auto& preview = row.notePreviewForTesting();

    bool ghostFired = false;
    juce::int64 selectedId = -1;
    juce::int64 doubleClickedId = -1;
    row.onGhostToggled = [&] (juce::int64, bool) { ghostFired = true; };
    row.onTrackSelected = [&] (juce::int64 id) { selectedId = id; };
    row.onTrackDoubleClicked = [&] (juce::int64 id) { doubleClickedId = id; };

    preview.mouseDown (eventAt (preview, preview.ghostToggleBounds().getCentre(), 1));

    CHECK (ghostFired);
    CHECK (preview.isGhostVisible());
    CHECK (selectedId == -1);
    CHECK (doubleClickedId == -1);
}

TEST_CASE ("TrackRowComponent: ghost-toggle forwarding reports this row's trackId alongside the new state", "[track-row]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    SongDocument doc;
    auto track = doc.addTrack ("Track A", 0xFFAABBCC, 0, 0);
    const auto trackId = (juce::int64) track.getProperty (SongIDs::trackId);

    TimelineViewState viewState;
    TrackRowComponent row (track, 1, viewState);
    row.setBounds (0, 0, 200, TrackRowComponent::rowHeight);

    juce::int64 firedId = -1;
    bool firedVisible = false;
    row.onGhostToggled = [&] (juce::int64 id, bool visible) { firedId = id; firedVisible = visible; };

    // Drive it through the row's embedded preview directly rather than a
    // synthetic top-level MouseEvent -- see TrackNotePreview_tests.cpp for
    // the same point-based convention.
    row.notePreviewForTesting().toggleGhostIfHit (row.notePreviewForTesting().ghostToggleBounds().getCentre());

    CHECK (firedId == trackId);
    CHECK (firedVisible);
}
