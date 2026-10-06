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
