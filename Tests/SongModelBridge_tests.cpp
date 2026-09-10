// Verifies SongModelBridge's ValueTree <-> forge_core plain-struct
// translation: appendImportedSong (import path) and buildConfigAndRawSong
// (export path), including the trackId -> positional-index seam that
// Config.midiTrackIndex depends on.

#include "UI/SongModelBridge.h"

#include <catch2/catch_test_macros.hpp>

using namespace lotro;

namespace
{
    Note makeNote (int pitch, int startTick, int dur, int velocity, bool isDrum,
                   int sourceTrackIndex, int sourceEventIndex)
    {
        Note n;
        n.pitch            = pitch;
        n.startTick        = startTick;
        n.durationTicks    = dur;
        n.velocity         = velocity;
        n.isDrum           = isDrum;
        n.sourceTrackIndex = sourceTrackIndex;
        n.sourceEventIndex = sourceEventIndex;
        return n;
    }

    Song threeTrackImportedSong()
    {
        Song s;
        s.ticksPerQuarter = 480;
        s.tempoMap = { { 0, 120.0 }, { 1920, 140.0 } };
        s.meterMap = { { 0, 4, 4 }, { 1920, 3, 4 } };

        Track t0;
        t0.name              = "Track Zero";
        t0.sourceMidiChannel = 0;
        t0.notes.push_back (makeNote (60, 0, 480, 100, false, 0, 0));
        t0.notes.push_back (makeNote (62, 480, 480, 90, false, 0, 1));
        s.tracks.push_back (t0);

        Track t1;
        t1.name              = "Track One";
        t1.sourceMidiChannel = 1;
        t1.notes.push_back (makeNote (64, 0, 240, 80, false, 1, 0));
        s.tracks.push_back (t1);

        Track t2;
        t2.name              = "Drum Track";
        t2.sourceMidiChannel = 9;
        t2.notes.push_back (makeNote (36, 0, 120, 127, true, 2, 0));
        t2.notes.push_back (makeNote (38, 120, 120, 100, true, 2, 1));
        t2.notes.push_back (makeNote (42, 240, 60, 60, true, 2, 2));
        s.tracks.push_back (t2);

        return s;
    }

    // Distinct values from threeTrackImportedSong() throughout (track names,
    // tempo/meter tick+value pairs) so a bug that clears-instead-of-appends
    // across multiple appendImportedSong calls is detectable rather than
    // masked by coincidentally-matching data.
    Song twoTrackImportedSongSecondBatch()
    {
        Song s;
        s.ticksPerQuarter = 480;
        s.tempoMap = { { 3840, 200.0 } };
        s.meterMap = { { 3840, 7, 8 } };

        Track t0;
        t0.name              = "Second Import Track Zero";
        t0.sourceMidiChannel = 2;
        t0.notes.push_back (makeNote (70, 0, 240, 70, false, 0, 0));
        s.tracks.push_back (t0);

        Track t1;
        t1.name              = "Second Import Track One";
        t1.sourceMidiChannel = 3;
        t1.notes.push_back (makeNote (72, 0, 240, 70, false, 1, 0));
        s.tracks.push_back (t1);

        return s;
    }
}

TEST_CASE ("SongModelBridge: appendImportedSong then buildConfigAndRawSong round-trips tracks/notes/tempoMap/meterMap byte-for-byte", "[songmodelbridge]")
{
    const auto imported = threeTrackImportedSong();

    SongDocument doc;
    appendImportedSong (doc, imported, 1);

    auto built = buildConfigAndRawSong (doc);

    REQUIRE (built.rawSong.tracks.size() == imported.tracks.size());
    for (size_t i = 0; i < imported.tracks.size(); ++i)
    {
        CHECK (built.rawSong.tracks[i].name == imported.tracks[i].name);
        CHECK (built.rawSong.tracks[i].sourceMidiChannel == imported.tracks[i].sourceMidiChannel);

        REQUIRE (built.rawSong.tracks[i].notes.size() == imported.tracks[i].notes.size());
        for (size_t n = 0; n < imported.tracks[i].notes.size(); ++n)
        {
            const auto& a = built.rawSong.tracks[i].notes[n];
            const auto& b = imported.tracks[i].notes[n];
            CHECK (a.pitch == b.pitch);
            CHECK (a.startTick == b.startTick);
            CHECK (a.durationTicks == b.durationTicks);
            CHECK (a.velocity == b.velocity);
            CHECK (a.isDrum == b.isDrum);
            CHECK (a.sourceTrackIndex == b.sourceTrackIndex);
            CHECK (a.sourceEventIndex == b.sourceEventIndex);
        }
    }

    REQUIRE (built.rawSong.tempoMap.size() == imported.tempoMap.size());
    for (size_t i = 0; i < imported.tempoMap.size(); ++i)
    {
        CHECK (built.rawSong.tempoMap[i].tick == imported.tempoMap[i].tick);
        CHECK (built.rawSong.tempoMap[i].bpm == imported.tempoMap[i].bpm);
    }

    REQUIRE (built.rawSong.meterMap.size() == imported.meterMap.size());
    for (size_t i = 0; i < imported.meterMap.size(); ++i)
    {
        CHECK (built.rawSong.meterMap[i].tick == imported.meterMap[i].tick);
        CHECK (built.rawSong.meterMap[i].numerator == imported.meterMap[i].numerator);
        CHECK (built.rawSong.meterMap[i].denominator == imported.meterMap[i].denominator);
    }

    CHECK (built.rawSong.ticksPerQuarter == imported.ticksPerQuarter);
}

TEST_CASE ("SongModelBridge: ConfigSource.midiTrackIndex resolves trackId to the track's positional index, not its trackId value", "[songmodelbridge]")
{
    SongDocument doc;
    auto trackA = doc.addTrackBulk ("A", 0, 0, 0);
    auto trackB = doc.addTrackBulk ("B", 0, 1, 0);
    auto trackC = doc.addTrackBulk ("C", 0, 2, 0);

    auto idA = (juce::int64) trackA.getProperty (SongIDs::trackId);
    auto idB = (juce::int64) trackB.getProperty (SongIDs::trackId);
    auto idC = (juce::int64) trackC.getProperty (SongIDs::trackId);

    // Prove trackIds are NOT 0,1,2 (the document's monotonic counter,
    // same proof pattern as SongDocument_tests.cpp) — if a bug read
    // trackId as if it were the positional index directly, this would
    // still coincidentally pass unless the ids diverge from 0-based
    // positions. addTrackBulk mints starting at 1, so idA==1 aligns with
    // position 0 — offset by one from position, catching an "assumed
    // trackId==index" bug.
    REQUIRE (idA != 0);
    REQUIRE (idB != 1);
    REQUIRE (idC != 2);

    auto part = doc.addPart ("LuteOfAges", "Lead");
    doc.addAssignment (part, idB, 0, 100, "octaveShift");

    auto built = buildConfigAndRawSong (doc);

    REQUIRE (built.config.instruments.size() == 1);
    REQUIRE (built.config.instruments[0].sources.size() == 1);
    CHECK (built.config.instruments[0].sources[0].midiTrackIndex == 1);
}

TEST_CASE ("SongModelBridge: title/transcriber/tempo are nullopt when absent, populated when set (tempo stays a double)", "[songmodelbridge]")
{
    {
        SongDocument doc;
        auto built = buildConfigAndRawSong (doc);
        CHECK (! built.config.title.has_value());
        CHECK (! built.config.transcriber.has_value());
        CHECK (! built.config.tempo.has_value());
    }
    {
        SongDocument doc;
        doc.getTree().setProperty (SongIDs::title, juce::String ("My Song"), nullptr);
        doc.getTree().setProperty (SongIDs::transcriber, juce::String ("Someone"), nullptr);
        doc.getTree().setProperty (SongIDs::tempoBpm, 123.5, nullptr);

        auto built = buildConfigAndRawSong (doc);
        REQUIRE (built.config.title.has_value());
        CHECK (*built.config.title == "My Song");
        REQUIRE (built.config.transcriber.has_value());
        CHECK (*built.config.transcriber == "Someone");
        REQUIRE (built.config.tempo.has_value());
        CHECK (*built.config.tempo == 123.5);
    }
}

TEST_CASE ("SongModelBridge: PART label/drumMap empty string maps to nullopt, non-empty maps to populated optional", "[songmodelbridge]")
{
    SongDocument doc;
    auto part = doc.addPart ("LuteOfAges", "");
    // addPart leaves drumMapPath as empty string by default too.

    auto built = buildConfigAndRawSong (doc);
    REQUIRE (built.config.instruments.size() == 1);
    CHECK (! built.config.instruments[0].label.has_value());
    CHECK (! built.config.instruments[0].drumMap.has_value());

    doc.setProperty (part, SongIDs::label, juce::String ("Lead"));
    doc.setProperty (part, SongIDs::drumMapPath, juce::String ("/some/drum_map.json"));

    auto built2 = buildConfigAndRawSong (doc);
    REQUIRE (built2.config.instruments.size() == 1);
    REQUIRE (built2.config.instruments[0].label.has_value());
    CHECK (*built2.config.instruments[0].label == "Lead");
    REQUIRE (built2.config.instruments[0].drumMap.has_value());
    CHECK (*built2.config.instruments[0].drumMap == "/some/drum_map.json");
}

TEST_CASE ("SongModelBridge: partIds filter selects only the requested subset", "[songmodelbridge]")
{
    SongDocument doc;
    auto p1 = doc.addPart ("LuteOfAges", "One");
    auto p2 = doc.addPart ("Harp", "Two");
    auto p3 = doc.addPart ("Drums", "Three");

    auto id1 = (juce::int64) p1.getProperty (SongIDs::partId);
    auto id2 = (juce::int64) p2.getProperty (SongIDs::partId);
    auto id3 = (juce::int64) p3.getProperty (SongIDs::partId);
    juce::ignoreUnused (id1, id3);

    auto allParts = buildConfigAndRawSong (doc);
    CHECK (allParts.config.instruments.size() == 3);

    auto onlyOne = buildConfigAndRawSong (doc, { id2 });
    REQUIRE (onlyOne.config.instruments.size() == 1);
    CHECK (onlyOne.config.instruments[0].name == "Harp");
}

TEST_CASE ("SongModelBridge: an Assignment referencing a removed/dangling trackId is silently omitted from sources", "[songmodelbridge]")
{
    SongDocument doc;
    auto track = doc.addTrackBulk ("A", 0, 0, 0);
    auto trackId = (juce::int64) track.getProperty (SongIDs::trackId);

    auto part = doc.addPart ("LuteOfAges", "Lead");
    doc.addAssignment (part, trackId, 0, 100, "octaveShift");

    // Remove the track the assignment references — dangling reference.
    doc.removeTrack (trackId);

    auto built = buildConfigAndRawSong (doc);
    REQUIRE (built.config.instruments.size() == 1);
    CHECK (built.config.instruments[0].sources.empty());
}

TEST_CASE ("SongModelBridge: rawSong.title derives from inputMidiPath's filename without extension; empty path leaves title empty", "[songmodelbridge]")
{
    {
        SongDocument doc;
        doc.getTree().setProperty (SongIDs::inputMidiPath, juce::String ("/some/path/My Song.mid"), nullptr);
        auto built = buildConfigAndRawSong (doc);
        CHECK (built.rawSong.title == "My Song");
    }
    {
        SongDocument doc;
        auto built = buildConfigAndRawSong (doc);
        CHECK (built.rawSong.title == "");
    }
}

TEST_CASE ("SongModelBridge: two appendImportedSong calls accumulate tracks/tempoMap/meterMap instead of the second clearing the first", "[songmodelbridge]")
{
    const auto firstImport  = threeTrackImportedSong();
    const auto secondImport = twoTrackImportedSongSecondBatch();

    SongDocument doc;
    appendImportedSong (doc, firstImport, 1);
    appendImportedSong (doc, secondImport, 2);

    // Tracks: both imports' tracks present, second import continues after
    // the first rather than replacing it.
    REQUIRE (doc.getNumTracks() == (int) (firstImport.tracks.size() + secondImport.tracks.size()));
    CHECK (doc.getTrack (0).getProperty (SongIDs::name).toString() == "Track Zero");
    CHECK (doc.getTrack (2).getProperty (SongIDs::name).toString() == "Drum Track");
    CHECK (doc.getTrack (3).getProperty (SongIDs::name).toString() == "Second Import Track Zero");
    CHECK (doc.getTrack (4).getProperty (SongIDs::name).toString() == "Second Import Track One");

    // TEMPO_MAP/METER_MAP: both imports' entries present, not just the
    // second's — a clear-then-append bug would leave only 1 child each.
    auto tempoMapNode = doc.getTempoMapNode();
    auto meterMapNode = doc.getMeterMapNode();
    REQUIRE (tempoMapNode.getNumChildren() == (int) (firstImport.tempoMap.size() + secondImport.tempoMap.size()));
    REQUIRE (meterMapNode.getNumChildren() == (int) (firstImport.meterMap.size() + secondImport.meterMap.size()));

    CHECK ((int) tempoMapNode.getChild (0).getProperty (SongIDs::tick) == 0);
    CHECK ((double) tempoMapNode.getChild (0).getProperty (SongIDs::bpm) == 120.0);
    CHECK ((int) tempoMapNode.getChild (2).getProperty (SongIDs::tick) == 3840);
    CHECK ((double) tempoMapNode.getChild (2).getProperty (SongIDs::bpm) == 200.0);

    CHECK ((int) meterMapNode.getChild (0).getProperty (SongIDs::tick) == 0);
    CHECK ((int) meterMapNode.getChild (2).getProperty (SongIDs::numerator) == 7);
    CHECK ((int) meterMapNode.getChild (2).getProperty (SongIDs::denominator) == 8);

    // buildConfigAndRawSong sees both imports' tempoMap/meterMap entries,
    // and the second import's tracks are positioned after the first's
    // (index 3, 4) rather than restarting at 0.
    auto built = buildConfigAndRawSong (doc);
    REQUIRE (built.rawSong.tempoMap.size() == 3);
    CHECK (built.rawSong.tempoMap[2].bpm == 200.0);
    REQUIRE (built.rawSong.meterMap.size() == 3);
    CHECK (built.rawSong.meterMap[2].numerator == 7);

    auto secondBatchTrack = doc.getTrack (3);
    auto secondBatchTrackId = (juce::int64) secondBatchTrack.getProperty (SongIDs::trackId);

    auto part = doc.addPart ("LuteOfAges", "Lead");
    doc.addAssignment (part, secondBatchTrackId, 0, 100, "octaveShift");

    auto builtWithAssignment = buildConfigAndRawSong (doc);
    REQUIRE (builtWithAssignment.config.instruments.size() == 1);
    REQUIRE (builtWithAssignment.config.instruments[0].sources.size() == 1);
    CHECK (builtWithAssignment.config.instruments[0].sources[0].midiTrackIndex == 3);
}
