// MidiImportPlan: conductor rules, note links and leftover events, decided
// purely from importMidi's Song and the raw file.

#include "UI/MidiImportPlan.h"
#include "UI/RawMidi.h"
#include "Core/MidiImporter.h"
#include "MidiTestBytes.h"

#include <catch2/catch_test_macros.hpp>

#include <juce_core/juce_core.h>

#include <algorithm>
#include <numeric>
#include <sstream>

using namespace lotro;
using namespace miditest;

namespace
{
    struct Parsed { Song song; RawMidiFile raw; };

    Parsed parse (const Bytes& bytes)
    {
        Diagnostics diags;
        std::istringstream in (std::string (bytes.begin(), bytes.end()), std::ios::binary);
        Parsed p { importMidi (in, "t", diags), readMidiBytes (bytes, "t") };
        return p;
    }

    Bytes fixtureBytes (const std::string& name)
    {
        juce::MemoryBlock block;
        REQUIRE (juce::File (__FILE__).getParentDirectory().getParentDirectory()
                     .getChildFile ("midi").getChildFile (name).loadFileAsData (block));
        const auto* d = static_cast<const std::uint8_t*> (block.getData());
        return Bytes (d, d + block.getSize());
    }

    TrackBody conductorBody()
    {
        TrackBody c;
        c.ev (0, { 0xFF, 0x51, 0x03, 0x07, 0xA1, 0x20 })   // tempo
         .ev (0, { 0xF0, 0x05, 0x7E, 0x7F, 0x09, 0x01, 0xF7 }) // GM on
         .eot (960);
        return c;
    }
}

TEST_CASE ("MidiImportPlan: a file with a conductor puts it in the conductor verbatim on first import", "[midiimportplan]")
{
    TrackBody melody;
    melody.ev (0, { 0xFF, 0x03, 0x01, 'M' }).ev (0, { 0xC0, 0x19 }).ev (0, { 0xB0, 0x07, 0x64 })
          .ev (0, { 0x90, 60, 100 }).ev (96, { 0x80, 60, 0x40 }).eot();
    const auto p = parse (smf (1, 96, { conductorBody(), melody }));

    Diagnostics diags;
    const auto plan = planMidiImport (p.song, p.raw, true, diags);

    CHECK (plan.writesConductor);
    REQUIRE (plan.conductorEvents.size() == 2);
    CHECK (plan.conductorEvents[0].bytes == p.raw.tracks[0].events[0].bytes);
    CHECK (plan.conductorEvents[1].order == 1);
    CHECK (plan.conductorEvents[1].relocatedFrom == -1);
    CHECK (plan.conductorEndTick == 960);
    CHECK (plan.relocatedEventCount == 0);

    REQUIRE (plan.tracks.size() == 1);
    const auto& t = plan.tracks[0];
    CHECK (t.rawTrackIndex == 1);
    CHECK (t.songTrackIndex == 0);
    CHECK (t.defaultChannel == 1);
    REQUIRE (t.noteLinks.size() == 1);
    CHECK (t.noteLinks[0].onOrder == 3);
    CHECK (t.noteLinks[0].offOrder == 4);
    CHECK (t.noteLinks[0].offVelocity == 0x40);
    CHECK_FALSE (t.noteLinks[0].offIsNoteOnZero);
    REQUIRE (t.events.size() == 3); // name, program, CC
    CHECK (t.events[0].order == 0);
    CHECK (t.events[2].order == 2);
    CHECK (diags.empty());
}

TEST_CASE ("MidiImportPlan: a later import drops the file's whole conductor track and counts it", "[midiimportplan]")
{
    TrackBody melody;
    melody.ev (0, { 0x90, 60, 100 }).ev (96, { 0x80, 60, 0x40 }).eot();
    const auto p = parse (smf (1, 96, { conductorBody(), melody }));

    Diagnostics diags;
    const auto plan = planMidiImport (p.song, p.raw, false, diags);

    CHECK_FALSE (plan.writesConductor);
    CHECK (plan.conductorEvents.empty());
    CHECK (plan.droppedEventCount == 2);
    REQUIRE (diags.size() == 1);
    CHECK (diags[0].severity == Severity::Info);
    CHECK (diags[0].source == "SongModelBridge");
}

TEST_CASE ("MidiImportPlan: a later import without a conductor drops its song-wide metas and counts them", "[midiimportplan]")
{
    TrackBody first;
    first.ev (0, { 0xFF, 0x51, 0x03, 0x07, 0xA1, 0x20 })   // tempo     -> dropped
         .ev (0, { 0xFF, 0x05, 0x02, 'l', 'a' })            // lyric     -> stays
         .ev (0, { 0x90, 60, 100 })
         .ev (48, { 0xFF, 0x06, 0x01, 'B' })                // marker    -> dropped
         .ev (48, { 0x80, 60, 0x40 }).eot();
    TrackBody second;
    second.ev (0, { 0xFF, 0x59, 0x02, 0x00, 0x00 })        // key sig   -> dropped
          .ev (0, { 0x91, 64, 90 }).ev (96, { 0x91, 64, 0 }).eot();
    const auto p = parse (smf (1, 96, { first, second }));

    Diagnostics diags;
    const auto plan = planMidiImport (p.song, p.raw, false, diags);

    CHECK (plan.droppedEventCount == 3);
    CHECK (plan.relocatedEventCount == 0);
    CHECK (plan.conductorEvents.empty());
    REQUIRE (plan.tracks.size() == 2);
    REQUIRE (plan.tracks[0].events.size() == 1);
    CHECK (plan.tracks[0].events[0].bytes == Bytes { 0xFF, 0x05, 'l', 'a' });
    CHECK (plan.tracks[1].events.empty());
    REQUIRE (diags.size() == 1);
    CHECK (diags[0].severity == Severity::Info);
}

TEST_CASE ("MidiImportPlan: without a conductor, song-wide metas move into it and everything else stays", "[midiimportplan]")
{
    // Format 1, notes in the first track -> no conductor.
    TrackBody first;
    first.ev (0, { 0xFF, 0x51, 0x03, 0x07, 0xA1, 0x20 })   // tempo     -> relocated
         .ev (0, { 0xFF, 0x05, 0x02, 'l', 'a' })            // lyric     -> stays
         .ev (0, { 0x90, 60, 100 })
         .ev (48, { 0xFF, 0x06, 0x01, 'B' })                // marker    -> relocated
         .ev (48, { 0x80, 60, 0x40 }).eot();
    TrackBody second;
    second.ev (0, { 0xFF, 0x59, 0x02, 0x00, 0x00 })        // key sig   -> relocated
          .ev (0, { 0x91, 64, 90 }).ev (96, { 0x91, 64, 0 }).eot();
    const auto p = parse (smf (1, 96, { first, second }));

    Diagnostics diags;
    const auto plan = planMidiImport (p.song, p.raw, true, diags);

    REQUIRE (plan.conductorEvents.size() == 3);
    CHECK (plan.relocatedEventCount == 3);
    CHECK (plan.conductorEvents[0].relocatedFrom == 0);
    CHECK (plan.conductorEvents[0].order == 0);
    CHECK (plan.conductorEvents[1].relocatedFrom == 0);
    CHECK (plan.conductorEvents[1].order == 3);
    CHECK (plan.conductorEvents[2].relocatedFrom == 1);
    CHECK (plan.conductorEvents[2].order == 0);

    REQUIRE (plan.tracks.size() == 2);
    REQUIRE (plan.tracks[0].events.size() == 1);
    CHECK (plan.tracks[0].events[0].bytes == Bytes { 0xFF, 0x05, 'l', 'a' });
    CHECK (plan.tracks[1].events.empty());
    REQUIRE (plan.tracks[1].noteLinks.size() == 1);
    CHECK (plan.tracks[1].noteLinks[0].channel == 2);
    CHECK (plan.tracks[1].noteLinks[0].offIsNoteOnZero);
    CHECK (plan.tracks[1].noteLinks[0].offVelocity == 0);
    CHECK (plan.tracks[1].defaultChannel == 2);
    REQUIRE (diags.size() == 1);
    CHECK (diags[0].severity == Severity::Info);
}

TEST_CASE ("MidiImportPlan: format 0 keeps each note's own channel", "[midiimportplan]")
{
    TrackBody t;
    t.ev (0, { 0xFF, 0x51, 0x03, 0x07, 0xA1, 0x20 })
     .ev (0, { 0x94, 64, 80 }).ev (0, { 0x99, 36, 90 })
     .ev (96, { 0x84, 64, 0 }).ev (0, { 0x89, 36, 0 }).eot();
    const auto p = parse (smf (0, 96, { t }));

    Diagnostics diags;
    const auto plan = planMidiImport (p.song, p.raw, true, diags);

    REQUIRE (plan.conductorEvents.size() == 1);
    REQUIRE (plan.tracks.size() == 1);
    REQUIRE (plan.tracks[0].noteLinks.size() == 2);
    CHECK (plan.tracks[0].noteLinks[0].channel == 5);
    CHECK (plan.tracks[0].noteLinks[1].channel == 10);
    CHECK (plan.tracks[0].defaultChannel == 5);
}

TEST_CASE ("MidiImportPlan: a note-less non-conductor track is planned from raw data", "[midiimportplan]")
{
    TrackBody lyrics;
    lyrics.ev (0, { 0xFF, 0x03, 0x01, 'a' }).ev (0, { 0xFF, 0x03, 0x01, 'b' })   // last name wins
          .ev (0, { 0xC2, 0x05 }).ev (10, { 0xFF, 0x05, 0x01, 'x' }).eot();
    TrackBody melody;
    melody.ev (0, { 0x90, 60, 100 }).ev (96, { 0x80, 60, 0x40 }).eot();
    const auto p = parse (smf (1, 96, { conductorBody(), lyrics, melody }));

    Diagnostics diags;
    const auto plan = planMidiImport (p.song, p.raw, true, diags);

    REQUIRE (plan.tracks.size() == 2);
    const auto& t = plan.tracks[0];
    CHECK (t.songTrackIndex == -1);
    CHECK (t.rawTrackIndex == 1);
    CHECK (t.name == "b");
    CHECK (t.sourceMidiChannel == 3);
    CHECK (t.sourceProgram == 5);
    CHECK (t.defaultChannel == 3);
    CHECK (t.events.size() == 4);
    CHECK (plan.tracks[1].songTrackIndex == 0);
}

TEST_CASE ("MidiImportPlan: skipped notes and unclaimed offs stay as events; the review's re-strike case links the right bytes", "[midiimportplan]")
{
    // Tick 0: on60 v100, off60, on60 v50, off60; later off60@10. importMidi
    // keeps one note: v100, duration 10 (on raw 0, off raw 4).
    TrackBody t;
    t.ev (0, { 0x90, 60, 100 }).ev (0, { 0x80, 60, 0x40 }).ev (0, { 0x90, 60, 50 }).ev (0, { 0x80, 60, 0x40 })
     .ev (10, { 0x80, 60, 0x40 }).eot();
    const auto p = parse (smf (1, 96, { conductorBody(), t }));

    Diagnostics diags;
    const auto plan = planMidiImport (p.song, p.raw, true, diags);

    REQUIRE (plan.tracks.size() == 1);
    REQUIRE (plan.tracks[0].noteLinks.size() == 1);
    CHECK (plan.tracks[0].noteLinks[0].onOrder == 0);
    CHECK (plan.tracks[0].noteLinks[0].offOrder == 4);
    REQUIRE (plan.tracks[0].events.size() == 3);
    CHECK (plan.tracks[0].events[0].order == 1);
    CHECK (plan.tracks[0].events[1].order == 2);
    CHECK (plan.tracks[0].events[2].order == 3);
    CHECK (diags.empty());
}

TEST_CASE ("MidiImportPlan: format 2 and parser disagreement throw MidiImportPlanError", "[midiimportplan]")
{
    TrackBody melody;
    melody.ev (0, { 0x90, 60, 100 }).ev (96, { 0x80, 60, 0x40 }).eot();

    SECTION ("format 2")
    {
        auto p = parse (smf (1, 96, { conductorBody(), melody }));
        p.raw.format = 2;
        Diagnostics diags;
        CHECK_THROWS_AS (planMidiImport (p.song, p.raw, true, diags), MidiImportPlanError);
        CHECK (diags.empty());
    }
    SECTION ("a note the raw parser can't find")
    {
        auto p = parse (smf (1, 96, { conductorBody(), melody }));
        p.song.tracks[0].notes[0].sourceEventIndex = 7;
        Diagnostics diags;
        CHECK_THROWS_AS (planMidiImport (p.song, p.raw, true, diags), MidiImportPlanError);
    }
    SECTION ("a raw track index out of range")
    {
        auto p = parse (smf (1, 96, { conductorBody(), melody }));
        p.song.tracks[0].notes[0].sourceTrackIndex = 9;
        Diagnostics diags;
        CHECK_THROWS_AS (planMidiImport (p.song, p.raw, true, diags), MidiImportPlanError);
    }
    SECTION ("two Song tracks claiming the same raw track")
    {
        // Without the check the second Song track would silently replace the
        // first, and the first's notes would never be linked or verified.
        auto p = parse (smf (1, 96, { conductorBody(), melody }));
        p.song.tracks.push_back (p.song.tracks[0]);
        Diagnostics diags;
        CHECK_THROWS_AS (planMidiImport (p.song, p.raw, true, diags), MidiImportPlanError);
    }
    SECTION ("a Song track on the file's conductor track")
    {
        // Without the check the conductor branch would skip the extra Song
        // track while the real one still links, so nothing would throw.
        auto p = parse (smf (1, 96, { conductorBody(), melody }));
        p.song.tracks.push_back (p.song.tracks[0]);
        p.song.tracks[1].notes[0].sourceTrackIndex = 0;
        Diagnostics diags;
        CHECK_THROWS_AS (planMidiImport (p.song, p.raw, true, diags), MidiImportPlanError);
    }
}

TEST_CASE ("MidiImportPlan: every raw event of every fixture is accounted for exactly once", "[midiimportplan]")
{
    for (const auto* name : { "blue.mid", "leah.mid", "anymore.mid", "hold.mid", "land.mid", "nobody.mid",
                              "right.mid", "tellit.mid", "Barnes Brothers Band - Pull The Wires.mid" })
    {
        INFO (name);
        const auto p = parse (fixtureBytes (name));
        Diagnostics diags;
        const auto plan = planMidiImport (p.song, p.raw, true, diags);

        // The note cross-check never fires; at most the relocation Info.
        for (const auto& d : diags)
            CHECK (d.severity == Severity::Info);

        const bool fileHasConductor = hasConductorTrack (p.raw);
        if (fileHasConductor)
            CHECK (plan.conductorEvents.size() == p.raw.tracks[0].events.size());

        for (const auto& t : plan.tracks)
        {
            size_t claimed = 0;
            for (const auto& link : t.noteLinks)
                claimed += link.offSynthesized ? 1 : 2;

            const auto relocated = (size_t) std::count_if (plan.conductorEvents.begin(), plan.conductorEvents.end(),
                                                           [&] (const PlannedEvent& e) { return e.relocatedFrom == t.rawTrackIndex; });
            CHECK (claimed + t.events.size() + relocated == p.raw.tracks[(size_t) t.rawTrackIndex].events.size());
        }

        const size_t totalRaw = std::accumulate (p.raw.tracks.begin(), p.raw.tracks.end(), size_t { 0 },
                                                 [] (size_t sum, const RawMidiTrack& r) { return sum + r.events.size(); });
        size_t totalPlanned = plan.conductorEvents.size();
        for (const auto& t : plan.tracks)
        {
            totalPlanned += t.events.size();
            for (const auto& link : t.noteLinks)
                totalPlanned += link.offSynthesized ? 1 : 2;
        }
        CHECK (totalPlanned == totalRaw);
    }
}
