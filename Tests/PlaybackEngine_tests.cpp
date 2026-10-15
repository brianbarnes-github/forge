#include "PlaybackTestSupport.h"
#include "UI/Playback/LoopRange.h"
#include "UI/Playback/PlaybackEngine.h"
#include "UI/Playback/PlaybackSnapshot.h"
#include "UI/Playback/Transport.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <juce_audio_basics/juce_audio_basics.h>

#include <algorithm>
#include <optional>
#include <vector>

using namespace lotro;
using namespace lotro::playbacktest;

namespace
{
    constexpr double sr = 48000.0;

    // A fresh SongDocument already holds a track, so the added track is not index 0.
    int trackIndexOf (const PlaybackSnapshot& s, const juce::ValueTree& track)
    {
        return s.trackIndexForId ((juce::int64) track.getProperty (SongIDs::trackId));
    }

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

TEST_CASE ("PlaybackEngine: pause, seek, Play with no block in between still delivers the notes at the target", "[playback][engine]")
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

TEST_CASE ("PlaybackEngine: a position move that does not bump the seek generation is detected, re-chased and fires no stale burst", "[playback][engine][chase]")
{
    SongDocument doc;
    auto track = addTrack (doc);
    addEvent (track, 0, { 0xC0, 9 });
    addNote (track, 60, 0, 240);
    addNote (track, 62, 960, 240);               // 1.0 s
    addNote (track, 64, 1440, 240);              // 1.5 s
    addNote (track, 65, 2400, 240);              // 2.5 s
    addNote (track, 67, 5760, 240);              // 6.0 s
    Rig rig (doc);
    rig.transport.play (rig.snapshot->endSeconds());
    rig.render (1);
    const unsigned generation = rig.transport.getSeekGeneration();
    const double position = rig.transport.getPositionSeconds();

    // Transport::advance stores the position without bumping the generation.
    REQUIRE (rig.transport.advance (position, 3.0, rig.snapshot->endSeconds()));
    REQUIRE (rig.transport.getSeekGeneration() == generation);

    const int releases = rig.sink.releaseAllCount;
    const int ons = rig.sink.count (PlaybackEventKind::NoteOn);
    rig.sink.records.clear();
    rig.render (1);

    CHECK (rig.sink.releaseAllCount == releases + 1);
    bool reappliedProgram = false;
    for (const auto& r : rig.sink.records)
        reappliedProgram |= r.event.kind == PlaybackEventKind::Program && r.event.data1 == 9;
    CHECK (reappliedProgram);
    CHECK (rig.sink.count (PlaybackEventKind::NoteOn) == 0);   // 1.0, 1.5 and 2.5 s are behind the playhead: no stale burst
    CHECK (ons == 1);

    rig.render (400);                                          // crosses 6.0 s
    CHECK (rig.sink.count (PlaybackEventKind::NoteOn) == 1);   // only the 6.0 s note
}

TEST_CASE ("PlaybackEngine: continuous playback does not releaseAll or re-chase every block", "[playback][engine][chase]")
{
    SongDocument doc;
    auto track = addTrack (doc);
    addEvent (track, 0, { 0xC0, 9 });
    addNote (track, 60, 0, 9600);                // 0..10 s
    Rig rig (doc);
    rig.transport.play (rig.snapshot->endSeconds());
    rig.render (1);
    const int releases = rig.sink.releaseAllCount;
    const int programs = rig.sink.count (PlaybackEventKind::Program);
    REQUIRE (programs >= 1);
    rig.render (20);
    REQUIRE (rig.transport.isPlaying());
    CHECK (rig.sink.releaseAllCount == releases);
    CHECK (rig.sink.count (PlaybackEventKind::Program) == programs);
}

TEST_CASE ("PlaybackEngine: starting mid-song replays the program and controllers that came before", "[playback][engine][chase]")
{
    SongDocument doc;
    auto t = addTrack (doc, "T", 1);
    addEvent (t, 0, { 0xC0, 24 });           // program 24 at tick 0
    addEvent (t, 100, { 0xB0, 7, 50 });      // volume 50
    addEvent (t, 200, { 0xE0, 0x00, 0x60 }); // bend
    addNote (t, 60, 4800, 480);              // note at 5 s
    Rig rig (doc);
    rig.transport.seek (2.0);                // play from 2 s: all three controllers are in the past
    rig.transport.play (rig.snapshot->endSeconds());
    rig.render (1);

    CHECK (rig.sink.count (PlaybackEventKind::Program) >= 2);   // setup + the real program-24 change
    bool sawProgram24 = false, sawVolume = false, sawBend = false;
    for (const auto& r : rig.sink.records)
    {
        sawProgram24 |= r.event.kind == PlaybackEventKind::Program && r.event.data1 == 24;
        sawVolume |= r.event.kind == PlaybackEventKind::Control && r.event.data1 == 7 && r.event.data2 == 50;
        sawBend |= r.event.kind == PlaybackEventKind::PitchBend && r.event.data1 == 12288;
    }
    CHECK (sawProgram24);
    CHECK (sawVolume);
    CHECK (sawBend);
    CHECK (rig.sink.count (PlaybackEventKind::NoteOn) == 0);   // chase never retriggers notes
}

TEST_CASE ("PlaybackEngine: notes already sounding at the playhead are not retriggered", "[playback][engine][chase]")
{
    SongDocument doc;
    addNote (addTrack (doc), 60, 0, 9600);   // 0..10 s
    Rig rig (doc);
    rig.transport.seek (3.0);
    rig.transport.play (rig.snapshot->endSeconds());
    rig.render (2);
    CHECK (rig.sink.count (PlaybackEventKind::NoteOn) == 0);
}

TEST_CASE ("PlaybackEngine: a seek while playing releases voices and re-chases", "[playback][engine][chase]")
{
    SongDocument doc;
    auto t = addTrack (doc);
    addEvent (t, 0, { 0xC0, 9 });
    addNote (t, 60, 0, 9600);
    Rig rig (doc);
    rig.transport.play (rig.snapshot->endSeconds());
    rig.render (2);
    const int releases = rig.sink.releaseAllCount;
    rig.sink.records.clear();
    rig.transport.seek (4.0);
    rig.render (1);
    CHECK (rig.sink.releaseAllCount == releases + 1);
    bool reappliedProgram = false;
    for (const auto& r : rig.sink.records)
        reappliedProgram |= r.event.kind == PlaybackEventKind::Program && r.event.data1 == 9;
    CHECK (reappliedProgram);
}

TEST_CASE ("PlaybackEngine: a snapshot swap mid-play releases voices, re-chases and keeps the playhead", "[playback][engine][swap]")
{
    SongDocument doc;
    auto t = addTrack (doc);
    addEvent (t, 0, { 0xC0, 9 });
    addNote (t, 60, 0, 9600);
    Rig rig (doc);
    rig.transport.play (rig.snapshot->endSeconds());
    rig.render (20);
    const double before = rig.transport.getPositionSeconds();
    const int releases = rig.sink.releaseAllCount;
    rig.sink.records.clear();

    addNote (t, 64, 9600, 480);                      // an edit
    rig.engine.publishSnapshot (buildSnapshot (doc));
    rig.render (1);

    CHECK (rig.sink.releaseAllCount == releases + 1);
    CHECK (rig.transport.getPositionSeconds() > before);   // kept going, not reset
    bool reappliedProgram = false;
    for (const auto& r : rig.sink.records)
        reappliedProgram |= r.event.kind == PlaybackEventKind::Program && r.event.data1 == 9;
    CHECK (reappliedProgram);
}

TEST_CASE ("PlaybackEngine: a replaced sink (new SoundFont) triggers the same release and re-chase", "[playback][engine][swap]")
{
    SongDocument doc;
    auto t = addTrack (doc);
    addEvent (t, 0, { 0xC0, 9 });
    addNote (t, 60, 0, 9600);
    Rig rig (doc);
    rig.transport.play (rig.snapshot->endSeconds());
    rig.render (2);
    rig.sink.records.clear();
    const int releases = rig.sink.releaseAllCount;
    rig.sink.replaced = true;
    rig.render (1);
    CHECK (rig.sink.releaseAllCount == releases + 1);
    CHECK (rig.sink.count (PlaybackEventKind::Program) >= 1);
}

TEST_CASE ("PlaybackEngine: muted tracks send no NoteOn but still send NoteOff", "[playback][engine][mute]")
{
    SongDocument doc;
    auto t = addTrack (doc);
    addNote (t, 60, 480, 480);
    Rig rig (doc);
    rig.snapshot->setAudible (1, false);
    rig.transport.play (rig.snapshot->endSeconds());
    rig.render (100);   // the note spans 0.5..1.0 s; the brief's 80 blocks (0.85 s) stop before its NoteOff
    CHECK (rig.sink.count (PlaybackEventKind::NoteOn) == 0);
    CHECK (rig.sink.count (PlaybackEventKind::NoteOff) == 1);
}

TEST_CASE ("PlaybackEngine: muting a track mid-play releases exactly that track's channels", "[playback][engine][mute]")
{
    SongDocument doc;
    auto a = addTrack (doc, "A", 1);
    auto b = addTrack (doc, "B", 2);
    addNote (a, 60, 0, 9600, 100, 1);
    addNote (b, 64, 0, 9600, 100, 2);
    Rig rig (doc);
    rig.transport.play (rig.snapshot->endSeconds());
    rig.render (2);
    CHECK (rig.sink.releasedChannels.empty());

    rig.snapshot->setAudible (1, false);   // mute A
    rig.render (1);
    REQUIRE (rig.sink.releasedChannels.size() == 1);
    CHECK (rig.sink.releasedChannels[0] == rig.snapshot->channelsOfTrack (1)[0]);
    rig.render (1);
    CHECK (rig.sink.releasedChannels.size() == 1);   // released once, not every block
}

TEST_CASE ("PlaybackEngine: starting from the end restarts from the beginning", "[playback][engine]")
{
    SongDocument doc;
    addNote (addTrack (doc), 60, 0, 480);
    Rig rig (doc);
    rig.transport.goToEnd (rig.snapshot->endSeconds());
    rig.transport.play (rig.snapshot->endSeconds());
    rig.render (1);
    CHECK (rig.sink.count (PlaybackEventKind::NoteOn) == 1);
}

namespace
{
    // Two tracks on two channels, each with a program and a CC in the past of 2 s.
    void buildResetSong (SongDocument& doc)
    {
        auto a = addTrack (doc, "A", 1);
        auto b = addTrack (doc, "B", 2);
        addEvent (a, 0, { 0xC0, 24 });
        addEvent (a, 100, { 0xB0, 7, 50 });
        addEvent (b, 0, { 0xC1, 40 });
        addEvent (b, 100, { 0xB1, 11, 30 });
        addNote (a, 60, 0, 9600, 100, 1);
        addNote (b, 64, 0, 9600, 100, 2);
    }

    // Exactly one reset per snapshot channel since the sink log was cleared, all
    // of them issued before the first event handed to the sink.
    void checkResetsBeforeChase (const Rig& rig)
    {
        const auto& channels = rig.snapshot->channels();
        REQUIRE (channels.size() == 2);
        REQUIRE (rig.sink.resets.size() == channels.size());
        std::vector<int> seen;
        for (const auto& r : rig.sink.resets)
        {
            CHECK (r.recordsBefore == 0);
            seen.push_back (r.channel);
        }
        std::sort (seen.begin(), seen.end());
        CHECK (seen == std::vector<int> { 0, 1 });
        CHECK (! rig.sink.records.empty());   // the chase did replay something after the resets
    }

    void clearSinkLog (Rig& rig)
    {
        rig.sink.records.clear();
        rig.sink.resets.clear();
    }
}

TEST_CASE ("PlaybackEngine: Stop then Play resets every channel before the chase replays", "[playback][engine][chase][reset]")
{
    SongDocument doc;
    buildResetSong (doc);
    Rig rig (doc);
    rig.transport.seek (2.0);
    rig.transport.play (rig.snapshot->endSeconds());
    rig.render (2);
    rig.transport.stop();
    rig.render (1);
    clearSinkLog (rig);

    rig.transport.play (rig.snapshot->endSeconds());   // back at the play-start, 2.0 s
    rig.render (1);
    checkResetsBeforeChase (rig);
}

TEST_CASE ("PlaybackEngine: Play again after the song ended resets every channel", "[playback][engine][chase][reset]")
{
    SongDocument doc;
    auto a = addTrack (doc, "A", 1);
    auto b = addTrack (doc, "B", 2);
    addEvent (a, 100, { 0xB0, 11, 0 });        // a fade at the end, nothing at tick 0
    addNote (a, 60, 0, 480, 100, 1);
    addNote (b, 64, 0, 480, 100, 2);
    Rig rig (doc);
    rig.transport.play (rig.snapshot->endSeconds());
    for (int i = 0; i < 200 && rig.transport.isPlaying(); ++i)
        rig.render (1);
    REQUIRE (! rig.transport.isPlaying());
    rig.render (1);
    clearSinkLog (rig);

    rig.transport.play (rig.snapshot->endSeconds());   // restarts from 0
    rig.render (1);
    REQUIRE (rig.sink.resets.size() == 2);
    for (const auto& r : rig.sink.resets)
        CHECK (r.recordsBefore == 0);
    CHECK (! rig.sink.records.empty());       // the tick-0 events fire after the resets
}

TEST_CASE ("PlaybackEngine: a seek while playing resets every channel before the chase", "[playback][engine][chase][reset]")
{
    SongDocument doc;
    buildResetSong (doc);
    Rig rig (doc);
    rig.transport.play (rig.snapshot->endSeconds());
    rig.render (2);
    clearSinkLog (rig);
    rig.transport.seek (4.0);
    rig.render (1);
    checkResetsBeforeChase (rig);
}

TEST_CASE ("PlaybackEngine: a snapshot swap mid-play resets every channel before the chase", "[playback][engine][swap][reset]")
{
    SongDocument doc;
    buildResetSong (doc);
    Rig rig (doc);
    rig.transport.seek (2.0);
    rig.transport.play (rig.snapshot->endSeconds());
    rig.render (2);
    clearSinkLog (rig);
    rig.engine.publishSnapshot (buildSnapshot (doc));
    rig.render (1);
    CHECK (rig.sink.resets.size() == 2);
    for (const auto& r : rig.sink.resets)
        CHECK (r.recordsBefore == 0);
    CHECK (! rig.sink.records.empty());
}

TEST_CASE ("PlaybackEngine: a replaced sink resets every channel before the chase", "[playback][engine][swap][reset]")
{
    SongDocument doc;
    buildResetSong (doc);
    Rig rig (doc);
    rig.transport.seek (2.0);
    rig.transport.play (rig.snapshot->endSeconds());
    rig.render (2);
    clearSinkLog (rig);
    rig.sink.replaced = true;
    rig.render (1);
    checkResetsBeforeChase (rig);
}

TEST_CASE ("PlaybackEngine: a bank-select CC reaches the sink before the Program change at the same tick", "[playback][engine]")
{
    SongDocument doc;
    auto t = addTrack (doc);
    addEvent (t, 0, { 0xC0, 40 });
    addEvent (t, 0, { 0xB0, 0, 3 });
    addEvent (t, 0, { 0xB0, 32, 9 });
    addNote (t, 60, 0, 480);
    Rig rig (doc);
    rig.transport.play (rig.snapshot->endSeconds());
    rig.render (1);

    int firstProgram = -1, lastBankCC = -1, ownProgram = -1;
    for (size_t i = 0; i < rig.sink.records.size(); ++i)
    {
        const auto& e = rig.sink.records[i].event;
        if (e.kind == PlaybackEventKind::Program && firstProgram < 0) firstProgram = (int) i;
        if (e.kind == PlaybackEventKind::Program && e.data1 == 40) ownProgram = (int) i;
        if (e.kind == PlaybackEventKind::Control && (e.data1 == 0 || e.data1 == 32)) lastBankCC = (int) i;
    }
    REQUIRE (firstProgram >= 0);
    REQUIRE (lastBankCC >= 0);
    REQUIRE (ownProgram >= 0);
    CHECK (lastBankCC < firstProgram);
    CHECK (lastBankCC < ownProgram);
}

TEST_CASE ("PlaybackEngine: continuous playback does not reset channels after the first block", "[playback][engine][chase][reset]")
{
    SongDocument doc;
    buildResetSong (doc);
    Rig rig (doc);
    rig.transport.play (rig.snapshot->endSeconds());
    rig.render (1);
    REQUIRE (rig.sink.resets.size() == 2);
    rig.render (20);
    REQUIRE (rig.transport.isPlaying());
    CHECK (rig.sink.resets.size() == 2);
}

TEST_CASE ("PlaybackEngine: a track at 50% plays NoteOn at half velocity, NoteOff unchanged", "[playback][engine][gain]")
{
    SongDocument doc;
    auto t = addTrack (doc);
    addNote (t, 60, 0, 240, 90);
    Rig rig (doc);
    rig.snapshot->setGainPercent (trackIndexOf (*rig.snapshot, t), 50);
    rig.transport.play (rig.snapshot->endSeconds());
    rig.render (60);
    bool sawOn = false, sawOff = false;
    for (const auto& r : rig.sink.records)
    {
        if (r.event.kind == PlaybackEventKind::NoteOn)  { sawOn = true;  CHECK (r.event.data2 == 45); }
        if (r.event.kind == PlaybackEventKind::NoteOff) { sawOff = true; CHECK (r.event.data1 == 60); }
    }
    CHECK (sawOn);
    CHECK (sawOff);
}

TEST_CASE ("PlaybackEngine: gain 0 drops NoteOn but a sounding note still gets its NoteOff", "[playback][engine][gain]")
{
    SongDocument doc;
    auto t = addTrack (doc);
    addNote (t, 60, 0, 960, 100);       // 0 - 1 s: sounding when the gain drops
    addNote (t, 62, 1440, 240, 100);    // 1.5 s: starts after the gain drops
    Rig rig (doc);
    rig.transport.play (rig.snapshot->endSeconds());
    rig.render (2);                     // ~21 ms: note 1 has started, not ended
    REQUIRE (rig.sink.count (PlaybackEventKind::NoteOn) == 1);
    REQUIRE (rig.sink.count (PlaybackEventKind::NoteOff) == 0);
    rig.snapshot->setGainPercent (trackIndexOf (*rig.snapshot, t), 0);
    rig.render (300);                   // ~3.2 s: past both notes
    CHECK (rig.sink.count (PlaybackEventKind::NoteOn) == 1);    // only the first
    CHECK (rig.sink.count (PlaybackEventKind::NoteOff) == 2);   // both offs delivered
}

namespace
{
    int countPanControls (const RecordingSink& sink, int trackIndex, size_t fromRecord = 0)
    {
        int n = 0;
        for (size_t i = fromRecord; i < sink.records.size(); ++i)
        {
            const auto& e = sink.records[i].event;
            n += (e.kind == PlaybackEventKind::Control && e.trackIndex == trackIndex && (e.data1 == 10 || e.data1 == 42)) ? 1 : 0;
        }
        return n;
    }

    const RecordingSink::Pan* lastPanFor (const RecordingSink& sink, int channel)
    {
        const RecordingSink::Pan* last = nullptr;
        for (const auto& p : sink.pans)
            if (p.channel == channel) last = &p;
        return last;
    }

    size_t lastResetFor (const RecordingSink& sink, int channel)
    {
        size_t before = 0;
        bool found = false;
        for (const auto& r : sink.resets)
            if (r.channel == channel) { before = r.recordsBefore; found = true; }
        REQUIRE (found);
        return before;
    }

    // The override is in force on `channel`: its last pan is `pan` and came after
    // the channel's last reset (a CC121 reset centres pan, so order matters).
    void checkOverrideApplied (const RecordingSink& sink, int channel, int pan)
    {
        const auto* p = lastPanFor (sink, channel);
        REQUIRE (p != nullptr);
        CHECK (p->pan == pan);
        CHECK (p->recordsBefore > lastResetFor (sink, channel));
    }
}

TEST_CASE ("engine: an overridden track drops its file CC10 and is panned on every channel", "[playback][engine][pan]")
{
    SongDocument doc;
    auto t = addTrack (doc, "T", 1);
    addEvent (t, 0, { 0xB0, 10, 20 });
    addEvent (t, 0, { 0xB0, 42, 5 });
    addNote (t, 60, 0, 9600);
    t.setProperty (SongIDs::playbackPan, -50, nullptr);
    Rig rig (doc);
    const int idx = trackIndexOf (*rig.snapshot, t);
    rig.transport.play (rig.snapshot->endSeconds());
    rig.render (2);
    CHECK (countPanControls (rig.sink, idx) == 0);
    REQUIRE (! rig.snapshot->channelsOfTrack (idx).empty());
    for (int vch : rig.snapshot->channelsOfTrack (idx))
    {
        const auto* p = lastPanFor (rig.sink, vch);
        REQUIRE (p != nullptr);
        CHECK (p->pan == -50);
    }
}

TEST_CASE ("engine: a track without an override keeps its file CC10 and gets no pan call", "[playback][engine][pan]")
{
    SongDocument doc;
    auto t = addTrack (doc, "T", 1);
    addEvent (t, 0, { 0xB0, 10, 20 });
    addNote (t, 60, 0, 9600);
    Rig rig (doc);
    rig.transport.play (rig.snapshot->endSeconds());
    rig.render (2);
    CHECK (countPanControls (rig.sink, trackIndexOf (*rig.snapshot, t)) == 1);
    CHECK (rig.sink.pans.empty());
}

TEST_CASE ("engine: the override survives a seek", "[playback][engine][pan]")
{
    SongDocument doc;
    auto t = addTrack (doc, "T", 1);
    addEvent (t, 0, { 0xB0, 10, 20 });
    addEvent (t, 2400, { 0xB0, 10, 90 });     // 2.5 s
    addNote (t, 60, 0, 9600);
    t.setProperty (SongIDs::playbackPan, -50, nullptr);
    Rig rig (doc);
    const int idx = trackIndexOf (*rig.snapshot, t);
    rig.transport.seek (1.0);
    rig.transport.play (rig.snapshot->endSeconds());
    rig.render (2);
    const size_t afterFirst = rig.sink.records.size();
    rig.transport.seek (4.0);                 // both CC10s now lie before the playhead
    rig.render (2);
    CHECK (countPanControls (rig.sink, idx, afterFirst) == 0);
    for (int vch : rig.snapshot->channelsOfTrack (idx))
        checkOverrideApplied (rig.sink, vch, -50);
}

TEST_CASE ("engine: the override survives Stop and Play", "[playback][engine][pan]")
{
    SongDocument doc;
    auto t = addTrack (doc, "T", 1);
    addEvent (t, 0, { 0xB0, 10, 20 });
    addNote (t, 60, 0, 9600);
    t.setProperty (SongIDs::playbackPan, 30, nullptr);
    Rig rig (doc);
    const int idx = trackIndexOf (*rig.snapshot, t);
    rig.transport.seek (2.0);
    rig.transport.play (rig.snapshot->endSeconds());
    rig.render (4);
    rig.transport.stop();
    rig.render (1);
    const size_t resetsBefore = rig.sink.resets.size();
    rig.transport.play (rig.snapshot->endSeconds());
    rig.render (2);
    REQUIRE (rig.sink.resets.size() > resetsBefore);
    CHECK (countPanControls (rig.sink, idx) == 0);
    for (int vch : rig.snapshot->channelsOfTrack (idx))
        checkOverrideApplied (rig.sink, vch, 30);
}

TEST_CASE ("engine: a file CC10 during playback is dropped for an overridden track", "[playback][engine][pan]")
{
    SongDocument doc;
    auto t = addTrack (doc, "T", 1);
    addEvent (t, 480, { 0xB0, 10, 90 });      // 0.5 s, reached by the live loop
    addNote (t, 60, 0, 9600);
    t.setProperty (SongIDs::playbackPan, -50, nullptr);
    Rig rig (doc);
    rig.transport.play (rig.snapshot->endSeconds());
    rig.render (100);                         // ~1.07 s
    CHECK (countPanControls (rig.sink, trackIndexOf (*rig.snapshot, t)) == 0);
}

TEST_CASE ("engine: a file CC121 during playback re-applies the override", "[playback][engine][pan]")
{
    SongDocument doc;
    auto t = addTrack (doc, "T", 1);
    addEvent (t, 480, { 0xB0, 121, 0 });      // 0.5 s: reset all controllers centres pan in tsf
    addNote (t, 60, 0, 9600);
    t.setProperty (SongIDs::playbackPan, -50, nullptr);
    Rig rig (doc);
    const int idx = trackIndexOf (*rig.snapshot, t);
    rig.transport.play (rig.snapshot->endSeconds());
    rig.render (100);
    size_t resetAt = 0;
    bool sawReset = false;
    for (size_t i = 0; i < rig.sink.records.size(); ++i)
        if (rig.sink.records[i].event.kind == PlaybackEventKind::Control && rig.sink.records[i].event.data1 == 121)
        { resetAt = i; sawReset = true; }
    REQUIRE (sawReset);
    for (int vch : rig.snapshot->channelsOfTrack (idx))
    {
        const auto* p = lastPanFor (rig.sink, vch);
        REQUIRE (p != nullptr);
        CHECK (p->pan == -50);
        CHECK (p->recordsBefore > resetAt);
    }
}

TEST_CASE ("engine: clearing the override restores the file's last CC10", "[playback][engine][pan]")
{
    SongDocument doc;
    auto t = addTrack (doc, "T", 1);
    addEvent (t, 0, { 0xB0, 10, 20 });
    addEvent (t, 480, { 0xB0, 10, 100 });     // 0.5 s
    addNote (t, 60, 0, 9600);
    t.setProperty (SongIDs::playbackPan, -50, nullptr);
    Rig rig (doc);
    const int idx = trackIndexOf (*rig.snapshot, t);
    rig.transport.play (rig.snapshot->endSeconds());
    rig.render (100);                         // ~1.07 s: past tick 480
    const size_t before = rig.sink.records.size();
    rig.snapshot->setPanOverride (idx, std::nullopt);
    rig.render (1);
    for (int vch : rig.snapshot->channelsOfTrack (idx))
    {
        bool restored = false;
        for (size_t i = before; i < rig.sink.records.size(); ++i)
        {
            const auto& e = rig.sink.records[i].event;
            restored |= e.kind == PlaybackEventKind::Control && e.virtualChannel == vch && e.data1 == 10 && e.data2 == 100;
        }
        CHECK (restored);
    }
    // The next block does not restore again.
    const size_t afterRestore = rig.sink.records.size();
    rig.render (1);
    CHECK (countPanControls (rig.sink, idx, afterRestore) == 0);
}

TEST_CASE ("engine: clearing the override with no file CC10 centres the channel", "[playback][engine][pan]")
{
    SongDocument doc;
    auto t = addTrack (doc, "T", 1);
    addNote (t, 60, 0, 9600);
    t.setProperty (SongIDs::playbackPan, -50, nullptr);
    Rig rig (doc);
    const int idx = trackIndexOf (*rig.snapshot, t);
    rig.transport.play (rig.snapshot->endSeconds());
    rig.render (10);
    rig.snapshot->setPanOverride (idx, std::nullopt);
    rig.render (1);
    for (int vch : rig.snapshot->channelsOfTrack (idx))
    {
        const auto* p = lastPanFor (rig.sink, vch);
        REQUIRE (p != nullptr);
        CHECK (p->pan == 0);
    }
}

TEST_CASE ("engine: setting an override mid-play pans the track at once", "[playback][engine][pan]")
{
    SongDocument doc;
    auto t = addTrack (doc, "T", 1);
    addNote (t, 60, 0, 9600);
    Rig rig (doc);
    const int idx = trackIndexOf (*rig.snapshot, t);
    rig.transport.play (rig.snapshot->endSeconds());
    rig.render (10);
    REQUIRE (rig.sink.pans.empty());
    rig.snapshot->setPanOverride (idx, 75);
    rig.render (1);
    for (int vch : rig.snapshot->channelsOfTrack (idx))
    {
        const auto* p = lastPanFor (rig.sink, vch);
        REQUIRE (p != nullptr);
        CHECK (p->pan == 75);
    }
    const size_t pansAfter = rig.sink.pans.size();
    rig.render (5);
    CHECK (rig.sink.pans.size() == pansAfter);   // applied once, not every block
}

TEST_CASE ("engine: every virtual channel of a multi-channel track is panned", "[playback][engine][pan]")
{
    SongDocument doc;
    auto t = addTrack (doc, "T", 1);
    addNote (t, 60, 0, 9600, 100, 1);
    addNote (t, 64, 0, 9600, 100, 2);
    addNote (t, 36, 0, 9600, 100, 10);   // drums
    t.setProperty (SongIDs::playbackPan, 40, nullptr);
    Rig rig (doc);
    const int idx = trackIndexOf (*rig.snapshot, t);
    REQUIRE (rig.snapshot->channelsOfTrack (idx).size() == 3);
    rig.transport.seek (2.0);
    rig.transport.play (rig.snapshot->endSeconds());
    rig.render (2);
    for (int vch : rig.snapshot->channelsOfTrack (idx))
        checkOverrideApplied (rig.sink, vch, 40);
}

TEST_CASE ("engine: master percent scales the rendered block and 100 is unity", "[playback][engine][master]")
{
    SongDocument doc;
    addNote (addTrack (doc), 60, 0, 9600);
    Rig rig (doc);
    rig.sink.fill = 1.0f;
    CHECK (rig.engine.masterPercent() == 100);

    auto checkBlock = [&rig] (float expected)
    {
        rig.render (1);
        for (int ch = 0; ch < 2; ++ch)
        {
            CHECK (rig.buffer.getSample (ch, 0) == Catch::Approx (expected));
            CHECK (rig.buffer.getSample (ch, 511) == Catch::Approx (expected));
        }
    };

    // Not playing.
    checkBlock (1.0f);
    rig.engine.setMasterPercent (50);
    CHECK (rig.engine.masterPercent() == 50);
    checkBlock (0.5f);
    rig.engine.setMasterPercent (0);
    checkBlock (0.0f);

    // Playing.
    rig.transport.play (rig.snapshot->endSeconds());
    rig.engine.setMasterPercent (100);
    checkBlock (1.0f);
    rig.engine.setMasterPercent (50);
    checkBlock (0.5f);
    rig.engine.setMasterPercent (0);
    checkBlock (0.0f);
}

TEST_CASE ("engine: master percent on a mono buffer is applied once", "[playback][engine][master]")
{
    SongDocument doc;
    addNote (addTrack (doc), 60, 0, 9600);
    Rig rig (doc);
    rig.sink.fill = 1.0f;
    juce::AudioBuffer<float> mono (1, 256);
    rig.engine.setMasterPercent (50);
    rig.engine.renderBlock (mono, 0, 256);
    CHECK (mono.getSample (0, 0) == Catch::Approx (0.5f));
    CHECK (mono.getSample (0, 255) == Catch::Approx (0.5f));
}

namespace
{
    // The frames at which a NoteOn of `pitch` reached the sink, in order.
    std::vector<long> noteOnFrames (const RecordingSink& sink, int pitch)
    {
        std::vector<long> frames;
        for (const auto& r : sink.records)
            if (r.event.kind == PlaybackEventKind::NoteOn && r.event.data1 == pitch)
                frames.push_back (r.frame);
        return frames;
    }

    // A song with a program change, a held note over the whole loop (pitch 60,
    // 0..10 s) and a short note at 0.5 s (pitch 64, ends 0.75 s). 120 BPM and
    // 480 PPQ, so 1 tick = 1/960 s. Returns the track.
    juce::ValueTree addLoopSong (SongDocument& doc)
    {
        auto t = addTrack (doc);
        addEvent (t, 0, { 0xC0, 9 });
        addNote (t, 60, 0, 9600);
        addNote (t, 64, 480, 240);
        return t;
    }

    // Moves the playhead from inside its first render() call, i.e. while a
    // block is being rendered (the message thread seeking mid-block).
    struct SeekingSink : RecordingSink
    {
        Transport* transport = nullptr;
        double seekTo = -1.0;

        void render (float* left, float* right, int n) noexcept override
        {
            RecordingSink::render (left, right, n);
            if (transport != nullptr && seekTo >= 0.0)
            {
                transport->seek (seekTo);
                seekTo = -1.0;
            }
        }
    };

    constexpr double kLoopStart = 0.4, kLoopEnd = 0.6;   // 19200 .. 28800 frames, 9600 frames long
    constexpr double kFrame = 1.0 / sr;
}

TEST_CASE ("PlaybackEngine loop: a block crossing the loop end wraps sample-accurately", "[playback][engine][loop]")
{
    SongDocument doc;
    addLoopSong (doc);
    Rig rig (doc);
    rig.engine.setLoopSpan (LoopSpan { kLoopStart, kLoopEnd });
    rig.transport.seek (kLoopStart);
    rig.transport.play (rig.snapshot->endSeconds());
    rig.render (1);
    const int releases = rig.sink.releaseAllCount;
    rig.render (59);   // 60 * 512 = 30720 frames: wraps at frames 9600, 19200 and 28800 (none on a block boundary)

    // The 0.5 s note is 4800 frames into every pass.
    CHECK (noteOnFrames (rig.sink, 64) == std::vector<long> { 4800, 14400, 24000 });
    CHECK (rig.sink.releaseAllCount == releases + 3);   // one release per wrap, no spurious re-chase
    CHECK (rig.transport.isPlaying());
    CHECK (rig.transport.getPositionSeconds() == Catch::Approx (0.44).margin (kFrame));   // 1920 frames past the last wrap
    CHECK (rig.sink.count (PlaybackEventKind::NoteOff) == 0);   // the 0.75 s NoteOff lies past the loop
}

TEST_CASE ("PlaybackEngine loop: a note held across the loop start sounds again at each wrap, but not at the first Play", "[playback][engine][loop]")
{
    SongDocument doc;
    addLoopSong (doc);   // pitch 60 is held over the whole loop
    Rig rig (doc);
    rig.engine.setLoopSpan (LoopSpan { kLoopStart, kLoopEnd });
    rig.transport.seek (kLoopStart);
    rig.transport.play (rig.snapshot->endSeconds());
    rig.render (60);   // three wraps, at frames 9600, 19200 and 28800
    CHECK (noteOnFrames (rig.sink, 60) == std::vector<long> { 9600, 19200, 28800 });
}

TEST_CASE ("PlaybackEngine loop: each wrap re-chases the program before the loop start", "[playback][engine][loop]")
{
    SongDocument doc;
    addLoopSong (doc);
    Rig rig (doc);
    rig.engine.setLoopSpan (LoopSpan { kLoopStart, kLoopEnd });
    rig.transport.seek (kLoopStart);
    rig.transport.play (rig.snapshot->endSeconds());
    rig.render (60);
    int program9 = 0;   // the file's own program change (the snapshot also adds a default per channel)
    for (const auto& r : rig.sink.records)
        if (r.event.kind == PlaybackEventKind::Program && r.event.data1 == 9)
            ++program9;
    CHECK (program9 == 4);   // the first chase plus three wraps
}

TEST_CASE ("PlaybackEngine loop: an event exactly at the loop end does not fire before the wrap", "[playback][engine][loop]")
{
    SongDocument doc;
    addNote (addLoopSong (doc), 67, 576, 240);   // 0.6 s: the loop end
    Rig rig (doc);
    rig.engine.setLoopSpan (LoopSpan { kLoopStart, kLoopEnd });
    // A start off the sample grid: the first wrapping segment rounds up past 0.6 s.
    rig.transport.seek (kLoopStart + 0.00001);
    rig.transport.play (rig.snapshot->endSeconds());
    rig.render (60);
    REQUIRE (noteOnFrames (rig.sink, 64).size() == 3);   // it did loop
    CHECK (noteOnFrames (rig.sink, 67).empty());
}

TEST_CASE ("PlaybackEngine loop: a playhead at or past the loop end never wraps", "[playback][engine][loop]")
{
    for (const double start : { 0.7, kLoopEnd })
    {
        SongDocument doc;
        addLoopSong (doc);
        Rig rig (doc);
        rig.engine.setLoopSpan (LoopSpan { kLoopStart, kLoopEnd });
        rig.transport.seek (start);
        rig.transport.play (rig.snapshot->endSeconds());
        rig.render (1);
        const int releases = rig.sink.releaseAllCount;
        rig.render (10);
        CHECK (rig.transport.getPositionSeconds() == Catch::Approx (start + 11 * 512 * kFrame).margin (kFrame));
        CHECK (rig.sink.releaseAllCount == releases);
    }
}

TEST_CASE ("PlaybackEngine loop: a seek past the end while looping plays on; a narrowed range wraps at once", "[playback][engine][loop]")
{
    SongDocument doc;
    addLoopSong (doc);
    Rig rig (doc);
    rig.engine.setLoopSpan (LoopSpan { kLoopStart, kLoopEnd });
    rig.transport.seek (kLoopStart);
    rig.transport.play (rig.snapshot->endSeconds());
    rig.render (10);   // 5120 frames into the loop
    REQUIRE (rig.transport.getPositionSeconds() == Catch::Approx (kLoopStart + 5120 * kFrame).margin (kFrame));

    SECTION ("a seek past the end")
    {
        rig.transport.seek (0.7);
        rig.render (10);
        CHECK (rig.transport.getPositionSeconds() == Catch::Approx (0.7 + 5120 * kFrame).margin (kFrame));
    }
    SECTION ("the end moved before the playhead")
    {
        const int releases = rig.sink.releaseAllCount;
        rig.engine.setLoopSpan (LoopSpan { kLoopStart, 0.45 });   // the playhead (~0.507 s) is past it
        rig.render (1);
        CHECK (rig.sink.releaseAllCount == releases + 1);          // one wrap, at the block start
        CHECK (rig.transport.getPositionSeconds() == Catch::Approx (kLoopStart + 512 * kFrame).margin (kFrame));
    }
}

TEST_CASE ("PlaybackEngine loop: a playhead before the range plays into it and then loops", "[playback][engine][loop]")
{
    SongDocument doc;
    addLoopSong (doc);
    Rig rig (doc);
    rig.engine.setLoopSpan (LoopSpan { kLoopStart, kLoopEnd });
    rig.transport.play (rig.snapshot->endSeconds());
    rig.render (70);   // 35840 frames: the end at 28800, one wrap, then 7040 frames into the loop

    CHECK (noteOnFrames (rig.sink, 64) == std::vector<long> { 24000, 33600 });
    CHECK (rig.transport.getPositionSeconds() == Catch::Approx (kLoopStart + 7040 * kFrame).margin (kFrame));
}

TEST_CASE ("PlaybackEngine loop: a loop shorter than a block wraps at most kMaxWrapsPerBlock times and stays in range", "[playback][engine][loop]")
{
    constexpr double shortEnd = 0.45;   // 2400 frames
    SECTION ("a 9600-frame block is exactly kMaxWrapsPerBlock passes")
    {
        SongDocument doc;
        addLoopSong (doc);
        Rig rig (doc);
        juce::AudioBuffer<float> big (2, 9600);
        rig.engine.setLoopSpan (LoopSpan { kLoopStart, shortEnd });
        rig.transport.seek (kLoopStart);
        rig.transport.play (rig.snapshot->endSeconds());
        rig.engine.renderBlock (big, 0, 9600);

        const auto chases = rig.sink.resets.size() / rig.snapshot->channels().size();
        CHECK (chases == (size_t) kMaxWrapsPerBlock + 1);       // the first chase plus four wraps
        CHECK (rig.sink.releaseAllCount == kMaxWrapsPerBlock + 1);
        CHECK (rig.transport.getPositionSeconds() >= kLoopStart);
        CHECK (rig.transport.getPositionSeconds() < shortEnd);
        CHECK (rig.sink.framesRendered == 9600);
    }
    SECTION ("a longer block stops wrapping at the cap")
    {
        SongDocument doc;
        addLoopSong (doc);
        Rig rig (doc);
        juce::AudioBuffer<float> big (2, 12000);   // five passes' worth
        rig.engine.setLoopSpan (LoopSpan { kLoopStart, shortEnd });
        rig.transport.seek (kLoopStart);
        rig.transport.play (rig.snapshot->endSeconds());
        rig.engine.renderBlock (big, 0, 12000);

        CHECK (rig.sink.releaseAllCount == kMaxWrapsPerBlock + 1);
        CHECK (rig.sink.framesRendered == 12000);
        CHECK (rig.transport.getPositionSeconds() >= kLoopStart);   // the remainder never escapes the loop
        CHECK (rig.transport.getPositionSeconds() < shortEnd);
        rig.render (20);
        CHECK (rig.transport.getPositionSeconds() >= kLoopStart);
        CHECK (rig.transport.getPositionSeconds() < shortEnd);
    }
}

TEST_CASE ("PlaybackEngine loop: clearing the loop plays on past the old end", "[playback][engine][loop]")
{
    SongDocument doc;
    addLoopSong (doc);
    Rig rig (doc);
    rig.engine.setLoopSpan (LoopSpan { kLoopStart, kLoopEnd });
    rig.transport.play (rig.snapshot->endSeconds());
    rig.render (70);
    REQUIRE (rig.transport.getPositionSeconds() == Catch::Approx (kLoopStart + 7040 * kFrame).margin (kFrame));
    const int releases = rig.sink.releaseAllCount;

    rig.engine.setLoopSpan (std::nullopt);
    rig.render (20);   // 10240 frames: through 0.6 s to 0.76 s
    CHECK (rig.transport.getPositionSeconds() == Catch::Approx (kLoopStart + (7040 + 10240) * kFrame).margin (kFrame));
    CHECK (rig.sink.releaseAllCount == releases);
    CHECK (rig.sink.count (PlaybackEventKind::NoteOff) == 1);   // the 0.75 s NoteOff now plays
}

TEST_CASE ("PlaybackEngine loop: a message-thread seek during the wrap block wins and re-chases", "[playback][engine][loop]")
{
    SongDocument doc;
    addLoopSong (doc);
    Transport transport;
    SeekingSink sink;
    PlaybackEngine engine { transport, sink };
    juce::AudioBuffer<float> buffer { 2, 512 };
    engine.prepare (sr, 512);
    auto snapshot = buildSnapshot (doc);
    engine.publishSnapshot (snapshot);

    engine.setLoopSpan (LoopSpan { kLoopStart, kLoopEnd });
    transport.seek (0.59);                     // 480 frames before the loop end: this block wraps
    transport.play (snapshot->endSeconds());
    sink.transport = &transport;
    sink.seekTo = 0.3;                         // lands during the block's first segment
    engine.renderBlock (buffer, 0, 512);

    CHECK (sink.releaseAllCount == 2);         // the first chase and the wrap
    CHECK (transport.getPositionSeconds() == Catch::Approx (0.3));   // the seek won over the block's commit

    const int releases = sink.releaseAllCount;
    engine.renderBlock (buffer, 0, 512);
    CHECK (sink.releaseAllCount == releases + 1);   // re-chased at the seek target
    CHECK (transport.getPositionSeconds() == Catch::Approx (0.3 + 512 * kFrame).margin (kFrame));
}

// loopSpanForTesting is const: it returns the writer-side copy and never reads the
// cell, whose read() writes the audio-thread-only cache (a race while the device runs).
TEST_CASE ("PlaybackEngine loop: loopSpanForTesting reports the last published span without reading the cell", "[playback][engine][loop]")
{
    SongDocument doc;
    addLoopSong (doc);
    Rig rig (doc);
    const PlaybackEngine& engine = rig.engine;
    CHECK_FALSE (engine.loopSpanForTesting().has_value());
    rig.engine.setLoopSpan (LoopSpan { kLoopStart, kLoopEnd });
    REQUIRE (engine.loopSpanForTesting().has_value());
    CHECK (engine.loopSpanForTesting()->startSeconds == kLoopStart);
    CHECK (engine.loopSpanForTesting()->endSeconds == kLoopEnd);
    rig.engine.setLoopSpan (std::nullopt);
    CHECK_FALSE (engine.loopSpanForTesting().has_value());
}
