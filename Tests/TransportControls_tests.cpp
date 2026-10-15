#include "PlaybackTestSupport.h"
#include "UI/Playback/PlaybackController.h"
#include "UI/Playback/RateMapping.h"
#include "UI/Playback/TransportControls.h"
#include "UI/Playback/TransportText.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace lotro;
using namespace lotro::playbacktest;
using Catch::Approx;

namespace
{
    // 4 bars of 4/4 at 120 BPM: ticks 0..7680 (8 s).
    struct Rig
    {
        juce::ScopedJuceInitialiser_GUI juceInit;
        SongDocument doc;
        RecordingSink sink;
        PlaybackController controller { doc, sink };
        std::unique_ptr<TransportControls> controls;

        Rig()
        {
            addTempo (doc, 0, 120.0);
            addNote (addTrack (doc), 60, 0, 7680);
            controller.flushRebuild();
            controls = std::make_unique<TransportControls> (controller, doc);
            controls->setSize (TransportControls::preferredWidth, 28);
        }

        void type (juce::TextEditor& box, const char* text)
        {
            box.setText (text, juce::dontSendNotification);
            box.onReturnKey();
        }
    };
}

TEST_CASE ("TransportControls: with no loop the Selection boxes are empty", "[playback][controls]")
{
    Rig rig;
    CHECK (rig.controls->startBoxForTesting().getText().isEmpty());
    CHECK (rig.controls->endBoxForTesting().getText().isEmpty());
    CHECK (rig.controls->lengthBoxForTesting().getText().isEmpty());
}

TEST_CASE ("TransportControls: typing a start and an end creates the loop range", "[playback][controls]")
{
    Rig rig;
    rig.type (rig.controls->startBoxForTesting(), "2.1.0");
    CHECK (! rig.controller.getLoopRange().has_value());   // held until both edges are valid
    rig.type (rig.controls->endBoxForTesting(), "3.1.0");

    REQUIRE (rig.controller.getLoopRange().has_value());
    CHECK (rig.controller.getLoopRange()->startTick == Approx (1920.0));
    CHECK (rig.controller.getLoopRange()->endTick == Approx (3840.0));
    CHECK (rig.controls->lengthBoxForTesting().getText() == "4");
}

TEST_CASE ("TransportControls: dragging the loop updates the boxes", "[playback][controls]")
{
    Rig rig;
    rig.controller.setLoopRange (960.0, 2880.0);
    CHECK (rig.controls->startBoxForTesting().getText() == "1.3.0");
    CHECK (rig.controls->endBoxForTesting().getText() == "2.3.0");
    CHECK (rig.controls->lengthBoxForTesting().getText() == "4");
    rig.controller.clearLoopRange();
    CHECK (rig.controls->startBoxForTesting().getText().isEmpty());
}

TEST_CASE ("TransportControls: typing a length moves the end", "[playback][controls]")
{
    Rig rig;
    rig.controller.setLoopRange (1920.0, 3840.0);
    rig.type (rig.controls->lengthBoxForTesting(), "8");
    REQUIRE (rig.controller.getLoopRange().has_value());
    CHECK (rig.controller.getLoopRange()->startTick == Approx (1920.0));
    CHECK (rig.controller.getLoopRange()->endTick == Approx (1920.0 + 8 * 480.0));
    CHECK (rig.controls->endBoxForTesting().getText() == "4.1.0");   // 1920 + 3840 = 5760 -> bar 4 beat 1
}

TEST_CASE ("TransportControls: a reversed start/end is shown ordered", "[playback][controls]")
{
    Rig rig;
    rig.controller.setLoopRange (960.0, 1920.0);
    rig.type (rig.controls->startBoxForTesting(), "3.1.0");   // now after the end (2.1.0)
    REQUIRE (rig.controller.getLoopRange().has_value());
    CHECK (rig.controller.getLoopRange()->startTick < rig.controller.getLoopRange()->endTick);
    CHECK (rig.controls->startBoxForTesting().getText() == juce::String (formatPosition (meterSegments (rig.doc.getMeterChanges(), 480), rig.controller.getLoopRange()->startTick)));
}

TEST_CASE ("TransportControls: an end beyond the song is shown clamped", "[playback][controls]")
{
    Rig rig;
    rig.type (rig.controls->startBoxForTesting(), "2.1.0");
    rig.type (rig.controls->endBoxForTesting(), "99.1.0");
    REQUIRE (rig.controller.getLoopRange().has_value());
    const double songEndTick = rig.controller.currentSnapshot()->tempo().secondsToTicks (rig.controller.currentSnapshot()->endSeconds());
    CHECK (rig.controller.getLoopRange()->endTick <= songEndTick + 1e-6);
    CHECK (rig.controls->endBoxForTesting().getText() != "99.1.0");
}

TEST_CASE ("TransportControls: bad Selection text reverts to the current range", "[playback][controls]")
{
    Rig rig;
    rig.controller.setLoopRange (1920.0, 3840.0);
    rig.type (rig.controls->startBoxForTesting(), "nonsense");
    CHECK (rig.controller.getLoopRange()->startTick == Approx (1920.0));
    CHECK (rig.controls->startBoxForTesting().getText() == "2.1.0");
    rig.type (rig.controls->lengthBoxForTesting(), "-3");
    CHECK (rig.controller.getLoopRange()->endTick == Approx (3840.0));
    CHECK (rig.controls->lengthBoxForTesting().getText() == "4");
}

TEST_CASE ("TransportControls: clearing all three boxes clears the loop", "[playback][controls]")
{
    Rig rig;
    rig.controller.setLoopRange (1920.0, 3840.0);
    for (auto* box : { &rig.controls->startBoxForTesting(), &rig.controls->endBoxForTesting(), &rig.controls->lengthBoxForTesting() })
        box->setText ("", juce::dontSendNotification);
    rig.controls->startBoxForTesting().onReturnKey();
    CHECK (! rig.controller.getLoopRange().has_value());
}

TEST_CASE ("TransportControls: the rate knob drives the controller and shows 1.000 by default", "[playback][controls][rate]")
{
    Rig rig;
    CHECK (rig.controls->rateReadoutForTesting().getText() == "1.000");
    CHECK (rig.controls->rateSliderForTesting().getValue() == Approx (0.0));
    CHECK (rig.controls->rateSliderForTesting().isDoubleClickReturnEnabled());
    CHECK (rig.controls->rateSliderForTesting().getDoubleClickReturnValue() == Approx (0.0));

    rig.controls->rateSliderForTesting().setValue (1.0, juce::sendNotificationSync);   // log2 -> 2.0
    CHECK (rig.controller.rate() == Approx (2.0));
    CHECK (rig.controls->rateReadoutForTesting().getText() == "2.000");
}

TEST_CASE ("TransportControls: typing a rate moves the knob, and a controller change moves the readout", "[playback][controls][rate]")
{
    Rig rig;
    rig.controls->rateReadoutForTesting().setText ("0.5", juce::dontSendNotification);
    rig.controls->rateReadoutForTesting().onTextChange();
    CHECK (rig.controller.rate() == Approx (0.5));
    CHECK (rig.controls->rateSliderForTesting().getValue() == Approx (-1.0));

    rig.controller.setRate (4.0);
    CHECK (rig.controls->rateReadoutForTesting().getText() == "4.000");
    CHECK (rig.controls->rateSliderForTesting().getValue() == Approx (2.0));

    rig.controls->rateReadoutForTesting().setText ("junk", juce::dontSendNotification);
    rig.controls->rateReadoutForTesting().onTextChange();
    CHECK (rig.controls->rateReadoutForTesting().getText() == "4.000");   // reverted
}
