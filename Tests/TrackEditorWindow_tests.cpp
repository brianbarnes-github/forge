#include "UI/SongsmithColours.h"
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

TEST_CASE ("TrackEditorWindow: a track appearance change retitles the window for its own track and repaints the roll for any", "[track-editor-window]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    SongDocument doc;
    auto trackA = doc.addTrack ("Track A", (int) 0xFFAABBCCu, 0, 0);
    auto trackB = doc.addTrack ("Track B", (int) 0xFFDDEEFFu, 1, 0);
    const auto idA = (juce::int64) trackA.getProperty (SongIDs::trackId);
    const auto idB = (juce::int64) trackB.getProperty (SongIDs::trackId);

    TrackEditorWindow window (doc, nullptr);
    window.setTrack (trackA);
    REQUIRE (window.getName() == "Edit Track: Track A");

    trackA.setProperty (SongIDs::name, "Harp", nullptr);
    trackB.setProperty (SongIDs::name, "Flute", nullptr);
    const int repaints = window.appearanceRepaintsForTesting();
    window.trackAppearanceChanged (idB);                       // a ghost's colour, say
    CHECK (window.getName() == "Edit Track: Track A");
    CHECK (window.appearanceRepaintsForTesting() == repaints + 1);
    window.trackAppearanceChanged (idA);
    CHECK (window.getName() == "Edit Track: Harp");
    CHECK (window.appearanceRepaintsForTesting() == repaints + 2);
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

TEST_CASE ("TrackEditorWindow: setPlayback keeps the roll its size and grows the window by the strip and ruler", "[track-editor][playhead]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SongDocument doc;
    auto track = doc.addTrack ("A", (int) 0xFFAABBCC, 0, 0);
    lotro::playbacktest::RecordingSink sink;
    PlaybackController controller (doc, sink);

    // Exactly as SongsmithMainComponent::trackDoubleClicked does: build, then setPlayback, then setTrack.
    TrackEditorWindow window (doc, nullptr);
    window.setTrack (track);
    using Access = TrackEditorWindowTestAccess;
    const auto oldWindowBounds = window.getBounds();
    const auto oldRollSize = Access::rollBounds (window);
    REQUIRE (oldWindowBounds.getWidth() > 0);
    REQUIRE (oldRollSize.getHeight() > 0);

    window.setPlayback (&controller);   // no manual sizing

    const int extra = TransportStrip::height + TimelineRuler::height;
    CHECK (window.getX() == oldWindowBounds.getX());
    CHECK (window.getY() == oldWindowBounds.getY());
    CHECK (window.getWidth() == oldWindowBounds.getWidth());
    CHECK (window.getHeight() == oldWindowBounds.getHeight() + extra);

    const auto strip = Access::stripBounds (window);
    const auto ruler = Access::rulerBounds (window);
    const auto roll = Access::rollBounds (window);
    CHECK (roll.getWidth() == oldRollSize.getWidth());
    CHECK (roll.getHeight() == oldRollSize.getHeight());
    CHECK (! strip.isEmpty());
    CHECK (! ruler.isEmpty());
    CHECK (strip.getY() == 0);
    CHECK (ruler.getY() == strip.getBottom());
    CHECK (roll.getY() == ruler.getBottom());
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

TEST_CASE ("TrackEditorWindow: right-click in the ruler seeks and left-click sets the marker, at the tick under that x of the roll", "[track-editor][playhead]")
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
    // Right-click moves the playhead...
    ruler.mouseDown (juce::MouseEvent (*source, pos, juce::ModifierKeys (juce::ModifierKeys::rightButtonModifier),
                                       0.0f, 0.0f, 0.0f, 0.0f, 0.0f, &ruler, &ruler, now, pos, now, 1, false));
    CHECK (controller.getPositionTicks() == Catch::Approx (4800.0).margin (30.0));
    CHECK (! controller.getMarkerTick().has_value());

    // ...a left click sets the marker and leaves the playhead where it was.
    const juce::Point<float> pos2 ((float) Access::roll (window).xForTickInComponent (2400.0), 20.0f);
    ruler.mouseDown (juce::MouseEvent (*source, pos2, juce::ModifierKeys(), 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                                       &ruler, &ruler, now, pos2, now, 1, false));
    REQUIRE (controller.getMarkerTick().has_value());
    CHECK (*controller.getMarkerTick() == Catch::Approx (2400.0).margin (30.0));
    CHECK (controller.getPositionTicks() == Catch::Approx (4800.0).margin (30.0));
}

TEST_CASE ("TrackEditorWindow: the timing bar draws bar lines from the keyboard gutter's edge, not under the gutter", "[track-editor][ruler]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SongDocument doc;
    lotro::playbacktest::RecordingSink sink;
    PlaybackController controller (doc, sink);
    TrackEditorWindow window (doc, nullptr);
    window.setPlayback (&controller);

    using Access = TrackEditorWindowTestAccess;
    Access::content (window).setSize (700, 400);
    REQUIRE (Access::roll (window).onViewChanged != nullptr);   // the roll tells the ruler to repaint

    auto& ruler = Access::ruler (window);
    juce::Image image (juce::Image::ARGB, ruler.getWidth(), ruler.getHeight(), true, juce::SoftwareImageType());
    {
        juce::Graphics g (image);
        ruler.paintEntireComponent (g, false);
    }

    const auto background = juce::Colour (SongsmithColours::background).brighter (0.1f);
    const int gutter = Access::roll (window).getGutterWidth();
    const int lowerRow = TimelineRuler::height - 4;

    CHECK (image.getPixelAt (gutter, lowerRow) != background);   // bar 1's line (tick 0, unscrolled)
    for (int x = 0; x < gutter; ++x)
        CHECK (image.getPixelAt (x, lowerRow) == background);
}
