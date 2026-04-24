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
        inst.sources = { { i, 0, 0 } };
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
    inst.sources = { { 0, 0, 0 }, { 2, 0, 0 } };
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
    inst.sources = { { 0, 0, 0 } };
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

TEST_CASE ("assembly: per-source transposeSemitones shifts every note from that source", "[assembly]")
{
    const auto raw = threeTrackRaw();

    lotro::Config cfg;
    cfg.input = "x.mid";
    lotro::ConfigInstrument inst;
    inst.x       = 1;
    inst.name    = "LuteOfAges";
    inst.sources = { { 0, -12, 0 } };
    cfg.instruments.push_back (inst);

    lotro::Diagnostics diag;
    const auto assembled = lotro::assembleInstruments (raw, cfg, diag);

    REQUIRE (assembled.tracks.size() == 1);
    CHECK (assembled.tracks[0].notes[0].pitch == 48);
}

TEST_CASE ("assembly: global transpose adds to per-source transpose", "[assembly]")
{
    const auto raw = threeTrackRaw();

    lotro::Config cfg;
    cfg.input     = "x.mid";
    cfg.transpose = -5;
    lotro::ConfigInstrument inst;
    inst.x       = 1;
    inst.name    = "LuteOfAges";
    inst.sources = { { 0, -7, 0 } };
    cfg.instruments.push_back (inst);

    lotro::Diagnostics diag;
    const auto assembled = lotro::assembleInstruments (raw, cfg, diag);

    CHECK (assembled.tracks[0].notes[0].pitch == 48);
}

// The user's per-source transpose must land literally when the target
// is in range. Previously, a RangeConstraint octave-fold was being
// applied AFTER the transpose, which silently undid the user's shift
// for notes near the envelope ceiling. See findings/transpose.md.
TEST_CASE ("assembly: +12 on an in-envelope source shifts the note up one octave", "[assembly][transpose]")
{
    lotro::Song raw;
    raw.ticksPerQuarter = 480;
    raw.tempoMap.push_back ({ 0, 120.0 });
    raw.meterMap.push_back ({ 0, 4, 4 });

    lotro::Track t0;
    t0.name = "Src";
    t0.notes.push_back (makeNote (60, 0, 480, 100));   // middle C — in LuteOfAges envelope [48,72]
    raw.tracks.push_back (t0);

    lotro::Config cfg;
    cfg.input = "x.mid";
    lotro::ConfigInstrument inst;
    inst.x       = 1;
    inst.name    = "LuteOfAges";
    inst.sources = { { 0, 12, 0 } };   // +12 semitones on source 0
    cfg.instruments.push_back (inst);

    lotro::Diagnostics diag;
    const auto assembled = lotro::assembleInstruments (raw, cfg, diag);

    REQUIRE (assembled.tracks.size() == 1);
    REQUIRE (assembled.tracks[0].notes.size() == 1);
    CHECK (assembled.tracks[0].notes[0].pitch == 72);   // literal +12, not folded back to 60
}

TEST_CASE ("assembly: transpose that pushes a note out of envelope drops it with a TransposeOutOfRange diagnostic", "[assembly][transpose]")
{
    lotro::Song raw;
    raw.ticksPerQuarter = 480;
    raw.tempoMap.push_back ({ 0, 120.0 });
    raw.meterMap.push_back ({ 0, 4, 4 });

    lotro::Track t0;
    t0.name = "Src";
    t0.notes.push_back (makeNote (65, 0, 480, 100));   // MIDI 65 on LuteOfAges ceiling 72
    raw.tracks.push_back (t0);

    lotro::Config cfg;
    cfg.input = "x.mid";
    lotro::ConfigInstrument inst;
    inst.x       = 1;
    inst.name    = "LuteOfAges";
    inst.sources = { { 0, 12, 0 } };   // +12 → 77, above 72 ceiling
    cfg.instruments.push_back (inst);

    lotro::Diagnostics diag;
    const auto assembled = lotro::assembleInstruments (raw, cfg, diag);

    REQUIRE (assembled.tracks.size() == 1);
    CHECK (assembled.tracks[0].notes.empty());

    const auto it = std::find_if (diag.begin(), diag.end(),
        [] (const lotro::Diagnostic& d) { return d.source == "TransposeOutOfRange"; });
    REQUIRE (it != diag.end());
    CHECK (it->severity == lotro::Severity::Warning);
    CHECK (it->pitch    == 65);
}

TEST_CASE ("assembly: source note outside envelope is songwriter-folded, then user transpose applies to the folded pitch", "[assembly][transpose]")
{
    lotro::Song raw;
    raw.ticksPerQuarter = 480;
    raw.tempoMap.push_back ({ 0, 120.0 });
    raw.meterMap.push_back ({ 0, 4, 4 });

    lotro::Track t0;
    t0.name = "Src";
    // MIDI 86 is above LuteOfAges ceiling 72. Songwriter-fold by -12 → 74 (still too high),
    // then -12 → 62 (in range). User transpose 0 → final 62.
    t0.notes.push_back (makeNote (86, 0, 480, 100));
    raw.tracks.push_back (t0);

    lotro::Config cfg;
    cfg.input = "x.mid";
    lotro::ConfigInstrument inst;
    inst.x       = 1;
    inst.name    = "LuteOfAges";
    inst.sources = { { 0, 0, 0 } };
    cfg.instruments.push_back (inst);

    lotro::Diagnostics diag;
    const auto assembled = lotro::assembleInstruments (raw, cfg, diag);

    REQUIRE (assembled.tracks.size() == 1);
    REQUIRE (assembled.tracks[0].notes.size() == 1);
    CHECK (assembled.tracks[0].notes[0].pitch == 62);   // folded, same pitch class as source
}

TEST_CASE ("assembly: volumePercent -20 scales velocity down 20%", "[assembly]")
{
    // Source velocity 100. volumePercent -20 -> scale 0.8 -> 80.
    const auto raw = threeTrackRaw();

    lotro::Config cfg;
    cfg.input = "x.mid";
    lotro::ConfigInstrument inst;
    inst.x       = 1;
    inst.name    = "LuteOfAges";
    inst.sources = { { 0, 0, -20 } };
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
    inst.sources = { { 0, 0, 0 } };
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
    inst.x       = 1;
    inst.name    = "LuteOfAges";
    inst.sources = { { 0, 0, 100 } };
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
    withLabel.sources = { { 0, 0, 0 } };
    cfg.instruments.push_back (withLabel);

    lotro::ConfigInstrument noLabel;
    noLabel.x       = 2;
    noLabel.name    = "Harp";
    noLabel.sources = { { 1, 0, 0 } };
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
    inst.sources = { { 0, 0, 0 } };
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
