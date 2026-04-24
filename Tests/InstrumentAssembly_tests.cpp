#include "Core/InstrumentAssembly.h"

#include <catch2/catch_test_macros.hpp>
#include <juce_core/juce_core.h>

#include <algorithm>

namespace
{
    lotro::Note makeNote (int pitch, int startTick, int dur, int velocity)
    {
        lotro::Note n;
        n.pitch            = pitch;
        n.startTick        = startTick;
        n.durationTicks    = dur;
        n.velocity         = velocity;
        n.sourceTrackIndex = 0;
        n.sourceEventIndex = 0;
        return n;
    }

    lotro::Song threeTrackRaw()
    {
        lotro::Song s;
        s.ticksPerQuarter = 480;
        s.tempoMap.push_back ({ 0, 120.0 });
        s.meterMap.push_back ({ 0, 4, 4 });

        for (int i = 0; i < 3; ++i)
        {
            lotro::Track t;
            t.name = "MidiTrack" + std::to_string (i);
            t.notes.push_back (makeNote (60 + i, 0, 480, 100));
            s.tracks.push_back (t);
        }
        return s;
    }
}

TEST_CASE ("assembly: one instrument per track, no merging", "[assembly]")
{
    const auto raw = threeTrackRaw();

    lotro::Config cfg;
    cfg.input = "x.mid";
    for (int i = 0; i < 3; ++i)
    {
        lotro::ConfigInstrument inst;
        inst.x       = i + 1;
        inst.name    = "LuteOfAges";
        inst.sources = { i };
        cfg.instruments.push_back (inst);
    }

    lotro::Diagnostics diag;
    const auto assembled = lotro::assembleInstruments (raw, cfg, diag);

    REQUIRE (assembled.tracks.size() == 3);
    for (int i = 0; i < 3; ++i)
    {
        CHECK (assembled.tracks[i].x == i + 1);
        CHECK (assembled.tracks[i].notes.size() == 1);
        CHECK (assembled.tracks[i].notes[0].pitch == 60 + i);
    }
}

TEST_CASE ("assembly: two sources merge into one instrument", "[assembly]")
{
    const auto raw = threeTrackRaw();

    lotro::Config cfg;
    cfg.input = "x.mid";
    lotro::ConfigInstrument inst;
    inst.x       = 1;
    inst.name    = "LuteOfAges";
    inst.sources = { 0, 2 };
    cfg.instruments.push_back (inst);

    lotro::Diagnostics diag;
    const auto assembled = lotro::assembleInstruments (raw, cfg, diag);

    REQUIRE (assembled.tracks.size() == 1);
    const auto& t = assembled.tracks[0];
    CHECK (t.notes.size() == 2);

    std::vector<int> pitches;
    for (const auto& n : t.notes) pitches.push_back (n.pitch);
    std::sort (pitches.begin(), pitches.end());
    CHECK (pitches == std::vector<int>{ 60, 62 });
}

TEST_CASE ("assembly: unreferenced MIDI track emits diagnostic and is dropped", "[assembly]")
{
    const auto raw = threeTrackRaw();

    lotro::Config cfg;
    cfg.input = "x.mid";
    lotro::ConfigInstrument inst;
    inst.x       = 1;
    inst.name    = "LuteOfAges";
    inst.sources = { 0 };
    cfg.instruments.push_back (inst);

    lotro::Diagnostics diag;
    const auto assembled = lotro::assembleInstruments (raw, cfg, diag);

    CHECK (assembled.tracks.size() == 1);
    int droppedCount = 0;
    for (const auto& d : diag)
        if (d.source == "InstrumentAssembly" && d.severity == lotro::Severity::Info)
            ++droppedCount;
    CHECK (droppedCount == 2);
}

TEST_CASE ("assembly: per-instrument transposeSemitones shifts every note", "[assembly]")
{
    const auto raw = threeTrackRaw();

    lotro::Config cfg;
    cfg.input = "x.mid";
    lotro::ConfigInstrument inst;
    inst.x                  = 1;
    inst.name               = "LuteOfAges";
    inst.sources            = { 0 };
    inst.transposeSemitones = -12;
    cfg.instruments.push_back (inst);

    lotro::Diagnostics diag;
    const auto assembled = lotro::assembleInstruments (raw, cfg, diag);

    REQUIRE (assembled.tracks.size() == 1);
    CHECK (assembled.tracks[0].notes[0].pitch == 48);
}

TEST_CASE ("assembly: global transpose adds to per-instrument transpose", "[assembly]")
{
    const auto raw = threeTrackRaw();

    lotro::Config cfg;
    cfg.input     = "x.mid";
    cfg.transpose = -5;
    lotro::ConfigInstrument inst;
    inst.x                  = 1;
    inst.name               = "LuteOfAges";
    inst.sources            = { 0 };
    inst.transposeSemitones = -7;
    cfg.instruments.push_back (inst);

    lotro::Diagnostics diag;
    const auto assembled = lotro::assembleInstruments (raw, cfg, diag);

    CHECK (assembled.tracks[0].notes[0].pitch == 48);
}

TEST_CASE ("assembly: volumePercent -20 scales velocity down 20%", "[assembly]")
{
    // Source velocity 100. volumePercent -20 -> scale 0.8 -> 80.
    const auto raw = threeTrackRaw();

    lotro::Config cfg;
    cfg.input = "x.mid";
    lotro::ConfigInstrument inst;
    inst.x             = 1;
    inst.name          = "LuteOfAges";
    inst.sources       = { 0 };
    inst.volumePercent = -20;
    cfg.instruments.push_back (inst);

    lotro::Diagnostics diag;
    const auto assembled = lotro::assembleInstruments (raw, cfg, diag);

    CHECK (assembled.tracks[0].notes[0].velocity == 80);
}

TEST_CASE ("assembly: volumePercent 0 leaves velocity unchanged", "[assembly]")
{
    // Struct default is 0 — no scaling applied, velocity passes through.
    const auto raw = threeTrackRaw();

    lotro::Config cfg;
    cfg.input = "x.mid";
    lotro::ConfigInstrument inst;
    inst.x       = 1;
    inst.name    = "LuteOfAges";
    inst.sources = { 0 };
    cfg.instruments.push_back (inst);

    lotro::Diagnostics diag;
    const auto assembled = lotro::assembleInstruments (raw, cfg, diag);

    CHECK (assembled.tracks[0].notes[0].velocity == 100);
}

TEST_CASE ("assembly: volumePercent 100 on velocity 100 clamps to 127 and emits Diagnostic", "[assembly]")
{
    // Source velocity 100. volumePercent +100 -> scale 2.0 -> 200 -> clamps to 127.
    const auto raw = threeTrackRaw();

    lotro::Config cfg;
    cfg.input = "x.mid";
    lotro::ConfigInstrument inst;
    inst.x             = 1;
    inst.name          = "LuteOfAges";
    inst.sources       = { 0 };
    inst.volumePercent = 100;
    cfg.instruments.push_back (inst);

    lotro::Diagnostics diag;
    const auto assembled = lotro::assembleInstruments (raw, cfg, diag);

    CHECK (assembled.tracks[0].notes[0].velocity == 127);

    bool sawClampDiag = false;
    for (const auto& d : diag)
        if (d.source == "VolumeScale") { sawClampDiag = true; break; }
    CHECK (sawClampDiag);
}

TEST_CASE ("assembly: label populates track.name; defaults to first source's MIDI name", "[assembly]")
{
    const auto raw = threeTrackRaw();

    lotro::Config cfg;
    cfg.input = "x.mid";

    lotro::ConfigInstrument withLabel;
    withLabel.x       = 1;
    withLabel.name    = "LuteOfAges";
    withLabel.label   = std::string ("Lead");
    withLabel.sources = { 0 };
    cfg.instruments.push_back (withLabel);

    lotro::ConfigInstrument noLabel;
    noLabel.x       = 2;
    noLabel.name    = "Harp";
    noLabel.sources = { 1 };
    cfg.instruments.push_back (noLabel);

    lotro::Diagnostics diag;
    const auto assembled = lotro::assembleInstruments (raw, cfg, diag);

    CHECK (assembled.tracks[0].name == "Lead");
    CHECK (assembled.tracks[1].name == "MidiTrack1");
}

TEST_CASE ("assembly: Drums instrument with drumMap populates track.drumMap", "[assembly]")
{
    const auto raw = threeTrackRaw();

    lotro::Config cfg;
    cfg.input = "x.mid";
    lotro::ConfigInstrument inst;
    inst.x       = 1;
    inst.name    = "Drums";
    inst.sources = { 0 };
    const auto drumMapPath = juce::File (__FILE__)
                                 .getParentDirectory()
                                 .getParentDirectory()
                                 .getChildFile ("drum_map.json")
                                 .getFullPathName()
                                 .toStdString();
    inst.drumMap = drumMapPath;
    cfg.instruments.push_back (inst);

    lotro::Diagnostics diag;
    const auto assembled = lotro::assembleInstruments (raw, cfg, diag);

    REQUIRE (assembled.tracks.size() == 1);
    CHECK (assembled.tracks[0].drumMap.has_value());
    CHECK (assembled.tracks[0].drumMap->size() > 0);
}
