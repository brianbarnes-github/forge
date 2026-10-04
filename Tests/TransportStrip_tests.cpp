#include "PlaybackTestSupport.h"
#include "UI/Playback/PlaybackController.h"
#include "UI/Playback/TransportStrip.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace lotro;
using namespace lotro::playbacktest;
using Catch::Approx;

namespace
{
    // Button::triggerClick() only posts a command message; pump the loop so the click lands.
    void click (juce::Button& b)
    {
        b.triggerClick();
        juce::MessageManager::getInstance()->runDispatchLoopUntil (20);
    }
}

TEST_CASE ("TransportStrip: buttons never take keyboard focus, so Space cannot re-click them", "[playback][strip]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SongDocument doc;
    RecordingSink sink;
    PlaybackController controller (doc, sink);
    TransportStrip strip (controller);
    strip.setSize (300, TransportStrip::height);
    for (auto* b : strip.buttonsForTesting())
        CHECK (! b->getWantsKeyboardFocus());
}

TEST_CASE ("TransportStrip: the play button toggles playback and its label follows the state", "[playback][strip]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SongDocument doc;
    RecordingSink sink;
    PlaybackController controller (doc, sink);
    addNote (addTrack (doc), 60, 0, 4800);
    controller.flushRebuild();
    TransportStrip strip (controller);

    CHECK (strip.playButtonForTesting().getButtonText() == "Play");
    click (strip.playButtonForTesting());
    CHECK (controller.isPlaying());
    CHECK (strip.playButtonForTesting().getButtonText() == "Pause");
    click (strip.playButtonForTesting());
    CHECK (! controller.isPlaying());
    CHECK (strip.playButtonForTesting().getButtonText() == "Play");
}

TEST_CASE ("TransportStrip: stop and go-to buttons drive the controller", "[playback][strip]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SongDocument doc;
    RecordingSink sink;
    PlaybackController controller (doc, sink);
    addNote (addTrack (doc), 60, 0, 4800);
    controller.flushRebuild();
    TransportStrip strip (controller);

    click (*strip.buttonsForTesting()[4]);           // go to end
    CHECK (controller.getPositionSeconds() > 0.0);
    click (*strip.buttonsForTesting()[0]);           // go to start
    CHECK (controller.getPositionSeconds() == Approx (0.0));
}
