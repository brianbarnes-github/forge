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

    // The same notes (pitch, start, duration, velocity, channel), just on one track.
    auto notesOf = [] (const SongDocument& doc)
    {
        std::vector<std::tuple<int, int, int, int, int>> out;
        for (auto track : doc.getSourceMidiNode())
            for (auto n : SongDocument::getNotesNode (track))
                out.emplace_back ((int) n.getProperty (SongIDs::pitch), (int) n.getProperty (SongIDs::startTick),
                                  (int) n.getProperty (SongIDs::durationTicks), (int) n.getProperty (SongIDs::velocity),
                                  (int) n.getProperty (SongIDs::channel, 1));
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
