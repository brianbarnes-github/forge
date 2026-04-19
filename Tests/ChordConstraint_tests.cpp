#include "Core/Constraints/ChordConstraint.h"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

namespace
{
    lotro::Note make (int pitch, int velocity = 100, int duration = 480)
    {
        lotro::Note n;
        n.pitch         = pitch;
        n.startTick     = 0;
        n.durationTicks = duration;
        n.velocity      = velocity;
        return n;
    }
}

TEST_CASE ("chord: four simultaneous notes are unchanged", "[chord]")
{
    lotro::Track track;
    for (int pitch : { 60, 64, 67, 71 })
        track.notes.push_back (make (pitch));

    lotro::Diagnostics warnings;
    lotro::applyChordConstraint (track, warnings);

    CHECK (track.notes.size() == 4);
    CHECK (warnings.empty());
}

TEST_CASE ("chord: exactly six simultaneous notes are unchanged", "[chord]")
{
    lotro::Track track;
    for (int pitch : { 60, 62, 64, 65, 67, 69 })
        track.notes.push_back (make (pitch));

    lotro::Diagnostics warnings;
    lotro::applyChordConstraint (track, warnings);

    CHECK (track.notes.size() == 6);
    CHECK (warnings.empty());
}

TEST_CASE ("chord: eight-note chord trims to top six by velocity", "[chord]")
{
    lotro::Track track;
    for (int i = 0; i < 8; ++i)
        track.notes.push_back (make (60 + i, /*velocity*/ 50 + i));

    lotro::Diagnostics warnings;
    lotro::applyChordConstraint (track, warnings);

    REQUIRE (track.notes.size() == 6);
    CHECK (warnings.size() == 1);

    std::vector<int> pitches;
    for (const auto& n : track.notes)
        pitches.push_back (n.pitch);
    CHECK (pitches == std::vector<int> { 62, 63, 64, 65, 66, 67 });
}

TEST_CASE ("chord: notes at same start keep their individual durations", "[chord]")
{
    // In the held-note chord-emission model, unequal inner durations are
    // meaningful — the shortest controls the stream advance, the longest
    // keeps ringing. ChordConstraint must not flatten them.
    lotro::Track track;
    track.notes.push_back (make (60, 100, 480));
    track.notes.push_back (make (64, 100, 240));

    lotro::Diagnostics warnings;
    lotro::applyChordConstraint (track, warnings);

    REQUIRE (track.notes.size() == 2);
    CHECK (track.notes[0].durationTicks == 480);
    CHECK (track.notes[1].durationTicks == 240);
}
