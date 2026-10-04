#include "PlaybackTestSupport.h"
#include "UI/Playback/PlaybackEngine.h"
#include "UI/Playback/PlaybackSnapshot.h"
#include "UI/Playback/Transport.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <juce_audio_basics/juce_audio_basics.h>

using namespace lotro;
using namespace lotro::playbacktest;

namespace
{
    constexpr double sr = 48000.0;

    struct Rig
    {
        Transport transport;
        RecordingSink sink;
        PlaybackEngine engine { transport, sink };
        juce::AudioBuffer<float> buffer { 2, 512 };
        std::shared_ptr<PlaybackSnapshot> snapshot;

        explicit Rig (SongDocument& doc)
        {
            engine.prepare (sr, 512);
            snapshot = buildSnapshot (doc);
            engine.publishSnapshot (snapshot);
        }

        void render (int blocks = 1) { for (int i = 0; i < blocks; ++i) engine.renderBlock (buffer, 0, 512); }
    };
}

TEST_CASE ("PlaybackEngine: not playing renders only (silence/tails) and fires no events", "[playback][engine]")
{
    SongDocument doc;
    addNote (addTrack (doc), 60, 0, 480);
    Rig rig (doc);
    rig.render (4);
    CHECK (rig.sink.records.empty());
    CHECK (rig.sink.framesRendered == 4 * 512);
}

TEST_CASE ("PlaybackEngine: a note at tick 480 (0.5 s at 120 BPM) fires at the predicted frame", "[playback][engine]")
{
    SongDocument doc;
    addNote (addTrack (doc), 60, 480, 240);
    Rig rig (doc);
    rig.transport.play (rig.snapshot->endSeconds());
    rig.render (60);   // 60 * 512 = 30720 frames > 24000

    const PlaybackEvent* on = nullptr;
    long onFrame = -1;
    for (const auto& r : rig.sink.records)
        if (r.event.kind == PlaybackEventKind::NoteOn) { on = &r.event; onFrame = r.frame; }
    REQUIRE (on != nullptr);
    CHECK (onFrame == 24000);   // 0.5 s * 48000
}

TEST_CASE ("PlaybackEngine: a mid-song tempo change moves later events", "[playback][engine]")
{
    SongDocument doc;
    addTempo (doc, 0, 120.0);
    addTempo (doc, 480, 60.0);
    addNote (addTrack (doc), 60, 960, 240);      // 1.5 s
    Rig rig (doc);
    rig.transport.play (rig.snapshot->endSeconds());
    rig.render (200);

    long onFrame = -1;
    for (const auto& r : rig.sink.records)
        if (r.event.kind == PlaybackEventKind::NoteOn) onFrame = r.frame;
    CHECK (onFrame == 72000);   // 1.5 s * 48000
}

TEST_CASE ("PlaybackEngine: playback auto-stops at the end of the song", "[playback][engine]")
{
    SongDocument doc;
    addNote (addTrack (doc), 60, 0, 480);        // ends at 0.5 s
    Rig rig (doc);
    rig.transport.play (rig.snapshot->endSeconds());
    rig.render (80);
    CHECK (! rig.transport.isPlaying());
    CHECK (rig.transport.getPositionSeconds() == Catch::Approx (rig.snapshot->endSeconds()));
    CHECK (rig.sink.count (PlaybackEventKind::NoteOff) == 1);
}

TEST_CASE ("PlaybackEngine: pausing releases voices and silences event delivery", "[playback][engine]")
{
    SongDocument doc;
    addNote (addTrack (doc), 60, 0, 4800);
    Rig rig (doc);
    rig.transport.play (rig.snapshot->endSeconds());
    rig.render (2);
    const int before = rig.sink.releaseAllCount;
    rig.transport.pause();
    rig.render (1);
    CHECK (rig.sink.releaseAllCount == before + 1);
    const auto n = rig.sink.records.size();
    rig.render (5);
    CHECK (rig.sink.records.size() == n);
}

TEST_CASE ("PlaybackEngine: Stop returns the playhead to the play-start and releases", "[playback][engine]")
{
    SongDocument doc;
    addNote (addTrack (doc), 60, 0, 4800);
    Rig rig (doc);
    rig.transport.play (rig.snapshot->endSeconds());
    rig.render (10);
    const int before = rig.sink.releaseAllCount;
    rig.transport.stop();
    rig.render (1);
    CHECK (rig.transport.getPositionSeconds() == Catch::Approx (0.0));
    CHECK (rig.sink.releaseAllCount == before + 1);
}

TEST_CASE ("PlaybackEngine: an empty song never plays", "[playback][engine]")
{
    SongDocument doc;
    Rig rig (doc);
    rig.transport.play (rig.snapshot->endSeconds());
    rig.render (4);
    CHECK (! rig.transport.isPlaying());
    CHECK (rig.sink.records.empty());
}

TEST_CASE ("PlaybackEngine: with no snapshot published it renders silence and does not crash", "[playback][engine]")
{
    Transport transport;
    RecordingSink sink;
    PlaybackEngine engine (transport, sink);
    juce::AudioBuffer<float> buffer (2, 256);
    engine.prepare (sr, 256);
    transport.play (10.0);
    engine.renderBlock (buffer, 0, 256);
    CHECK (buffer.getMagnitude (0, 256) == Catch::Approx (0.0f));
}

TEST_CASE ("PlaybackEngine: Pause then seek then Play re-chases program events and repositions", "[playback][engine]")
{
    SongDocument doc;
    auto track = addTrack (doc);
    addEvent (track, 0, { 0xC0, 7 });            // program change at 0 s
    addNote (track, 60, 0, 240);
    addNote (track, 62, 960, 240);               // 1.0 s
    addNote (track, 64, 1920, 240);              // 2.0 s
    Rig rig (doc);
    rig.transport.play (rig.snapshot->endSeconds());
    rig.render (2);
    const int programsBefore = rig.sink.count (PlaybackEventKind::Program);   // snapshot may add its own default program
    REQUIRE (programsBefore >= 1);

    rig.transport.pause();
    rig.render (1);
    rig.transport.seek (1.5);
    rig.transport.play (rig.snapshot->endSeconds());
    rig.render (1);
    CHECK (rig.sink.count (PlaybackEventKind::Program) == 2 * programsBefore);   // chased again

    const int notesBefore = rig.sink.count (PlaybackEventKind::NoteOn);
    rig.render (60);                                            // crosses 2.0 s
    CHECK (rig.sink.count (PlaybackEventKind::NoteOn) == notesBefore + 1);
    int skipped = 0;                                            // the 1.0 s note lies before the seek target
    for (const auto& r : rig.sink.records)
        if (r.event.kind == PlaybackEventKind::NoteOn && r.event.tick == 960)
            ++skipped;
    CHECK (skipped == 0);
}

TEST_CASE ("PlaybackEngine: a snapshot published while paused is picked up on the next Play without a seek", "[playback][engine]")
{
    SongDocument oldDoc;
    auto oldTrack = addTrack (oldDoc);
    addNote (oldTrack, 60, 0, 10);
    addNote (oldTrack, 62, 20, 10);
    addNote (oldTrack, 64, 40, 10);
    addNote (oldTrack, 65, 960, 10);
    Rig rig (oldDoc);
    rig.transport.play (rig.snapshot->endSeconds());
    rig.render (2);                              // old events consumed; nextEvent is well past 0
    rig.transport.pause();
    rig.render (1);

    SongDocument newDoc;
    addNote (addTrack (newDoc), 72, 480, 240);   // 0.5 s
    auto newSnapshot = buildSnapshot (newDoc);
    rig.engine.publishSnapshot (newSnapshot);
    rig.render (1);                              // paused: the swap is consumed here
    rig.transport.play (newSnapshot->endSeconds());
    const int onsBefore = rig.sink.count (PlaybackEventKind::NoteOn);
    rig.render (60);
    CHECK (rig.sink.count (PlaybackEventKind::NoteOn) == onsBefore + 1);
}

TEST_CASE ("PlaybackEngine: Play right after an auto-stop (no block in between) replays from the start", "[playback][engine]")
{
    SongDocument doc;
    addNote (addTrack (doc), 60, 0, 480);        // ends at 0.5 s
    Rig rig (doc);
    rig.transport.play (rig.snapshot->endSeconds());
    for (int i = 0; i < 200 && rig.transport.isPlaying(); ++i)
        rig.render (1);                          // stop exactly on the block that auto-stops
    REQUIRE (! rig.transport.isPlaying());
    REQUIRE (rig.sink.count (PlaybackEventKind::NoteOn) == 1);

    rig.transport.play (rig.snapshot->endSeconds());   // before the next audio block
    rig.render (60);
    CHECK (rig.sink.count (PlaybackEventKind::NoteOn) == 2);
}

TEST_CASE ("PlaybackEngine: a seek whose block-end advance is dropped still delivers the notes at the target", "[playback][engine]")
{
    SongDocument doc;
    auto track = addTrack (doc);
    addNote (track, 60, 0, 240);
    addNote (track, 62, 1920, 240);              // 2.0 s, a bar line
    Rig rig (doc);
    rig.transport.play (rig.snapshot->endSeconds());
    rig.render (1);
    const double oldPosition = rig.transport.getPositionSeconds();

    rig.transport.seek (2.0);                    // lands "during" the block above
    CHECK (! rig.transport.advance (oldPosition, 512.0 / sr, rig.snapshot->endSeconds()));   // the late CAS is dropped
    CHECK (rig.transport.getPositionSeconds() == Catch::Approx (2.0));

    const int onsBefore = rig.sink.count (PlaybackEventKind::NoteOn);
    rig.render (1);
    REQUIRE (rig.sink.count (PlaybackEventKind::NoteOn) == onsBefore + 1);
    CHECK (rig.sink.records.back().event.tick == 1920);
}

TEST_CASE ("PlaybackEngine: a playhead move without a generation bump is detected and re-chased", "[playback][engine]")
{
    SongDocument doc;
    auto track = addTrack (doc);
    addNote (track, 60, 0, 240);
    addNote (track, 62, 1920, 240);
    Rig rig (doc);
    rig.transport.play (rig.snapshot->endSeconds());
    rig.render (1);
    const int onsBefore = rig.sink.count (PlaybackEventKind::NoteOn);
    rig.transport.pause();
    rig.transport.seek (2.0);
    rig.transport.play (rig.snapshot->endSeconds());   // no block ran while paused
    rig.render (1);
    CHECK (rig.sink.count (PlaybackEventKind::NoteOn) == onsBefore + 1);
}
