// JuceNoteReplica: must agree note-for-note with juce::MidiFile::readFrom
// (createMatchingNoteOffs = true), which is what importMidi uses. Checked
// against JUCE itself on hand cases from the spec review and 500 seeded
// random files, plus every fixture.

#include "UI/JuceNoteReplica.h"
#include "UI/RawMidi.h"
#include "MidiTestBytes.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <juce_audio_basics/juce_audio_basics.h>

#include <random>

using namespace lotro;
using namespace miditest;

namespace
{
    juce::File midiFixture (const std::string& name)
    {
        return juce::File (__FILE__).getParentDirectory().getParentDirectory()
                   .getChildFile ("midi").getChildFile (name);
    }

    void checkAgainstJuce (const Bytes& bytes)
    {
        juce::MidiFile paired, unpaired;
        {
            juce::MemoryInputStream in (bytes.data(), bytes.size(), false);
            REQUIRE (paired.readFrom (in, true));
        }
        {
            juce::MemoryInputStream in (bytes.data(), bytes.size(), false);
            REQUIRE (unpaired.readFrom (in, false));
        }

        const auto raw = readMidiBytes (bytes, "diff");
        REQUIRE (paired.getNumTracks() == (int) raw.tracks.size());

        for (int t = 0; t < paired.getNumTracks(); ++t)
        {
            const auto& seq = *paired.getTrack (t);
            const auto& rawTrack = raw.tracks[(size_t) t];

            std::vector<const juce::MidiMessageSequence::MidiEventHolder*> ons;
            for (int i = 0; i < seq.getNumEvents(); ++i)
                if (seq.getEventPointer (i)->message.isNoteOn())
                    ons.push_back (seq.getEventPointer (i));

            const auto replica = replicateJuceNoteOns (rawTrack);
            REQUIRE (replica.size() == ons.size());

            int synthesizedCount = 0;
            for (size_t k = 0; k < ons.size(); ++k)
            {
                const auto& m = ons[k]->message;
                const auto& r = replica[k];
                CHECK (r.pitch == m.getNoteNumber());
                CHECK (r.channel == m.getChannel());
                CHECK (r.velocity == (int) m.getVelocity());
                CHECK (r.onTick == (int) m.getTimeStamp());
                CHECK (r.hasOff == (ons[k]->noteOffObject != nullptr));
                if (ons[k]->noteOffObject != nullptr)
                    CHECK (r.offTick == (int) ons[k]->noteOffObject->message.getTimeStamp());

                const auto& onRaw = rawTrack.events[(size_t) r.onRawIndex];
                CHECK (onRaw.tick == r.onTick);
                CHECK ((int) onRaw.bytes[1] == r.pitch);
                CHECK ((int) onRaw.bytes[2] == r.velocity);

                if (r.hasOff && ! r.offSynthesized)
                {
                    const auto& offRaw = rawTrack.events[(size_t) r.offRawIndex];
                    CHECK (offRaw.tick == r.offTick);
                    CHECK ((int) offRaw.bytes[1] == r.pitch);
                    CHECK (((offRaw.bytes[0] & 0x0F) + 1) == r.channel);
                }
                if (r.offSynthesized)
                    ++synthesizedCount;
            }

            // JUCE adds exactly one event per invented note-off.
            CHECK (synthesizedCount == seq.getNumEvents() - unpaired.getTrack (t)->getNumEvents());
        }
    }

    Bytes randomFile (std::mt19937& rng)
    {
        std::uniform_int_distribution<int> delta (0, 2), kind (0, 9), note (60, 62), chan (0, 1), vel (1, 127);
        std::vector<TrackBody> tracks (2);
        for (auto& t : tracks)
        {
            for (int i = 0; i < 40; ++i)
            {
                const auto d  = (std::uint32_t) delta (rng);
                const auto ch = (std::uint8_t) chan (rng);
                const auto nn = (std::uint8_t) note (rng);
                const int  k  = kind (rng);
                if (k <= 3)      t.ev (d, { (std::uint8_t) (0x90 | ch), nn, (std::uint8_t) vel (rng) });
                else if (k <= 5) t.ev (d, { (std::uint8_t) (0x80 | ch), nn, 0x40 });
                else if (k <= 7) t.ev (d, { (std::uint8_t) (0x90 | ch), nn, 0 });
                else if (k == 8) t.ev (d, { (std::uint8_t) (0xB0 | ch), nn, 0x10 }); // CC number == a note number
                else             t.ev (d, { 0xFF, 0x01, 0x01, 'x' });
            }
            t.eot();
        }
        return smf (1, 96, tracks);
    }
}

TEST_CASE ("JuceNoteReplica: JUCE's within-tick swap can move a note-on ahead of another", "[replica]")
{
    // Tick 0: on60, on62, off60; later off62@10, off60@20. JUCE swaps on60
    // with the off60 in its tick group, so on62 becomes note-on ordinal 0.
    TrackBody t;
    t.ev (0, { 0x90, 60, 100 }).ev (0, { 0x90, 62, 100 }).ev (0, { 0x80, 60, 0x40 })
     .ev (10, { 0x80, 62, 0x40 }).ev (10, { 0x80, 60, 0x40 }).eot();
    const auto bytes = smf (1, 96, { t });

    const auto replica = replicateJuceNoteOns (readMidiBytes (bytes, "a").tracks[0]);
    REQUIRE (replica.size() == 2);
    CHECK (replica[0].pitch == 62);
    CHECK (replica[0].onRawIndex == 1);
    CHECK (replica[0].offRawIndex == 3);
    CHECK (replica[0].offTick == 10);
    CHECK (replica[1].pitch == 60);
    CHECK (replica[1].onRawIndex == 0);
    CHECK (replica[1].offRawIndex == 4);
    CHECK (replica[1].offTick == 20);

    checkAgainstJuce (bytes);
}

TEST_CASE ("JuceNoteReplica: a same-tick re-strike gets a JUCE-invented note-off and the later velocity first", "[replica]")
{
    // Tick 0: on60 v100, off60, on60 v50, off60; later off60@10.
    TrackBody t;
    t.ev (0, { 0x90, 60, 100 }).ev (0, { 0x80, 60, 0x40 }).ev (0, { 0x90, 60, 50 }).ev (0, { 0x80, 60, 0x40 })
     .ev (10, { 0x80, 60, 0x40 }).eot();
    const auto bytes = smf (1, 96, { t });

    const auto replica = replicateJuceNoteOns (readMidiBytes (bytes, "b").tracks[0]);
    REQUIRE (replica.size() == 2);
    CHECK (replica[0].velocity == 50);
    CHECK (replica[0].onRawIndex == 2);
    CHECK (replica[0].offSynthesized);
    CHECK (replica[0].offTick == 0);
    CHECK (replica[1].velocity == 100);
    CHECK (replica[1].onRawIndex == 0);
    CHECK (replica[1].offRawIndex == 4);
    CHECK (replica[1].offTick == 10);

    checkAgainstJuce (bytes);
}

TEST_CASE ("JuceNoteReplica: an unmatched note-on has no off", "[replica]")
{
    TrackBody t;
    t.ev (0, { 0x90, 60, 100 }).eot (50);
    const auto replica = replicateJuceNoteOns (readMidiBytes (smf (1, 96, { t }), "c").tracks[0]);
    REQUIRE (replica.size() == 1);
    CHECK_FALSE (replica[0].hasOff);
    CHECK (replica[0].offRawIndex == -1);
}

TEST_CASE ("JuceNoteReplica: agrees with juce::MidiFile on 500 seeded random files", "[replica]")
{
    std::mt19937 rng (20261003);
    for (int i = 0; i < 500; ++i)
        checkAgainstJuce (randomFile (rng));
}

TEST_CASE ("JuceNoteReplica: agrees with juce::MidiFile on every tracked fixture", "[replica]")
{
    auto name = GENERATE (as<std::string>{},
        "Barnes Brothers Band - Pull The Wires.mid", "anymore.mid", "blue.mid",
        "hold.mid", "land.mid", "leah.mid", "nobody.mid", "right.mid", "tellit.mid");

    DYNAMIC_SECTION (name)
    {
        juce::MemoryBlock block;
        REQUIRE (midiFixture (name).loadFileAsData (block));
        const auto* p = static_cast<const std::uint8_t*> (block.getData());
        checkAgainstJuce (Bytes (p, p + block.getSize()));
    }
}
