#include "MidiTestBytes.h"
#include "UI/KeyDetection.h"
#include "UI/KeyDetector.h"
#include "UI/MidiImportPlan.h"
#include "UI/SongDocument.h"
#include "UI/SongModelBridge.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <juce_core/juce_core.h>

#include <set>

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

    struct ScaleNote
    {
        int pitch;
        int ticks;
    };

    // One note track on `channel` (0-based status nibble), notes back to back.
    TrackBody noteTrack (const std::vector<ScaleNote>& notes, std::uint8_t channel = 0)
    {
        TrackBody t;
        for (const auto& n : notes)
            t.ev (0, { (std::uint8_t) (0x90 | channel), (std::uint8_t) n.pitch, 100 })
             .ev ((std::uint32_t) n.ticks, { (std::uint8_t) (0x80 | channel), (std::uint8_t) n.pitch, 0x40 });
        t.eot();
        return t;
    }

    TrackBody conductor()
    {
        TrackBody c;
        c.ev (0, { 0xFF, 0x51, 0x03, 0x07, 0xA1, 0x20 }).eot();
        return c;
    }

    // Imports the tracks through the real importMidiFile path and detects from the document.
    KeyEstimate detectFromMidi (const std::vector<TrackBody>& tracks, KeyProfileSet profiles = KeyProfileSet::krumhanslKessler,
                                const ImportOptions& options = {})
    {
        std::vector<TrackBody> all { conductor() };
        all.insert (all.end(), tracks.begin(), tracks.end());
        TempMidi midi (smf (1, 480, all));
        SongDocument doc;
        Diagnostics d;
        REQUIRE (importMidiFile (doc, midi.file, 1, d, options));
        return detectKey (collectKeyNotes (doc.getTree()), profiles);
    }

    std::vector<ScaleNote> scale (int tonic, std::initializer_list<int> steps, int ticks = 480)
    {
        std::vector<ScaleNote> out;
        for (const int s : steps)
            out.push_back ({ tonic + s, ticks });
        return out;
    }

    const auto majorSteps = { 0, 2, 4, 5, 7, 9, 11, 12 };
    const auto minorSteps = { 0, 2, 3, 5, 7, 8, 10, 12 };   // natural minor
}

TEST_CASE ("KeyDetector: a C major scale is C major", "[key-detector]")
{
    const auto k = detectFromMidi ({ noteTrack (scale (60, majorSteps)) });
    CHECK (k.best.tonic == 0);
    CHECK (k.best.mode == KeyMode::major);
    CHECK (k.best.name() == "C major");
    CHECK (k.confidence > 0.0);
}

TEST_CASE ("KeyDetector: an A minor scale with an emphasised tonic is A minor", "[key-detector]")
{
    auto notes = scale (57, minorSteps);
    notes.front().ticks = 1920;   // a long opening A
    notes.back().ticks  = 1920;   // and a long closing A
    const auto k = detectFromMidi ({ noteTrack (notes) });
    CHECK (k.best.tonic == 9);
    CHECK (k.best.mode == KeyMode::minor);
    CHECK (k.best.name() == "A minor");
}

TEST_CASE ("KeyDetector: a scale moved to E flat is E flat major", "[key-detector]")
{
    const auto k = detectFromMidi ({ noteTrack (scale (63, majorSteps)) });
    CHECK (k.best.tonic == 3);
    CHECK (k.best.mode == KeyMode::major);
    CHECK (k.best.name() == "E\xe2\x99\xad major");
}

TEST_CASE ("KeyDetector: the ranked list holds all 24 keys, best first", "[key-detector]")
{
    const auto k = detectFromMidi ({ noteTrack (scale (67, majorSteps)) });
    REQUIRE (k.ranked.size() == 24);
    std::set<std::pair<int, KeyMode>> seen;
    for (std::size_t i = 0; i < k.ranked.size(); ++i)
    {
        seen.insert ({ k.ranked[i].tonic, k.ranked[i].mode });
        if (i > 0)
            CHECK (k.ranked[i - 1].correlation >= k.ranked[i].correlation);
    }
    CHECK (seen.size() == 24);
    CHECK (k.ranked.front().name() == k.best.name());
    CHECK (k.best.name() == "G major");
    CHECK (k.confidence == Catch::Approx (k.ranked[0].correlation - k.ranked[1].correlation));
}

TEST_CASE ("KeyDetector: channel 10 notes are left out", "[key-detector]")
{
    // A heavy F# drum line would pull the result off C major if it counted.
    const auto k = detectFromMidi ({ noteTrack (scale (60, majorSteps)),
                                     noteTrack ({ { 42, 9600 }, { 42, 9600 }, { 54, 9600 } }, 9) });
    CHECK (k.best.name() == "C major");
}

TEST_CASE ("KeyDetector: channel 10 notes are left out after a merged import too", "[key-detector]")
{
    ImportOptions merged;
    merged.tracks = TrackMode::merged;
    const auto k = detectFromMidi ({ noteTrack (scale (60, majorSteps)),
                                     noteTrack ({ { 42, 9600 }, { 42, 9600 }, { 54, 9600 } }, 9) },
                                   KeyProfileSet::krumhanslKessler, merged);
    CHECK (k.best.name() == "C major");
}

TEST_CASE ("KeyDetector: a percussion-only file reports no confidence and does not crash", "[key-detector]")
{
    const auto k = detectFromMidi ({ noteTrack ({ { 36, 480 }, { 38, 480 }, { 42, 480 } }, 9) });
    CHECK (k.confidence == Catch::Approx (0.0).margin (1e-12));
    CHECK (k.ranked.size() == 24);
    CHECK (formatKeyText (k).empty());
}

TEST_CASE ("KeyDetector: no notes, or an even chromatic spread, reports no confidence", "[key-detector]")
{
    const auto empty = detectKey (std::vector<KeyNote> {});
    CHECK (empty.confidence == Catch::Approx (0.0).margin (1e-12));
    CHECK (empty.ranked.size() == 24);
    CHECK (formatKeyText (empty).empty());

    std::vector<KeyNote> chromatic;
    for (int p = 60; p < 72; ++p)
        chromatic.push_back ({ p, 480, false });
    const auto flat = detectKey (chromatic);
    CHECK (flat.confidence == Catch::Approx (0.0).margin (1e-12));
    CHECK (formatKeyText (flat).empty());
}

TEST_CASE ("KeyDetector: the histogram weights by duration, counts a zero-length note once, and sums to 1", "[key-detector]")
{
    const auto h = pitchClassHistogram ({ { 60, 300, false }, { 72, 100, false }, { 67, 0, false }, { 38, 5000, true } });
    // C: 400, G: 1 (count fallback), drums skipped.
    CHECK (h[0] == Catch::Approx (400.0 / 401.0));
    CHECK (h[7] == Catch::Approx (1.0 / 401.0));
    CHECK (h[2] == Catch::Approx (0.0).margin (1e-12));
}

TEST_CASE ("KeyDetector: every profile set finds C major and A minor", "[key-detector]")
{
    std::vector<KeyNote> cMajor;
    for (const int s : majorSteps)
        cMajor.push_back ({ 60 + s, 480, false });
    std::vector<KeyNote> aMinor;
    for (const int s : minorSteps)
        aMinor.push_back ({ 57 + s, (s == 0 || s == 12) ? 1920 : 480, false });
    aMinor.push_back ({ 64, 960, false });   // a long dominant E too

    for (const auto set : { KeyProfileSet::krumhanslKessler, KeyProfileSet::temperley, KeyProfileSet::albrechtShanahan })
    {
        CAPTURE ((int) set);
        CHECK (detectKey (cMajor, set).best.name() == "C major");
        CHECK (detectKey (aMinor, set).best.name() == "A minor");
    }
}

TEST_CASE ("KeyDetector: the profile sets are distinct", "[key-detector]")
{
    const auto& kk = keyProfiles (KeyProfileSet::krumhanslKessler);
    const auto& t  = keyProfiles (KeyProfileSet::temperley);
    const auto& as = keyProfiles (KeyProfileSet::albrechtShanahan);
    CHECK (kk.major != t.major);
    CHECK (kk.major != as.major);
    CHECK (t.minor != as.minor);
}

TEST_CASE ("KeyDetector: key names use the usual spellings", "[key-detector]")
{
    CHECK (KeyCandidate { 6, KeyMode::major, 0.0 }.name() == "F\xe2\x99\xaf major");
    CHECK (KeyCandidate { 10, KeyMode::major, 0.0 }.name() == "B\xe2\x99\xad major");
    CHECK (KeyCandidate { 1, KeyMode::minor, 0.0 }.name() == "C\xe2\x99\xaf minor");
    CHECK (KeyCandidate { 3, KeyMode::minor, 0.0 }.name() == "E\xe2\x99\xad minor");
    CHECK (KeyCandidate { 8, KeyMode::minor, 0.0 }.name() == "G\xe2\x99\xaf minor");
}

TEST_CASE ("KeyDetector: the head text is 'key: ' and the name", "[key-detector]")
{
    const auto k = detectFromMidi ({ noteTrack (scale (64, minorSteps, 480)) });
    CHECK (formatKeyText (k) == "key: " + k.best.name());
}

TEST_CASE ("KeyDetection: the document's key text after an import", "[key-detector]")
{
    TempMidi midi (smf (1, 480, { conductor(), noteTrack (scale (62, majorSteps)) }));
    SongDocument doc;
    CHECK (detectedKeyText (doc).isEmpty());
    Diagnostics d;
    REQUIRE (importMidiFile (doc, midi.file, 1, d));
    CHECK (detectedKeyText (doc) == "key: D major");
}
