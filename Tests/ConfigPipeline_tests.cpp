#include "Core/AbcWriter.h"
#include "Core/ConfigLoader.h"
#include "Core/InstrumentAssembly.h"
#include "Core/Song.h"
#include "Core/Constraints/ChordConstraint.h"
#include "Core/Constraints/CollisionGuard.h"
#include "Core/Constraints/DurationConstraint.h"
#include "Core/Constraints/DynamicMapper.h"
#include "Core/Constraints/RangeConstraint.h"
#include "Core/Constraints/TempoCollapse.h"

#include <catch2/catch_test_macros.hpp>

namespace
{
    lotro::Note note (int pitch, int startTick, int dur, int vel = 100)
    {
        lotro::Note n;
        n.pitch = pitch; n.startTick = startTick;
        n.durationTicks = dur; n.velocity = vel;
        return n;
    }

    lotro::Song rawSong (int ppq = 480)
    {
        lotro::Song s;
        s.ticksPerQuarter = ppq;
        s.tempoMap.push_back ({ 0, 120.0 });
        s.meterMap.push_back ({ 0, 4, 4 });
        return s;
    }

    void runPipeline (lotro::Song& song, lotro::Diagnostics& diag)
    {
        for (auto& t : song.tracks)
        {
            if (! t.enabled) continue;
            lotro::applyRangeConstraint    (t, diag);
            lotro::applyChordConstraint    (t, diag);
            lotro::applyDurationConstraint (t, song, diag);
            lotro::applyTempoCollapse      (t, song, diag);
            lotro::applyCollisionGuard     (t, diag);
            lotro::applyDynamicMapper      (t, diag);
        }
        lotro::applyTempoCollapseToSongMaps (song);
    }

    bool contains (const std::string& hay, const std::string& needle)
    {
        return hay.find (needle) != std::string::npos;
    }
}

TEST_CASE ("config-pipeline: two MIDI sources merge into one instrument; chord forms at shared tick", "[config-pipeline]")
{
    auto raw = rawSong();
    lotro::Track t0, t1;
    t0.name = "lead";    t0.notes.push_back (note (60, 0, 480));
    t1.name = "harmony"; t1.notes.push_back (note (64, 0, 480));
    raw.tracks.push_back (t0);
    raw.tracks.push_back (t1);

    lotro::Config cfg;
    cfg.input = "x.mid";
    lotro::ConfigInstrument inst;
    inst.x       = 1;
    inst.name    = "LuteOfAges";
    inst.sources = { 0, 1 };
    cfg.instruments.push_back (inst);

    lotro::Diagnostics diag;
    auto song = lotro::assembleInstruments (raw, cfg, diag);
    runPipeline (song, diag);
    const auto abc = lotro::writeAbc (song);

    CHECK (contains (abc, "[C"));
    CHECK (contains (abc, "E"));
    CHECK (contains (abc, "X:1"));
}

TEST_CASE ("config-pipeline: per-instrument transpose shifts the emitted pitch", "[config-pipeline]")
{
    auto raw = rawSong();
    lotro::Track t;
    t.name = "src"; t.notes.push_back (note (60, 0, 480));
    raw.tracks.push_back (t);

    lotro::Config cfg;
    cfg.input = "x.mid";
    lotro::ConfigInstrument inst;
    inst.x                  = 1;
    inst.name               = "LuteOfAges";
    inst.sources            = { 0 };
    inst.transposeSemitones = 12;
    cfg.instruments.push_back (inst);

    lotro::Diagnostics diag;
    auto song = lotro::assembleInstruments (raw, cfg, diag);
    runPipeline (song, diag);
    const auto abc = lotro::writeAbc (song);

    CHECK (contains (abc, "c"));
}

TEST_CASE ("config-pipeline: volume scale down lands in the quieter dynamic bucket", "[config-pipeline]")
{
    auto raw = rawSong();
    lotro::Track t;
    t.name = "src"; t.notes.push_back (note (60, 0, 480, 100));
    raw.tracks.push_back (t);

    lotro::Config cfg;
    cfg.input = "x.mid";
    lotro::ConfigInstrument inst;
    inst.x             = 1;
    inst.name          = "LuteOfAges";
    inst.sources       = { 0 };
    // -70%: velocity 100 * 0.3 = 30 -> pp bucket.
    inst.volumePercent = -70;
    cfg.instruments.push_back (inst);

    lotro::Diagnostics diag;
    auto song = lotro::assembleInstruments (raw, cfg, diag);
    runPipeline (song, diag);
    const auto abc = lotro::writeAbc (song);

    CHECK (contains (abc, "+pp+"));
    CHECK_FALSE (contains (abc, "+mf+"));
}

TEST_CASE ("config-pipeline: three instruments emit in x-ascending order", "[config-pipeline]")
{
    auto raw = rawSong();
    for (int i = 0; i < 3; ++i)
    {
        lotro::Track t;
        t.name = "src" + std::to_string (i);
        t.notes.push_back (note (60 + i, 0, 480));
        raw.tracks.push_back (t);
    }

    lotro::Config cfg;
    cfg.input = "x.mid";
    int xs[]      = { 7, 1, 3 };
    int sources[] = { 0, 1, 2 };
    for (size_t i = 0; i < 3; ++i)
    {
        lotro::ConfigInstrument inst;
        inst.x       = xs[i];
        inst.name    = "LuteOfAges";
        inst.sources = { sources[i] };
        cfg.instruments.push_back (inst);
    }

    lotro::Diagnostics diag;
    auto song = lotro::assembleInstruments (raw, cfg, diag);
    runPipeline (song, diag);
    const auto abc = lotro::writeAbc (song);

    const auto x1 = abc.find ("X:1");
    const auto x3 = abc.find ("X:3");
    const auto x7 = abc.find ("X:7");
    REQUIRE (x1 != std::string::npos);
    REQUIRE (x3 != std::string::npos);
    REQUIRE (x7 != std::string::npos);
    CHECK (x1 < x3);
    CHECK (x3 < x7);
}

TEST_CASE ("config-pipeline: config label becomes T: suffix", "[config-pipeline]")
{
    auto raw = rawSong();
    lotro::Track t;
    t.name = "Boring MIDI Name";
    t.notes.push_back (note (60, 0, 480));
    raw.tracks.push_back (t);

    lotro::Config cfg;
    cfg.input = "x.mid";
    cfg.title = std::string ("Test Song");
    lotro::ConfigInstrument inst;
    inst.x       = 1;
    inst.name    = "LuteOfAges";
    inst.label   = std::string ("Lead Lute");
    inst.sources = { 0 };
    cfg.instruments.push_back (inst);

    lotro::Diagnostics diag;
    auto song = lotro::assembleInstruments (raw, cfg, diag);
    runPipeline (song, diag);
    const auto abc = lotro::writeAbc (song);

    CHECK (contains (abc, "T:Test Song - Lead Lute"));
    CHECK_FALSE (contains (abc, "Boring MIDI Name"));
}

TEST_CASE ("config-pipeline: full JSON round-trip through loader + assembly + writer", "[config-pipeline][json]")
{
    auto raw = rawSong();
    lotro::Track t;
    t.name = "src"; t.notes.push_back (note (60, 0, 480));
    raw.tracks.push_back (t);

    const std::string json = R"({
        "input": "x.mid",
        "title": "RoundTrip",
        "instruments": [
            { "x": 1, "name": "Harp", "label": "Harp Part", "sources": [0] }
        ]
    })";

    lotro::Config cfg;
    REQUIRE (lotro::loadConfigFromString (json, lotro::ConfigFormat::Json, cfg).empty());

    lotro::Diagnostics diag;
    auto song = lotro::assembleInstruments (raw, cfg, diag);
    runPipeline (song, diag);
    const auto abc = lotro::writeAbc (song);

    CHECK (contains (abc, "T:RoundTrip - Harp Part"));
    CHECK (contains (abc, "X:1"));
}
