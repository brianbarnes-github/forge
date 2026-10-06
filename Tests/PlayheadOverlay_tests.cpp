#include "PlaybackTestSupport.h"
#include "UI/Playback/PlaybackController.h"
#include "UI/Playback/PlayheadOverlay.h"
#include "UI/Playback/TimelineRuler.h"
#include "UI/SongsmithColours.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace lotro;
using namespace lotro::playbacktest;

namespace
{
    juce::MouseEvent mouseAt (juce::Component& c, int x, int y = 3, juce::ModifierKeys mods = {})
    {
        return juce::MouseEvent (juce::Desktop::getInstance().getMainMouseSource(),
                                 juce::Point<float> ((float) x, (float) y), mods,
                                 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, &c, &c, juce::Time::getCurrentTime(),
                                 juce::Point<float> ((float) x, (float) y), juce::Time::getCurrentTime(), 1, false);
    }

    const juce::ModifierKeys rightButton (juce::ModifierKeys::rightButtonModifier);
}

TEST_CASE ("PlayheadOverlay: ignores mouse clicks so it never blocks editing", "[playback][overlay]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SongDocument doc;
    RecordingSink sink;
    PlaybackController controller (doc, sink);
    PlayheadOverlay overlay (controller, [] (double tick) { return (int) (tick * 0.1); });
    overlay.setSize (500, 100);
    // getInterceptsMouseClicks returns void and fills out-params; start both true so a no-op fails.
    bool onThis = true;
    bool onChildren = true;
    overlay.getInterceptsMouseClicks (onThis, onChildren);
    CHECK (! onThis);
    CHECK (! onChildren);
}

TEST_CASE ("PlayheadOverlay: x follows the controller's position through the supplied tick mapping", "[playback][overlay]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SongDocument doc;
    RecordingSink sink;
    PlaybackController controller (doc, sink);
    addNote (addTrack (doc), 60, 0, 9600);
    controller.flushRebuild();
    PlayheadOverlay overlay (controller, [] (double tick) { return (int) (tick * 0.1); });
    overlay.setSize (500, 100);

    controller.seekToTick (1000.0);
    CHECK (overlay.currentX() == 100);
    controller.seekToTick (0.0);
    CHECK (overlay.currentX() == 0);
}

TEST_CASE ("TimelineRuler: left click and drag set the marker to the tick under the pointer, clamped at zero", "[playback][ruler][marker]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    TimelineRuler ruler ([] (int x) { return (double) x * 10.0; });
    ruler.setSize (500, TimelineRuler::height);
    double markerTick = -1.0;
    bool seeked = false;
    ruler.onSetMarker = [&] (double t) { markerTick = t; };
    ruler.onSeek = [&] (double) { seeked = true; };

    ruler.mouseDown (mouseAt (ruler, 25, 20));
    CHECK (markerTick == Catch::Approx (250.0));
    ruler.mouseDrag (mouseAt (ruler, 40, 20));
    CHECK (markerTick == Catch::Approx (400.0));
    ruler.mouseDrag (mouseAt (ruler, -30, 20));
    CHECK (markerTick == Catch::Approx (0.0).margin (1e-9));
    CHECK (! seeked);   // a left click never moves the playhead
}

TEST_CASE ("TimelineRuler: right click and drag move the playhead instead", "[playback][ruler]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    TimelineRuler ruler ([] (int x) { return (double) x * 10.0; });
    ruler.setSize (500, TimelineRuler::height);
    double seekTick = -1.0;
    bool markerSet = false;
    ruler.onSeek = [&] (double t) { seekTick = t; };
    ruler.onSetMarker = [&] (double) { markerSet = true; };

    ruler.mouseDown (mouseAt (ruler, 25, 20, rightButton));
    CHECK (seekTick == Catch::Approx (250.0));
    ruler.mouseDrag (mouseAt (ruler, 40, 20, rightButton));
    CHECK (seekTick == Catch::Approx (400.0));
    ruler.mouseDrag (mouseAt (ruler, -30, 20, rightButton));
    CHECK (seekTick == Catch::Approx (0.0).margin (1e-9));
    CHECK (! markerSet);
}

TEST_CASE ("TimelineRuler: the marker's triangle sits in the bar; pressing it clears the marker, anything else sets it", "[playback][ruler][marker]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    TimelineRuler ruler ([] (int x) { return (double) x * 10.0; });
    ruler.setSize (500, TimelineRuler::height);
    std::optional<double> marker;
    ruler.setMarks ([] (double tick) { return (int) (tick / 10.0); },
                    [] { return RulerGrid { TempoMap ({}, 480), { 4, 4 } }; });
    ruler.setMarker ([&] { return marker; });
    ruler.onSetMarker = [&] (double t) { marker = t; };
    ruler.onClearMarker = [&] { marker.reset(); };

    // No marker yet: the same spot just sets one.
    CHECK (! ruler.markerHandleBounds().has_value());
    ruler.mouseDown (mouseAt (ruler, 100, 3));
    REQUIRE (marker.has_value());
    CHECK (*marker == Catch::Approx (1000.0));

    const auto handle = ruler.markerHandleBounds();
    REQUIRE (handle.has_value());
    CHECK (handle->getCentreX() == 100);
    CHECK (handle->getY() == 0);
    CHECK (handle->getWidth() >= 12);   // big enough to click
    CHECK (handle->getHeight() >= 8);

    // Elsewhere in the bar (even right next to the triangle's row) the marker moves...
    ruler.mouseDown (mouseAt (ruler, 300, 3));
    CHECK (*marker == Catch::Approx (3000.0));
    ruler.mouseDown (mouseAt (ruler, 100, TimelineRuler::height - 4));   // lower row: not the triangle
    CHECK (*marker == Catch::Approx (1000.0));

    // ...but pressing the triangle itself clears it, and a drag that follows does not set it again.
    ruler.mouseDown (mouseAt (ruler, 100, 3));
    CHECK (! marker.has_value());
    ruler.mouseDrag (mouseAt (ruler, 150, 3));
    CHECK (! marker.has_value());
}

TEST_CASE ("TimelineRuler: the marker's line and triangle are drawn through both rows", "[playback][ruler][marker]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    TimelineRuler ruler ([] (int x) { return (double) x * 10.0; });
    ruler.setSize (500, TimelineRuler::height);
    std::optional<double> marker;
    ruler.setMarks ([] (double tick) { return (int) (tick / 10.0); },
                    [] { return RulerGrid { TempoMap ({}, 480), { 4, 4 } }; });
    ruler.setMarker ([&] { return marker; });

    const auto amber = juce::Colour (SongsmithColours::accentAmber);
    auto render = [&]
    {
        juce::Image image (juce::Image::ARGB, 500, TimelineRuler::height, true, juce::SoftwareImageType());
        juce::Graphics g (image);
        ruler.paintEntireComponent (g, false);
        return image;
    };

    marker.reset();
    CHECK (render().getPixelAt (100, 3) != amber);

    marker = 1000.0;   // x = 100
    const auto image = render();
    CHECK (image.getPixelAt (100, 3) == amber);                          // the triangle
    CHECK (image.getPixelAt (100, TimelineRuler::height - 3) == amber);  // the line, in the clock row
}

TEST_CASE ("MarkerOverlay: shows nothing until a marker is set", "[playback][overlay][marker]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SongDocument doc;
    RecordingSink sink;
    PlaybackController controller (doc, sink);
    addNote (addTrack (doc), 60, 0, 9600);   // a MIDI is open: markers need something to mark
    controller.flushRebuild();
    MarkerOverlay overlay (controller, [] (double tick) { return (int) (tick * 0.1); });
    overlay.setSize (500, 100);

    CHECK (! overlay.currentX().has_value());
    controller.setMarkerTick (1000.0);
    REQUIRE (overlay.currentX().has_value());
    CHECK (*overlay.currentX() == 100);
    controller.clearMarker();
    CHECK (! overlay.currentX().has_value());
}

TEST_CASE ("MarkerOverlay: it is only a line and never takes clicks", "[playback][overlay][marker]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SongDocument doc;
    RecordingSink sink;
    PlaybackController controller (doc, sink);
    addNote (addTrack (doc), 60, 0, 9600);   // a MIDI is open: markers need something to mark
    controller.flushRebuild();
    MarkerOverlay overlay (controller, [] (double tick) { return (int) (tick * 0.1); });
    overlay.setSize (500, 100);
    controller.setMarkerTick (1000.0);   // x = 100

    bool onThis = true;
    bool onChildren = true;
    overlay.getInterceptsMouseClicks (onThis, onChildren);
    CHECK (! onThis);
    CHECK (! onChildren);

    // The line runs the whole height, from the very top (the bar above carries the triangle).
    juce::Image image (juce::Image::ARGB, 500, 100, true, juce::SoftwareImageType());
    juce::Graphics g (image);
    overlay.paintEntireComponent (g, false);
    const auto amber = juce::Colour (SongsmithColours::accentAmber);
    CHECK (image.getPixelAt (100, 0) == amber);
    CHECK (image.getPixelAt (100, 99) == amber);
    CHECK (image.getPixelAt (110, 50) != amber);
}
