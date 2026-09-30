#include "UI/SongDocument.h"
#include "UI/TrackEditorWindow.h"

#include <catch2/catch_test_macros.hpp>

using namespace lotro;

TEST_CASE ("TrackEditorWindow: setTrack re-points the same window to a different track without recreating it", "[track-editor-window]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    SongDocument doc;
    auto trackA = doc.addTrack ("Track A", 0xFFAABBCCu, 0, 0);
    auto trackB = doc.addTrack ("Track B", 0xFFDDEEFFu, 1, 0);
    const auto idA = (juce::int64) trackA.getProperty (SongIDs::trackId);
    const auto idB = (juce::int64) trackB.getProperty (SongIDs::trackId);

    TrackEditorWindow window (doc, nullptr);
    window.setTrack (trackA);
    CHECK (window.getTrackId() == idA);

    window.setTrack (trackB);
    CHECK (window.getTrackId() == idB);
}

TEST_CASE ("TrackEditorWindow: onClosed fires when the window's close button is pressed", "[track-editor-window]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    SongDocument doc;
    auto track = doc.addTrack ("Track A", 0xFFAABBCCu, 0, 0);

    TrackEditorWindow window (doc, nullptr);
    window.setTrack (track);

    bool closed = false;
    window.onClosed = [&] { closed = true; };

    window.closeButtonPressed();

    CHECK (closed);
}

TEST_CASE ("TrackEditorWindow: offers minimise, maximise and close title-bar buttons", "[track-editor-window]")
{
    // With a native title bar, these style flags are what tell the OS to show
    // the buttons — and, on Windows, whether title-bar double-click / snap
    // may maximise the window at all.
    juce::ScopedJuceInitialiser_GUI juceInit;

    SongDocument doc;
    TrackEditorWindow window (doc, nullptr);

    const int flags = window.getDesktopWindowStyleFlags();
    CHECK ((flags & juce::ComponentPeer::windowHasMaximiseButton) != 0);
    CHECK ((flags & juce::ComponentPeer::windowHasMinimiseButton) != 0);
    CHECK ((flags & juce::ComponentPeer::windowHasCloseButton) != 0);
    // Not the windowIsResizable style flag: JUCE sets that only where
    // Desktop::supportsBorderlessNonClientResize() (true on Windows, false on
    // headless Linux), so it would test the platform rather than this class.
    CHECK (window.isResizable());
}

TEST_CASE ("TrackEditorWindow: pops up centred over the application window", "[track-editor-window]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    // Inside the (headless) primary display, clear of its edges, so the
    // centring isn't also being constrained on-screen.
    const auto screen = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay()->userArea;
    juce::Component app;
    app.setBounds (juce::Rectangle<int> (1000, 700).withCentre (screen.getCentre()).translated (-20, -30));

    SongDocument doc;
    TrackEditorWindow window (doc, &app);

    const auto centre = window.getScreenBounds().getCentre();
    CHECK (std::abs (centre.x - app.getScreenBounds().getCentreX()) <= 1);
    CHECK (std::abs (centre.y - app.getScreenBounds().getCentreY()) <= 1);
}
