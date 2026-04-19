#include "Core/AutoInstrument.h"
#include "Core/Track.h"

#include <catch2/catch_test_macros.hpp>

namespace
{
    lotro::Note noteAt (int pitch)
    {
        lotro::Note n;
        n.pitch         = pitch;
        n.durationTicks = 240;
        n.velocity      = 100;
        return n;
    }

    lotro::Track trackWithPitches (std::initializer_list<int> pitches)
    {
        lotro::Track t;
        for (int p : pitches) t.notes.push_back (noteAt (p));
        return t;
    }
}

TEST_CASE ("auto-instrument: bass-range track picks Theorbo", "[autoinst]")
{
    // Bass at MIDI 26-44 — none of this fits a Lute natively (36-72 on paper
    // but below 48 gets clamped up). Theorbo's 24-60 range covers all of it.
    auto track = trackWithPitches ({ 26, 32, 38, 40, 44 });
    CHECK (lotro::pickInstrumentForTrack (track) == lotro::LotroInstrument::Theorbo);
}

TEST_CASE ("auto-instrument: mid-range melody picks LuteOfAges by tiebreak", "[autoinst]")
{
    // Pitches all within 36-72 — Lute, BasicLute, Harp, Bassoon all fit.
    // LuteOfAges wins the tiebreak because it's first in the preference list.
    auto track = trackWithPitches ({ 48, 55, 60, 62, 67, 71 });
    CHECK (lotro::pickInstrumentForTrack (track) == lotro::LotroInstrument::LuteOfAges);
}

TEST_CASE ("auto-instrument: high-range melody picks Flute", "[autoinst]")
{
    // 72-90 — Flute (60-96) fits all, Lute (36-72) only fits the bottom note,
    // Clarinet (48-84) fits most but not the top ones.
    auto track = trackWithPitches ({ 72, 76, 80, 84, 88, 90 });
    CHECK (lotro::pickInstrumentForTrack (track) == lotro::LotroInstrument::Flute);
}

TEST_CASE ("auto-instrument: drums track stays Drums", "[autoinst]")
{
    lotro::Track track;
    track.instrument = lotro::LotroInstrument::Drums;
    track.notes.push_back (noteAt (38));
    CHECK (lotro::pickInstrumentForTrack (track) == lotro::LotroInstrument::Drums);
}

TEST_CASE ("auto-instrument: empty track keeps its current instrument", "[autoinst]")
{
    lotro::Track track;
    track.instrument = lotro::LotroInstrument::Harp;
    // No notes — nothing to score, preserve caller's choice
    CHECK (lotro::pickInstrumentForTrack (track) == lotro::LotroInstrument::Harp);
}

TEST_CASE ("auto-instrument: mixed-range picks instrument with most coverage", "[autoinst]")
{
    // Pitches spread 40-80 — Clarinet (48-84) covers 40-80... but 40 misses.
    // Theorbo (24-60) covers 40-60. Lute (36-72) covers 40-72. Clarinet covers
    // 48-80. Count: lute=covers 40-72 = 6 notes; clarinet=covers 48-80 = 6 notes.
    // Tiebreak → Lute wins.
    auto track = trackWithPitches ({ 40, 50, 60, 70, 75, 80 });
    // Lute: fits 40,50,60,70 = 4
    // Clarinet (48-84): fits 50,60,70,75,80 = 5
    CHECK (lotro::pickInstrumentForTrack (track) == lotro::LotroInstrument::Clarinet);
}
