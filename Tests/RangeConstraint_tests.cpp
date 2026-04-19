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

TEST_CASE ("range: notes below C, get transposed into the ABC envelope", "[range]")
{
    lotro::Track track;
    track.instrument = lotro::LotroInstrument::LuteOfAges;  // spec range 36..72
    track.notes.push_back (noteAt (36));                    // C,, in ABC — below envelope

    lotro::Diagnostics warnings;
    lotro::applyRangeConstraint (track, warnings);

    REQUIRE (track.notes.size() == 1);
    CHECK (track.notes.front().pitch >= 48);
    CHECK (track.notes.front().pitch <= 84);
}

TEST_CASE ("range: notes above c' get transposed into the ABC envelope", "[range]")
{
    lotro::Track track;
    track.instrument = lotro::LotroInstrument::Flute;       // spec range 60..96
    track.notes.push_back (noteAt (96));                    // c'' in ABC — above envelope

    lotro::Diagnostics warnings;
    lotro::applyRangeConstraint (track, warnings);

    REQUIRE (track.notes.size() == 1);
    CHECK (track.notes.front().pitch >= 48);
    CHECK (track.notes.front().pitch <= 84);
}
