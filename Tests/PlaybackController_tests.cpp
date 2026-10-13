#include "PlaybackTestSupport.h"
#include "UI/Playback/PlaybackController.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_events/juce_events.h>

using namespace lotro;
using namespace lotro::playbacktest;
using Catch::Approx;

namespace
{
    struct Rig
    {
        juce::ScopedJuceInitialiser_GUI juceInit;
        SongDocument doc;
        RecordingSink sink;
        PlaybackController controller { doc, sink };
        juce::AudioBuffer<float> buffer { 2, 512 };

        Rig() { controller.engine().prepare (48000.0, 512); }
        void render (int blocks = 1) { for (int i = 0; i < blocks; ++i) controller.engine().renderBlock (buffer, 0, 512); }
    };
}

TEST_CASE ("PlaybackController: play on an empty song is a no-op", "[playback][controller]")
{
    Rig r;
    r.controller.flushRebuild();
    r.controller.play();
    CHECK (! r.controller.isPlaying());
}

TEST_CASE ("PlaybackController: notes added to the document are heard after a rebuild", "[playback][controller]")
{
    Rig r;
    addNote (addTrack (r.doc), 60, 0, 480);
    r.controller.flushRebuild();
    r.controller.play();
    r.render();
    CHECK (r.sink.count (PlaybackEventKind::NoteOn) == 1);
}

TEST_CASE ("PlaybackController: many tree changes coalesce into one rebuild", "[playback][controller]")
{
    Rig r;
    auto t = addTrack (r.doc);
    r.controller.flushRebuild();
    const auto before = r.controller.currentSnapshot();
    for (int i = 0; i < 100; ++i)
        addNote (t, 60 + (i % 12), i * 10, 5);
    CHECK (r.controller.currentSnapshot() == before);   // nothing rebuilt synchronously
    r.controller.flushRebuild();
    CHECK (r.controller.currentSnapshot() != before);
}

TEST_CASE ("PlaybackController: renaming or recolouring a track does not rebuild (held notes keep ringing)", "[playback][controller]")
{
    Rig r;
    auto t = addTrack (r.doc);
    addNote (t, 60, 0, 480);
    r.controller.flushRebuild();
    const auto before = r.controller.currentSnapshot();
    t.setProperty (SongIDs::name, "Renamed", nullptr);
    t.setProperty (SongIDs::colorArgb, static_cast<juce::int64> (0xff000000u), nullptr);
    r.controller.flushRebuild();
    CHECK (r.controller.currentSnapshot() == before);
}

TEST_CASE ("PlaybackController: changes outside the source MIDI (parts/assignments) do not rebuild", "[playback][controller]")
{
    Rig r;
    auto t = addTrack (r.doc);
    addNote (t, 60, 0, 480);
    r.controller.flushRebuild();
    const auto before = r.controller.currentSnapshot();
    r.doc.addPart ("Lute", "Part 1");
    r.controller.flushRebuild();
    CHECK (r.controller.currentSnapshot() == before);
}

TEST_CASE ("PlaybackController: a mid-play edit swaps the snapshot and keeps playing from the same place", "[playback][controller]")
{
    Rig r;
    auto t = addTrack (r.doc);
    addNote (t, 60, 0, 9600);
    r.controller.flushRebuild();
    r.controller.play();
    r.render (20);
    const double before = r.controller.getPositionSeconds();
    addNote (t, 64, 9600, 480);
    r.controller.flushRebuild();
    r.render (1);
    CHECK (r.controller.isPlaying());
    CHECK (r.controller.getPositionSeconds() > before);
}

TEST_CASE ("PlaybackController: mute and solo govern what is audible and survive a rebuild", "[playback][controller]")
{
    Rig r;
    auto a = addTrack (r.doc, "A", 1);
    auto b = addTrack (r.doc, "B", 2);
    const auto idA = (juce::int64) a.getProperty (SongIDs::trackId);
    const auto idB = (juce::int64) b.getProperty (SongIDs::trackId);
    addNote (a, 60, 0, 480, 100, 1);
    addNote (b, 64, 0, 480, 100, 2);
    r.controller.setMuted (idA, true);
    addNote (b, 65, 480, 480, 100, 2);   // forces a rebuild
    r.controller.flushRebuild();
    r.controller.play();
    r.render (60);

    int onFromA = 0, onFromB = 0;
    const auto snap = r.controller.currentSnapshot();
    for (const auto& rec : r.sink.records)
        if (rec.event.kind == PlaybackEventKind::NoteOn)
            (snap->trackIds()[(size_t) rec.event.trackIndex] == idA ? onFromA : onFromB)++;
    CHECK (onFromA == 0);
    CHECK (onFromB == 2);
    CHECK (r.controller.isSilencedBySolo (idB) == false);
}

TEST_CASE ("PlaybackController: mute state is kept for a track that is removed and restored by undo", "[playback][controller]")
{
    Rig r;
    auto t = addTrack (r.doc);
    const auto id = (juce::int64) t.getProperty (SongIDs::trackId);
    r.controller.setMuted (id, true);
    r.doc.removeTrack (id);
    r.controller.flushRebuild();
    r.doc.undo();
    r.controller.flushRebuild();
    CHECK (r.controller.isMuted (id));
}

TEST_CASE ("PlaybackController: documentReplaced stops playback, zeroes the playhead and clears mute/solo", "[playback][controller]")
{
    Rig r;
    auto t = addTrack (r.doc);
    addNote (t, 60, 0, 9600);
    r.controller.flushRebuild();
    r.controller.setSoloed ((juce::int64) t.getProperty (SongIDs::trackId), true);
    r.controller.play();
    r.render (10);

    r.doc.resetToEmpty();
    r.controller.documentReplaced();
    CHECK (! r.controller.isPlaying());
    CHECK (r.controller.getPositionSeconds() == Approx (0.0));
    CHECK (! r.controller.isSoloed ((juce::int64) t.getProperty (SongIDs::trackId)));
    r.render (2);
    CHECK (r.sink.releaseAllCount >= 1);
}

TEST_CASE ("PlaybackController: Stop in a newly opened song does not jump to the previous song's play-start", "[playback][controller]")
{
    Rig r;
    addNote (addTrack (r.doc), 60, 0, 9600);
    r.controller.flushRebuild();
    r.controller.seekToTick (960.0);      // 1 s at the default tempo
    r.controller.play();                  // the play-start is now 1 s
    r.render (2);
    REQUIRE (r.controller.getPositionSeconds() > 1.0);

    r.doc.resetToEmpty();
    r.controller.documentReplaced();
    r.controller.stop();                  // before any Play in the new song
    CHECK (r.controller.getPositionSeconds() == Approx (0.0));
}

TEST_CASE ("PlaybackController: seekToTick converts through the tempo map", "[playback][controller]")
{
    Rig r;
    addTempo (r.doc, 0, 60.0);            // 1 s per quarter note
    addNote (addTrack (r.doc), 60, 0, 4800);
    r.controller.flushRebuild();
    r.controller.seekToTick (960.0);      // two quarter notes at 480 PPQ
    CHECK (r.controller.getPositionSeconds() == Approx (2.0));
    CHECK (r.controller.getPositionTicks() == Approx (960.0));
}

TEST_CASE ("PlaybackController: rewindOneBar steps back to the previous bar line", "[playback][controller]")
{
    Rig r;
    addNote (addTrack (r.doc), 60, 0, 19200);
    r.controller.flushRebuild();
    r.controller.seekToTick (2500.0);     // 4/4 at 480 PPQ = 1920 ticks per bar
    r.controller.rewindOneBar();
    CHECK (r.controller.getPositionTicks() == Approx (1920.0));
}

TEST_CASE ("rewindOneBar steps back across a meter change", "[playback][tempo-sync]")
{
    Rig r;
    addNote (addTrack (r.doc), 60, 0, 19200);
    r.doc.setTimeBase (true);
    auto events = SongDocument::getEventsNode (r.doc.getConductorTrack());
    const auto addMeter = [&events] (int tick, std::uint8_t numerator)
    {
        juce::ValueTree event (SongIDs::EVENT);
        event.setProperty (SongIDs::tick, tick, nullptr);
        event.setProperty (SongIDs::order, events.getNumChildren(), nullptr);
        const std::uint8_t bytes[] = { 0xFF, 0x58, numerator, 2, 24, 8 };   // numerator/4
        event.setProperty (SongIDs::data, juce::var (juce::MemoryBlock (bytes, sizeof bytes)), nullptr);
        SongDocument::appendChildBulk (events, event);
    };
    addMeter (0, 4);
    addMeter (3840, 3);
    r.controller.flushRebuild();
    r.controller.seekToTick (5380.0);     // 100 ticks into the second 3/4 bar (1440 ticks each)
    r.controller.rewindOneBar();
    CHECK (r.controller.getPositionTicks() == Approx (5280.0));
    r.controller.rewindOneBar();
    CHECK (r.controller.getPositionTicks() == Approx (3840.0));
    r.controller.rewindOneBar();
    CHECK (r.controller.getPositionTicks() == Approx (1920.0));   // the last 4/4 bar
}

TEST_CASE ("PlaybackController: onBeforePlay can veto Play", "[playback][controller]")
{
    Rig r;
    addNote (addTrack (r.doc), 60, 0, 480);
    r.controller.flushRebuild();
    r.controller.onBeforePlay = [] { return false; };
    r.controller.play();
    CHECK (! r.controller.isPlaying());
}

TEST_CASE ("PlaybackController: listeners hear mute/solo changes and state changes", "[playback][controller]")
{
    Rig r;
    struct L : PlaybackController::Listener
    {
        int mute = 0, state = 0;
        void muteSoloChanged() override { ++mute; }
        void playbackStateChanged() override { ++state; }
    } listener;
    r.controller.addListener (&listener);
    addNote (addTrack (r.doc), 60, 0, 480);
    r.controller.flushRebuild();
    r.controller.setMuted (1, true);
    r.controller.play();
    CHECK (listener.mute == 1);
    CHECK (listener.state >= 1);
    r.controller.removeListener (&listener);
}

namespace
{
    void pumpMessages() { juce::MessageManager::getInstance()->runDispatchLoopUntil (100); }
}

TEST_CASE ("PlaybackController: a pitch edit deep inside a note rebuilds asynchronously", "[playback][controller]")
{
    Rig r;
    auto t = addTrack (r.doc);
    addNote (t, 60, 0, 480);
    r.controller.flushRebuild();
    const auto before = r.controller.currentSnapshot();
    auto note = SongDocument::getNotesNode (t).getChild (0);
    r.doc.setProperty (note, SongIDs::pitch, 72);
    CHECK (r.controller.currentSnapshot() == before);
    pumpMessages();
    CHECK (r.controller.currentSnapshot() != before);
}

TEST_CASE ("PlaybackController: undo of a note edit rebuilds asynchronously", "[playback][controller]")
{
    Rig r;
    auto t = addTrack (r.doc);
    addNote (t, 60, 0, 480);
    r.controller.flushRebuild();
    r.doc.setProperty (SongDocument::getNotesNode (t).getChild (0), SongIDs::pitch, 72);
    r.controller.flushRebuild();
    const auto before = r.controller.currentSnapshot();
    r.doc.undo();
    pumpMessages();
    CHECK (r.controller.currentSnapshot() != before);
}

TEST_CASE ("PlaybackController: play right after an edit uses the fresh snapshot without an explicit flush", "[playback][controller]")
{
    Rig r;
    r.controller.flushRebuild();                       // empty song snapshot
    addNote (addTrack (r.doc), 60, 0, 480);            // rebuild still pending
    r.controller.play();
    CHECK (r.controller.isPlaying());
}

TEST_CASE ("PlaybackController: destroying the document-side controller first is safe and stops listening", "[playback][controller]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SongDocument doc;
    RecordingSink sink;
    {
        PlaybackController controller (doc, sink);
        addNote (addTrack (doc), 60, 0, 480);          // leaves an async update pending
    }
    addNote (addTrack (doc), 62, 0, 480);              // no listener left
    pumpMessages();
    SUCCEED();
}

TEST_CASE ("PlaybackController: with no marker, Play starts where the playhead is", "[playback][controller][marker]")
{
    Rig r;
    addNote (addTrack (r.doc), 60, 0, 9600);
    r.controller.flushRebuild();
    CHECK (! r.controller.getMarkerTick().has_value());
    r.controller.seekToTick (960.0);      // 1 s at the default tempo
    r.controller.play();
    CHECK (r.controller.getPositionSeconds() == Approx (1.0));
}

TEST_CASE ("PlaybackController: Play starts from the marker, wherever the playhead is, even after Pause or Stop", "[playback][controller][marker]")
{
    Rig r;
    addNote (addTrack (r.doc), 60, 0, 9600);
    r.controller.flushRebuild();
    r.controller.setMarkerTick (960.0);   // 1 s
    CHECK (r.controller.getMarkerTick().value() == Approx (960.0));
    // Setting the marker does not move the playhead.
    CHECK (r.controller.getPositionSeconds() == Approx (0.0));

    r.controller.play();
    CHECK (r.controller.getPositionSeconds() == Approx (1.0));
    r.render (20);
    REQUIRE (r.controller.getPositionSeconds() > 1.0);

    r.controller.pause();
    r.controller.play();                  // not a resume: back to the marker
    CHECK (r.controller.getPositionSeconds() == Approx (1.0));

    r.render (20);
    r.controller.stop();
    CHECK (r.controller.getPositionSeconds() == Approx (1.0));

    r.controller.seekToTick (1920.0);     // a ruler seek does not override the marker either
    r.controller.play();
    CHECK (r.controller.getPositionSeconds() == Approx (1.0));
}

TEST_CASE ("PlaybackController: the marker is announced to listeners and survives edits but not a new document", "[playback][controller][marker]")
{
    struct Counter : PlaybackController::Listener
    {
        int markerChanges = 0;
        void playbackMarkerChanged() override { ++markerChanges; }
    };

    Rig r;
    auto t = addTrack (r.doc);
    addNote (t, 60, 0, 9600);
    r.controller.flushRebuild();
    Counter counter;
    r.controller.addListener (&counter);

    r.controller.setMarkerTick (480.0);
    CHECK (counter.markerChanges == 1);

    addNote (t, 62, 960, 480);            // an edit rebuilds the snapshot; the marker stays
    r.controller.flushRebuild();
    CHECK (r.controller.getMarkerTick().value() == Approx (480.0));

    r.doc.resetToEmpty();
    r.controller.documentReplaced();
    CHECK (! r.controller.getMarkerTick().has_value());
    CHECK (counter.markerChanges == 2);

    r.controller.clearMarker();           // already clear: no spurious notification
    CHECK (counter.markerChanges == 2);
    r.controller.removeListener (&counter);
}

TEST_CASE ("PlaybackController: with no MIDI open (nothing to play) a marker cannot be set", "[playback][controller][marker]")
{
    struct Counter : PlaybackController::Listener
    {
        int markerChanges = 0;
        void playbackMarkerChanged() override { ++markerChanges; }
    };

    Rig r;                                   // a new, empty Song
    Counter counter;
    r.controller.addListener (&counter);

    r.controller.setMarkerTick (480.0);
    CHECK (! r.controller.getMarkerTick().has_value());
    CHECK (counter.markerChanges == 0);

    // Once a MIDI is open (here: a track with a note), the same call works --
    // without waiting for the debounced snapshot rebuild.
    addNote (addTrack (r.doc), 60, 0, 9600);
    r.controller.setMarkerTick (480.0);
    REQUIRE (r.controller.getMarkerTick().has_value());
    CHECK (*r.controller.getMarkerTick() == Approx (480.0));
    CHECK (counter.markerChanges == 1);
}

TEST_CASE ("PlaybackController: changing playbackVolume updates the live snapshot without rebuilding", "[playback][controller][gain]")
{
    Rig r;
    auto t = addTrack (r.doc);
    addNote (t, 60, 0, 480);
    r.controller.flushRebuild();
    const auto before = r.controller.currentSnapshot();
    const int index = before->trackIndexForId ((juce::int64) t.getProperty (SongIDs::trackId));
    REQUIRE (index >= 0);
    REQUIRE (before->gainPercent (index) == 100);
    t.setProperty (SongIDs::playbackVolume, 25, nullptr);
    r.controller.flushRebuild();
    CHECK (r.controller.currentSnapshot() == before);
    CHECK (before->gainPercent (index) == 25);
}

TEST_CASE ("PlaybackController: changing playbackPan updates the live snapshot without rebuilding", "[playback][controller][pan]")
{
    Rig r;
    auto t = addTrack (r.doc);
    addNote (t, 60, 0, 480);
    r.controller.flushRebuild();
    const auto before = r.controller.currentSnapshot();
    const int index = before->trackIndexForId ((juce::int64) t.getProperty (SongIDs::trackId));
    REQUIRE (index >= 0);
    REQUIRE (! before->panOverride (index).has_value());
    t.setProperty (SongIDs::playbackPan, -30, nullptr);
    r.controller.flushRebuild();
    CHECK (r.controller.currentSnapshot() == before);
    CHECK (before->panOverride (index) == std::optional<int> (-30));
    t.removeProperty (SongIDs::playbackPan, nullptr);
    r.controller.flushRebuild();
    CHECK (r.controller.currentSnapshot() == before);
    CHECK (! before->panOverride (index).has_value());
}

TEST_CASE ("PlaybackController: master percent is clamped, reaches the engine and notifies", "[playback][controller][master]")
{
    struct Counter : PlaybackController::Listener { int changes = 0; void masterChanged() override { ++changes; } };
    Rig r;
    Counter counter;
    r.controller.addListener (&counter);
    CHECK (r.controller.masterPercent() == 100);
    r.controller.setMasterPercent (40);
    CHECK (r.controller.masterPercent() == 40);
    CHECK (r.controller.engine().masterPercent() == 40);
    r.controller.setMasterPercent (150);
    CHECK (r.controller.masterPercent() == 100);
    r.controller.setMasterPercent (-5);
    CHECK (r.controller.masterPercent() == 0);
    CHECK (r.controller.engine().masterPercent() == 0);
    CHECK (counter.changes == 3);
    r.controller.removeListener (&counter);
}

// Loop range. The Rig's Song runs at the default 120 BPM, 480 PPQ: 960 ticks = 1 s,
// so kMinLoopSeconds (0.05 s) is 48 ticks.
namespace
{
    struct LoopCounter : PlaybackController::Listener
    {
        int loopChanges = 0;
        void playbackLoopChanged() override { ++loopChanges; }
    };
}

TEST_CASE ("PlaybackController: setLoopRange stores an ordered range and notifies once", "[playback][controller][loop]")
{
    Rig r;
    addNote (addTrack (r.doc), 60, 0, 9600);   // a 10 s Song
    r.controller.flushRebuild();
    LoopCounter counter;
    r.controller.addListener (&counter);

    r.controller.setLoopRange (1920.0, 480.0);   // reversed edges
    const auto range = r.controller.getLoopRange();
    REQUIRE (range.has_value());
    CHECK (range->startTick == Approx (480.0));
    CHECK (range->endTick == Approx (1920.0));
    CHECK (counter.loopChanges == 1);

    r.controller.setLoopRange (480.0, 1920.0);   // the same range again: no change, no notice
    CHECK (counter.loopChanges == 1);

    const auto span = r.controller.engine().loopSpanForTesting();
    REQUIRE (span.has_value());
    CHECK (span->startSeconds == Approx (0.5));
    CHECK (span->endSeconds == Approx (2.0));

    r.controller.clearLoopRange();
    CHECK (! r.controller.getLoopRange().has_value());
    CHECK (! r.controller.engine().loopSpanForTesting().has_value());
    CHECK (counter.loopChanges == 2);
    r.controller.clearLoopRange();               // already clear: no spurious notice
    CHECK (counter.loopChanges == 2);
    r.controller.removeListener (&counter);
}

TEST_CASE ("PlaybackController: setLoopRange is ignored with nothing to play and when too short", "[playback][controller][loop]")
{
    Rig r;                                       // a new, empty Song
    LoopCounter counter;
    r.controller.addListener (&counter);
    r.controller.setLoopRange (480.0, 1920.0);
    CHECK (! r.controller.getLoopRange().has_value());
    CHECK (counter.loopChanges == 0);

    addNote (addTrack (r.doc), 60, 0, 9600);     // no explicit flush: setLoopRange flushes
    r.controller.setLoopRange (480.0, 485.0);    // 5 ticks ~ 0.005 s < kMinLoopSeconds
    CHECK (! r.controller.getLoopRange().has_value());
    CHECK (counter.loopChanges == 0);

    r.controller.setLoopRange (960.0, 1920.0);
    REQUIRE (r.controller.getLoopRange().has_value());
    CHECK (counter.loopChanges == 1);

    r.controller.setLoopRange (2880.0, 2885.0);  // too short: the existing range is kept
    REQUIRE (r.controller.getLoopRange().has_value());
    CHECK (r.controller.getLoopRange()->startTick == Approx (960.0));
    CHECK (r.controller.getLoopRange()->endTick == Approx (1920.0));

    r.controller.setLoopRange (12000.0, 14000.0);   // wholly past the 9600-tick Song end: kept
    REQUIRE (r.controller.getLoopRange().has_value());
    CHECK (r.controller.getLoopRange()->startTick == Approx (960.0));
    CHECK (counter.loopChanges == 1);
    r.controller.removeListener (&counter);
}

TEST_CASE ("PlaybackController: the range is clamped to the end of the Song", "[playback][controller][loop]")
{
    Rig r;
    addNote (addTrack (r.doc), 60, 0, 9600);     // ends at tick 9600 = 10 s
    r.controller.flushRebuild();
    r.controller.setLoopRange (4800.0, 20000.0);
    const auto range = r.controller.getLoopRange();
    REQUIRE (range.has_value());
    CHECK (range->startTick == Approx (4800.0));
    CHECK (range->endTick == Approx (9600.0));
    const auto span = r.controller.engine().loopSpanForTesting();
    REQUIRE (span.has_value());
    CHECK (span->endSeconds == Approx (10.0));
}

TEST_CASE ("PlaybackController: play() starts at the range start unless the playhead is inside it", "[playback][controller][loop]")
{
    Rig r;
    addNote (addTrack (r.doc), 60, 0, 9600);
    r.controller.flushRebuild();
    r.controller.setLoopRange (480.0, 960.0);    // 0.5 .. 1.0 s

    r.controller.play();                         // playhead at 0: outside, so to the start
    CHECK (r.controller.getPositionSeconds() == Approx (0.5));
    r.controller.pause();

    r.controller.seekToTick (720.0);             // 0.75 s, inside: resume in place
    r.controller.play();
    CHECK (r.controller.getPositionSeconds() == Approx (0.75));
    r.controller.pause();

    r.controller.seekToTick (960.0);             // exactly the end is outside [start, end)
    r.controller.play();
    CHECK (r.controller.getPositionSeconds() == Approx (0.5));
    r.controller.pause();

    r.controller.setMarkerTick (1920.0);         // 2 s: ignored while the range exists
    r.controller.seekToTick (720.0);
    r.controller.play();
    CHECK (r.controller.getPositionSeconds() == Approx (0.75));
    r.controller.pause();
    r.controller.seekToTick (0.0);
    r.controller.play();
    CHECK (r.controller.getPositionSeconds() == Approx (0.5));
    r.controller.pause();
    REQUIRE (r.controller.getMarkerTick().has_value());
    CHECK (*r.controller.getMarkerTick() == Approx (1920.0));   // untouched

    r.controller.clearLoopRange();               // the marker applies again
    r.controller.seekToTick (720.0);
    r.controller.play();
    CHECK (r.controller.getPositionSeconds() == Approx (2.0));
}

TEST_CASE ("PlaybackController: a tempo edit moves the loop edges with the music", "[playback][controller][loop]")
{
    Rig r;
    addNote (addTrack (r.doc), 60, 0, 9600);
    r.controller.flushRebuild();
    r.controller.setLoopRange (480.0, 960.0);
    auto span = r.controller.engine().loopSpanForTesting();
    REQUIRE (span.has_value());
    CHECK (span->startSeconds == Approx (0.5));
    CHECK (span->endSeconds == Approx (1.0));

    addTempo (r.doc, 0, 60.0);                   // halve the tempo: 480 ticks = 1 s
    r.controller.flushRebuild();
    REQUIRE (r.controller.getLoopRange().has_value());
    CHECK (r.controller.getLoopRange()->startTick == Approx (480.0));
    CHECK (r.controller.getLoopRange()->endTick == Approx (960.0));
    span = r.controller.engine().loopSpanForTesting();
    REQUIRE (span.has_value());
    CHECK (span->startSeconds == Approx (1.0));
    CHECK (span->endSeconds == Approx (2.0));
}

TEST_CASE ("PlaybackController: an edit that shortens the Song clamps the published loop span", "[playback][controller][loop]")
{
    Rig r;
    auto t = addTrack (r.doc);
    addNote (t, 60, 0, 9600);                    // 10 s
    r.controller.flushRebuild();
    r.controller.setLoopRange (4800.0, 9600.0);  // 5 .. 10 s
    auto note = SongDocument::getNotesNode (t).getChild (0);

    r.doc.setProperty (note, SongIDs::durationTicks, 7200);   // the Song now ends at 7.5 s
    r.controller.flushRebuild();
    auto span = r.controller.engine().loopSpanForTesting();
    REQUIRE (span.has_value());
    CHECK (span->startSeconds == Approx (5.0));
    CHECK (span->endSeconds == Approx (7.5));
    REQUIRE (r.controller.getLoopRange().has_value());
    CHECK (r.controller.getLoopRange()->endTick == Approx (9600.0));   // ticks kept

    r.doc.setProperty (note, SongIDs::durationTicks, 4810);   // ends ~5.01 s: span < kMinLoopSeconds
    r.controller.flushRebuild();
    CHECK (! r.controller.engine().loopSpanForTesting().has_value());
    REQUIRE (r.controller.getLoopRange().has_value());
    CHECK (r.controller.getLoopRange()->startTick == Approx (4800.0));
    CHECK (r.controller.getLoopRange()->endTick == Approx (9600.0));

    r.controller.seekToTick (960.0);             // with the span suppressed, Play is the plain path
    r.controller.play();
    CHECK (r.controller.getPositionSeconds() == Approx (1.0));
    r.controller.pause();

    r.doc.setProperty (note, SongIDs::durationTicks, 9600);   // grows back: the loop returns
    r.controller.flushRebuild();
    span = r.controller.engine().loopSpanForTesting();
    REQUIRE (span.has_value());
    CHECK (span->startSeconds == Approx (5.0));
    CHECK (span->endSeconds == Approx (10.0));
}

TEST_CASE ("PlaybackController: documentReplaced clears the range", "[playback][controller][loop]")
{
    Rig r;
    addNote (addTrack (r.doc), 60, 0, 9600);
    r.controller.flushRebuild();
    LoopCounter counter;
    r.controller.addListener (&counter);
    r.controller.setLoopRange (480.0, 960.0);
    REQUIRE (counter.loopChanges == 1);

    r.doc.resetToEmpty();
    r.controller.documentReplaced();
    CHECK (! r.controller.getLoopRange().has_value());
    CHECK (! r.controller.engine().loopSpanForTesting().has_value());
    CHECK (counter.loopChanges == 2);
    r.controller.removeListener (&counter);
}

TEST_CASE ("PlaybackController: setLoopRange reports whether the request was accepted", "[playback][controller][loop]")
{
    Rig r;
    CHECK_FALSE (r.controller.setLoopRange (480.0, 1920.0));   // nothing to play
    addNote (addTrack (r.doc), 60, 0, 9600);
    CHECK_FALSE (r.controller.setLoopRange (480.0, 485.0));    // too short
    CHECK (r.controller.setLoopRange (960.0, 1920.0));
    CHECK (r.controller.setLoopRange (960.0, 1920.0));         // the same range again: still accepted
    CHECK (r.controller.setLoopRange (4800.0, 20000.0));       // clamped to the Song end: accepted
    CHECK (r.controller.setLoopRange (4800.0, 21000.0));       // clamps to the same range: accepted
    CHECK_FALSE (r.controller.setLoopRange (2880.0, 2885.0));  // too short: rejected, range kept
    REQUIRE (r.controller.getLoopRange().has_value());
    CHECK (r.controller.getLoopRange()->startTick == Approx (4800.0));
    CHECK_FALSE (r.controller.setLoopRange (12000.0, 14000.0));   // wholly past the Song end
}

// The engine remembers that the playhead was inside the loop: an edit that leaves it
// at or past the loop end (a narrower range, a faster tempo) wraps it, where a seek
// past the end would play on.
TEST_CASE ("PlaybackController: narrowing the range past a looping playhead wraps it back into the loop", "[playback][controller][loop]")
{
    Rig r;
    addNote (addTrack (r.doc), 60, 0, 9600);
    r.controller.flushRebuild();
    r.controller.setLoopRange (960.0, 3840.0);   // 1 s .. 4 s
    r.controller.play();
    r.render (200);                              // ~2.13 s in
    REQUIRE (r.controller.getPositionSeconds() > 2.0);

    r.controller.setLoopRange (960.0, 1920.0);   // 1 s .. 2 s: the playhead is past the new end
    r.render (600);
    CHECK (r.controller.isPlaying());
    CHECK (r.controller.getPositionSeconds() >= 1.0);
    CHECK (r.controller.getPositionSeconds() < 2.0);
}

TEST_CASE ("PlaybackController: a tempo speed-up while looping keeps the playhead in the loop", "[playback][controller][loop]")
{
    Rig r;
    addNote (addTrack (r.doc), 60, 0, 9600);
    r.controller.flushRebuild();
    r.controller.setLoopRange (960.0, 1920.0);   // 1 s .. 2 s
    r.controller.play();
    r.render (150);                              // 1.6 s
    addTempo (r.doc, 0, 240.0);                  // double the tempo: the loop is now 0.5 s .. 1 s
    r.controller.flushRebuild();
    r.render (200);
    CHECK (r.controller.getPositionSeconds() >= 0.5);
    CHECK (r.controller.getPositionSeconds() < 1.0);
}

TEST_CASE ("PlaybackController: a block much longer than the loop does not carry the playhead out of it", "[playback][controller][loop]")
{
    Rig r;
    addNote (addTrack (r.doc), 60, 0, 9600);
    r.controller.flushRebuild();
    r.controller.setLoopRange (960.0, 1008.0);   // 1 s .. 1.05 s: 2400 frames
    r.controller.play();
    juce::AudioBuffer<float> big (2, 13000);     // over five passes: the wrap cap binds
    r.controller.engine().renderBlock (big, 0, 13000);
    CHECK (r.controller.getPositionSeconds() >= 1.0);
    CHECK (r.controller.getPositionSeconds() < 1.05);
    r.render (100);
    CHECK (r.controller.getPositionSeconds() >= 1.0);
    CHECK (r.controller.getPositionSeconds() < 1.05);
}
