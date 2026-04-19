#include "Core/MidiImporter.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_formats/juce_audio_formats.h>

#include <string>
#include <vector>

namespace
{
    struct FixtureFile
    {
        juce::TemporaryFile temp { ".mid" };

        juce::File get() const { return temp.getFile(); }

        void write (const juce::MidiFile& midi)
        {
            juce::FileOutputStream stream (temp.getFile());
            REQUIRE (stream.openedOk());
            REQUIRE (midi.writeTo (stream));
        }
    };

    juce::MidiMessageSequence makeHeaderSequence (double bpm, int numerator, int denominator)
    {
        juce::MidiMessageSequence header;
        header.addEvent (juce::MidiMessage::tempoMetaEvent ((int) (60'000'000.0 / bpm)));
        header.addEvent (juce::MidiMessage::timeSignatureMetaEvent (numerator, denominator));
        header.addEvent (juce::MidiMessage::endOfTrack());
        return header;
    }

    juce::MidiMessageSequence makeSingleNoteTrack (const juce::String& name,
                                                   int channel,
                                                   int pitch,
                                                   int velocity,
                                                   int startTick,
                                                   int durationTicks)
    {
        juce::MidiMessageSequence track;
        track.addEvent (juce::MidiMessage::textMetaEvent (3, name));
        auto noteOn  = juce::MidiMessage::noteOn  (channel, pitch, (juce::uint8) velocity);
        auto noteOff = juce::MidiMessage::noteOff (channel, pitch);
        noteOn.setTimeStamp  (startTick);
        noteOff.setTimeStamp (startTick + durationTicks);
        track.addEvent (noteOn);
        track.addEvent (noteOff);
        track.updateMatchedPairs();
        track.addEvent (juce::MidiMessage::endOfTrack());
        return track;
    }
}

TEST_CASE ("MidiImporter: loads a tiny format-1 MIDI round-trip", "[midi]")
{
    juce::MidiFile midi;
    midi.setTicksPerQuarterNote (480);
    midi.addTrack (makeHeaderSequence (120.0, 4, 4));
    midi.addTrack (makeSingleNoteTrack ("Melody", 1, 60, 100, 0, 480));

    FixtureFile fixture;
    fixture.write (midi);

    lotro::Diagnostics warnings;
    auto song = lotro::importMidi (fixture.get(), warnings);

    CHECK (warnings.empty());
    CHECK (song.ticksPerQuarter == 480);
    REQUIRE (song.tempoMap.size() == 1);
    CHECK (song.tempoMap.front().bpm == Catch::Approx (120.0));
    REQUIRE (song.meterMap.size() == 1);
    CHECK (song.meterMap.front().numerator   == 4);
    CHECK (song.meterMap.front().denominator == 4);

    REQUIRE (song.tracks.size() == 1);
    const auto& track = song.tracks.front();
    CHECK (track.name == "Melody");
    CHECK (track.instrument == lotro::LotroInstrument::LuteOfAges);
    REQUIRE (track.notes.size() == 1);
    const auto& note = track.notes.front();
    CHECK (note.pitch         == 60);
    CHECK (note.startTick     == 0);
    CHECK (note.durationTicks == 480);
    CHECK (note.velocity      == 100);
    CHECK_FALSE (note.isDrum);
}

TEST_CASE ("MidiImporter: channel-10 tracks auto-detect as Drums", "[midi]")
{
    juce::MidiFile midi;
    midi.setTicksPerQuarterNote (480);
    midi.addTrack (makeHeaderSequence (120.0, 4, 4));
    midi.addTrack (makeSingleNoteTrack ("Percussion", 10, 38, 100, 0, 240));

    FixtureFile fixture;
    fixture.write (midi);

    lotro::Diagnostics warnings;
    auto song = lotro::importMidi (fixture.get(), warnings);

    REQUIRE (song.tracks.size() == 1);
    const auto& track = song.tracks.front();
    CHECK (track.instrument       == lotro::LotroInstrument::Drums);
    CHECK (track.sourceMidiChannel == 10);
    REQUIRE (track.notes.size() == 1);
    CHECK (track.notes.front().isDrum);
}

TEST_CASE ("MidiImporter: missing tempo/meter get defaults", "[midi]")
{
    juce::MidiFile midi;
    midi.setTicksPerQuarterNote (96);
    midi.addTrack (makeSingleNoteTrack ("Bare", 1, 64, 80, 0, 96));

    FixtureFile fixture;
    fixture.write (midi);

    lotro::Diagnostics warnings;
    auto song = lotro::importMidi (fixture.get(), warnings);

    CHECK (song.ticksPerQuarter == 96);
    REQUIRE (song.tempoMap.size() == 1);
    CHECK (song.tempoMap.front().bpm == Catch::Approx (120.0));
    REQUIRE (song.meterMap.size() == 1);
    CHECK (song.meterMap.front().numerator == 4);
}

TEST_CASE ("MidiImporter: rejects missing file", "[midi]")
{
    lotro::Diagnostics warnings;
    REQUIRE_THROWS_AS (
        lotro::importMidi (juce::File ("/tmp/does-not-exist-zzzz.mid"), warnings),
        lotro::MidiImportError);
}
