#include "PlaybackTestSupport.h"
#include "UI/Playback/PlaybackController.h"
#include "UI/Playback/RateMapping.h"
#include "UI/Playback/TransportControls.h"
#include "UI/Playback/TransportText.h"
#include "UI/TempoEdit.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <memory>

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
            box.setText (juce::String::fromUTF8 (text), juce::dontSendNotification);
            box.onReturnKey();
        }
    };
}

TEST_CASE ("TransportControls: with no loop the Selection boxes show the zero defaults", "[playback][controls]")
{
    Rig rig;
    CHECK (rig.controls->startBoxForTesting().getText() == "1.1.0");
    CHECK (rig.controls->endBoxForTesting().getText() == "1.1.0");
    CHECK (rig.controls->lengthBoxForTesting().getText() == "0");
    CHECK (rig.controls->startBoxForTesting().isEnabled());
}

TEST_CASE ("TransportControls: typing an end beside the default start creates the loop from zero", "[playback][controls]")
{
    Rig rig;
    rig.type (rig.controls->endBoxForTesting(), "3.1.0");
    REQUIRE (rig.controller.getLoopRange().has_value());
    CHECK (rig.controller.getLoopRange()->startTick == Approx (0.0));
    CHECK (rig.controller.getLoopRange()->endTick == Approx (3840.0));
    CHECK (rig.controls->startBoxForTesting().getText() == "1.1.0");
    CHECK (rig.controls->lengthBoxForTesting().getText() == "8");
}

TEST_CASE ("TransportControls: typing a length beside the default start creates the loop from zero", "[playback][controls]")
{
    Rig rig;
    rig.type (rig.controls->lengthBoxForTesting(), "6");
    REQUIRE (rig.controller.getLoopRange().has_value());
    CHECK (rig.controller.getLoopRange()->startTick == Approx (0.0));
    CHECK (rig.controller.getLoopRange()->endTick == Approx (6 * 480.0));
    CHECK (rig.controls->endBoxForTesting().getText() == "2.3.0");
}

TEST_CASE ("TransportControls: a start typed beside the default end is held until an end is typed", "[playback][controls]")
{
    Rig rig;
    rig.controls->pretendFocusedForTesting (true);
    rig.type (rig.controls->startBoxForTesting(), "2.1.0");
    CHECK (! rig.controller.getLoopRange().has_value());              // no zero-length or silently reordered loop
    CHECK (rig.controls->startBoxForTesting().getText() == "2.1.0");  // held, not reverted
    CHECK (rig.controls->reasonShownForTesting().isEmpty());
    rig.type (rig.controls->endBoxForTesting(), "3.1.0");
    REQUIRE (rig.controller.getLoopRange().has_value());
    CHECK (rig.controller.getLoopRange()->startTick == Approx (1920.0));
    CHECK (rig.controller.getLoopRange()->endTick == Approx (3840.0));
}

TEST_CASE ("TransportControls: a tiny end beside the default start is rejected with a reason and the defaults return", "[playback][controls]")
{
    Rig rig;
    rig.type (rig.controls->endBoxForTesting(), "1.1.1");
    CHECK (! rig.controller.getLoopRange().has_value());
    CHECK (rig.controls->reasonShownForTesting().isNotEmpty());
    CHECK (rig.controls->endBoxForTesting().getText() == "1.1.0");
}

TEST_CASE ("TransportControls: emptying all three boxes with no loop shows the defaults again", "[playback][controls]")
{
    Rig rig;
    for (auto* box : { &rig.controls->startBoxForTesting(), &rig.controls->endBoxForTesting(), &rig.controls->lengthBoxForTesting() })
        box->setText ("", juce::dontSendNotification);
    rig.controls->startBoxForTesting().onReturnKey();
    CHECK (! rig.controller.getLoopRange().has_value());
    CHECK (rig.controls->startBoxForTesting().getText() == "1.1.0");
    CHECK (rig.controls->endBoxForTesting().getText() == "1.1.0");
    CHECK (rig.controls->lengthBoxForTesting().getText() == "0");
}

TEST_CASE ("TransportControls: clearing the loop externally returns the defaults", "[playback][controls]")
{
    Rig rig;
    rig.controller.setLoopRange (960.0, 2880.0);
    rig.controller.clearLoopRange();
    CHECK (rig.controls->startBoxForTesting().getText() == "1.1.0");
    CHECK (rig.controls->endBoxForTesting().getText() == "1.1.0");
    CHECK (rig.controls->lengthBoxForTesting().getText() == "0");
}

TEST_CASE ("TransportControls: with no song the Selection boxes stay blank", "[playback][controls]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SongDocument doc;
    RecordingSink sink;
    PlaybackController controller (doc, sink);
    TransportControls controls (controller, doc);
    CHECK (controls.startBoxForTesting().getText().isEmpty());
    CHECK (controls.endBoxForTesting().getText().isEmpty());
    CHECK (controls.lengthBoxForTesting().getText().isEmpty());
}

TEST_CASE ("TransportControls: the rate knob leads the row and is a top-centred rotary", "[playback][controls][rate]")
{
    Rig rig;
    auto& slider = rig.controls->rateSliderForTesting();
    const auto bounds = rig.controls->getLocalBounds();
    CHECK (slider.getBounds().getWidth() == slider.getBounds().getHeight());
    CHECK (slider.getBounds().getWidth() == 24);
    CHECK (bounds.contains (slider.getBounds()));
    CHECK (slider.getBounds().getCentreY() == bounds.getCentreY());
    CHECK (slider.getBounds().getRight() <= rig.controls->rateReadoutForTesting().getX());
    CHECK (rig.controls->rateReadoutForTesting().getRight() <= rig.controls->startBoxForTesting().getX());
    CHECK (rig.controls->startBoxForTesting().getX() < rig.controls->meterBoxForTesting().getX());
    CHECK (rig.controls->meterBoxForTesting().getX() < rig.controls->tempoBoxForTesting().getX());

    CHECK (slider.isRotary());
    const auto params = slider.getRotaryParameters();
    CHECK ((params.startAngleRadians + params.endAngleRadians) / 2.0f == Approx (2.0f * juce::MathConstants<float>::pi));
    CHECK (params.stopAtEnd);
    CHECK (slider.getValue() == Approx (0.0));   // neutral, so it points straight up
}

TEST_CASE ("TransportControls: the knob and readout fit in 300 px", "[playback][controls][rate]")
{
    Rig rig;
    CHECK (rig.controls->rateSliderForTesting().getRight() <= 300);
    CHECK (rig.controls->rateReadoutForTesting().getRight() <= 300);
}

TEST_CASE ("TransportControls: typing a start and an end creates the loop range", "[playback][controls]")
{
    Rig rig;
    rig.type (rig.controls->startBoxForTesting(), "2.1.0");
    CHECK (! rig.controller.getLoopRange().has_value());   // held until an end is typed
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
    CHECK (rig.controls->startBoxForTesting().getText() == "1.1.0");
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

TEST_CASE ("TransportControls: clearing the three boxes one by one clears the loop", "[playback][controls]")
{
    Rig rig;
    rig.controller.setLoopRange (1920.0, 3840.0);
    rig.controls->pretendFocusedForTesting (true);
    auto& start = rig.controls->startBoxForTesting();
    auto& end = rig.controls->endBoxForTesting();
    auto& length = rig.controls->lengthBoxForTesting();

    start.setText ("", juce::dontSendNotification);
    start.onFocusLost();   // tabbing away: the empty box is held, the loop untouched
    CHECK (start.getText().isEmpty());
    CHECK (rig.controller.getLoopRange().has_value());

    end.setText ("", juce::dontSendNotification);
    end.onFocusLost();
    CHECK (end.getText().isEmpty());
    CHECK (rig.controller.getLoopRange().has_value());

    length.setText ("", juce::dontSendNotification);
    length.onFocusLost();
    CHECK (! rig.controller.getLoopRange().has_value());
    CHECK (start.getText() == "1.1.0");   // the defaults return
    CHECK (end.getText() == "1.1.0");
    CHECK (length.getText() == "0");
}

TEST_CASE ("TransportControls: clearing all three boxes and pressing Enter clears the loop", "[playback][controls]")
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

TEST_CASE ("TransportControls: bad text in a focused box is replaced by the true value", "[playback][controls]")
{
    Rig rig;
    rig.controller.setLoopRange (1920.0, 3840.0);
    auto& start = rig.controls->startBoxForTesting();
    rig.controls->pretendFocusedForTesting (true);   // real focus needs a window manager
    rig.type (start, "nonsense");
    CHECK (start.getText() == "2.1.0");

    auto& length = rig.controls->lengthBoxForTesting();
    rig.type (length, "-3");
    CHECK (length.getText() == "4");
}

TEST_CASE ("TransportControls: a valid reversed entry in a focused box is shown ordered", "[playback][controls]")
{
    Rig rig;
    rig.controller.setLoopRange (960.0, 1920.0);
    auto& start = rig.controls->startBoxForTesting();
    rig.controls->pretendFocusedForTesting (true);   // real focus needs a window manager
    rig.type (start, "3.1.0");   // after the end (2.1.0)
    CHECK (start.getText() == "2.1.0");
    CHECK (rig.controls->endBoxForTesting().getText() == "3.1.0");
}

TEST_CASE ("TransportControls: the meter and tempo boxes show the values at the playhead", "[playback][controls][timing]")
{
    Rig rig;
    CHECK (rig.controls->meterBoxForTesting().getText() == "4/4");
    CHECK (rig.controls->tempoBoxForTesting().getText().contains ("120"));
    CHECK (rig.controls->meterBoxForTesting().isEnabled());
}

TEST_CASE ("TransportControls: typing a meter sets it at the nearest bar to the playhead, as one undo step", "[playback][controls][timing]")
{
    Rig rig;
    rig.controller.seekToTick (2000.0);   // 80 ticks past the bar-2 line
    rig.doc.getUndoManager().clearUndoHistory();
    rig.type (rig.controls->meterBoxForTesting(), "6/8");
    rig.controller.flushRebuild();

    bool found = false;
    for (const auto& e : listTempoMeterEvents (rig.doc))
        if (e.kind == TempoMeterKind::meter && e.tick == 1920 && e.numerator == 6 && e.denominator == 8)
            found = true;
    CHECK (found);
    CHECK (rig.controls->meterBoxForTesting().getText() == "6/8");

    rig.doc.getUndoManager().undo();
    rig.controller.flushRebuild();
    CHECK (rig.controls->meterBoxForTesting().getText() == "4/4");
}

TEST_CASE ("TransportControls: typing a tempo sets it at the playhead and undo restores the box", "[playback][controls][timing]")
{
    Rig rig;
    rig.controller.seekToTick (1920.0);
    rig.doc.getUndoManager().clearUndoHistory();
    rig.type (rig.controls->tempoBoxForTesting(), "\xe2\x99\xa9=90");
    rig.controller.flushRebuild();

    CHECK (bpmAt (listTempoMeterEvents (rig.doc), 1920) == Approx (90.0).margin (0.01));
    CHECK (bpmAt (listTempoMeterEvents (rig.doc), 0) == Approx (120.0).margin (0.01));
    CHECK (rig.controls->tempoBoxForTesting().getText().contains ("90"));

    rig.doc.getUndoManager().undo();
    rig.controller.flushRebuild();
    CHECK (rig.controls->tempoBoxForTesting().getText().contains ("120"));
}

TEST_CASE ("TransportControls: the boxes follow the playhead across a tempo change", "[playback][controls][timing]")
{
    Rig rig;
    rig.controller.seekToTick (1920.0);
    rig.type (rig.controls->tempoBoxForTesting(), "60");
    rig.controller.flushRebuild();
    rig.controller.seekToTick (0.0);
    CHECK (rig.controls->tempoBoxForTesting().getText().contains ("120"));
    rig.controller.seekToTick (3000.0);
    CHECK (rig.controls->tempoBoxForTesting().getText().contains ("60"));
}

TEST_CASE ("TransportControls: a rejected meter or tempo reverts and leaves the document alone", "[playback][controls][timing]")
{
    Rig rig;
    rig.doc.getUndoManager().clearUndoHistory();
    rig.type (rig.controls->meterBoxForTesting(), "40/4");   // numerator above 32
    CHECK (rig.controls->meterBoxForTesting().getText() == "4/4");
    rig.type (rig.controls->meterBoxForTesting(), "banana");
    CHECK (rig.controls->meterBoxForTesting().getText() == "4/4");
    rig.type (rig.controls->tempoBoxForTesting(), "5000");   // above maxBpm
    CHECK (rig.controls->tempoBoxForTesting().getText().contains ("120"));
    CHECK (! rig.doc.getUndoManager().canUndo());
}

TEST_CASE ("TransportControls: a rejected entry reverts even in a focused box", "[playback][controls][timing]")
{
    Rig rig;
    rig.controls->pretendFocusedForTesting (true);
    rig.doc.getUndoManager().clearUndoHistory();
    rig.type (rig.controls->meterBoxForTesting(), "banana");
    CHECK (rig.controls->meterBoxForTesting().getText() == "4/4");
    rig.type (rig.controls->meterBoxForTesting(), "40/4");
    CHECK (rig.controls->meterBoxForTesting().getText() == "4/4");
    rig.type (rig.controls->tempoBoxForTesting(), "banana");
    CHECK (rig.controls->tempoBoxForTesting().getText().contains ("120"));
    rig.type (rig.controls->tempoBoxForTesting(), "5000");
    CHECK (rig.controls->tempoBoxForTesting().getText().contains ("120"));
    CHECK (! rig.doc.getUndoManager().canUndo());
}

TEST_CASE ("TransportControls: a committed entry is replaced by the true value even in a focused box", "[playback][controls][timing]")
{
    Rig rig;
    rig.controls->pretendFocusedForTesting (true);
    rig.controller.seekToTick (1920.0);
    rig.type (rig.controls->tempoBoxForTesting(), "90.0");   // true value is shown in the exact-tempo format
    rig.controller.flushRebuild();
    CHECK (rig.controls->tempoBoxForTesting().getText() == juce::String::fromUTF8 (formatExactTempoText (90.0).c_str()));
}

TEST_CASE ("TransportControls: typing is not overwritten while the playhead moves", "[playback][controls][timing]")
{
    Rig rig;
    rig.controls->pretendFocusedForTesting (true);
    rig.controls->meterBoxForTesting().setText ("6", juce::dontSendNotification);   // typed, not committed
    rig.controls->tempoBoxForTesting().setText ("77", juce::dontSendNotification);
    rig.controller.seekToTick (480.0);                                              // refreshTiming runs
    CHECK (rig.controls->meterBoxForTesting().getText() == "6");
    CHECK (rig.controls->tempoBoxForTesting().getText() == "77");

    rig.controls->pretendFocusedForTesting (false);
    rig.controller.seekToTick (960.0);
    CHECK (rig.controls->meterBoxForTesting().getText() == "4/4");
}

TEST_CASE ("TransportControls: with no song the boxes are disabled and typing does nothing", "[playback][controls][timing]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SongDocument doc;
    RecordingSink sink;
    PlaybackController controller (doc, sink);
    TransportControls controls (controller, doc);
    controls.setSize (TransportControls::preferredWidth, 28);

    CHECK (! controls.meterBoxForTesting().isEnabled());
    CHECK (! controls.tempoBoxForTesting().isEnabled());
    CHECK (! controls.startBoxForTesting().isEnabled());
    controls.meterBoxForTesting().setText ("6/8", juce::dontSendNotification);
    controls.meterBoxForTesting().onReturnKey();   // must not throw
    CHECK (controller.getLoopRange() == std::nullopt);
    CHECK (! doc.getUndoManager().canUndo());
}

TEST_CASE ("TransportControls: a meter typed late in a bar lands at the start of the bar containing the playhead", "[playback][controls][timing]")
{
    Rig rig;
    rig.controller.seekToTick (3000.0);   // bar 2 runs 1920..3840; 3000 is nearer the 3840 line
    rig.type (rig.controls->meterBoxForTesting(), "6/8");
    rig.controller.flushRebuild();

    std::vector<int> meterTicks;
    for (const auto& e : listTempoMeterEvents (rig.doc))
        if (e.kind == TempoMeterKind::meter && e.numerator == 6 && e.denominator == 8)
            meterTicks.push_back (e.tick);
    CHECK (meterTicks == std::vector<int> { 1920 });
}

TEST_CASE ("TransportControls: re-entering the value already in force edits nothing", "[playback][controls][timing]")
{
    Rig rig;
    rig.controller.seekToTick (1920.0);
    rig.doc.getUndoManager().clearUndoHistory();

    rig.type (rig.controls->meterBoxForTesting(), "04/04");
    CHECK (! rig.doc.getUndoManager().canUndo());
    CHECK (rig.controls->meterBoxForTesting().getText() == "4/4");

    rig.type (rig.controls->tempoBoxForTesting(), "120.00001");   // rounds to the same microseconds per beat
    CHECK (! rig.doc.getUndoManager().canUndo());
    CHECK (rig.controls->tempoBoxForTesting().getText().contains ("120"));

    rig.type (rig.controls->tempoBoxForTesting(), "121");   // a real change still edits
    CHECK (rig.doc.getUndoManager().canUndo());
}

TEST_CASE ("TransportControls: a focus loss after Enter does not edit twice", "[playback][controls][timing]")
{
    Rig rig;
    rig.controller.seekToTick (1920.0);
    rig.doc.getUndoManager().clearUndoHistory();
    rig.type (rig.controls->meterBoxForTesting(), "6/8");
    rig.controls->meterBoxForTesting().onFocusLost();
    rig.doc.getUndoManager().undo();
    CHECK (! rig.doc.getUndoManager().canUndo());   // exactly one step was recorded
}

TEST_CASE ("TransportControls: Escape reverts a box to the true value", "[playback][controls][timing]")
{
    Rig rig;
    rig.controls->pretendFocusedForTesting (true);
    rig.controls->meterBoxForTesting().setText ("9/9", juce::dontSendNotification);
    rig.controls->meterBoxForTesting().onEscapeKey();
    CHECK (rig.controls->meterBoxForTesting().getText() == "4/4");
    rig.controls->tempoBoxForTesting().setText ("77", juce::dontSendNotification);
    rig.controls->tempoBoxForTesting().onEscapeKey();
    CHECK (rig.controls->tempoBoxForTesting().getText().contains ("120"));

    rig.controller.setLoopRange (960.0, 2880.0);
    rig.controls->startBoxForTesting().setText ("3.1.0", juce::dontSendNotification);
    rig.controls->startBoxForTesting().onEscapeKey();
    CHECK (rig.controls->startBoxForTesting().getText() == "1.3.0");
    CHECK (rig.controller.getLoopRange()->startTick == Approx (960.0));
}

TEST_CASE ("TransportControls: emptying only some Selection boxes restores the true range", "[playback][controls]")
{
    Rig rig;
    rig.controller.setLoopRange (960.0, 2880.0);
    rig.controls->pretendFocusedForTesting (true);
    rig.controls->startBoxForTesting().setText ({}, juce::dontSendNotification);
    rig.controls->startBoxForTesting().onReturnKey();
    CHECK (rig.controls->startBoxForTesting().getText() == "1.3.0");
    CHECK (rig.controls->endBoxForTesting().getText() == "2.3.0");
    CHECK (rig.controls->lengthBoxForTesting().getText() == "4");
    REQUIRE (rig.controller.getLoopRange().has_value());

    rig.controls->endBoxForTesting().setText ({}, juce::dontSendNotification);
    rig.controls->lengthBoxForTesting().setText ({}, juce::dontSendNotification);
    rig.controls->lengthBoxForTesting().onReturnKey();
    CHECK (rig.controls->endBoxForTesting().getText() == "2.3.0");
    CHECK (rig.controls->lengthBoxForTesting().getText() == "4");
}

TEST_CASE ("TransportControls: Enter gives keyboard focus away", "[playback][controls][timing]")
{
    Rig rig;
    rig.controls->addToDesktop (0);
    rig.controls->setVisible (true);
    rig.controls->meterBoxForTesting().grabKeyboardFocus();
    if (! rig.controls->meterBoxForTesting().hasKeyboardFocus (true))
    {
        rig.controls->removeFromDesktop();
        SKIP ("no window manager: keyboard focus cannot be obtained here");
    }
    rig.controls->meterBoxForTesting().setText ("6/8", juce::dontSendNotification);
    rig.controls->meterBoxForTesting().onReturnKey();
    CHECK (! rig.controls->meterBoxForTesting().hasKeyboardFocus (true));
    rig.controls->removeFromDesktop();
}

TEST_CASE ("TransportControls: a rejected entry shows its reason in a bubble and keeps the hint tooltip", "[playback][controls][timing]")
{
    Rig rig;
    CHECK (rig.controls->reasonShownForTesting().isEmpty());
    const auto hint = rig.controls->meterBoxForTesting().getTooltip();
    CHECK (hint.isNotEmpty());
    rig.type (rig.controls->meterBoxForTesting(), "40/4");
    CHECK (rig.controls->reasonShownForTesting().containsIgnoreCase ("numerator"));
    CHECK (rig.controls->meterBoxForTesting().getTooltip() == hint);
    rig.type (rig.controls->tempoBoxForTesting(), "banana");
    CHECK (rig.controls->reasonShownForTesting().containsIgnoreCase ("tempo"));
}

TEST_CASE ("TransportControls: the rate menu resets the rate to 1.000", "[playback][controls][rate]")
{
    Rig rig;
    rig.controller.setRate (2.0);
    rig.controls->resetRate();
    CHECK (rig.controller.rate() == Approx (1.0));
    CHECK (rig.controls->rateReadoutForTesting().getText() == "1.000");
}

TEST_CASE ("TransportControls: setting the first tempo to the current tempo scales it and resets the rate, as one undo step", "[playback][controls][rate]")
{
    Rig rig;
    rig.controller.setRate (1.5);
    rig.doc.getUndoManager().clearUndoHistory();
    rig.controls->bakeRate (RateBake::firstTempo);
    rig.controller.flushRebuild();

    CHECK (bpmAt (listTempoMeterEvents (rig.doc), 0) == Approx (180.0).margin (0.01));
    CHECK (rig.controller.rate() == Approx (1.0));

    rig.doc.getUndoManager().undo();
    rig.controller.flushRebuild();
    CHECK (bpmAt (listTempoMeterEvents (rig.doc), 0) == Approx (120.0).margin (0.01));
}

TEST_CASE ("TransportControls: setting the playhead tempo to the current tempo writes tempo x rate there and leaves the rate on", "[playback][controls][rate]")
{
    Rig rig;
    REQUIRE_FALSE (setTempo (rig.doc, 1920, 90.0));
    rig.controller.flushRebuild();
    rig.controller.seekToTick (2000.0);
    rig.controller.setRate (2.0);
    rig.doc.getUndoManager().clearUndoHistory();
    rig.controls->bakeRate (RateBake::playheadTempo);
    rig.controller.flushRebuild();

    const auto events = listTempoMeterEvents (rig.doc);
    CHECK (bpmAt (events, 0) == Approx (120.0).margin (0.01));      // the first tempo is left alone
    CHECK (bpmAt (events, 1920) == Approx (90.0).margin (0.01));
    CHECK (bpmAt (events, 2000) == Approx (180.0).margin (0.01));
    CHECK (rig.controller.rate() == Approx (2.0));   // the knob stays on

    rig.doc.getUndoManager().undo();
    rig.controller.flushRebuild();
    CHECK (bpmAt (listTempoMeterEvents (rig.doc), 2000) == Approx (90.0).margin (0.01));
}

TEST_CASE ("TransportControls: the tempo menu items at rate 1.000 add no marker and no undo step", "[playback][controls][rate]")
{
    Rig rig;
    rig.controller.seekToTick (2000.0);
    rig.doc.getUndoManager().clearUndoHistory();
    const auto before = listTempoMeterEvents (rig.doc).size();
    rig.controls->bakeRate (RateBake::playheadTempo);
    rig.controls->bakeRate (RateBake::firstTempo);
    CHECK (listTempoMeterEvents (rig.doc).size() == before);
    CHECK_FALSE (rig.doc.getUndoManager().canUndo());
}

TEST_CASE ("TransportControls: setting a tempo that the rate pushes the tempo out of range is rejected and changes nothing", "[playback][controls][rate]")
{
    Rig rig;
    REQUIRE_FALSE (setTempo (rig.doc, 0, 600.0));
    rig.controller.flushRebuild();
    rig.controller.setRate (2.0);   // 600 x 2 = 1200 > 1000
    rig.doc.getUndoManager().clearUndoHistory();
    rig.controls->bakeRate (RateBake::firstTempo);
    rig.controller.flushRebuild();

    CHECK (bpmAt (listTempoMeterEvents (rig.doc), 0) == Approx (600.0).margin (0.01));
    CHECK (rig.controller.rate() == Approx (2.0));
    CHECK (rig.controls->reasonShownForTesting().isNotEmpty());
}
