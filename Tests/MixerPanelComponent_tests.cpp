#include "PlaybackTestSupport.h"
#include "UI/Mixer/ActivePlaybackTarget.h"
#include "UI/Mixer/MixerPanelComponent.h"
#include "UI/Mixer/MixerStripComponent.h"
#include "UI/Mixer/SourceMidiTarget.h"

#include <catch2/catch_test_macros.hpp>

#include <juce_events/juce_events.h>
#include <juce_gui_basics/juce_gui_basics.h>

using namespace lotro;
using namespace lotro::playbacktest;

namespace
{
    struct Rig
    {
        juce::ScopedJuceInitialiser_GUI juceInit;
        SongDocument doc;
        RecordingSink sink;
        PlaybackController controller { doc, sink };
        SourceMidiTarget target { doc, controller };
        ActivePlaybackTarget active { target };
    };

    juce::int64 idOf (const juce::ValueTree& t) { return static_cast<juce::int64> (t.getProperty (SongIDs::trackId)); }

    // A slider drag as JUCE brackets it: onDragStart, value changes, onDragEnd.
    void drag (juce::Slider& s, std::initializer_list<double> values)
    {
        REQUIRE (s.onDragStart != nullptr);
        REQUIRE (s.onDragEnd != nullptr);
        s.onDragStart();
        for (const double v : values)
            s.setValue (v, juce::sendNotificationSync);
        s.onDragEnd();
    }

    juce::MouseEvent doubleClickOn (juce::Component& c)
    {
        const auto now = juce::Time::getCurrentTime();
        const juce::Point<float> p (5.0f, 5.0f);
        return juce::MouseEvent (juce::Desktop::getInstance().getMainMouseSource(), p,
                                 juce::ModifierKeys (juce::ModifierKeys::leftButtonModifier), 1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                                 &c, &c, now, p, now, 2, false);
    }

    void click (juce::Button& b)
    {
        b.triggerClick();
        juce::MessageManager::getInstance()->runDispatchLoopUntil (20);
    }
}

TEST_CASE ("MixerPanel: one strip per non-conductor track, in order", "[mixer][panel]")
{
    Rig r;
    r.doc.getConductorTrack();
    auto a = addTrack (r.doc, "A");
    auto b = addTrack (r.doc, "B");
    auto c = addTrack (r.doc, "C");
    MixerPanelComponent panel (r.active);
    panel.setSize (400, 300);

    REQUIRE (panel.numTrackStripsForTesting() == 3);
    CHECK (panel.trackStripForTesting (0)->getChannelId() == idOf (a));
    CHECK (panel.trackStripForTesting (1)->getChannelId() == idOf (b));
    CHECK (panel.trackStripForTesting (2)->getChannelId() == idOf (c));
    CHECK (panel.trackStripForTesting (3) == nullptr);
}

TEST_CASE ("MixerPanel: moving a fader writes playbackVolume in one undo step", "[mixer][panel]")
{
    Rig r;
    auto t = addTrack (r.doc, "A");
    MixerPanelComponent panel (r.active);
    auto& fader = panel.trackStripForTesting (0)->faderForTesting();
    CHECK (juce::roundToInt (fader.getValue()) == 100);

    // An earlier edit still open as a transaction: the drag must start its own
    // (startsGesture == true on its first change), or undo would revert both.
    r.doc.setProperty (t, SongIDs::name, "Renamed", true);

    drag (fader, { 40.0, 30.0, 20.0 });
    CHECK (trackPlaybackVolume (t) == 20);

    r.doc.getUndoManager().undo();
    CHECK (trackPlaybackVolume (t) == 100);
    CHECK (t.getProperty (SongIDs::name).toString() == "Renamed");

    // A second drag is a second undo step.
    drag (fader, { 70.0, 60.0 });
    drag (fader, { 50.0 });
    r.doc.getUndoManager().undo();
    CHECK (trackPlaybackVolume (t) == 60);
}

TEST_CASE ("MixerPanel: a change outside a drag is its own undo step", "[mixer][panel]")
{
    Rig r;
    auto t = addTrack (r.doc, "A");
    MixerPanelComponent panel (r.active);
    auto& fader = panel.trackStripForTesting (0)->faderForTesting();
    fader.setValue (80.0, juce::sendNotificationSync);
    fader.setValue (70.0, juce::sendNotificationSync);
    r.doc.getUndoManager().undo();
    CHECK (trackPlaybackVolume (t) == 80);
}

TEST_CASE ("MixerPanel: a head-side volume change moves the fader without echoing", "[mixer][panel]")
{
    Rig r;
    auto t = addTrack (r.doc, "A");
    MixerPanelComponent panel (r.active);
    r.target.flushPendingForTesting();
    r.doc.getUndoManager().clearUndoHistory();

    r.doc.setProperty (t, SongIDs::playbackVolume, 25, true);
    r.target.flushPendingForTesting();
    CHECK (juce::roundToInt (panel.trackStripForTesting (0)->faderForTesting().getValue()) == 25);
    CHECK (trackPlaybackVolume (t) == 25);

    // Exactly the one head-side transaction: a single undo empties the history.
    r.doc.getUndoManager().undo();
    CHECK (trackPlaybackVolume (t) == 100);
    CHECK (! r.doc.getUndoManager().canUndo());

    // ... and the undo itself refreshes the fader, again without echo.
    r.target.flushPendingForTesting();
    CHECK (juce::roundToInt (panel.trackStripForTesting (0)->faderForTesting().getValue()) == 100);
    CHECK (r.doc.getUndoManager().canRedo());
}

TEST_CASE ("MixerPanel: pan knob override and double-click clearing", "[mixer][panel][pan]")
{
    Rig r;
    auto t = addTrack (r.doc, "A");
    MixerPanelComponent panel (r.active);
    panel.setSize (400, 300);
    auto* strip = panel.trackStripForTesting (0);
    CHECK (strip->showsMidiPanMarkerForTesting());

    auto& knob = strip->panForTesting();
    knob.setValue (-60.0, juce::sendNotificationSync);
    CHECK (trackPlaybackPan (t) == std::optional<int> (-60));
    r.target.flushPendingForTesting();
    strip = panel.trackStripForTesting (0);
    CHECK (! strip->showsMidiPanMarkerForTesting());

    strip->panForTesting().mouseDoubleClick (doubleClickOn (strip->panForTesting()));
    CHECK (! t.hasProperty (SongIDs::playbackPan));
    r.target.flushPendingForTesting();
    strip = panel.trackStripForTesting (0);
    CHECK (strip->showsMidiPanMarkerForTesting());
    CHECK (juce::roundToInt (strip->panForTesting().getValue()) == 0);
}

TEST_CASE ("MixerPanel: a pan drag is one undo step", "[mixer][panel][pan]")
{
    Rig r;
    auto t = addTrack (r.doc, "A");
    MixerPanelComponent panel (r.active);
    r.doc.setProperty (t, SongIDs::name, "Renamed", true);
    drag (panel.trackStripForTesting (0)->panForTesting(), { -10.0, -20.0, -30.0 });
    CHECK (trackPlaybackPan (t) == std::optional<int> (-30));
    r.doc.getUndoManager().undo();
    CHECK (! t.hasProperty (SongIDs::playbackPan));
    CHECK (t.getProperty (SongIDs::name).toString() == "Renamed");
}

TEST_CASE ("MixerPanel: mute and solo stay in step with the controller", "[mixer][panel]")
{
    Rig r;
    auto a = addTrack (r.doc, "A");
    auto b = addTrack (r.doc, "B");
    MixerPanelComponent panel (r.active);
    panel.setSize (400, 300);

    auto* stripA = panel.trackStripForTesting (0);
    click (stripA->muteForTesting());
    CHECK (r.controller.isMuted (idOf (a)));
    r.target.flushPendingForTesting();
    stripA = panel.trackStripForTesting (0);
    CHECK (stripA->muteForTesting().getToggleState());
    CHECK (stripA->isDimmedForTesting());

    click (stripA->muteForTesting());
    CHECK (! r.controller.isMuted (idOf (a)));
    r.target.flushPendingForTesting();
    CHECK (! panel.trackStripForTesting (0)->isDimmedForTesting());

    r.controller.setSoloed (idOf (b), true);
    r.target.flushPendingForTesting();
    CHECK (panel.trackStripForTesting (0)->isDimmedForTesting());
    CHECK (! panel.trackStripForTesting (0)->muteForTesting().getToggleState());
    CHECK (panel.trackStripForTesting (1)->soloForTesting().getToggleState());
    CHECK (! panel.trackStripForTesting (1)->isDimmedForTesting());

    click (panel.trackStripForTesting (1)->soloForTesting());
    CHECK (! r.controller.isSoloed (idOf (b)));
}

TEST_CASE ("MixerPanel: strips follow track add, remove and a replaced document", "[mixer][panel]")
{
    Rig r;
    r.doc.getConductorTrack();
    addTrack (r.doc, "A");
    auto b = addTrack (r.doc, "B");
    addTrack (r.doc, "C");
    MixerPanelComponent panel (r.active);
    panel.setSize (400, 300);
    REQUIRE (panel.numTrackStripsForTesting() == 3);

    addTrack (r.doc, "D");
    r.target.flushPendingForTesting();
    CHECK (panel.numTrackStripsForTesting() == 4);

    r.doc.removeTrack (idOf (b));
    r.target.flushPendingForTesting();
    REQUIRE (panel.numTrackStripsForTesting() == 3);
    for (int i = 0; i < 3; ++i)
    {
        auto* s = panel.trackStripForTesting (i);
        REQUIRE (s != nullptr);
        CHECK (s->getChannelId() != idOf (b));
        CHECK (juce::roundToInt (s->faderForTesting().getValue()) == 100);
    }

    SongDocument other;
    for (const char* n : { "X", "Y" })
        other.addTrack (n, static_cast<int> (0xff336699u), 1, other.mintImportBatch());
    r.doc.replaceContents (other.getTree());
    r.target.flushPendingForTesting();
    const auto ch = r.target.channels();
    REQUIRE (panel.numTrackStripsForTesting() == 2);
    CHECK (panel.trackStripForTesting (0)->getChannelId() == ch[0].id);
    CHECK (panel.trackStripForTesting (1)->getChannelId() == ch[1].id);

    SongDocument conductorOnly;
    conductorOnly.getConductorTrack();
    r.doc.replaceContents (conductorOnly.getTree());
    r.target.flushPendingForTesting();
    CHECK (panel.numTrackStripsForTesting() == 0);
    CHECK (panel.trackStripForTesting (0) == nullptr);
    CHECK (panel.masterFaderForTesting().isVisible());
}

TEST_CASE ("MixerPanel: an empty Song shows only the master strip", "[mixer][panel][master]")
{
    Rig r;
    MixerPanelComponent panel (r.active);
    panel.setSize (400, 300);
    CHECK (panel.numTrackStripsForTesting() == 0);
    CHECK (panel.masterFaderForTesting().isVisible());
    CHECK (juce::roundToInt (panel.masterFaderForTesting().getValue()) == r.controller.masterPercent());
}

TEST_CASE ("MixerPanel: the master fader drives the controller", "[mixer][panel][master]")
{
    Rig r;
    MixerPanelComponent panel (r.active);
    panel.masterFaderForTesting().setValue (60.0, juce::sendNotificationSync);
    CHECK (r.controller.masterPercent() == 60);

    r.controller.setMasterPercent (35);
    r.target.flushPendingForTesting();
    CHECK (juce::roundToInt (panel.masterFaderForTesting().getValue()) == 35);
}

TEST_CASE ("MixerPanel: a note edit only refreshes, keeping the same strips", "[mixer][panel]")
{
    Rig r;
    auto t = addTrack (r.doc, "A");
    MixerPanelComponent panel (r.active);
    auto* before = panel.trackStripForTesting (0);
    addNote (t, 60, 0, 480);
    t.setProperty (SongIDs::name, "Lead", nullptr);
    r.target.flushPendingForTesting();
    CHECK (panel.trackStripForTesting (0) == before);
    CHECK (before->getChannelName() == "Lead");
}
