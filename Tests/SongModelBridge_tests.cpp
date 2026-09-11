// Verifies SongModelBridge's ValueTree <-> forge_core plain-struct
// translation: appendImportedSong (import path) and buildConfigAndRawSong
// (export path), including the trackId -> positional-index seam that
// Config.midiTrackIndex depends on.

#include "UI/SongModelBridge.h"

#include "Core/LotroInstrument.h"

#include <catch2/catch_test_macros.hpp>

#include <string>

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
        // Deliberately NOT 480 (SongDocument's constructor default) — a
        // fixture matching the default would let a bridge that drops
        // ticksPerQuarter entirely pass by coincidence.
        s.ticksPerQuarter = 960;
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
    Diagnostics diagnostics;
    appendImportedSong (doc, imported, 1, diagnostics);

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

// This test pins two different things per Ruling 1 (Phase 3): tracks still
// *accumulate* across imports (second import's tracks are added alongside
// the first's, positional indices continuing rather than restarting), but
// TEMPO_MAP/METER_MAP belong to the FIRST import only — a second import's
// tempo/meter entries are never appended/concatenated, since a single
// document can only have one sorted timeline (TempoCollapse assumes it).
// This supersedes the previous "both imports' entries accumulate" pin: that
// was ordering-only scaffolding, not a claim that concatenating two files'
// timelines is semantically meaningful (it isn't).
TEST_CASE ("SongModelBridge: two appendImportedSong calls accumulate tracks but keep only the first import's tempoMap/meterMap (Ruling 1)", "[songmodelbridge]")
{
    const auto firstImport  = threeTrackImportedSong();
    const auto secondImport = twoTrackImportedSongSecondBatch();

    SongDocument doc;
    Diagnostics diagnostics;
    appendImportedSong (doc, firstImport, 1, diagnostics);
    appendImportedSong (doc, secondImport, 2, diagnostics);

    // Tracks: both imports' tracks present, second import continues after
    // the first rather than replacing it.
    REQUIRE (doc.getNumTracks() == (int) (firstImport.tracks.size() + secondImport.tracks.size()));
    CHECK (doc.getTrack (0).getProperty (SongIDs::name).toString() == "Track Zero");
    CHECK (doc.getTrack (2).getProperty (SongIDs::name).toString() == "Drum Track");
    CHECK (doc.getTrack (3).getProperty (SongIDs::name).toString() == "Second Import Track Zero");
    CHECK (doc.getTrack (4).getProperty (SongIDs::name).toString() == "Second Import Track One");

    // TEMPO_MAP/METER_MAP: only the FIRST import's entries are present — the
    // second import's differing tempo/meter (Ruling 1) never got appended. A
    // bug that concatenates both would leave 3 children each instead of 2.
    auto tempoMapNode = doc.getTempoMapNode();
    auto meterMapNode = doc.getMeterMapNode();
    REQUIRE (tempoMapNode.getNumChildren() == (int) firstImport.tempoMap.size());
    REQUIRE (meterMapNode.getNumChildren() == (int) firstImport.meterMap.size());

    CHECK ((int) tempoMapNode.getChild (0).getProperty (SongIDs::tick) == 0);
    CHECK ((double) tempoMapNode.getChild (0).getProperty (SongIDs::bpm) == 120.0);
    CHECK ((int) tempoMapNode.getChild (1).getProperty (SongIDs::tick) == 1920);
    CHECK ((double) tempoMapNode.getChild (1).getProperty (SongIDs::bpm) == 140.0);

    CHECK ((int) meterMapNode.getChild (0).getProperty (SongIDs::tick) == 0);
    CHECK ((int) meterMapNode.getChild (1).getProperty (SongIDs::numerator) == 3);
    CHECK ((int) meterMapNode.getChild (1).getProperty (SongIDs::denominator) == 4);

    // buildConfigAndRawSong sees only the first import's tempoMap/meterMap
    // entries, and the second import's tracks are positioned after the
    // first's (index 3, 4) rather than restarting at 0.
    auto built = buildConfigAndRawSong (doc);
    REQUIRE (built.rawSong.tempoMap.size() == 2);
    CHECK (built.rawSong.tempoMap[1].bpm == 140.0);
    REQUIRE (built.rawSong.meterMap.size() == 2);
    CHECK (built.rawSong.meterMap[1].numerator == 3);

    // ticksPerQuarter: only the first import sets the document's time base
    // (960, from threeTrackImportedSong()) — the second import's differing
    // value (480, from twoTrackImportedSongSecondBatch()) is silently NOT
    // applied, proving "first import wins" rather than "last write wins".
    CHECK (built.rawSong.ticksPerQuarter == 960);

    auto secondBatchTrack = doc.getTrack (3);
    auto secondBatchTrackId = (juce::int64) secondBatchTrack.getProperty (SongIDs::trackId);

    auto part = doc.addPart ("LuteOfAges", "Lead");
    doc.addAssignment (part, secondBatchTrackId, 0, 100, "octaveShift");

    auto builtWithAssignment = buildConfigAndRawSong (doc);
    REQUIRE (builtWithAssignment.config.instruments.size() == 1);
    REQUIRE (builtWithAssignment.config.instruments[0].sources.size() == 1);
    CHECK (builtWithAssignment.config.instruments[0].sources[0].midiTrackIndex == 3);
}

// R1 also requires a diagnostic when a later import's (rescaled) tempo/meter
// timeline differs from what the document already holds. firstImport's PPQ
// (960) and secondImport's (480) differ too, so this exercises both R2 (the
// rescale is an exact x2 upscale -> Info) and R1 (the timelines themselves
// differ in both entry count and values -> Warning) from the same call.
TEST_CASE ("SongModelBridge: a later import's differing tempo/meter timeline emits one Warning Diagnostic naming SongModelBridge as the source", "[songmodelbridge]")
{
    const auto firstImport  = threeTrackImportedSong();
    const auto secondImport = twoTrackImportedSongSecondBatch();

    SongDocument doc;
    Diagnostics diagnostics;
    appendImportedSong (doc, firstImport, 1, diagnostics);
    REQUIRE (diagnostics.empty());

    appendImportedSong (doc, secondImport, 2, diagnostics);

    int infoCount = 0, warningCount = 0;
    for (const auto& d : diagnostics)
    {
        if (d.source != "SongModelBridge") continue;
        if (d.severity == Severity::Info) ++infoCount;
        else if (d.severity == Severity::Warning) ++warningCount;
    }

    // Exactly one rescale diagnostic (Info: 960/480 is an exact x2 upscale)
    // and exactly one timeline diagnostic (Warning: secondImport's tempo/
    // meter maps deliberately differ from firstImport's, both in entry
    // count and in tick/bpm/numerator/denominator values).
    CHECK (infoCount == 1);
    CHECK (warningCount == 1);
}

// The real-fixture round-trip tests only ever exercise an exact rescale
// (their PPQ ratios all happen to divide evenly), so the "Warning when
// roundedValueCount > 0" branch of the rescale diagnostic was never taken by
// any test with real data. This synthetic fixture uses a 3:2 ratio and a
// startTick of 7 (7*3 = 21, not divisible by 2) specifically so the rescale
// is inexact, independent of any MIDI file's coincidental tick alignment.
//
// This is also the load-bearing test for std::lround's rounding mode
// (round-half-away-from-zero): the hand-computed expectations 11 and 9 below
// can only be satisfied by that rounding mode, not by truncation. The
// Tests/SongsmithRoundTrip_tests.cpp helpers of the same name reuse the
// bridge's own formula rather than an independently-derived one, so they
// cannot by themselves catch a wrong rounding mode — this test is what does.
TEST_CASE ("SongModelBridge: an inexact rescale emits a Warning naming the correct rounded-value count", "[songmodelbridge]")
{
    Song first;
    first.ticksPerQuarter = 3;
    first.tempoMap = { { 0, 120.0 } };
    first.meterMap = { { 0, 4, 4 } };
    Track t0;
    t0.name              = "First Track";
    t0.sourceMidiChannel = 0;
    t0.notes.push_back (makeNote (60, 0, 3, 100, false, 0, 0));
    first.tracks.push_back (t0);

    Song second;
    second.ticksPerQuarter = 2;
    // Same tempo/meter maps as `first` (values unchanged, tick 0 rescales to
    // 0 either way) so the only diagnostic in play is the rescale one, not
    // R1's timeline-diff Warning.
    second.tempoMap = { { 0, 120.0 } };
    second.meterMap = { { 0, 4, 4 } };
    Track t1;
    t1.name              = "Second Track";
    t1.sourceMidiChannel = 1;
    // startTick 7 at a 3:2 ratio: (7*3) % 2 == 1 -> inexact, rounds to 11.
    // durationTicks 6 at the same ratio: (6*3) % 2 == 0 -> exact, rounds to 9.
    t1.notes.push_back (makeNote (61, 7, 6, 90, false, 1, 0));
    second.tracks.push_back (t1);

    SongDocument doc;
    Diagnostics diag1;
    appendImportedSong (doc, first, 1, diag1);
    REQUIRE (diag1.empty());

    Diagnostics diag2;
    appendImportedSong (doc, second, 2, diag2);

    REQUIRE (diag2.size() == 1);
    CHECK (diag2[0].source == "SongModelBridge");
    CHECK (diag2[0].severity == Severity::Warning);
    CHECK (diag2[0].message.find ("1 value(s) rounded") != std::string::npos);

    auto secondTrackTree = doc.getTrack (1);
    REQUIRE (secondTrackTree.getNumChildren() == 1);
    auto noteTree = secondTrackTree.getChild (0);
    CHECK ((int) noteTree.getProperty (SongIDs::startTick) == 11);
    CHECK ((int) noteTree.getProperty (SongIDs::durationTicks) == 9);
}

// A lossy downscale can round a note's durationTicks all the way to 0.
// DurationConstraint (Source/Core/Constraints/DurationConstraint.cpp) later
// drops zero-duration notes silently, without a Diagnostic of its own, so the
// bridge must name the loss at rescale time or the note vanishes from the
// ABC with no signal anywhere. This fixture picks a 1:8 ratio (document PPQ
// 1, incoming PPQ 8) specifically so one note's duration rounds to 0
// (3 * 1 / 8 = 0.375 -> lround 0) while another survives (8 * 1 / 8 = 1,
// exact) — the bridge must count and report the dropped note, not clamp its
// duration to invent a value, and must not delete it itself (that is
// DurationConstraint's job, later in the pipeline).
TEST_CASE ("SongModelBridge: a rescale that zeroes a note's duration names the dropped-note count as a Warning, without deleting the note", "[songmodelbridge]")
{
    Song first;
    first.ticksPerQuarter = 1;
    first.tempoMap = { { 0, 120.0 } };
    first.meterMap = { { 0, 4, 4 } };
    Track t0;
    t0.name              = "First Track";
    t0.sourceMidiChannel = 0;
    t0.notes.push_back (makeNote (60, 0, 1, 100, false, 0, 0));
    first.tracks.push_back (t0);

    Song second;
    second.ticksPerQuarter = 8;
    // Same tempo/meter as `first` (both rescale tick 0 -> 0) so the only
    // diagnostic in play is the rescale one, not R1's timeline-diff Warning.
    second.tempoMap = { { 0, 120.0 } };
    second.meterMap = { { 0, 4, 4 } };
    Track t1;
    t1.name              = "Second Track";
    t1.sourceMidiChannel = 1;
    // durationTicks 3 at a 1:8 ratio: lround(3/8) == 0 -> zeroed, dropped later.
    t1.notes.push_back (makeNote (61, 0, 3, 90, false, 1, 0));
    // durationTicks 8 at the same ratio: lround(8/8) == 1, exact -> survives.
    t1.notes.push_back (makeNote (62, 8, 8, 80, false, 1, 1));
    second.tracks.push_back (t1);

    SongDocument doc;
    Diagnostics diag1;
    appendImportedSong (doc, first, 1, diag1);
    REQUIRE (diag1.empty());

    Diagnostics diag2;
    appendImportedSong (doc, second, 2, diag2);

    REQUIRE (diag2.size() == 1);
    CHECK (diag2[0].source == "SongModelBridge");
    CHECK (diag2[0].severity == Severity::Warning);
    CHECK (diag2[0].message.find ("1 value(s) rounded") != std::string::npos);
    CHECK (diag2[0].message.find ("1 note(s) reduced to zero length and will be dropped") != std::string::npos);

    // The bridge does NOT delete the zero-duration note itself — that is
    // DurationConstraint's job, downstream in the pipeline.
    auto secondTrackTree = doc.getTrack (1);
    REQUIRE (secondTrackTree.getNumChildren() == 2);
    auto zeroedNote = secondTrackTree.getChild (0);
    CHECK ((int) zeroedNote.getProperty (SongIDs::durationTicks) == 0);
    auto survivingNote = secondTrackTree.getChild (1);
    CHECK ((int) survivingNote.getProperty (SongIDs::durationTicks) == 1);
}

// A later import that contributes zero tracks (and therefore zero notes)
// touches nothing during rescale — no Diagnostic should claim a rescale
// happened when no tick was ever actually rescaled.
TEST_CASE ("SongModelBridge: a trackless later import at a different PPQ emits no rescale diagnostic", "[songmodelbridge]")
{
    const auto first = threeTrackImportedSong(); // PPQ 960.

    Song tracklessSecond;
    tracklessSecond.ticksPerQuarter = 480; // Differs from the document's 960.
    // Ticks chosen so that, after the 2x rescale (960/480), they land exactly
    // on `first`'s own tempo/meter map ({0, 120.0}/{1920, 140.0} and
    // {0,4,4}/{1920,3,4} at doc PPQ 960) — no timeline-diff Warning muddies
    // the assertion below.
    tracklessSecond.tempoMap = { { 0, 120.0 }, { 960, 140.0 } };
    tracklessSecond.meterMap = { { 0, 4, 4 }, { 960, 3, 4 } };
    // Deliberately no tracks.

    SongDocument doc;
    Diagnostics diagnostics;
    appendImportedSong (doc, first, 1, diagnostics);
    REQUIRE (diagnostics.empty());

    appendImportedSong (doc, tracklessSecond, 2, diagnostics);

    CHECK (diagnostics.empty());
}

// Ruling 1's "doc.getNumTracks() == 0" first-import guard was not distinct
// from "has any track ever landed" — an all-silent first MIDI file (every
// track's notes empty, so importMidi drops all of them) would leave the
// document with zero tracks despite having already set
// ticksPerQuarter/TEMPO_MAP/METER_MAP, so a second import would incorrectly
// be treated as the first import too. The fix defines "first import" as an
// empty TEMPO_MAP instead (importMidi always seeds a non-empty one), which
// this test's zero-track first import still leaves non-empty.
TEST_CASE ("SongModelBridge: a zero-track first import still counts as 'first' for a later import (Ruling 1's guard is TEMPO_MAP emptiness, not track count)", "[songmodelbridge]")
{
    Song zeroTrackFirst;
    zeroTrackFirst.ticksPerQuarter = 960;
    zeroTrackFirst.tempoMap = { { 0, 100.0 } };
    zeroTrackFirst.meterMap = { { 0, 4, 4 } };
    // Deliberately no tracks — simulates a MIDI file whose every track had
    // no note-on events and was dropped by importMidi.

    const auto second = twoTrackImportedSongSecondBatch(); // PPQ 480, differing tempo/meter.

    SongDocument doc;
    Diagnostics diagnostics;
    appendImportedSong (doc, zeroTrackFirst, 1, diagnostics);
    REQUIRE (diagnostics.empty());
    REQUIRE (doc.getNumTracks() == 0);

    appendImportedSong (doc, second, 2, diagnostics);

    // Document PPQ stays at the first import's value (960), not the second's
    // (480) — proving the second import was NOT (incorrectly) treated as
    // the first.
    CHECK ((int) doc.getSourceMidiNode().getProperty (SongIDs::ticksPerQuarter) == 960);

    // TEMPO_MAP/METER_MAP still hold only the zero-track first import's
    // entries — count and values.
    auto tempoMapNode = doc.getTempoMapNode();
    auto meterMapNode = doc.getMeterMapNode();
    REQUIRE (tempoMapNode.getNumChildren() == 1);
    CHECK ((int) tempoMapNode.getChild (0).getProperty (SongIDs::tick) == 0);
    CHECK ((double) tempoMapNode.getChild (0).getProperty (SongIDs::bpm) == 100.0);
    REQUIRE (meterMapNode.getNumChildren() == 1);
    CHECK ((int) meterMapNode.getChild (0).getProperty (SongIDs::numerator) == 4);
    CHECK ((int) meterMapNode.getChild (0).getProperty (SongIDs::denominator) == 4);

    // The second import's notes were rescaled 2x (960/480), exactly.
    REQUIRE (doc.getNumTracks() == (int) second.tracks.size());
    for (size_t t = 0; t < second.tracks.size(); ++t)
    {
        auto trackTree = doc.getTrack ((int) t);
        REQUIRE (trackTree.getNumChildren() == (int) second.tracks[t].notes.size());
        for (size_t n = 0; n < second.tracks[t].notes.size(); ++n)
        {
            auto noteTree = trackTree.getChild ((int) n);
            const auto& note = second.tracks[t].notes[n];
            CHECK ((int) noteTree.getProperty (SongIDs::startTick) == note.startTick * 2);
            CHECK ((int) noteTree.getProperty (SongIDs::durationTicks) == note.durationTicks * 2);
        }
    }

    // One timeline Warning was emitted (the second import's tempo/meter
    // differs from what the document holds).
    int timelineWarnings = 0;
    for (const auto& d : diagnostics)
        if (d.source == "SongModelBridge" && d.severity == Severity::Warning
            && d.message.find ("timeline") != std::string::npos)
            ++timelineWarnings;
    CHECK (timelineWarnings == 1);
}

// A-R1: default-part synthesis becomes production code, replacing the
// inline per-test loop that used to live in SongsmithRoundTrip_tests.cpp.
TEST_CASE ("SongModelBridge: synthesiseDefaultParts adds one Part+Assignment per unassigned track, skipping already-assigned tracks", "[songmodelbridge]")
{
    SongDocument doc;
    auto melodic = doc.addTrackBulk ("Melodic", 0, 0, 1);
    auto drum    = doc.addTrackBulk ("Drum", 0, 10, 1);
    auto already = doc.addTrackBulk ("Already Arranged", 0, 3, 1);

    auto melodicId = (juce::int64) melodic.getProperty (SongIDs::trackId);
    auto drumId    = (juce::int64) drum.getProperty (SongIDs::trackId);
    auto alreadyId = (juce::int64) already.getProperty (SongIDs::trackId);

    // Pre-existing arrangement on `already` — must be left untouched, and
    // its track must be skipped by synthesis (not double-assigned).
    auto existingPart = doc.addPart ("Harp", "Pre-arranged");
    doc.addAssignment (existingPart, alreadyId, 5, 90, "octaveShift");

    synthesiseDefaultParts (doc);

    // Exactly two new parts synthesized (melodic, drum) plus the
    // pre-existing one = 3 total.
    REQUIRE (doc.getNumParts() == 3);

    // The pre-existing part/assignment is untouched.
    CHECK (doc.getPart (0).getProperty (SongIDs::instrumentName).toString() == "Harp");
    CHECK (SongDocument::getNumAssignments (doc.getPart (0)) == 1);
    CHECK ((int) SongDocument::getAssignment (doc.getPart (0), 0).getProperty (SongIDs::transposeSemitones) == 5);

    // A synthesized part exists for the melodic track, using LuteOfAges
    // (non-drum, sourceMidiChannel != 10).
    auto melodicPart = juce::ValueTree();
    auto drumPart = juce::ValueTree();
    for (int i = 0; i < doc.getNumParts(); ++i)
    {
        auto p = doc.getPart (i);
        if (SongDocument::getNumAssignments (p) != 1) continue;
        auto assignedTrackId = (juce::int64) SongDocument::getAssignment (p, 0).getProperty (SongIDs::trackId);
        if (assignedTrackId == melodicId) melodicPart = p;
        if (assignedTrackId == drumId)    drumPart = p;
    }

    REQUIRE (melodicPart.isValid());
    REQUIRE (drumPart.isValid());
    CHECK (melodicPart.getProperty (SongIDs::instrumentName).toString()
           == juce::String (std::string (displayName (LotroInstrument::LuteOfAges))));
    CHECK (drumPart.getProperty (SongIDs::instrumentName).toString()
           == juce::String (std::string (displayName (LotroInstrument::Drums))));
    CHECK (melodicPart.getProperty (SongIDs::label).toString() == "");

    auto melodicAssignment = SongDocument::getAssignment (melodicPart, 0);
    CHECK ((int) melodicAssignment.getProperty (SongIDs::transposeSemitones) == 0);
    CHECK ((int) melodicAssignment.getProperty (SongIDs::volumePercent) == 0);
    CHECK (melodicAssignment.getProperty (SongIDs::rangePolicy).toString() == "octaveShift");
}

TEST_CASE ("SongModelBridge: synthesiseDefaultParts is exactly one undo transaction — one undo() removes every part it added", "[songmodelbridge]")
{
    SongDocument doc;
    doc.addTrackBulk ("A", 0, 0, 1);
    doc.addTrackBulk ("B", 0, 10, 1);
    doc.addTrackBulk ("C", 0, 1, 1);

    REQUIRE_FALSE (doc.canUndo());

    synthesiseDefaultParts (doc);

    REQUIRE (doc.getNumParts() == 3);
    REQUIRE (doc.canUndo());

    doc.undo();

    CHECK (doc.getNumParts() == 0);
    // Tracks (added via addTrackBulk, non-undoable) are unaffected.
    CHECK (doc.getNumTracks() == 3);
    // The single undo() fully reverted the synthesis — nothing left to undo
    // from it (an earlier state, if any, could still be undoable; here
    // there is none).
    CHECK_FALSE (doc.canUndo());
}

TEST_CASE ("SongModelBridge: synthesiseDefaultParts on a document with nothing left to arrange adds no additional part", "[songmodelbridge]")
{
    SongDocument doc;
    auto track = doc.addTrackBulk ("A", 0, 0, 1);
    auto trackId = (juce::int64) track.getProperty (SongIDs::trackId);
    auto part = doc.addPart ("LuteOfAges", "Lead", false);
    doc.addAssignment (part, trackId, 0, 0, "octaveShift", false);

    REQUIRE (doc.getNumParts() == 1);

    synthesiseDefaultParts (doc);

    CHECK (doc.getNumParts() == 1);
}
