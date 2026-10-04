#include "PlaybackTestSupport.h"
#include "UI/Playback/PlaybackSnapshot.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace lotro;
using namespace lotro::playbacktest;
using Catch::Approx;

namespace
{
    std::vector<PlaybackEvent> ofKind (const PlaybackSnapshot& s, PlaybackEventKind k)
    {
        std::vector<PlaybackEvent> out;
        for (const auto& e : s.events())
            if (e.kind == k) out.push_back (e);
        return out;
    }
}

TEST_CASE ("buildSnapshot: an empty song has no events and zero length", "[playback][snapshot]")
{
    SongDocument doc;
    const auto snap = buildSnapshot (doc);
    CHECK (snap->events().empty());
    CHECK (snap->endSeconds() == Approx (0.0));
}

TEST_CASE ("buildSnapshot: a note becomes timed NoteOn/NoteOff in seconds at 120 BPM", "[playback][snapshot]")
{
    SongDocument doc;
    auto t = addTrack (doc);
    addNote (t, 60, 480, 240, 90, 1);   // 0.5 s .. 0.75 s at 120 BPM / 480 PPQ

    const auto snap = buildSnapshot (doc);
    const auto on = ofKind (*snap, PlaybackEventKind::NoteOn);
    const auto off = ofKind (*snap, PlaybackEventKind::NoteOff);
    REQUIRE (on.size() == 1);
    REQUIRE (off.size() == 1);
    CHECK (on[0].seconds == Approx (0.5));
    CHECK (on[0].data1 == 60);
    CHECK (on[0].data2 == 90);
    CHECK (off[0].seconds == Approx (0.75));
    CHECK (snap->endSeconds() == Approx (0.75));
}

TEST_CASE ("buildSnapshot: a mid-song tempo change is honoured", "[playback][snapshot]")
{
    SongDocument doc;
    addTempo (doc, 0, 120.0);
    addTempo (doc, 480, 60.0);
    auto t = addTrack (doc);
    addNote (t, 60, 960, 480);   // starts after 0.5 s @120 + 1.0 s @60 = 1.5 s

    const auto snap = buildSnapshot (doc);
    CHECK (ofKind (*snap, PlaybackEventKind::NoteOn)[0].seconds == Approx (1.5));
}

TEST_CASE ("buildSnapshot: NoteOff sorts before NoteOn at the same tick, controllers before both", "[playback][snapshot]")
{
    SongDocument doc;
    auto t = addTrack (doc);
    addNote (t, 60, 0, 480);
    addNote (t, 60, 480, 480);                 // retrigger exactly at the previous note's off
    addEvent (t, 480, { 0xB0, 7, 100 });       // CC7 on channel 1 at the same tick

    const auto snap = buildSnapshot (doc);
    std::vector<PlaybackEventKind> at480;
    for (const auto& e : snap->events())
        if (e.tick == 480) at480.push_back (e.kind);
    REQUIRE (at480.size() == 3);
    CHECK (at480[0] == PlaybackEventKind::Control);
    CHECK (at480[1] == PlaybackEventKind::NoteOff);
    CHECK (at480[2] == PlaybackEventKind::NoteOn);
}

TEST_CASE ("buildSnapshot: program, control and pitch bend events are decoded; others ignored", "[playback][snapshot]")
{
    SongDocument doc;
    auto t = addTrack (doc, "T", 3);
    addNote (t, 60, 0, 480, 100, 3);
    addEvent (t, 0, { 0xC2, 24 });              // program 24 on channel 3
    addEvent (t, 0, { 0xB2, 64, 127 });         // CC64 sustain
    addEvent (t, 10, { 0xE2, 0x00, 0x60 });     // bend: 0x00 | (0x60 << 7) = 12288
    addEvent (t, 20, { 0xF0, 0x01, 0xF7 });     // SysEx: ignored
    addEvent (t, 30, { 0xFF, 0x2F, 0x00 });     // meta: ignored
    addEvent (t, 40, { 0xD2, 50 });             // channel pressure: ignored

    const auto snap = buildSnapshot (doc);
    const auto programs = ofKind (*snap, PlaybackEventKind::Program);
    REQUIRE (programs.size() == 2);              // 1 setup event + the real one
    CHECK (programs.back().data1 == 24);
    const auto controls = ofKind (*snap, PlaybackEventKind::Control);
    REQUIRE (controls.size() == 1);
    CHECK (controls[0].data1 == 64);
    CHECK (controls[0].data2 == 127);
    const auto bends = ofKind (*snap, PlaybackEventKind::PitchBend);
    REQUIRE (bends.size() == 1);
    CHECK (bends[0].data1 == 12288);
    CHECK (snap->events().size() == 6);   // setup program, real program, CC, bend, NoteOn, NoteOff
}

TEST_CASE ("buildSnapshot: each (track, channel) pair gets its own virtual channel", "[playback][snapshot]")
{
    SongDocument doc;
    auto a = addTrack (doc, "A", 1);
    auto b = addTrack (doc, "B", 1);             // same MIDI channel as A
    addNote (a, 60, 0, 480, 100, 1);
    addNote (a, 62, 0, 480, 100, 2);             // A also uses channel 2
    addNote (b, 64, 0, 480, 100, 1);
    addEvent (a, 0, { 0xC0, 5 });
    addEvent (b, 0, { 0xC0, 40 });

    const auto snap = buildSnapshot (doc);
    CHECK (snap->channels().size() == 3);
    int programFor5 = -1, programFor40 = -1;
    for (const auto& e : snap->events())
        if (e.kind == PlaybackEventKind::Program && e.data1 == 5) programFor5 = e.virtualChannel;
        else if (e.kind == PlaybackEventKind::Program && e.data1 == 40) programFor40 = e.virtualChannel;
    CHECK (programFor5 != programFor40);
    CHECK (snap->channelsOfTrack (snap->trackIndexForId ((juce::int64) a.getProperty (SongIDs::trackId))).size() == 2);
}

TEST_CASE ("buildSnapshot: channel 10 uses the drum bank even with no program-change event", "[playback][snapshot]")
{
    SongDocument doc;
    auto t = addTrack (doc, "Drums", 10);
    addNote (t, 36, 0, 120, 100, 10);

    const auto snap = buildSnapshot (doc);
    REQUIRE (snap->channels().size() == 1);
    CHECK (snap->channels()[0].isDrum);
    const auto programs = ofKind (*snap, PlaybackEventKind::Program);
    REQUIRE (programs.size() == 1);
    CHECK (programs[0].data2 == 1);
    CHECK (programs[0].seconds == Approx (0.0));
}

TEST_CASE ("buildSnapshot: the conductor's events are ignored for sound", "[playback][snapshot]")
{
    SongDocument doc;
    addEvent (doc.getConductorTrack(), 0, { 0xB0, 7, 100 });
    CHECK (buildSnapshot (doc)->events().empty());
}

TEST_CASE ("buildSnapshot: degenerate note data is clamped", "[playback][snapshot]")
{
    SongDocument doc;
    auto t = addTrack (doc);
    addNote (t, 200, 0, 0, 0, 1);       // pitch > 127, zero length, velocity 0
    juce::ValueTree bare (SongIDs::NOTE);   // no properties at all
    SongDocument::appendChildBulk (SongDocument::getNotesNode (t), bare);

    const auto snap = buildSnapshot (doc);
    const auto on = ofKind (*snap, PlaybackEventKind::NoteOn);
    const auto off = ofKind (*snap, PlaybackEventKind::NoteOff);
    REQUIRE (on.size() == 2);
    CHECK (on[0].data1 == 127);
    CHECK (on[0].data2 >= 1);
    CHECK (off[0].tick > on[0].tick);   // zero-length becomes at least one tick
}

TEST_CASE ("buildSnapshot: more than 64 tracks keep independent audible flags", "[playback][snapshot]")
{
    SongDocument doc;
    for (int i = 0; i < 70; ++i)
        addNote (addTrack (doc), 60, 0, 480);

    const auto snap = buildSnapshot (doc);
    REQUIRE (snap->numTracks() == 71);   // 70 + conductor
    snap->setAudible (69, false);
    CHECK (! snap->isAudible (69));
    CHECK (snap->isAudible (68));
    CHECK (snap->isAudible (1));
}

TEST_CASE ("buildSnapshot: more than 256 (track, channel) pairs are dropped without crashing", "[playback][snapshot]")
{
    SongDocument doc;
    for (int i = 0; i < 20; ++i)
    {
        auto t = addTrack (doc);
        for (int ch = 1; ch <= 16; ++ch)
            addNote (t, 60, 0, 480, 100, ch);
    }
    const auto snap = buildSnapshot (doc);
    CHECK ((int) snap->channels().size() == kMaxVirtualChannels);
}
