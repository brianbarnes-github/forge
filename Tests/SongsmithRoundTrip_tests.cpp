// Phase 3 verification: the Songsmith document round-trip (importMidiFile +
// buildConfigAndRawSong) must produce byte-identical ABC to the CLI's
// ad-hoc-mode path (importMidi -> synthesiseConfig -> assembleInstruments ->
// runPipeline -> writeAbc) on every tracked fixture, and multi-import time-
// base reconciliation (Rulings R1/R2/R3 in the Phase 3 brief) must behave
// exactly as specified against real files.

#include "UI/SongModelBridge.h"

#include "Core/AbcWriter.h"
#include "Core/InstrumentAssembly.h"
#include "Core/MidiImporter.h"
#include "Core/Pipeline.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <juce_core/juce_core.h>

#include <cmath>
#include <fstream>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

using namespace lotro;

namespace
{
    juce::File projectRoot()
    {
        return juce::File (__FILE__).getParentDirectory().getParentDirectory();
    }

    juce::File midiFixture (const std::string& name)
    {
        return projectRoot().getChildFile ("midi").getChildFile (name);
    }

    // Ruling R2's rescale formula, reused here (not independently derived —
    // this is the same expression as SongModelBridge.cpp's rescaleTick) so
    // these real-fixture tests can compare the bridge's note-by-note output
    // against a direct import's ground-truth ticks. Because it's the same
    // formula, these tests alone can't catch a wrong rounding mode (e.g.
    // truncation instead of round-half-away) — that is pinned independently,
    // with hand-computed values, by the 3:2-ratio test in
    // Tests/SongModelBridge_tests.cpp ("an inexact rescale emits a Warning
    // naming the correct rounded-value count").
    int rescaleTick (int tick, int docPpq, int importedPpq)
    {
        return (int) std::lround ((double) tick * (double) docPpq / (double) importedPpq);
    }

    bool isExactRescale (int tick, int docPpq, int importedPpq)
    {
        return ((long long) tick * (long long) docPpq) % (long long) importedPpq == 0;
    }

    // Direct (non-bridge) import of a fixture, for building "expected" values.
    Song directImport (const juce::File& file, Diagnostics& diagnostics)
    {
        std::ifstream input (file.getFullPathName().toStdString(), std::ios::binary);
        REQUIRE (input);
        return importMidi (input, file.getFileNameWithoutExtension().toStdString(), diagnostics);
    }

    // The CLI's ad-hoc-mode path (Source/Main.cpp:141-188), reproduced here so
    // the round-trip test can compare against it. Mirrors EndToEnd_tests.cpp's
    // per-track constraint pass.
    Song runFullCliPipeline (const juce::File& file, Diagnostics& diagnostics, Song& outRawSong)
    {
        auto song = directImport (file, diagnostics);
        outRawSong = song;

        auto cfg = synthesiseConfig (song, file.getFullPathName().toStdString(), "",
                                     std::nullopt, 0, {});
        auto assembled = assembleInstruments (song, cfg, diagnostics);
        runPipeline (assembled, diagnostics);
        return assembled;
    }

    struct TempoMapSnapshot
    {
        std::vector<std::pair<int, double>> entries;
    };

    struct MeterMapSnapshot
    {
        std::vector<std::tuple<int, int, int>> entries;
    };

    TempoMapSnapshot snapshotTempoMap (const SongDocument& doc)
    {
        TempoMapSnapshot snap;
        for (auto c : doc.getTempoMapNode())
            snap.entries.push_back ({ (int) c.getProperty (SongIDs::tick), (double) c.getProperty (SongIDs::bpm) });
        return snap;
    }

    MeterMapSnapshot snapshotMeterMap (const SongDocument& doc)
    {
        MeterMapSnapshot snap;
        for (auto c : doc.getMeterMapNode())
            snap.entries.push_back ({ (int) c.getProperty (SongIDs::tick),
                                       (int) c.getProperty (SongIDs::numerator),
                                       (int) c.getProperty (SongIDs::denominator) });
        return snap;
    }

    // Independent (bridge-free) computation of whether R1's "later import's
    // timeline differs from the document's" condition holds, using the
    // ground-truth direct import and the rescale rule from R2.
    bool rescaledTimelineDiffers (const TempoMapSnapshot& docTempo, const MeterMapSnapshot& docMeter,
                                 const Song& directSong, int docPpq, int importedPpq)
    {
        if (docTempo.entries.size() != directSong.tempoMap.size())
            return true;
        for (size_t i = 0; i < directSong.tempoMap.size(); ++i)
        {
            const auto rescaled = rescaleTick (directSong.tempoMap[i].tick, docPpq, importedPpq);
            if (docTempo.entries[i].first != rescaled || docTempo.entries[i].second != directSong.tempoMap[i].bpm)
                return true;
        }

        if (docMeter.entries.size() != directSong.meterMap.size())
            return true;
        for (size_t i = 0; i < directSong.meterMap.size(); ++i)
        {
            const auto rescaled = rescaleTick (directSong.meterMap[i].tick, docPpq, importedPpq);
            if (std::get<0> (docMeter.entries[i]) != rescaled
                || std::get<1> (docMeter.entries[i]) != directSong.meterMap[i].numerator
                || std::get<2> (docMeter.entries[i]) != directSong.meterMap[i].denominator)
                return true;
        }

        return false;
    }
}

TEST_CASE ("SongsmithRoundTrip: import->assemble->pipeline->ABC matches the CLI's ad-hoc path for every tracked fixture", "[songsmith-roundtrip]")
{
    struct Fixture { std::string filename; int ppq; };
    auto fixture = GENERATE (
        Fixture { "Barnes Brothers Band - Pull The Wires.mid", 480 },
        Fixture { "anymore.mid", 960 },
        Fixture { "blue.mid", 120 },
        Fixture { "hold.mid", 480 },
        Fixture { "land.mid", 960 },
        Fixture { "leah.mid", 960 },
        Fixture { "nobody.mid", 960 },
        Fixture { "right.mid", 480 },
        Fixture { "tellit.mid", 120 });

    DYNAMIC_SECTION (fixture.filename)
    {
        const auto file = midiFixture (fixture.filename);
        REQUIRE (file.existsAsFile());

        // Path A: CLI ad-hoc path.
        Diagnostics diagA;
        Song rawSongA;
        auto assembledA = runFullCliPipeline (file, diagA, rawSongA);
        REQUIRE (rawSongA.ticksPerQuarter == fixture.ppq);
        const auto abcA = writeAbc (assembledA);

        // Path B: Songsmith document round-trip.
        SongDocument doc;
        Diagnostics diagB;
        REQUIRE (importMidiFile (doc, file, 1, diagB));
        const size_t importDiagCountB = diagB.size();

        synthesiseDefaultParts (doc);

        auto built = buildConfigAndRawSong (doc);
        auto assembledB = assembleInstruments (built.rawSong, built.config, diagB);
        runPipeline (assembledB, diagB);
        const auto abcB = writeAbc (assembledB);
        const size_t postImportDiagCountB = diagB.size() - importDiagCountB;

        // diagA's post-import count is everything after the direct importMidi
        // call inside runFullCliPipeline; recompute it the same way here so
        // both counts cover exactly assembleInstruments + runPipeline.
        Diagnostics importOnlyDiagA;
        directImport (file, importOnlyDiagA);
        const size_t postImportDiagCountA = diagA.size() - importOnlyDiagA.size();

        CHECK (abcA == abcB);
        CHECK (postImportDiagCountA == postImportDiagCountB);
    }
}

TEST_CASE ("SongsmithRoundTrip: a later, lower-PPQ import is upscaled into the document's PPQ", "[songsmith-roundtrip]")
{
    const auto firstFile  = midiFixture ("Barnes Brothers Band - Pull The Wires.mid");
    const auto secondFile = midiFixture ("blue.mid");
    REQUIRE (firstFile.existsAsFile());
    REQUIRE (secondFile.existsAsFile());

    SongDocument doc;
    Diagnostics diag1;
    REQUIRE (importMidiFile (doc, firstFile, 1, diag1));
    REQUIRE ((int) doc.getSourceMidiNode().getProperty (SongIDs::ticksPerQuarter) == 480);

    const int tracksAfterFirst = doc.getNumTracks();
    const auto tempoAfterFirst = snapshotTempoMap (doc);
    const auto meterAfterFirst = snapshotMeterMap (doc);

    Diagnostics diag2;
    REQUIRE (importMidiFile (doc, secondFile, 2, diag2));

    // Document PPQ never changes after the first import.
    CHECK ((int) doc.getSourceMidiNode().getProperty (SongIDs::ticksPerQuarter) == 480);

    // TEMPO_MAP/METER_MAP unchanged from the first import (R1).
    CHECK (snapshotTempoMap (doc).entries == tempoAfterFirst.entries);
    CHECK (snapshotMeterMap (doc).entries == meterAfterFirst.entries);

    Diagnostics directDiag;
    auto directSong = directImport (secondFile, directDiag);
    REQUIRE (directSong.ticksPerQuarter == 120);

    const int docPpq = 480;
    const int importedPpq = 120;

    REQUIRE (doc.getNumTracks() == tracksAfterFirst + (int) directSong.tracks.size());
    for (size_t t = 0; t < directSong.tracks.size(); ++t)
    {
        auto trackTree = doc.getTrack (tracksAfterFirst + (int) t);
        const auto& directTrack = directSong.tracks[t];
        REQUIRE (trackTree.getNumChildren() == (int) directTrack.notes.size());

        for (size_t n = 0; n < directTrack.notes.size(); ++n)
        {
            auto noteTree = trackTree.getChild ((int) n);
            const auto& directNote = directTrack.notes[n];

            CHECK ((int) noteTree.getProperty (SongIDs::startTick)
                   == rescaleTick (directNote.startTick, docPpq, importedPpq));
            CHECK ((int) noteTree.getProperty (SongIDs::durationTicks)
                   == rescaleTick (directNote.durationTicks, docPpq, importedPpq));
            CHECK ((int) noteTree.getProperty (SongIDs::pitch) == directNote.pitch);
            CHECK ((int) noteTree.getProperty (SongIDs::velocity) == directNote.velocity);
            CHECK ((bool) noteTree.getProperty (SongIDs::isDrum) == directNote.isDrum);
            CHECK ((int) noteTree.getProperty (SongIDs::sourceTrackIndex) == directNote.sourceTrackIndex);
            CHECK ((int) noteTree.getProperty (SongIDs::sourceEventIndex) == directNote.sourceEventIndex);

            // 480/120 is an exact integer factor: every rescale here must be exact.
            CHECK (isExactRescale (directNote.startTick, docPpq, importedPpq));
            CHECK (isExactRescale (directNote.durationTicks, docPpq, importedPpq));
        }
    }

    int infoCount = 0, warningCount = 0;
    for (const auto& d : diag2)
    {
        if (d.source != "SongModelBridge") continue;
        if (d.severity == Severity::Info) ++infoCount;
        else if (d.severity == Severity::Warning) ++warningCount;
    }

    // 4x upscale is always exact -> Info, never Warning, for the rescale diagnostic.
    CHECK (infoCount == 1);

    const bool timelineDiffers = rescaledTimelineDiffers (tempoAfterFirst, meterAfterFirst, directSong, docPpq, importedPpq);
    CHECK (warningCount == (timelineDiffers ? 1 : 0));
}

// A-R3 (Phase 4): this used to pin the Phase 3 lossy-downscale behaviour
// (blue.mid's 120 PPQ document downscaling anymore.mid's 960-PPQ notes,
// lossily where inexact). Under the LCM rule, lcm(120, 960) = 960, which is
// > the document's current PPQ (120) and well under the 15360 cap, so the
// document's time base is now RAISED to 960 instead — blue.mid's EXISTING
// notes are rescaled ×8 (exact; the incoming file's own PPQ needs no rescale
// at all, since the raise lands exactly on it). This is also the F1
// regression fixture from the Phase 3 review's own numbers (blue.mid was one
// of the two 120-PPQ files named there) but exercised as an EXISTING-side
// upscale rather than the dangerous incoming-side downscale — see the
// dedicated F1 regression test below for the actual danger case (right.mid).
TEST_CASE ("SongsmithRoundTrip: a later, higher-PPQ import raises the document's time base instead of lossily downscaling (blue.mid then anymore.mid)", "[songsmith-roundtrip]")
{
    const auto firstFile  = midiFixture ("blue.mid");
    const auto secondFile = midiFixture ("anymore.mid");
    REQUIRE (firstFile.existsAsFile());
    REQUIRE (secondFile.existsAsFile());

    Diagnostics directFirstDiag;
    auto directFirstSong = directImport (firstFile, directFirstDiag);
    REQUIRE (directFirstSong.ticksPerQuarter == 120);

    SongDocument doc;
    Diagnostics diag1;
    REQUIRE (importMidiFile (doc, firstFile, 1, diag1));
    REQUIRE ((int) doc.getSourceMidiNode().getProperty (SongIDs::ticksPerQuarter) == 120);

    const int tracksAfterFirst = doc.getNumTracks();
    const auto tempoAfterFirst = snapshotTempoMap (doc);
    const auto meterAfterFirst = snapshotMeterMap (doc);

    Diagnostics diag2;
    REQUIRE (importMidiFile (doc, secondFile, 2, diag2));

    // Raised: lcm(120, 960) = 960 > 120, so the document's PPQ is now 960.
    REQUIRE ((int) doc.getSourceMidiNode().getProperty (SongIDs::ticksPerQuarter) == 960);

    const int raiseFactor = 8; // 960 / 120

    // blue.mid's existing notes rescaled ×8, exactly, versus a direct import.
    REQUIRE (doc.getNumTracks() >= tracksAfterFirst);
    for (int t = 0; t < tracksAfterFirst; ++t)
    {
        auto trackTree = doc.getTrack (t);
        const auto& directTrack = directFirstSong.tracks[(size_t) t];
        REQUIRE (trackTree.getNumChildren() == (int) directTrack.notes.size());

        for (size_t n = 0; n < directTrack.notes.size(); ++n)
        {
            auto noteTree = trackTree.getChild ((int) n);
            const auto& directNote = directTrack.notes[n];
            CHECK ((int) noteTree.getProperty (SongIDs::startTick) == directNote.startTick * raiseFactor);
            CHECK ((int) noteTree.getProperty (SongIDs::durationTicks) == directNote.durationTicks * raiseFactor);

            // Provenance untouched by the raise (only tick fields are
            // multiplied).
            CHECK ((int) noteTree.getProperty (SongIDs::pitch) == directNote.pitch);
            CHECK ((int) noteTree.getProperty (SongIDs::velocity) == directNote.velocity);
            CHECK ((int) noteTree.getProperty (SongIDs::sourceTrackIndex) == directNote.sourceTrackIndex);
            CHECK ((int) noteTree.getProperty (SongIDs::sourceEventIndex) == directNote.sourceEventIndex);
        }
    }

    // blue.mid's existing tempo/meter map ticks rescaled ×8 too.
    auto tempoMapNode = doc.getTempoMapNode();
    REQUIRE (tempoMapNode.getNumChildren() == (int) tempoAfterFirst.entries.size());
    for (size_t i = 0; i < tempoAfterFirst.entries.size(); ++i)
        CHECK ((int) tempoMapNode.getChild ((int) i).getProperty (SongIDs::tick)
               == tempoAfterFirst.entries[i].first * raiseFactor);

    auto meterMapNode = doc.getMeterMapNode();
    REQUIRE (meterMapNode.getNumChildren() == (int) meterAfterFirst.entries.size());
    for (size_t i = 0; i < meterAfterFirst.entries.size(); ++i)
        CHECK ((int) meterMapNode.getChild ((int) i).getProperty (SongIDs::tick)
               == std::get<0> (meterAfterFirst.entries[i]) * raiseFactor);

    // anymore.mid's incoming notes verbatim — the raise landed exactly on
    // its own PPQ (960), so no rescale was needed on the incoming side.
    Diagnostics directSecondDiag;
    auto directSecondSong = directImport (secondFile, directSecondDiag);
    REQUIRE (directSecondSong.ticksPerQuarter == 960);

    REQUIRE (doc.getNumTracks() == tracksAfterFirst + (int) directSecondSong.tracks.size());
    for (size_t t = 0; t < directSecondSong.tracks.size(); ++t)
    {
        auto trackTree = doc.getTrack (tracksAfterFirst + (int) t);
        const auto& directTrack = directSecondSong.tracks[t];
        REQUIRE (trackTree.getNumChildren() == (int) directTrack.notes.size());

        for (size_t n = 0; n < directTrack.notes.size(); ++n)
        {
            auto noteTree = trackTree.getChild ((int) n);
            const auto& directNote = directTrack.notes[n];
            CHECK ((int) noteTree.getProperty (SongIDs::startTick) == directNote.startTick);
            CHECK ((int) noteTree.getProperty (SongIDs::durationTicks) == directNote.durationTicks);
        }
    }

    // The raise is always exact -> Info, NEVER a Warning about rounding.
    int infoCount = 0, warningCount = 0;
    for (const auto& d : diag2)
    {
        if (d.source != "SongModelBridge") continue;
        if (d.severity == Severity::Info) ++infoCount;
        else if (d.severity == Severity::Warning) ++warningCount;
    }
    CHECK (infoCount >= 1);
    for (const auto& d : diag2)
        if (d.source == "SongModelBridge" && d.severity == Severity::Warning)
            CHECK (d.message.find ("rounded") == std::string::npos);
}

// F1 regression (Phase 3 review): importing right.mid (480 PPQ) after a
// 120-PPQ document (blue.mid first) used to downscale right.mid's notes
// lossily into 120 PPQ, rounding 126 of its 1-tick notes to 0 and silently
// destroying them (DurationConstraint drops zero-duration notes without a
// Diagnostic of its own). Under A-R3's LCM rule, lcm(120, 480) = 480 > 120,
// so the document is raised to 480 (right.mid's own PPQ) instead — right's
// notes need NO rescale at all (480/480 == 1), so they survive with their
// exact original tick values; it is blue.mid's EXISTING notes that get
// rescaled (×4, exactly) to fit the raised time base.
TEST_CASE ("SongsmithRoundTrip: importing a higher-PPQ file no longer destroys its short notes (F1 regression: blue.mid then right.mid)", "[songsmith-roundtrip]")
{
    const auto firstFile  = midiFixture ("blue.mid");
    const auto secondFile = midiFixture ("right.mid");
    REQUIRE (firstFile.existsAsFile());
    REQUIRE (secondFile.existsAsFile());

    SongDocument doc;
    Diagnostics diag1;
    REQUIRE (importMidiFile (doc, firstFile, 1, diag1));
    REQUIRE ((int) doc.getSourceMidiNode().getProperty (SongIDs::ticksPerQuarter) == 120);

    const int tracksAfterFirst = doc.getNumTracks();

    Diagnostics diag2;
    REQUIRE (importMidiFile (doc, secondFile, 2, diag2));

    // Raised: lcm(120, 480) = 480 > 120.
    REQUIRE ((int) doc.getSourceMidiNode().getProperty (SongIDs::ticksPerQuarter) == 480);

    Diagnostics directDiag;
    auto directSong = directImport (secondFile, directDiag);
    REQUIRE (directSong.ticksPerQuarter == 480);

    // F1's specific failure mode: right.mid has 1-tick-duration notes that
    // the OLD lossy downscale (480 -> 120) would round to 0 and silently
    // drop. Count them in the direct import, then assert every single one of
    // them survives in the document with its EXACT original duration (1,
    // unchanged) — the raise means the incoming side needed no rescale at
    // all, so nothing here can round to zero.
    int oneTickNotesInDirectImport = 0;
    for (const auto& track : directSong.tracks)
        for (const auto& note : track.notes)
            if (note.durationTicks == 1)
                ++oneTickNotesInDirectImport;
    REQUIRE (oneTickNotesInDirectImport > 0);

    int oneTickNotesInDocument = 0;
    int zeroTickNotesInDocument = 0;
    for (int t = tracksAfterFirst; t < doc.getNumTracks(); ++t)
    {
        auto trackTree = doc.getTrack (t);
        for (int n = 0; n < trackTree.getNumChildren(); ++n)
        {
            const int duration = (int) trackTree.getChild (n).getProperty (SongIDs::durationTicks);
            if (duration == 1) ++oneTickNotesInDocument;
            if (duration == 0) ++zeroTickNotesInDocument;
        }
    }

    CHECK (oneTickNotesInDocument == oneTickNotesInDirectImport);
    CHECK (zeroTickNotesInDocument == 0);

    // No Warning naming rounded/dropped notes — the raise itself is always
    // exact. (A separate tempo/meter-timeline Warning may legitimately fire
    // per A-R4 if blue.mid's and right.mid's tempo/meter maps differ — that
    // is an unrelated diagnostic and not what this regression test guards.)
    for (const auto& d : diag2)
        if (d.source == "SongModelBridge" && d.severity == Severity::Warning)
            CHECK (d.message.find ("rounded") == std::string::npos);
}

TEST_CASE ("SongsmithRoundTrip: a later, same-PPQ import keeps ticks verbatim and emits no rescale diagnostic", "[songsmith-roundtrip]")
{
    const auto firstFile  = midiFixture ("Barnes Brothers Band - Pull The Wires.mid");
    const auto secondFile = midiFixture ("hold.mid");
    REQUIRE (firstFile.existsAsFile());
    REQUIRE (secondFile.existsAsFile());

    SongDocument doc;
    Diagnostics diag1;
    REQUIRE (importMidiFile (doc, firstFile, 1, diag1));
    REQUIRE ((int) doc.getSourceMidiNode().getProperty (SongIDs::ticksPerQuarter) == 480);

    const int tracksAfterFirst = doc.getNumTracks();
    const auto tempoAfterFirst = snapshotTempoMap (doc);
    const auto meterAfterFirst = snapshotMeterMap (doc);

    Diagnostics diag2;
    REQUIRE (importMidiFile (doc, secondFile, 2, diag2));

    Diagnostics directDiag;
    auto directSong = directImport (secondFile, directDiag);
    REQUIRE (directSong.ticksPerQuarter == 480);

    REQUIRE (doc.getNumTracks() == tracksAfterFirst + (int) directSong.tracks.size());
    for (size_t t = 0; t < directSong.tracks.size(); ++t)
    {
        auto trackTree = doc.getTrack (tracksAfterFirst + (int) t);
        const auto& directTrack = directSong.tracks[t];
        REQUIRE (trackTree.getNumChildren() == (int) directTrack.notes.size());

        for (size_t n = 0; n < directTrack.notes.size(); ++n)
        {
            auto noteTree = trackTree.getChild ((int) n);
            CHECK ((int) noteTree.getProperty (SongIDs::startTick) == directTrack.notes[n].startTick);
            CHECK ((int) noteTree.getProperty (SongIDs::durationTicks) == directTrack.notes[n].durationTicks);
        }
    }

    // Same PPQ -> no rescale, so no rescale diagnostic at all (neither Info
    // nor Warning) — the only SongModelBridge diagnostic that can appear here
    // is R1's timeline-diff Warning, and only if the two files' tempo/meter
    // maps actually differ. Assert the exact count, not just "every one is a
    // Warning" (which would also pass if unrelated warnings appeared).
    const int docPpq = 480, importedPpq = 480;
    const bool timelineDiffers = rescaledTimelineDiffers (tempoAfterFirst, meterAfterFirst, directSong, docPpq, importedPpq);
    const int expectedSongModelBridgeDiagnostics = timelineDiffers ? 1 : 0;

    int actualSongModelBridgeDiagnostics = 0;
    for (const auto& d : diag2)
        if (d.source == "SongModelBridge")
            ++actualSongModelBridgeDiagnostics;

    CHECK (actualSongModelBridgeDiagnostics == expectedSongModelBridgeDiagnostics);
    for (const auto& d : diag2)
        if (d.source == "SongModelBridge")
            CHECK (d.severity == Severity::Warning);
}

TEST_CASE ("SongsmithRoundTrip: importMidiFile on an unopenable path fails cleanly", "[songsmith-roundtrip]")
{
    SongDocument doc;
    Diagnostics diag;
    const juce::File missing ("/nonexistent/does-not-exist-forge-phase3.mid");
    REQUIRE_FALSE (missing.existsAsFile());

    const bool ok = importMidiFile (doc, missing, 1, diag);

    CHECK_FALSE (ok);
    REQUIRE (diag.size() == 1);
    CHECK (diag[0].severity == Severity::Error);
    CHECK (diag[0].source == "SongModelBridge");
    CHECK (doc.getNumTracks() == 0);
    CHECK (doc.getTree().getProperty (SongIDs::inputMidiPath).toString().isEmpty());
}

TEST_CASE ("SongsmithRoundTrip: importMidiFile on a file that opens but doesn't parse reports a Diagnostic instead of throwing", "[songsmith-roundtrip]")
{
    // importMidi throws lotro::MidiImportError for content that opens fine
    // (passes the ifstream check) but doesn't parse as MIDI (empty buffer,
    // malformed data, unsupported SMPTE time format) — see
    // Tests/MidiImporter_tests.cpp's "rejects malformed input". importMidiFile
    // must catch that and report a Diagnostic rather than letting the
    // exception escape into the caller (e.g. the Songsmith UI).
    auto tempFile = juce::File::createTempFile ("forge-phase3-malformed");
    REQUIRE (tempFile.replaceWithText ("not a midi file, just plain garbage text"));

    struct DeleteOnScopeExit
    {
        juce::File file;
        ~DeleteOnScopeExit() { file.deleteFile(); }
    } cleanup { tempFile };

    SongDocument doc;
    Diagnostics diag;

    const bool ok = importMidiFile (doc, tempFile, 1, diag);

    CHECK_FALSE (ok);
    REQUIRE (diag.size() == 1);
    CHECK (diag[0].severity == Severity::Error);
    CHECK (diag[0].source == "SongModelBridge");
    CHECK (doc.getNumTracks() == 0);
    CHECK (doc.getTree().getProperty (SongIDs::inputMidiPath).toString().isEmpty());
    CHECK_FALSE (doc.canUndo());
}

TEST_CASE ("SongsmithRoundTrip: inputMidiPath and derived title come from the first import, not later ones", "[songsmith-roundtrip]")
{
    const auto firstFile  = midiFixture ("Barnes Brothers Band - Pull The Wires.mid");
    const auto secondFile = midiFixture ("hold.mid");

    SongDocument doc;
    Diagnostics diag1, diag2;
    REQUIRE (importMidiFile (doc, firstFile, 1, diag1));
    REQUIRE (importMidiFile (doc, secondFile, 2, diag2));

    CHECK (doc.getTree().getProperty (SongIDs::inputMidiPath).toString().toStdString()
           == firstFile.getFullPathName().toStdString());

    auto built = buildConfigAndRawSong (doc);
    CHECK (built.rawSong.title == firstFile.getFileNameWithoutExtension().toStdString());
}

TEST_CASE ("SongsmithRoundTrip: importMidiFile never opens an undo transaction", "[songsmith-roundtrip]")
{
    const auto file = midiFixture ("Barnes Brothers Band - Pull The Wires.mid");
    SongDocument doc;
    Diagnostics diag;

    REQUIRE (importMidiFile (doc, file, 1, diag));
    CHECK_FALSE (doc.canUndo());
}
