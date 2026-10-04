#include "PlaybackTestSupport.h"
#include "UI/Playback/PlaybackController.h"
#include "UI/SongDocument.h"
#include "UI/TrackEditorWindow.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace lotro;

namespace lotro
{
    struct TrackEditorWindowTestAccess
    {
        static juce::Rectangle<int> stripBounds (TrackEditorWindow& w) { return w.content->strip.getBounds(); }
        static juce::Rectangle<int> rulerBounds (TrackEditorWindow& w) { return w.content->ruler.getBounds(); }
        static juce::Rectangle<int> rollBounds (TrackEditorWindow& w) { return w.roll.getBounds(); }
        static TimelineRuler& ruler (TrackEditorWindow& w) { return w.content->ruler; }
        static PianoRollComponent& roll (TrackEditorWindow& w) { return w.roll; }
        static juce::Component& content (TrackEditorWindow& w) { return *w.content; }
    };
}

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

TEST_CASE ("TrackEditorWindow: with playback set, the window hosts the transport strip and ruler above the roll", "[track-editor][playhead]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SongDocument doc;
    lotro::playbacktest::RecordingSink sink;
    PlaybackController controller (doc, sink);
    TrackEditorWindow window (doc, nullptr);
    CHECK (! window.hasTransportStripForTesting());
    CHECK (! window.hasRulerForTesting());

    window.setPlayback (&controller);
    CHECK (window.hasTransportStripForTesting());
    CHECK (window.hasRulerForTesting());

    using Access = TrackEditorWindowTestAccess;
    Access::content (window).setSize (700, 400);
    const auto strip = Access::stripBounds (window);
    const auto ruler = Access::rulerBounds (window);
    const auto roll = Access::rollBounds (window);
    CHECK (strip.getY() == 0);
    CHECK (strip.getHeight() == TransportStrip::height);
    CHECK (ruler.getY() == strip.getBottom());
    CHECK (ruler.getHeight() == TimelineRuler::height);
    CHECK (roll.getY() == ruler.getBottom());
    CHECK (roll.getBottom() == 400);
    // The ruler spans the roll's width, so a ruler x is a roll x.
    CHECK (ruler.getX() == roll.getX());
    CHECK (ruler.getWidth() == roll.getWidth());
}

TEST_CASE ("TrackEditorWindow: Space toggles playback and other keys are left alone", "[track-editor][playhead]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SongDocument doc;
    auto track = doc.addTrack ("A", (int) 0xFFAABBCC, 0, 0);
    lotro::playbacktest::addNote (track, 60, 0, 960);   // play() refuses an empty song
    lotro::playbacktest::RecordingSink sink;
    PlaybackController controller (doc, sink);
    TrackEditorWindow window (doc, nullptr);

    // Without playback the window does not claim Space.
    CHECK (! window.keyPressed (juce::KeyPress (juce::KeyPress::spaceKey)));

    window.setPlayback (&controller);
    CHECK (window.keyPressed (juce::KeyPress (juce::KeyPress::spaceKey)));
    CHECK (controller.isPlaying());
    CHECK (window.keyPressed (juce::KeyPress (juce::KeyPress::spaceKey)));
    CHECK (! controller.isPlaying());
    CHECK (! window.keyPressed (juce::KeyPress ('x')));
}

TEST_CASE ("TrackEditorWindow: clicking the ruler seeks to the tick under that x of the roll", "[track-editor][playhead]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SongDocument doc;
    auto track = doc.addTrack ("A", (int) 0xFFAABBCC, 0, 0);
    lotro::playbacktest::addNote (track, 60, 0, 9600);
    lotro::playbacktest::RecordingSink sink;
    PlaybackController controller (doc, sink);
    controller.flushRebuild();
    TrackEditorWindow window (doc, nullptr);
    window.setPlayback (&controller);
    window.setTrack (track);

    using Access = TrackEditorWindowTestAccess;
    Access::content (window).setSize (800, 400);
    auto& ruler = Access::ruler (window);
    const int x = Access::roll (window).xForTickInComponent (4800.0);

    auto* source = juce::Desktop::getInstance().getMouseSource (0);
    const auto now = juce::Time::getCurrentTime();
    const juce::Point<float> pos ((float) x, 3.0f);
    ruler.mouseDown (juce::MouseEvent (*source, pos, juce::ModifierKeys(), 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                                       &ruler, &ruler, now, pos, now, 1, false));

    CHECK (controller.getPositionTicks() == Catch::Approx (4800.0).margin (30.0));
}
