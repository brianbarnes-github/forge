#include "Core/Constraints/DynamicMapper.h"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

namespace
{
    lotro::Note noteWithVelocity (int startTick, int velocity)
    {
        lotro::Note n;
        n.pitch         = 60;
        n.startTick     = startTick;
        n.durationTicks = 240;
        n.velocity      = velocity;
        return n;
    }
}

TEST_CASE ("dynamics: all-mf track produces no dynamic changes", "[dynamics]")
{
    lotro::Track track;
    for (int i = 0; i < 4; ++i)
        track.notes.push_back (noteWithVelocity (i * 240, 70));

    lotro::Diagnostics warnings;
    lotro::applyDynamicMapper (track, warnings);

    CHECK (track.dynamicChanges.empty());
}

TEST_CASE ("dynamics: starting in a non-mf bucket emits immediately", "[dynamics]")
{
    lotro::Track track;
    track.notes.push_back (noteWithVelocity (0, 40));      // p bucket

    lotro::Diagnostics warnings;
    lotro::applyDynamicMapper (track, warnings);

    REQUIRE (track.dynamicChanges.size() == 1);
    CHECK (track.dynamicChanges.front().startTick == 0);
    CHECK (track.dynamicChanges.front().marking == (int) lotro::DynamicMarking::p);
}

TEST_CASE ("dynamics: change to f emits one marker at the transition", "[dynamics]")
{
    lotro::Track track;
    track.notes.push_back (noteWithVelocity (   0, 70));   // mf
    track.notes.push_back (noteWithVelocity ( 480, 70));   // mf
    track.notes.push_back (noteWithVelocity ( 960, 90));   // f
    track.notes.push_back (noteWithVelocity (1440, 95));   // f

    lotro::Diagnostics warnings;
    lotro::applyDynamicMapper (track, warnings);

    REQUIRE (track.dynamicChanges.size() == 1);
    CHECK (track.dynamicChanges.front().startTick == 960);
    CHECK (track.dynamicChanges.front().marking == (int) lotro::DynamicMarking::f);
}

TEST_CASE ("dynamics: returning to mf emits an explicit +mf+ marker", "[dynamics]")
{
    lotro::Track track;
    track.notes.push_back (noteWithVelocity (   0, 40));   // p
    track.notes.push_back (noteWithVelocity ( 480, 70));   // mf

    lotro::Diagnostics warnings;
    lotro::applyDynamicMapper (track, warnings);

    REQUIRE (track.dynamicChanges.size() == 2);
    CHECK (track.dynamicChanges[0].marking == (int) lotro::DynamicMarking::p);
    CHECK (track.dynamicChanges[1].marking == (int) lotro::DynamicMarking::mf);
}

TEST_CASE ("dynamics: multiple notes at the same tick pick the loudest bucket", "[dynamics]")
{
    lotro::Track track;
    track.notes.push_back (noteWithVelocity (0,  40));   // p
    track.notes.push_back (noteWithVelocity (0, 120));   // fff  ← loudest wins
    track.notes.push_back (noteWithVelocity (0,  70));   // mf

    lotro::Diagnostics warnings;
    lotro::applyDynamicMapper (track, warnings);

    REQUIRE (track.dynamicChanges.size() == 1);
    CHECK (track.dynamicChanges.front().startTick == 0);
    CHECK (track.dynamicChanges.front().marking == (int) lotro::DynamicMarking::fff);
}

TEST_CASE ("dynamics: bucket boundary table matches spec section 2.7", "[dynamics]")
{
    using lotro::bucketForVelocity;
    using M = lotro::DynamicMarking;

    CHECK (bucketForVelocity (  1) == M::ppp);
    CHECK (bucketForVelocity ( 15) == M::ppp);
    CHECK (bucketForVelocity ( 16) == M::pp);
    CHECK (bucketForVelocity ( 32) == M::pp);
    CHECK (bucketForVelocity ( 33) == M::p);
    CHECK (bucketForVelocity ( 49) == M::p);
    CHECK (bucketForVelocity ( 50) == M::mp);
    CHECK (bucketForVelocity ( 65) == M::mp);
    CHECK (bucketForVelocity ( 66) == M::mf);
    CHECK (bucketForVelocity ( 81) == M::mf);
    CHECK (bucketForVelocity ( 82) == M::f);
    CHECK (bucketForVelocity ( 97) == M::f);
    CHECK (bucketForVelocity ( 98) == M::ff);
    CHECK (bucketForVelocity (113) == M::ff);
    CHECK (bucketForVelocity (114) == M::fff);
    CHECK (bucketForVelocity (127) == M::fff);
}
