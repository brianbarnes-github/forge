// Import options end to end: tempo replace and track merge through importMidiFile,
// including the lossless export of a merged import.

#include "UI/MidiExport.h"
#include "UI/MidiImportPlan.h"
#include "UI/RawMidi.h"
#include "UI/SongModelBridge.h"
#include "MidiTestBytes.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <juce_core/juce_core.h>

#include <algorithm>
#include <tuple>
#include <utility>
#include <vector>

using namespace lotro;
using namespace miditest;

namespace
{
    struct TempMidi
    {
        juce::File file = juce::File::createTempFile (".mid");
        explicit TempMidi (const Bytes& bytes) { REQUIRE (file.replaceWithData (bytes.data(), bytes.size())); }
        ~TempMidi() { file.deleteFile(); }
    };

    // Tempo `microsPerQuarter` at tick 0 and, optionally, a second tempo at `changeTick`.
    TrackBody conductor (std::uint32_t microsPerQuarter, int changeTick = -1, std::uint32_t changeMicros = 0)
    {
        auto tempo = [] (std::uint32_t us) -> Bytes
        { return { 0xFF, 0x51, 0x03, (std::uint8_t) (us >> 16), (std::uint8_t) (us >> 8), (std::uint8_t) us }; };
        TrackBody c;
        const auto first = tempo (microsPerQuarter);
        c.ev (0, { first[0], first[1], first[2], first[3], first[4], first[5] });
        if (changeTick >= 0)
        {
            const auto second = tempo (changeMicros);
            c.ev ((std::uint32_t) changeTick, { second[0], second[1], second[2], second[3], second[4], second[5] });
        }
        c.eot (960);
        return c;
    }

    TrackBody melody (int pitch)
    {
        TrackBody t;
        t.ev (0, { 0x90, (std::uint8_t) pitch, 100 }).ev (96, { 0x80, (std::uint8_t) pitch, 0x40 }).eot();
        return t;
    }

    int noteTrackCount (const SongDocument& doc)
    {
        int n = 0;
        for (int i = 0; i < doc.getNumTracks(); ++i)
            if (SongDocument::isAssignableTrack (doc.getTrack (i)))
                ++n;
        return n;
    }

    bool containsMessage (const Diagnostics& diags, Severity severity, const std::string& text)
    {
        return std::any_of (diags.begin(), diags.end(), [&] (const Diagnostic& d)
                            { return d.severity == severity && d.message.find (text) != std::string::npos; });
    }

    constexpr std::uint32_t usFor120 = 500000, usFor100 = 600000;
}

TEST_CASE ("ImportOptions: tempo replace swaps the tempo map and conductor events, keep leaves them", "[import-options]")
{
    TempMidi first  (smf (1, 96, { conductor (usFor120), melody (60) }));
    TempMidi second (smf (1, 96, { conductor (usFor100), melody (64) }));

    SECTION ("replace")
    {
        SongDocument doc;
        Diagnostics d1, d2;
        REQUIRE (importMidiFile (doc, first.file, 1, d1));
        ImportOptions options;
        options.tempo = TempoMode::replace;
        REQUIRE (importMidiFile (doc, second.file, 2, d2, options));

        REQUIRE (doc.getTempoMapNode().getNumChildren() == 1);
        CHECK ((double) doc.getTempoMapNode().getChild (0).getProperty (SongIDs::bpm) == Catch::Approx (100.0));
        const auto events = SongDocument::getEventsNode (doc.getConductorTrack());
        REQUIRE (events.getNumChildren() == 1);   // the old conductor's event is gone, not duplicated
        CHECK (noteTrackCount (doc) == 2);        // both imports' tracks remain
        CHECK (containsMessage (d2, Severity::Info, "Replaced"));
        CHECK_FALSE (containsMessage (d2, Severity::Warning, "differs"));
    }

    SECTION ("keep (the default)")
    {
        SongDocument doc;
        Diagnostics d1, d2;
        REQUIRE (importMidiFile (doc, first.file, 1, d1));
        REQUIRE (importMidiFile (doc, second.file, 2, d2));

        CHECK ((double) doc.getTempoMapNode().getChild (0).getProperty (SongIDs::bpm) == Catch::Approx (120.0));
        CHECK (containsMessage (d2, Severity::Warning, "differs"));
    }
}

TEST_CASE ("ImportOptions: tempo replace rescales the file's ticks to the Song's time base", "[import-options]")
{
    // Song at 96 PPQ; the second file is 48 PPQ with a tempo change at its tick 48 (= 96 in the Song).
    TempMidi first  (smf (1, 96, { conductor (usFor120), melody (60) }));
    TempMidi second (smf (1, 48, { conductor (usFor120, 48, usFor100), melody (64) }));

    SongDocument doc;
    Diagnostics d1, d2;
    REQUIRE (importMidiFile (doc, first.file, 1, d1));
    ImportOptions options;
    options.tempo = TempoMode::replace;
    REQUIRE (importMidiFile (doc, second.file, 2, d2, options));

    CHECK ((int) doc.getSourceMidiNode().getProperty (SongIDs::ticksPerQuarter) == 96);
    const auto tempoMap = doc.getTempoMapNode();
    REQUIRE (tempoMap.getNumChildren() == 2);
    CHECK ((int) tempoMap.getChild (1).getProperty (SongIDs::tick) == 96);
    CHECK ((double) tempoMap.getChild (1).getProperty (SongIDs::bpm) == Catch::Approx (100.0));

    const auto events = SongDocument::getEventsNode (doc.getConductorTrack());
    REQUIRE (events.getNumChildren() == 2);
    CHECK ((int) events.getChild (1).getProperty (SongIDs::tick) == 96);
}

TEST_CASE ("ImportOptions: merged imports one track whose notes keep their channels and export losslessly", "[import-options]")
{
    TrackBody a;   // channel 1
    a.ev (0, { 0xFF, 0x03, 0x01, 'A' }).ev (0, { 0x90, 60, 100 }).ev (96, { 0x80, 60, 0x40 }).eot();
    TrackBody b;   // channel 3
    b.ev (0, { 0xFF, 0x03, 0x01, 'B' }).ev (0, { 0x92, 64, 90 }).ev (48, { 0x92, 67, 80 })
     .ev (48, { 0x82, 64, 0x40 }).ev (48, { 0x82, 67, 0x40 }).eot();
    const auto bytes = smf (1, 96, { conductor (usFor120), a, b });
    TempMidi tmp (bytes);

    SongDocument expanded, merged;
    Diagnostics de, dm;
    REQUIRE (importMidiFile (expanded, tmp.file, 1, de));
    ImportOptions options;
    options.tracks = TrackMode::merged;
    REQUIRE (importMidiFile (merged, tmp.file, 1, dm, options));

    CHECK (noteTrackCount (expanded) == 2);
    REQUIRE (noteTrackCount (merged) == 1);
    CHECK (merged.getNumTracks() == expanded.getNumTracks() - 1);

    juce::String name;
    for (int i = 0; i < merged.getNumTracks(); ++i)
        if (SongDocument::isAssignableTrack (merged.getTrack (i)))
            name = merged.getTrack (i).getProperty (SongIDs::name).toString();
    CHECK (name.endsWith (" (merged)"));

    // The same notes (pitch, start, duration, velocity, channel, source provenance), just on one track.
    auto notesOf = [] (const SongDocument& doc)
    {
        std::vector<std::tuple<int, int, int, int, int, int, int>> out;
        for (auto track : doc.getSourceMidiNode())
            for (auto n : SongDocument::getNotesNode (track))
                out.emplace_back ((int) n.getProperty (SongIDs::pitch), (int) n.getProperty (SongIDs::startTick),
                                  (int) n.getProperty (SongIDs::durationTicks), (int) n.getProperty (SongIDs::velocity),
                                  (int) n.getProperty (SongIDs::channel, 1),
                                  (int) n.getProperty (SongIDs::sourceTrackIndex),
                                  (int) n.getProperty (SongIDs::sourceEventIndex));
        std::sort (out.begin(), out.end());
        return out;
    };
    CHECK (notesOf (merged) == notesOf (expanded));

    // Export: conductor + one track holding exactly the original tracks' events.
    const auto original = readMidiBytes (bytes, "o");
    const auto exported = buildRawMidiFile (merged);
    REQUIRE (exported.tracks.size() == 2);
    std::vector<std::pair<int, Bytes>> want, got;   // compared as sorted multisets: same-tick order may differ
    for (size_t t = 1; t < original.tracks.size(); ++t)
        for (const auto& e : original.tracks[t].events)
            want.emplace_back (e.tick, e.bytes);
    for (const auto& e : exported.tracks[1].events)
        got.emplace_back (e.tick, e.bytes);
    std::sort (want.begin(), want.end());
    std::sort (got.begin(), got.end());
    CHECK (got == want);
    CHECK (std::is_sorted (exported.tracks[1].events.begin(), exported.tracks[1].events.end(),
                           [] (const RawMidiEvent& x, const RawMidiEvent& y) { return x.tick < y.tick; }));
}

TEST_CASE ("ImportOptions: merged on a single note track imports it unchanged", "[import-options]")
{
    TempMidi tmp (smf (1, 96, { conductor (usFor120), melody (60) }));
    SongDocument doc;
    Diagnostics diags;
    ImportOptions options;
    options.tracks = TrackMode::merged;
    REQUIRE (importMidiFile (doc, tmp.file, 1, diags, options));

    REQUIRE (noteTrackCount (doc) == 1);
    for (int i = 0; i < doc.getNumTracks(); ++i)
        if (SongDocument::isAssignableTrack (doc.getTrack (i)))
            CHECK_FALSE (doc.getTrack (i).getProperty (SongIDs::name).toString().endsWith (" (merged)"));
}

TEST_CASE ("ImportOptions: tempo replace at a different PPQ raises the Song to the LCM and rescales everything consistently", "[import-options]")
{
    // Song at 96 PPQ; the replacing file is 64 PPQ with a tempo change at its tick 64. lcm(96, 64) = 192:
    // the existing Song scales by 2, the incoming file by 3.
    TempMidi first  (smf (1, 96, { conductor (usFor120), melody (60) }));
    TempMidi second (smf (1, 64, { conductor (usFor120, 64, usFor100), melody (64) }));

    SongDocument doc;
    Diagnostics d1, d2;
    REQUIRE (importMidiFile (doc, first.file, 1, d1));
    ImportOptions options;
    options.tempo = TempoMode::replace;
    REQUIRE (importMidiFile (doc, second.file, 2, d2, options));

    CHECK ((int) doc.getSourceMidiNode().getProperty (SongIDs::ticksPerQuarter) == 192);

    const auto tempoMap = doc.getTempoMapNode();
    REQUIRE (tempoMap.getNumChildren() == 2);
    CHECK ((int) tempoMap.getChild (1).getProperty (SongIDs::tick) == 192);   // 64 * 3
    CHECK ((double) tempoMap.getChild (1).getProperty (SongIDs::bpm) == Catch::Approx (100.0));

    // Conductor events and endTick come from the replacing file, scaled by 3 (eot is 960 ticks after tick 64).
    const auto conductorTrack = doc.getConductorTrack();
    const auto events = SongDocument::getEventsNode (conductorTrack);
    REQUIRE (events.getNumChildren() == 2);
    CHECK ((int) events.getChild (0).getProperty (SongIDs::tick) == 0);
    CHECK ((int) events.getChild (1).getProperty (SongIDs::tick) == 192);
    CHECK ((int) conductorTrack.getProperty (SongIDs::endTick) == (64 + 960) * 3);

    // The earlier import's note (tick 0, 96 long at 96 PPQ) was scaled by 2; the new file's (96 long at 64 PPQ) by 3.
    std::vector<std::pair<int, int>> notes;   // (startTick, durationTicks) per note-track, in row order
    for (int i = 0; i < doc.getNumTracks(); ++i)
        if (SongDocument::isAssignableTrack (doc.getTrack (i)))
        {
            const auto n = SongDocument::getNotesNode (doc.getTrack (i)).getChild (0);
            notes.emplace_back ((int) n.getProperty (SongIDs::startTick), (int) n.getProperty (SongIDs::durationTicks));
        }
    REQUIRE (notes.size() == 2);
    CHECK (notes[0] == std::make_pair (0, 192));
    CHECK (notes[1] == std::make_pair (0, 288));
}

TEST_CASE ("ImportOptions: tempo replace clears the meter map and takes the new file's", "[import-options]")
{
    auto meterConductor = [] (std::uint8_t numerator, std::uint8_t denominatorPow2) -> TrackBody
    {
        TrackBody c;
        c.ev (0, { 0xFF, 0x51, 0x03, 0x07, 0xA1, 0x20 })
         .ev (0, { 0xFF, 0x58, 0x04, numerator, denominatorPow2, 0x18, 0x08 })
         .eot (960);
        return c;
    };
    TempMidi first  (smf (1, 96, { meterConductor (3, 2), melody (60) }));   // 3/4
    TempMidi second (smf (1, 96, { meterConductor (6, 3), melody (64) }));   // 6/8

    SongDocument doc;
    Diagnostics d1, d2;
    REQUIRE (importMidiFile (doc, first.file, 1, d1));
    REQUIRE (doc.getMeterMapNode().getNumChildren() == 1);
    CHECK ((int) doc.getMeterMapNode().getChild (0).getProperty (SongIDs::numerator) == 3);

    ImportOptions options;
    options.tempo = TempoMode::replace;
    REQUIRE (importMidiFile (doc, second.file, 2, d2, options));

    const auto meterMap = doc.getMeterMapNode();
    REQUIRE (meterMap.getNumChildren() == 1);   // the 3/4 entry is gone, not appended after
    CHECK ((int) meterMap.getChild (0).getProperty (SongIDs::tick) == 0);
    CHECK ((int) meterMap.getChild (0).getProperty (SongIDs::numerator) == 6);
    CHECK ((int) meterMap.getChild (0).getProperty (SongIDs::denominator) == 8);
}

TEST_CASE ("ImportOptions: a diagnostic from a folded-in track lands on the merged row", "[import-options]")
{
    TrackBody a;   // a clean first note track
    a.ev (0, { 0x90, 60, 100 }).ev (96, { 0x80, 60, 0x40 }).eot();
    TrackBody b;   // the second note track also holds a note-on (pitch 70) that is never released
    b.ev (0, { 0x90, 64, 90 }).ev (0, { 0x90, 70, 90 }).ev (96, { 0x80, 64, 0x40 }).eot();
    TempMidi tmp (smf (1, 96, { conductor (usFor120), a, b }));

    auto rowOfDiagnostic = [&] (const ImportOptions& options, SongDocument& doc, int& noteRow)
    {
        Diagnostics diags;
        REQUIRE (importMidiFile (doc, tmp.file, 1, diags, options));
        noteRow = -1;
        for (int i = doc.getNumTracks() - 1; i >= 0; --i)
            if (SongDocument::isAssignableTrack (doc.getTrack (i)))
                noteRow = i;   // first note row
        const auto it = std::find_if (diags.begin(), diags.end(), [] (const Diagnostic& d)
                                      { return d.message.find ("Unmatched note-on") != std::string::npos; });
        REQUIRE (it != diags.end());
        return it->trackIndex;
    };

    SongDocument expanded, merged;
    int expandedFirstRow = -1, mergedRow = -1;
    const int expandedIndex = rowOfDiagnostic ({}, expanded, expandedFirstRow);
    CHECK (expandedIndex == expandedFirstRow + 1);   // sanity: on the second track's row when expanded

    ImportOptions options;
    options.tracks = TrackMode::merged;
    const int mergedIndex = rowOfDiagnostic (options, merged, mergedRow);
    REQUIRE (noteTrackCount (merged) == 1);
    CHECK (mergedIndex == mergedRow);   // the merged row, not -1 and not a stale second-track row
}

TEST_CASE ("ImportOptions: merged export keeps same-tick events in (raw track, original index) order", "[import-options]")
{
    // Both tracks have events on ticks 0 and 96. Track a uses channel 3 (status 0x92/0x82) and track b channel 1
    // (0x90/0x80), b's tick-0 note-ons descend in pitch, and a's tick-0 note-on has a higher index than b's first, so neither byte order nor pitch order equals
    // the required (track, index) order.
    TrackBody a;
    a.ev (0, { 0xC2, 5 }).ev (0, { 0x92, 72, 100 }).ev (96, { 0x82, 72, 0x40 }).eot();   // a's note-on has index 1
    TrackBody b;
    b.ev (0, { 0x90, 67, 90 }).ev (0, { 0x90, 60, 80 }).ev (96, { 0x80, 67, 0x40 }).ev (0, { 0x80, 60, 0x40 }).eot();
    const auto bytes = smf (1, 96, { conductor (usFor120), a, b });
    TempMidi tmp (bytes);

    SongDocument merged;
    Diagnostics diags;
    ImportOptions options;
    options.tracks = TrackMode::merged;
    REQUIRE (importMidiFile (merged, tmp.file, 1, diags, options));
    REQUIRE (noteTrackCount (merged) == 1);

    const auto original = readMidiBytes (bytes, "o");
    const auto exported = buildRawMidiFile (merged);
    REQUIRE (exported.tracks.size() == 2);

    // (tick, raw track, original index, bytes), sorted by the first three.
    std::vector<std::tuple<int, size_t, size_t, Bytes>> ordered;
    for (size_t t = 1; t < original.tracks.size(); ++t)
        for (size_t i = 0; i < original.tracks[t].events.size(); ++i)
            ordered.emplace_back (original.tracks[t].events[i].tick, t, i, original.tracks[t].events[i].bytes);
    std::sort (ordered.begin(), ordered.end(), [] (const auto& x, const auto& y)
               { return std::tie (std::get<0> (x), std::get<1> (x), std::get<2> (x))
                      < std::tie (std::get<0> (y), std::get<1> (y), std::get<2> (y)); });

    std::vector<std::pair<int, Bytes>> want, got;
    for (const auto& o : ordered)
        want.emplace_back (std::get<0> (o), std::get<3> (o));
    for (const auto& e : exported.tracks[1].events)
        got.emplace_back (e.tick, e.bytes);
    CHECK (got == want);   // sequence equality, not multiset
}
