#include "Core/Constraints/RangeConstraint.h"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

namespace
{
    lotro::Note noteAt (int pitch)
    {
        lotro::Note n;
        n.pitch         = pitch;
        n.startTick     = 0;
        n.durationTicks = 480;
        n.velocity      = 100;
        return n;
    }
}

TEST_CASE ("range: note above range is transposed down octaves until in range", "[range]")
{
    lotro::Track track;
    track.instrument = lotro::LotroInstrument::LuteOfAges; // 36..72
    track.notes.push_back (noteAt (85));                   // one octave too high

    lotro::Diagnostics warnings;
    lotro::applyRangeConstraint (track, warnings);

    REQUIRE (track.notes.size() == 1);
    CHECK (track.notes.front().pitch == 73 - 12);
    CHECK (warnings.empty());
}

TEST_CASE ("range: note below range is transposed up octaves until in range", "[range]")
{
    lotro::Track track;
    track.instrument = lotro::LotroInstrument::Flute;      // 60..96
    track.notes.push_back (noteAt (48));                   // one octave below

    lotro::Diagnostics warnings;
    lotro::applyRangeConstraint (track, warnings);

    REQUIRE (track.notes.size() == 1);
    CHECK (track.notes.front().pitch == 60);
    CHECK (warnings.empty());
}

TEST_CASE ("range: note already in range is untouched", "[range]")
{
    lotro::Track track;
    track.instrument = lotro::LotroInstrument::LuteOfAges;
    track.notes.push_back (noteAt (60));

    lotro::Diagnostics warnings;
    lotro::applyRangeConstraint (track, warnings);

    REQUIRE (track.notes.size() == 1);
    CHECK (track.notes.front().pitch == 60);
}

TEST_CASE ("range: drum track is skipped entirely", "[range]")
{
    lotro::Track track;
    track.instrument = lotro::LotroInstrument::Drums;
    track.notes.push_back (noteAt (200));

    lotro::Diagnostics warnings;
    lotro::applyRangeConstraint (track, warnings);

    REQUIRE (track.notes.size() == 1);
    CHECK (track.notes.front().pitch == 200);
}

TEST_CASE ("range: user transpose is applied before clamping", "[range]")
{
    lotro::Track track;
    track.instrument        = lotro::LotroInstrument::LuteOfAges;
    track.transposeSemitones = 12;
    track.notes.push_back (noteAt (36));                   // +12 → 48, hits the ABC envelope floor

    lotro::Diagnostics warnings;
    lotro::applyRangeConstraint (track, warnings);

    REQUIRE (track.notes.size() == 1);
    CHECK (track.notes.front().pitch == 48);
}

// Each LOTRO instrument has its own 36-semitone native MIDI range (see
// Picture1.jpg / LotroInstrument.cpp). Fold targets THAT range, not a
// shared C,..c' envelope — earlier code was intersecting with [48,84]
// which chopped off Theorbo's bottom and Flute's top octaves.
TEST_CASE ("range: LuteOfAges accepts MIDI 36 (its low C) without folding", "[range]")
{
    lotro::Track track;
    track.instrument = lotro::LotroInstrument::LuteOfAges;  // range 36..72
    track.notes.push_back (noteAt (36));

    lotro::Diagnostics warnings;
    lotro::applyRangeConstraint (track, warnings);

    REQUIRE (track.notes.size() == 1);
    CHECK (track.notes.front().pitch == 36);
    CHECK (warnings.empty());
}

TEST_CASE ("range: Flute accepts MIDI 96 (its high c) without folding", "[range]")
{
    lotro::Track track;
    track.instrument = lotro::LotroInstrument::Flute;       // range 60..96
    track.notes.push_back (noteAt (96));

    lotro::Diagnostics warnings;
    lotro::applyRangeConstraint (track, warnings);

    REQUIRE (track.notes.size() == 1);
    CHECK (track.notes.front().pitch == 96);
    CHECK (warnings.empty());
}

TEST_CASE ("range: Theorbo accepts MIDI 24 (its low C) without folding", "[range]")
{
    lotro::Track track;
    track.instrument = lotro::LotroInstrument::Theorbo;     // range 24..60
    track.notes.push_back (noteAt (24));

    lotro::Diagnostics warnings;
    lotro::applyRangeConstraint (track, warnings);

    REQUIRE (track.notes.size() == 1);
    CHECK (track.notes.front().pitch == 24);
    CHECK (warnings.empty());
}
