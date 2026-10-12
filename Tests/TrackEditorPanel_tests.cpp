#include "UI/SongsmithColours.h"
#include "PlaybackTestSupport.h"
#include "UI/Playback/PlaybackController.h"
#include "UI/SongDocument.h"
#include "UI/TrackEditorPanel.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace lotro;

namespace lotro
{
    struct TrackEditorPanelTestAccess
    {
        static juce::Rectangle<int> stripBounds (TrackEditorPanel& w) { return w.content->strip.getBounds(); }
        static juce::Rectangle<int> rulerBounds (TrackEditorPanel& w) { return w.content->ruler.getBounds(); }
        static juce::Rectangle<int> rollBounds (TrackEditorPanel& w) { return w.roll.getBounds(); }
        static TimelineRuler& ruler (TrackEditorPanel& w) { return w.content->ruler; }
        static PianoRollComponent& roll (TrackEditorPanel& w) { return w.roll; }
        static juce::Component& content (TrackEditorPanel& w) { return *w.content; }
    };
}

TEST_CASE ("TrackEditorPanel: setTrack re-points the same window to a different track without recreating it", "[track-editor-window]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    SongDocument doc;
    auto trackA = doc.addTrack ("Track A", 0xFFAABBCCu, 0, 0);
    auto trackB = doc.addTrack ("Track B", 0xFFDDEEFFu, 1, 0);
    const auto idA = (juce::int64) trackA.getProperty (SongIDs::trackId);
    const auto idB = (juce::int64) trackB.getProperty (SongIDs::trackId);

    TrackEditorPanel window (doc);
    window.setTrack (trackA);
    CHECK (window.getTrackId() == idA);

    window.setTrack (trackB);
    CHECK (window.getTrackId() == idB);
}

TEST_CASE ("TrackEditorPanel: a track appearance change retitles the window for its own track and repaints the roll for any", "[track-editor-window]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    SongDocument doc;
    auto trackA = doc.addTrack ("Track A", (int) 0xFFAABBCCu, 0, 0);
    auto trackB = doc.addTrack ("Track B", (int) 0xFFDDEEFFu, 1, 0);
    const auto idA = (juce::int64) trackA.getProperty (SongIDs::trackId);
    const auto idB = (juce::int64) trackB.getProperty (SongIDs::trackId);

    TrackEditorPanel window (doc);
    window.setTrack (trackA);
    REQUIRE (window.title() == "Edit Track: Track A");

    trackA.setProperty (SongIDs::name, "Harp", nullptr);
    trackB.setProperty (SongIDs::name, "Flute", nullptr);
    juce::StringArray retitles;
    window.onTitleChanged = [&] (const juce::String& t) { retitles.add (t); };
    const int repaints = window.appearanceRepaintsForTesting();
    window.trackAppearanceChanged (idB);                       // a ghost's colour, say
    CHECK (retitles.isEmpty());                                // not this panel's track: no retitle
    CHECK (window.appearanceRepaintsForTesting() == repaints + 1);
    window.trackAppearanceChanged (idA);
    CHECK (retitles == juce::StringArray { "Edit Track: Harp" });
    CHECK (window.title() == "Edit Track: Harp");
    CHECK (window.appearanceRepaintsForTesting() == repaints + 2);
}

TEST_CASE ("TrackEditorPanel: with playback set, the window hosts the transport strip and ruler above the roll", "[track-editor][playhead]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SongDocument doc;
    lotro::playbacktest::RecordingSink sink;
    PlaybackController controller (doc, sink);
    TrackEditorPanel window (doc);
    CHECK (! window.hasTransportStripForTesting());
    CHECK (! window.hasRulerForTesting());

    window.setPlayback (&controller);
    CHECK (window.hasTransportStripForTesting());
    CHECK (window.hasRulerForTesting());

    using Access = TrackEditorPanelTestAccess;
    window.setSize (700, 400);
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

TEST_CASE ("TrackEditorPanel: a fresh panel is empty, titled MIDI Editor, and clearTrack goes back to that", "[track-editor-window]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SongDocument doc;
    auto track = doc.addTrack ("Track A", (int) 0xFFAABBCC, 0, 0);

    TrackEditorPanel panel (doc);
    CHECK (panel.getTrackId() == -1);
    CHECK (panel.title() == "MIDI Editor");

    juce::StringArray titles;
    panel.onTitleChanged = [&] (const juce::String& t) { titles.add (t); };
    panel.setTrack (track);
    CHECK (panel.getTrackId() != -1);
    CHECK (panel.title() == "Edit Track: Track A");

    panel.clearTrack();
    CHECK (panel.getTrackId() == -1);
    CHECK (panel.title() == "MIDI Editor");
    CHECK (titles == juce::StringArray { "Edit Track: Track A", "MIDI Editor" });
}

TEST_CASE ("TrackEditorPanel: setPlayback fills the panel with strip, ruler and roll", "[track-editor][playhead]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SongDocument doc;
    lotro::playbacktest::RecordingSink sink;
    PlaybackController controller (doc, sink);
    TrackEditorPanel panel (doc);
    panel.setSize (700, 400);
    panel.setPlayback (&controller);

    using Access = TrackEditorPanelTestAccess;
    CHECK (Access::content (panel).getBounds() == juce::Rectangle<int> (0, 0, 700, 400));
    CHECK (Access::stripBounds (panel).getBottom() == Access::rulerBounds (panel).getY());
    CHECK (Access::rollBounds (panel).getBottom() == 400);
}

TEST_CASE ("TrackEditorPanel: Space toggles playback and other keys are left alone", "[track-editor][playhead]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SongDocument doc;
    auto track = doc.addTrack ("A", (int) 0xFFAABBCC, 0, 0);
    lotro::playbacktest::addNote (track, 60, 0, 960);   // play() refuses an empty song
    lotro::playbacktest::RecordingSink sink;
    PlaybackController controller (doc, sink);
    TrackEditorPanel window (doc);

    // Without playback the window does not claim Space.
    CHECK (! window.keyPressed (juce::KeyPress (juce::KeyPress::spaceKey)));

    window.setPlayback (&controller);
    CHECK (window.keyPressed (juce::KeyPress (juce::KeyPress::spaceKey)));
    CHECK (controller.isPlaying());
    CHECK (window.keyPressed (juce::KeyPress (juce::KeyPress::spaceKey)));
    CHECK (! controller.isPlaying());
    CHECK (! window.keyPressed (juce::KeyPress ('x')));
}

TEST_CASE ("TrackEditorPanel: right-click in the ruler seeks and left-click sets the marker, at the tick under that x of the roll", "[track-editor][playhead]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SongDocument doc;
    auto track = doc.addTrack ("A", (int) 0xFFAABBCC, 0, 0);
    lotro::playbacktest::addNote (track, 60, 0, 9600);
    lotro::playbacktest::RecordingSink sink;
    PlaybackController controller (doc, sink);
    controller.flushRebuild();
    TrackEditorPanel window (doc);
    window.setPlayback (&controller);
    window.setTrack (track);

    using Access = TrackEditorPanelTestAccess;
    window.setSize (800, 400);
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

TEST_CASE ("TrackEditorPanel: the timing bar draws bar lines from the keyboard gutter's edge, not under the gutter", "[track-editor][ruler]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SongDocument doc;
    lotro::playbacktest::RecordingSink sink;
    PlaybackController controller (doc, sink);
    TrackEditorPanel window (doc);
    window.setPlayback (&controller);

    using Access = TrackEditorPanelTestAccess;
    window.setSize (700, 400);
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
