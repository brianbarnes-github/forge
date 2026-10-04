#include "PlaybackTestSupport.h"
#include "UI/Playback/PlaybackController.h"
#include "UI/Playback/PlayheadOverlay.h"
#include "UI/Playback/TimelineRuler.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace lotro;
using namespace lotro::playbacktest;

namespace
{
    juce::MouseEvent mouseAt (juce::Component& c, int x)
    {
        return juce::MouseEvent (juce::Desktop::getInstance().getMainMouseSource(),
                                 juce::Point<float> ((float) x, 3.0f), juce::ModifierKeys(),
                                 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, &c, &c, juce::Time::getCurrentTime(),
                                 juce::Point<float> ((float) x, 3.0f), juce::Time::getCurrentTime(), 1, false);
    }
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

TEST_CASE ("TimelineRuler: click and drag report the tick under the pointer, clamped at zero", "[playback][ruler]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    TimelineRuler ruler ([] (int x) { return (double) x * 10.0; });
    ruler.setSize (500, TimelineRuler::height);
    double lastTick = -1.0;
    ruler.onSeek = [&] (double t) { lastTick = t; };

    ruler.mouseDown (mouseAt (ruler, 25));
    CHECK (lastTick == Catch::Approx (250.0));
    ruler.mouseDrag (mouseAt (ruler, 40));
    CHECK (lastTick == Catch::Approx (400.0));
    ruler.mouseDrag (mouseAt (ruler, -30));
    CHECK (lastTick == Catch::Approx (0.0).margin (1e-9));
}
