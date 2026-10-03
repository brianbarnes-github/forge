// RawMidi: lossless SMF reader/writer. Pins event-for-event fidelity
// (order, explicit status, velocity-0 note-ons, SysEx, every meta type,
// End-of-Track tick) and track-structure parity with juce::MidiFile.

#include "UI/RawMidi.h"
#include "MidiTestBytes.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <juce_audio_basics/juce_audio_basics.h>

#include <fstream>
#include <sstream>

using namespace lotro;
using namespace miditest;

namespace
{
    juce::File midiFixture (const std::string& name)
    {
        return juce::File (__FILE__).getParentDirectory().getParentDirectory()
                   .getChildFile ("midi").getChildFile (name);
    }

    Bytes readFileBytes (const juce::File& f)
    {
        juce::MemoryBlock block;
        REQUIRE (f.loadFileAsData (block));
        const auto* p = static_cast<const std::uint8_t*> (block.getData());
        return Bytes (p, p + block.getSize());
    }
}

TEST_CASE ("RawMidi: running status, velocity-0 note-ons, SysEx and meta events are read verbatim", "[rawmidi]")
{
    TrackBody t;
    t.ev (0,  { 0xFF, 0x03, 0x02, 'H', 'i' })      // track name
     .ev (0,  { 0xF0, 0x03, 0x7E, 0x7F, 0xF7 })     // SysEx, length 3
     .ev (0,  { 0x90, 60, 100 })                     // note-on, explicit status
     .ev (10, { 62, 90 })                            // running status
     .ev (5,  { 60, 0 })                             // running status, velocity-0 note-on
     .ev (0,  { 0xFF, 0x7F, 0x01, 0x42 })            // meta does NOT cancel running status (JUCE)
     .ev (5,  { 62, 0 })
     .eot (20);

    const auto raw = readMidiBytes (smf (1, 480, { t }), "t");

    REQUIRE (raw.format == 1);
    REQUIRE (raw.ticksPerQuarter == 480);
    REQUIRE (raw.tracks.size() == 1);
    const auto& ev = raw.tracks[0].events;
    REQUIRE (ev.size() == 7);
    CHECK (ev[0].bytes == Bytes { 0xFF, 0x03, 'H', 'i' });
    CHECK (ev[1].bytes == Bytes { 0xF0, 0x7E, 0x7F, 0xF7 });
    CHECK (ev[2].bytes == Bytes { 0x90, 60, 100 });
    CHECK (ev[3].tick == 10);
    CHECK (ev[3].bytes == Bytes { 0x90, 62, 90 });
    CHECK (ev[4].tick == 15);
    CHECK (ev[4].bytes == Bytes { 0x90, 60, 0 });
    CHECK (ev[5].bytes == Bytes { 0xFF, 0x7F, 0x42 });
    CHECK (ev[6].tick == 20);
    CHECK (ev[6].bytes == Bytes { 0x90, 62, 0 });
    CHECK (raw.tracks[0].endTick == 40);
}

TEST_CASE ("RawMidi: write then read reproduces the file event-for-event", "[rawmidi]")
{
    RawMidiFile file;
    file.format = 1;
    file.ticksPerQuarter = 96;

    RawMidiTrack conductor;
    conductor.events = { { 0, { 0xFF, 0x51, 0x07, 0xA1, 0x20 } },
                         { 0, { 0xFF, 0x58, 0x04, 0x02, 0x18, 0x08 } },
                         { 0, { 0xF0, 0x41, 0x10, 0x42, 0xF7 } },
                         { 0, { 0xF7, 0x01, 0x02 } },                  // F7 escape packet
                         { 384, { 0xFF, 0x06, 'A' } } };               // marker
    conductor.endTick = 1000;                                          // trailing silence
    RawMidiTrack notes;
    notes.events = { { 0, { 0xC3, 0x19 } },
                     { 0, { 0xB3, 0x07, 0x64 } },
                     { 0, { 0x93, 60, 100 } },
                     { 96, { 0x83, 60, 0x40 } },
                     { 96, { 0xE3, 0x00, 0x40 } },
                     { 100, { 0xFF, 0x05, 'l', 'a' } },               // lyric
                     { 100, { 0xFF, 0x60, 0x01 } } };                 // unknown meta type
    notes.endTick = 100;
    file.tracks = { conductor, notes };

    const auto reread = readMidiBytes (writeMidiBytes (file), "w");
    CHECK (reread == file);
}

TEST_CASE ("RawMidi: a track without End-of-Track ends at its last event; events after End-of-Track are kept", "[rawmidi]")
{
    TrackBody noEot;
    noEot.ev (0, { 0x90, 60, 100 }).ev (50, { 0x80, 60, 0 });

    TrackBody afterEot;
    afterEot.ev (0, { 0x90, 60, 100 }).eot (10).ev (5, { 0x80, 60, 0 });

    const auto raw = readMidiBytes (smf (1, 96, { noEot, afterEot }), "t");
    CHECK (raw.tracks[0].endTick == 50);
    REQUIRE (raw.tracks[1].events.size() == 2);
    CHECK (raw.tracks[1].endTick == 10);
    CHECK (raw.tracks[1].events[1].tick == 15);
}

TEST_CASE ("RawMidi: a format-0 file keeps every channel in its single track", "[rawmidi]")
{
    TrackBody t;
    t.ev (0, { 0x90, 60, 100 }).ev (0, { 0x99, 36, 90 }).ev (0, { 0x94, 64, 80 })
     .ev (96, { 0x80, 60, 0 }).ev (0, { 0x89, 36, 0 }).ev (0, { 0x84, 64, 0 }).eot();

    const auto raw = readMidiBytes (smf (0, 96, { t }), "t");
    REQUIRE (raw.format == 0);
    REQUIRE (raw.tracks.size() == 1);
    CHECK (raw.tracks[0].events[1].bytes == Bytes { 0x99, 36, 90 });
    CHECK (raw.tracks[0].events[2].bytes == Bytes { 0x94, 64, 80 });
}

TEST_CASE ("RawMidi: a non-MTrk chunk consumes one of the header's track slots and is skipped", "[rawmidi]")
{
    TrackBody t;
    t.ev (0, { 0x90, 60, 100 }).ev (10, { 0x80, 60, 0 }).eot();

    Bytes bytes = header (1, 2, 96);
    append (bytes, chunk ("XFIH", { 1, 2, 3 }));
    append (bytes, chunk ("MTrk", t.body));

    const auto raw = readMidiBytes (bytes, "t");
    CHECK (raw.tracks.size() == 1);
}

TEST_CASE ("RawMidi: malformed input throws MidiImportError", "[rawmidi]")
{
    TrackBody good;
    good.ev (0, { 0x90, 60, 100 }).eot();

    SECTION ("empty input")             { CHECK_THROWS_AS (readMidiBytes ({}, "t"), MidiImportError); }
    SECTION ("not a MIDI header")       { CHECK_THROWS_AS (readMidiBytes (chunk ("MThx", { 0, 1, 0, 1, 0, 96 }), "t"), MidiImportError); }
    SECTION ("format above 2")          { Bytes b = header (3, 1, 96); append (b, chunk ("MTrk", good.body)); CHECK_THROWS_AS (readMidiBytes (b, "t"), MidiImportError); }
    SECTION ("format 0 with 2 tracks")  { CHECK_THROWS_AS (readMidiBytes (smf (0, 96, { good, good }), "t"), MidiImportError); }
    SECTION ("SMPTE time division")     { Bytes b = header (1, 1, 0xE728); append (b, chunk ("MTrk", good.body)); CHECK_THROWS_AS (readMidiBytes (b, "t"), MidiImportError); }
    SECTION ("chunk runs past the end") { Bytes b = header (1, 1, 96); append (b, be32 (0x4D54726B)); append (b, be32 (100)); append (b, { 0, 0x90 }); CHECK_THROWS_AS (readMidiBytes (b, "t"), MidiImportError); }
    SECTION ("fewer chunks than declared") { Bytes b = header (1, 2, 96); append (b, chunk ("MTrk", good.body)); CHECK_THROWS_AS (readMidiBytes (b, "t"), MidiImportError); }
    SECTION ("trailing bytes")          { Bytes b = smf (1, 96, { good }); b.push_back (0); CHECK_THROWS_AS (readMidiBytes (b, "t"), MidiImportError); }
    SECTION ("unterminated delta time") { TrackBody t; t.body = { 0x81, 0x81, 0x81, 0x81, 0x00 }; CHECK_THROWS_AS (readMidiBytes (smf (1, 96, { t }), "t"), MidiImportError); }
    SECTION ("data byte, no running status") { TrackBody t; t.ev (0, { 60, 100 }); CHECK_THROWS_AS (readMidiBytes (smf (1, 96, { t }), "t"), MidiImportError); }
    SECTION ("truncated channel message") { TrackBody t; t.ev (0, { 0x90, 60 }); CHECK_THROWS_AS (readMidiBytes (smf (1, 96, { t }), "t"), MidiImportError); }
    SECTION ("truncated meta")          { TrackBody t; t.ev (0, { 0xFF, 0x03, 0x05, 'a' }); CHECK_THROWS_AS (readMidiBytes (smf (1, 96, { t }), "t"), MidiImportError); }
    SECTION ("system common byte")      { TrackBody t; t.ev (0, { 0xF2, 0x00, 0x00 }); CHECK_THROWS_AS (readMidiBytes (smf (1, 96, { t }), "t"), MidiImportError); }
}

TEST_CASE ("RawMidi: the writer rejects events out of tick order and empty events", "[rawmidi]")
{
    RawMidiFile file;
    RawMidiTrack t;

    SECTION ("out of order")
    {
        t.events = { { 10, { 0x90, 60, 100 } }, { 5, { 0x80, 60, 0 } } };
        file.tracks = { t };
        CHECK_THROWS_AS (writeMidiBytes (file), MidiExportError);
    }
    SECTION ("empty event")
    {
        t.events = { { 0, {} } };
        file.tracks = { t };
        CHECK_THROWS_AS (writeMidiBytes (file), MidiExportError);
    }
}

TEST_CASE ("RawMidi: writeMidiFile throws MidiExportError when the stream fails", "[rawmidi]")
{
    std::ofstream bad;  // never opened
    RawMidiFile file;
    file.tracks.push_back ({});
    CHECK_THROWS_AS (writeMidiFile (file, bad), MidiExportError);
}

TEST_CASE ("RawMidi: every tracked fixture reads, writes and reads back identically, with JUCE's track count", "[rawmidi]")
{
    auto name = GENERATE (as<std::string>{},
        "Barnes Brothers Band - Pull The Wires.mid", "anymore.mid", "blue.mid",
        "hold.mid", "land.mid", "leah.mid", "nobody.mid", "right.mid", "tellit.mid");

    DYNAMIC_SECTION (name)
    {
        const auto bytes = readFileBytes (midiFixture (name));
        const auto raw = readMidiBytes (bytes, name);

        juce::MidiFile juceFile;
        juce::MemoryInputStream in (bytes.data(), bytes.size(), false);
        REQUIRE (juceFile.readFrom (in, false));
        CHECK ((int) raw.tracks.size() == juceFile.getNumTracks());

        CHECK (readMidiBytes (writeMidiBytes (raw), name) == raw);
    }
}
