// Verifies PreviewPipeline::computePartPreview reuses the real engine
// (assembleInstruments + runPipeline) scoped to one part, and that the
// "unreferenced track" Info diagnostic InstrumentAssembly emits for tracks
// outside the scoped Config is filtered out of the result.

#include "UI/PreviewPipeline.h"
#include "UI/SongModelBridge.h"

#include "Core/LotroInstrument.h"

#include <catch2/catch_test_macros.hpp>

using namespace lotro;

namespace
{
    Note makeNote (int pitch, int startTick, int dur, int sourceTrackIndex, int sourceEventIndex)
    {
        Note n;
        n.pitch            = pitch;
        n.startTick        = startTick;
        n.durationTicks    = dur;
        n.velocity         = 100;
        n.isDrum           = false;
        n.sourceTrackIndex = sourceTrackIndex;
        n.sourceEventIndex = sourceEventIndex;
        return n;
    }

    // Imports `imported` and wires its single track to a new LuteOfAges
    // part, returning the minted partId.
    juce::int64 importOneTrackAsLuteOfAgesPart (SongDocument& doc, const Song& imported)
    {
        Diagnostics importDiags;
        appendImportedSong (doc, imported, 1, importDiags);

        auto track = doc.getTrack (doc.getNumTracks() - 1);
        auto trackId = (juce::int64) track.getProperty (SongIDs::trackId);

        auto part = doc.addPart ("LuteOfAges", "Lead");
        doc.addAssignment (part, trackId, 0, 0, "octaveShift");
        return (juce::int64) part.getProperty (SongIDs::partId);
    }
}

TEST_CASE ("PreviewPipeline: an in-range note passes through assembled and pipelined at the same pitch, with no unreferenced-track diagnostic", "[previewpipeline]")
{
    Song raw;
    raw.ticksPerQuarter = 480;
    Track t;
    t.name = "Track Zero";
    // 60 is inside LuteOfAges's native range (36..72) — nothing should fold.
    t.notes.push_back (makeNote (60, 0, 480, 0, 0));
    raw.tracks.push_back (t);

    SongDocument doc;
    const auto partId = importOneTrackAsLuteOfAgesPart (doc, raw);

    auto result = computePartPreview (doc, partId);

    REQUIRE (result.assembled.tracks.size() == 1);
    REQUIRE (result.assembled.tracks[0].notes.size() == 1);
    CHECK (result.assembled.tracks[0].notes[0].pitch == 60);

    REQUIRE (result.pipelined.tracks.size() == 1);
    REQUIRE (result.pipelined.tracks[0].notes.size() == 1);
    CHECK (result.pipelined.tracks[0].notes[0].pitch == 60);

    for (const auto& d : result.diagnostics)
        CHECK (d.message.find ("not referenced by any instrument") == std::string::npos);
}

TEST_CASE ("PreviewPipeline: a note outside the target instrument's range is folded by the real pipeline, so pipelined differs from assembled", "[previewpipeline]")
{
    Song raw;
    raw.ticksPerQuarter = 480;
    Track t;
    t.name = "Track Zero";
    // 20 is below LuteOfAges's native range (36..72); RangeConstraint folds
    // it up by whole octaves (20 -> 32 -> 44) to bring it into range.
    t.notes.push_back (makeNote (20, 0, 480, 0, 0));
    raw.tracks.push_back (t);

    SongDocument doc;
    const auto partId = importOneTrackAsLuteOfAgesPart (doc, raw);

    auto result = computePartPreview (doc, partId);

    REQUIRE (result.assembled.tracks.size() == 1);
    REQUIRE (result.assembled.tracks[0].notes.size() == 1);
    CHECK (result.assembled.tracks[0].notes[0].pitch == 20);

    REQUIRE (result.pipelined.tracks.size() == 1);
    REQUIRE (result.pipelined.tracks[0].notes.size() == 1);
    CHECK (result.pipelined.tracks[0].notes[0].pitch == 44);
    CHECK (result.pipelined.tracks[0].notes[0].pitch != result.assembled.tracks[0].notes[0].pitch);
}
