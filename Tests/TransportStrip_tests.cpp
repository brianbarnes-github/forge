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

TEST_CASE ("TransportStrip: every icon is a non-empty path that stays inside the unit square", "[playback][strip]")
{
    for (auto icon : { TransportIcon::goToStart, TransportIcon::rewind, TransportIcon::play,
                       TransportIcon::pause, TransportIcon::stop, TransportIcon::goToEnd })
    {
        const auto bounds = transportIconPath (icon).getBounds();
        CHECK (! bounds.isEmpty());
        CHECK (bounds.getX() >= 0.0f);
        CHECK (bounds.getY() >= 0.0f);
        CHECK (bounds.getRight() <= 1.0f);
        CHECK (bounds.getBottom() <= 1.0f);
    }
}

TEST_CASE ("TransportStrip: buttons never take keyboard focus, so Space cannot re-click them", "[playback][strip]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SongDocument doc;
    RecordingSink sink;
    PlaybackController controller (doc, sink);
    TransportStrip strip (controller, doc);
    strip.setSize (300, TransportStrip::height);
    for (auto* b : strip.buttonsForTesting())
        CHECK (! b->getWantsKeyboardFocus());
}

TEST_CASE ("TransportStrip: the play button toggles playback and its icon follows the state", "[playback][strip]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SongDocument doc;
    RecordingSink sink;
    PlaybackController controller (doc, sink);
    addNote (addTrack (doc), 60, 0, 4800);
    controller.flushRebuild();
    TransportStrip strip (controller, doc);

    CHECK (strip.playIconForTesting() == TransportIcon::play);
    click (strip.playButtonForTesting());
    CHECK (controller.isPlaying());
    CHECK (strip.playIconForTesting() == TransportIcon::pause);
    click (strip.playButtonForTesting());
    CHECK (! controller.isPlaying());
    CHECK (strip.playIconForTesting() == TransportIcon::play);
}

TEST_CASE ("TransportStrip: stop and go-to buttons drive the controller", "[playback][strip]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SongDocument doc;
    RecordingSink sink;
    PlaybackController controller (doc, sink);
    addNote (addTrack (doc), 60, 0, 4800);
    controller.flushRebuild();
    TransportStrip strip (controller, doc);

    click (*strip.buttonsForTesting()[4]);           // go to end
    CHECK (controller.getPositionSeconds() > 0.0);
    click (*strip.buttonsForTesting()[0]);           // go to start
    CHECK (controller.getPositionSeconds() == Approx (0.0));
}

TEST_CASE ("TransportStrip: the bar starts as three equal areas with the buttons centred in the middle one", "[playback][strip]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SongDocument doc;
    RecordingSink sink;
    PlaybackController controller (doc, sink);
    TransportStrip strip (controller, doc);

    for (const int width : { 600, 900, 1200 })
    {
        strip.setSize (width, TransportStrip::height);
        const auto left = strip.leftAreaForTesting().getBounds();
        const auto mid = strip.centreAreaForTesting().getBounds();
        const auto right = strip.rightAreaForTesting().getBounds();

        CHECK (std::abs (left.getWidth() - mid.getWidth()) <= 8);
        CHECK (std::abs (mid.getWidth() - right.getWidth()) <= 8);
        CHECK (left.getRight() <= mid.getX());
        CHECK (mid.getRight() <= right.getX());

        const auto buttons = strip.buttonsForTesting();
        const int centreOfButtons = (buttons.front()->getX() + buttons.back()->getRight()) / 2;
        CHECK (std::abs (centreOfButtons - (mid.getX() + mid.getWidth() / 2)) <= 1);
        CHECK (buttons.front()->getX() >= mid.getX());
        CHECK (buttons.back()->getRight() <= mid.getRight());
    }
}

TEST_CASE ("TransportStrip: the buttons stay inside the middle area on a narrow bar", "[playback][strip]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SongDocument doc;
    RecordingSink sink;
    PlaybackController controller (doc, sink);
    TransportStrip strip (controller, doc);

    strip.setSize (300, TransportStrip::height);
    const auto mid = strip.centreAreaForTesting().getBounds();
    CHECK (strip.buttonsForTesting().front()->getX() >= mid.getX());
    CHECK (strip.buttonsForTesting().back()->getRight() <= mid.getRight());
}

TEST_CASE ("TransportStrip: dragging a divider resizes the areas and the buttons follow the middle one", "[playback][strip]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SongDocument doc;
    RecordingSink sink;
    PlaybackController controller (doc, sink);
    TransportStrip strip (controller, doc);
    strip.setSize (900, TransportStrip::height);

    const int rightBefore = strip.rightAreaForTesting().getX();
    strip.dragDividerForTesting (0, 100);

    CHECK (strip.leftAreaForTesting().getRight() == 100);
    CHECK (strip.rightAreaForTesting().getX() == rightBefore);

    const auto mid = strip.centreAreaForTesting().getBounds();
    const auto buttons = strip.buttonsForTesting();
    CHECK (std::abs ((buttons.front()->getX() + buttons.back()->getRight()) / 2 - (mid.getX() + mid.getWidth() / 2)) <= 1);
}

TEST_CASE ("TransportStrip: the right area holds the transport controls and they follow its bounds", "[playback][strip]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SongDocument doc;
    RecordingSink sink;
    PlaybackController controller (doc, sink);
    TransportStrip strip (controller, doc);
    strip.setSize (1400, TransportStrip::height);

    auto& controls = strip.controlsForTesting();
    CHECK (controls.getParentComponent() == &strip.rightAreaForTesting());
    CHECK (controls.getBounds() == strip.rightAreaForTesting().getLocalBounds());
    strip.dragDividerForTesting (1, 1000);
    CHECK (controls.getBounds() == strip.rightAreaForTesting().getLocalBounds());
}

TEST_CASE ("TransportStrip: a narrow right area clips the controls without touching the buttons", "[playback][strip]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SongDocument doc;
    RecordingSink sink;
    PlaybackController controller (doc, sink);
    TransportStrip strip (controller, doc);
    strip.setSize (500, TransportStrip::height);

    CHECK (strip.rightAreaForTesting().getX() >= strip.centreAreaForTesting().getRight());
    CHECK (strip.rightAreaForTesting().getRight() <= strip.getWidth());
}
