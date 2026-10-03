// MidiImport: importMidiFile's raw-aware path writes every raw event into
// the document (conductor, NOTE links, EVENTs), rescales events with notes,
// and refuses files the two parsers disagree on.

#include "UI/SongModelBridge.h"
#include "UI/RawMidi.h"
#include "MidiTestBytes.h"

#include <catch2/catch_test_macros.hpp>

#include <juce_core/juce_core.h>

using namespace lotro;
using namespace miditest;

namespace
{
    juce::File midiFixture (const std::string& name)
    {
        return juce::File (__FILE__).getParentDirectory().getParentDirectory()
                   .getChildFile ("midi").getChildFile (name);
    }

    struct TempMidi
    {
        juce::File file = juce::File::createTempFile (".mid");
        explicit TempMidi (const Bytes& bytes) { REQUIRE (file.replaceWithData (bytes.data(), bytes.size())); }
        ~TempMidi() { file.deleteFile(); }
    };

    Bytes eventBytes (const juce::ValueTree& event)
    {
        const auto* block = event.getProperty (SongIDs::data).getBinaryData();
        REQUIRE (block != nullptr);
        const auto* d = static_cast<const std::uint8_t*> (block->getData());
        return Bytes (d, d + block->getSize());
    }

    TrackBody conductorBody()
    {
        TrackBody c;
        c.ev (0, { 0xFF, 0x51, 0x03, 0x07, 0xA1, 0x20 }).ev (0, { 0xF0, 0x05, 0x7E, 0x7F, 0x09, 0x01, 0xF7 }).eot (960);
        return c;
    }
}

TEST_CASE ("MidiImport: a fixture's conductor, notes and events all land in the document", "[midiimport]")
{
    const auto file = midiFixture ("land.mid");
    juce::MemoryBlock block;
    REQUIRE (file.loadFileAsData (block));
    const auto* d = static_cast<const std::uint8_t*> (block.getData());
    const auto raw = readMidiBytes (Bytes (d, d + block.getSize()), "land");

    SongDocument doc;
    Diagnostics diags;
    REQUIRE (importMidiFile (doc, file, 1, diags));

    auto conductor = doc.getConductorTrack();
    CHECK (SongDocument::getEventsNode (conductor).getNumChildren() == (int) raw.tracks[0].events.size());
    CHECK ((int) conductor.getProperty (SongIDs::endTick) == raw.tracks[0].endTick);

    // Every other raw track became a MIDI_TRACK, note-less ones included.
    REQUIRE (doc.getNumTracks() == (int) raw.tracks.size());
    for (int r = 1; r < (int) raw.tracks.size(); ++r)
    {
        auto track = doc.getTrack (r);
        CHECK ((int) track.getProperty (SongIDs::sourceTrackIndex) == r);
        CHECK ((int) track.getProperty (SongIDs::endTick) == raw.tracks[(size_t) r].endTick);

        int claimed = 0;
        for (auto note : SongDocument::getNotesNode (track))
            claimed += (bool) note.getProperty (SongIDs::offSynthesized) ? 1 : 2;
        CHECK (claimed + SongDocument::getEventsNode (track).getNumChildren()
               == (int) raw.tracks[(size_t) r].events.size());

        for (auto event : SongDocument::getEventsNode (track))
        {
            const int order = (int) event.getProperty (SongIDs::order);
            CHECK (eventBytes (event) == raw.tracks[(size_t) r].events[(size_t) order].bytes);
            CHECK ((int) event.getProperty (SongIDs::tick) == raw.tracks[(size_t) r].events[(size_t) order].tick);
        }
    }
}

TEST_CASE ("MidiImport: linked NOTEs carry channel, orders and note-off style", "[midiimport]")
{
    TrackBody melody;
    melody.ev (0, { 0x93, 60, 100 }).ev (96, { 0x93, 60, 0 }).ev (0, { 0x93, 62, 90 }).ev (96, { 0x83, 62, 0x30 }).eot();
    TempMidi tmp (smf (1, 96, { conductorBody(), melody }));

    SongDocument doc;
    Diagnostics diags;
    REQUIRE (importMidiFile (doc, tmp.file, 1, diags));

    auto notes = SongDocument::getNotesNode (doc.getTrack (1));
    REQUIRE (notes.getNumChildren() == 2);
    CHECK ((int) notes.getChild (0).getProperty (SongIDs::channel) == 4);
    CHECK ((int) notes.getChild (0).getProperty (SongIDs::onOrder) == 0);
    CHECK ((int) notes.getChild (0).getProperty (SongIDs::offOrder) == 1);
    CHECK ((bool) notes.getChild (0).getProperty (SongIDs::offIsNoteOnZero));
    CHECK ((int) notes.getChild (1).getProperty (SongIDs::offVelocity) == 0x30);
    CHECK_FALSE ((bool) notes.getChild (1).getProperty (SongIDs::offIsNoteOnZero));
    CHECK ((int) doc.getTrack (1).getProperty (SongIDs::defaultChannel) == 4);
}

TEST_CASE ("MidiImport: format 2 is refused with an Error and the document is unchanged", "[midiimport]")
{
    TrackBody melody;
    melody.ev (0, { 0x90, 60, 100 }).ev (96, { 0x80, 60, 0x40 }).eot();
    TempMidi tmp (smf (2, 96, { melody }));

    SongDocument doc;
    Diagnostics diags;
    CHECK_FALSE (importMidiFile (doc, tmp.file, 1, diags));
    REQUIRE (diags.size() == 1);
    CHECK (diags[0].severity == Severity::Error);
    CHECK (doc.getNumTracks() == 1);
    CHECK (doc.getTempoMapNode().getNumChildren() == 0);
    CHECK (SongDocument::getEventsNode (doc.getConductorTrack()).getNumChildren() == 0);
    CHECK (doc.getTree().getProperty (SongIDs::inputMidiPath).toString().isEmpty());
}

TEST_CASE ("MidiImport: a later import keeps the first conductor and reports what it dropped", "[midiimport]")
{
    TrackBody melody;
    melody.ev (0, { 0x90, 60, 100 }).ev (96, { 0x80, 60, 0x40 }).eot();
    TempMidi first (smf (1, 96, { conductorBody(), melody }));
    TempMidi second (smf (1, 96, { conductorBody(), melody }));

    SongDocument doc;
    Diagnostics d1, d2;
    REQUIRE (importMidiFile (doc, first.file, 1, d1));
    const int conductorEvents = SongDocument::getEventsNode (doc.getConductorTrack()).getNumChildren();

    REQUIRE (importMidiFile (doc, second.file, 2, d2));
    CHECK (SongDocument::getEventsNode (doc.getConductorTrack()).getNumChildren() == conductorEvents);
    CHECK (doc.getNumTracks() == 3);
    bool sawDropInfo = false;
    for (const auto& d : d2)
        sawDropInfo |= d.severity == Severity::Info && d.message.find ("Dropped 2 song-wide") != std::string::npos;
    CHECK (sawDropInfo);
}

TEST_CASE ("MidiImport: a later lower-PPQ import rescales its EVENT ticks and endTick", "[midiimport]")
{
    TrackBody firstMelody;
    firstMelody.ev (0, { 0x90, 60, 100 }).ev (480, { 0x80, 60, 0x40 }).eot();
    TrackBody secondMelody;
    secondMelody.ev (0, { 0x90, 60, 100 }).ev (48, { 0xB0, 0x07, 0x50 }).ev (48, { 0x80, 60, 0x40 }).eot (24);
    TempMidi first (smf (1, 480, { conductorBody(), firstMelody }));
    TempMidi second (smf (1, 96, { conductorBody(), secondMelody }));

    SongDocument doc;
    Diagnostics d1, d2;
    REQUIRE (importMidiFile (doc, first.file, 1, d1));
    REQUIRE (importMidiFile (doc, second.file, 2, d2));

    auto incoming = doc.getTrack (2);
    auto events = SongDocument::getEventsNode (incoming);
    REQUIRE (events.getNumChildren() == 1);
    CHECK ((int) events.getChild (0).getProperty (SongIDs::tick) == 240);  // 48 * 480/96
    CHECK ((int) incoming.getProperty (SongIDs::endTick) == 600);          // 120 * 480/96
}

TEST_CASE ("MidiImport: an LCM raise rescales existing EVENT ticks and endTicks, conductor included", "[midiimport]")
{
    TrackBody firstMelody;
    firstMelody.ev (0, { 0x90, 60, 100 }).ev (60, { 0xB0, 0x07, 0x50 }).ev (60, { 0x80, 60, 0x40 }).eot();
    TrackBody secondMelody;
    secondMelody.ev (0, { 0x90, 60, 100 }).ev (480, { 0x80, 60, 0x40 }).eot();
    TrackBody firstConductor;
    firstConductor.ev (0, { 0xFF, 0x51, 0x03, 0x07, 0xA1, 0x20 }).ev (120, { 0xFF, 0x06, 0x01, 'A' }).eot (240);
    TempMidi first (smf (1, 120, { firstConductor, firstMelody }));
    TempMidi second (smf (1, 480, { conductorBody(), secondMelody }));

    SongDocument doc;
    Diagnostics d1, d2;
    REQUIRE (importMidiFile (doc, first.file, 1, d1));
    REQUIRE (importMidiFile (doc, second.file, 2, d2));
    REQUIRE ((int) doc.getSourceMidiNode().getProperty (SongIDs::ticksPerQuarter) == 480);

    auto conductorEvents = SongDocument::getEventsNode (doc.getConductorTrack());
    CHECK ((int) conductorEvents.getChild (1).getProperty (SongIDs::tick) == 480);
    CHECK ((int) doc.getConductorTrack().getProperty (SongIDs::endTick) == 1440); // (120 + 240) * 4
    CHECK ((int) SongDocument::getEventsNode (doc.getTrack (1)).getChild (0).getProperty (SongIDs::tick) == 240);
    CHECK ((int) doc.getTrack (1).getProperty (SongIDs::endTick) == 480);
}

TEST_CASE ("MidiImport: importer diagnostics point at the document row of their track", "[midiimport]")
{
    TrackBody lyrics;
    lyrics.ev (0, { 0xFF, 0x05, 0x01, 'x' }).eot();
    TrackBody melody;
    melody.ev (0, { 0x90, 60, 100 }).ev (0, { 0x90, 64, 100 }).ev (96, { 0x80, 60, 0x40 }).eot(); // 64 never ends
    TempMidi tmp (smf (1, 96, { conductorBody(), lyrics, melody }));

    SongDocument doc;
    Diagnostics diags;
    REQUIRE (importMidiFile (doc, tmp.file, 1, diags));

    bool found = false;
    for (const auto& d : diags)
        if (d.source == "MidiImporter")
        {
            found = true;
            CHECK (d.trackIndex == 2); // row 0 conductor, row 1 lyrics, row 2 melody
        }
    CHECK (found);
}
