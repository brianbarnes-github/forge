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
            CHECK (plan.conductorEvents.size() == p.raw.tracks[0].events.size() + (size_t) plan.relocatedEventCount);

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

TEST_CASE ("MidiImportPlan: tempo replace on a later import writes the file's conductor and drops nothing", "[midiimportplan]")
{
    TrackBody melody;
    melody.ev (0, { 0x90, 60, 100 }).ev (96, { 0x80, 60, 0x40 }).eot();
    const auto p = parse (smf (1, 96, { conductorBody(), melody }));

    ImportOptions options;
    options.tempo = TempoMode::replace;
    Diagnostics diags;
    const auto plan = planMidiImport (p.song, p.raw, false, diags, options);

    CHECK (plan.writesConductor);
    REQUIRE (plan.conductorEvents.size() == 2);
    CHECK (plan.conductorEvents[0].bytes == p.raw.tracks[0].events[0].bytes);
    CHECK (plan.conductorEndTick == 960);
    CHECK (plan.droppedEventCount == 0);
    REQUIRE (diags.size() == 1);
    CHECK (diags[0].severity == Severity::Info);
    CHECK (diags[0].source == "SongModelBridge");
    CHECK (diags[0].message.find ("Replaced") != std::string::npos);
}

TEST_CASE ("MidiImportPlan: tempo replace on a later import relocates a conductor-less file's song-wide metas", "[midiimportplan]")
{
    TrackBody first;
    first.ev (0, { 0xFF, 0x51, 0x03, 0x07, 0xA1, 0x20 }).ev (0, { 0xFF, 0x05, 0x02, 'l', 'a' })
         .ev (0, { 0x90, 60, 100 }).ev (96, { 0x80, 60, 0x40 }).eot();
    const auto p = parse (smf (1, 96, { first }));

    ImportOptions options;
    options.tempo = TempoMode::replace;
    Diagnostics diags;
    const auto plan = planMidiImport (p.song, p.raw, false, diags, options);

    CHECK (plan.writesConductor);
    REQUIRE (plan.conductorEvents.size() == 1);
    CHECK (plan.conductorEvents[0].relocatedFrom == 0);
    CHECK (plan.relocatedEventCount == 1);
    CHECK (plan.droppedEventCount == 0);
    REQUIRE (plan.tracks.size() == 1);
    REQUIRE (plan.tracks[0].events.size() == 1);   // the lyric stays on the track
}

TEST_CASE ("MidiImportPlan: tempo replace on the first import is identical to keep", "[midiimportplan]")
{
    TrackBody melody;
    melody.ev (0, { 0x90, 60, 100 }).ev (96, { 0x80, 60, 0x40 }).eot();
    const auto p = parse (smf (1, 96, { conductorBody(), melody }));

    ImportOptions options;
    options.tempo = TempoMode::replace;
    Diagnostics keepDiags, replaceDiags;
    const auto keep    = planMidiImport (p.song, p.raw, true, keepDiags);
    const auto replace = planMidiImport (p.song, p.raw, true, replaceDiags, options);

    CHECK (replace.writesConductor == keep.writesConductor);
    CHECK (replace.conductorEvents.size() == keep.conductorEvents.size());
    CHECK (replaceDiags.size() == keepDiags.size());   // no "Replaced" line when nothing existed
}

namespace
{
    ImportOptions mergedOptions()
    {
        ImportOptions o;
        o.tracks = TrackMode::merged;
        return o;
    }
}

TEST_CASE ("MidiImportPlan: merged folds every note track into one, keeping channels and renumbering orders", "[midiimportplan]")
{
    TrackBody a;   // MIDI channel 1
    a.ev (0, { 0xFF, 0x03, 0x01, 'A' }).ev (0, { 0x90, 60, 100 }).ev (96, { 0x80, 60, 0x40 }).eot();
    TrackBody b;   // MIDI channel 3
    b.ev (0, { 0xFF, 0x03, 0x01, 'B' }).ev (0, { 0x92, 64, 90 }).ev (48, { 0x92, 67, 80 })
     .ev (48, { 0x82, 64, 0x40 }).ev (48, { 0x82, 67, 0x40 }).eot();
    const auto p = parse (smf (1, 96, { conductorBody(), a, b }));

    Diagnostics diags;
    const auto plan = planMidiImport (p.song, p.raw, true, diags, mergedOptions());

    REQUIRE (plan.tracks.size() == 1);
    const auto& t = plan.tracks[0];
    REQUIRE (t.mergedTrack.has_value());
    CHECK (t.mergedTrack->name == "t (merged)");
    CHECK (t.mergedSongTrackIndices == std::vector<int> { 0, 1 });
    CHECK (t.songTrackIndex == 0);
    CHECK (t.rawTrackIndex == 1);

    // Notes stable-sorted by start tick: A60@0, B64@0, B67@48.
    REQUIRE (t.mergedTrack->notes.size() == 3);
    CHECK (t.mergedTrack->notes[0].pitch == 60);
    CHECK (t.mergedTrack->notes[1].pitch == 64);
    CHECK (t.mergedTrack->notes[2].pitch == 67);
    REQUIRE (t.noteLinks.size() == 3);
    CHECK (t.noteLinks[0].channel == 1);
    CHECK (t.noteLinks[1].channel == 3);
    CHECK (t.noteLinks[2].channel == 3);

    // Raw events sorted by (tick, raw track, index): A.name 0, A.on 1, B.name 2, B.on64 3,
    // B.on67 4, A.off 5, B.off64 6, B.off67 7.
    CHECK (t.noteLinks[0].onOrder == 1);  CHECK (t.noteLinks[0].offOrder == 5);
    CHECK (t.noteLinks[1].onOrder == 3);  CHECK (t.noteLinks[1].offOrder == 6);
    CHECK (t.noteLinks[2].onOrder == 4);  CHECK (t.noteLinks[2].offOrder == 7);
    REQUIRE (t.events.size() == 2);       // the two track names
    CHECK (t.events[0].order == 0);
    CHECK (t.events[1].order == 2);

    const auto merged = std::any_of (diags.begin(), diags.end(), [] (const Diagnostic& d)
        { return d.severity == Severity::Info && d.message.find ("Merged 2") != std::string::npos; });
    CHECK (merged);
}

TEST_CASE ("MidiImportPlan: merged leaves note-less tracks alone and mixes drums with melody without losing flags", "[midiimportplan]")
{
    TrackBody melody;
    melody.ev (0, { 0x90, 60, 100 }).ev (96, { 0x80, 60, 0x40 }).eot();
    TrackBody drums;   // MIDI channel 10
    drums.ev (0, { 0x99, 36, 100 }).ev (48, { 0x89, 36, 0x40 }).eot();
    TrackBody markers; // no notes
    markers.ev (0, { 0xFF, 0x03, 0x01, 'X' }).eot();
    const auto p = parse (smf (1, 96, { conductorBody(), melody, markers, drums }));

    Diagnostics diags;
    const auto plan = planMidiImport (p.song, p.raw, true, diags, mergedOptions());

    REQUIRE (plan.tracks.size() == 2);                // merged track + the note-less one
    REQUIRE (plan.tracks[0].mergedTrack.has_value());
    CHECK_FALSE (plan.tracks[1].mergedTrack.has_value());
    CHECK (plan.tracks[1].songTrackIndex == -1);
    const auto& notes = plan.tracks[0].mergedTrack->notes;
    REQUIRE (notes.size() == 2);
    CHECK_FALSE (notes[0].isDrum);
    CHECK (notes[1].isDrum);
    CHECK (plan.tracks[0].noteLinks[1].channel == 10);
}

TEST_CASE ("MidiImportPlan: merged with fewer than two note tracks changes nothing", "[midiimportplan]")
{
    TrackBody melody;
    melody.ev (0, { 0x90, 60, 100 }).ev (96, { 0x80, 60, 0x40 }).eot();
    const auto p = parse (smf (1, 96, { conductorBody(), melody }));

    Diagnostics expandedDiags, mergedDiags;
    const auto expanded = planMidiImport (p.song, p.raw, true, expandedDiags);
    const auto merged   = planMidiImport (p.song, p.raw, true, mergedDiags, mergedOptions());

    REQUIRE (merged.tracks.size() == expanded.tracks.size());
    CHECK_FALSE (merged.tracks[0].mergedTrack.has_value());
    CHECK (merged.tracks[0].songTrackIndex == expanded.tracks[0].songTrackIndex);
    CHECK (merged.tracks[0].noteLinks.size() == expanded.tracks[0].noteLinks.size());
    CHECK (mergedDiags.size() == expandedDiags.size());
}

TEST_CASE ("planMidiImport: tempo and meter in a note track move to the conductor even when the file has a conductor track", "[midiimportplan][import-plan][tempo-sync]")
{
    TrackBody conductor;
    conductor.ev (0, { 0xFF, 0x51, 0x03, 0x07, 0xA1, 0x20 }).eot();
    TrackBody notes;
    notes.ev (0, { 0x90, 60, 100 })
         .ev (96, { 0xFF, 0x51, 0x03, 0x09, 0x27, 0xC0 })
         .ev (0, { 0xFF, 0x58, 0x04, 0x03, 0x02, 0x18, 0x08 })
         .ev (0, { 0x80, 60, 0x40 }).eot();
    const auto p = parse (smf (1, 96, { conductor, notes }));

    const auto isTempoOrMeter = [] (const PlannedEvent& e)
    {
        return e.bytes.size() > 1 && e.bytes[0] == 0xFF && (e.bytes[1] == 0x51 || e.bytes[1] == 0x58);
    };

    Diagnostics diags;
    const auto plan = planMidiImport (p.song, p.raw, true, diags);

    int tempoCount = 0, meterCount = 0;
    for (const auto& e : plan.conductorEvents)
    {
        if (e.bytes.size() > 1 && e.bytes[1] == 0x51) ++tempoCount;
        if (e.bytes.size() > 1 && e.bytes[1] == 0x58) ++meterCount;
    }
    CHECK (tempoCount == 2);
    CHECK (meterCount == 1);
    REQUIRE (plan.tracks.size() == 1);
    for (const auto& e : plan.tracks[0].events)
        CHECK_FALSE (isTempoOrMeter (e));
    CHECK (plan.relocatedEventCount == 2);

    // A later import with the tempo map kept drops them instead.
    Diagnostics laterDiags;
    const auto later = planMidiImport (p.song, p.raw, false, laterDiags);
    CHECK (later.conductorEvents.empty());
    REQUIRE (later.tracks.size() == 1);
    for (const auto& e : later.tracks[0].events)
        CHECK_FALSE (isTempoOrMeter (e));
    CHECK (later.droppedEventCount >= 3);
}
