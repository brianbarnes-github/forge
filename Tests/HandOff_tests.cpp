#include "UI/Playback/HandOff.h"

#include <catch2/catch_test_macros.hpp>

#include <memory>

using namespace lotro;

namespace { struct Thing { int value; }; }

TEST_CASE ("HandOff: nothing published acquires null", "[playback][handoff]")
{
    HandOff<Thing> h;
    bool swapped = true;
    CHECK (h.acquire (swapped) == nullptr);
    CHECK (! swapped);
}

TEST_CASE ("HandOff: a published object is acquired once, then stays current", "[playback][handoff]")
{
    HandOff<Thing> h;
    h.publish (std::make_shared<Thing> (Thing { 1 }));
    bool swapped = false;
    auto* a = h.acquire (swapped);
    REQUIRE (a != nullptr);
    CHECK (a->value == 1);
    CHECK (swapped);
    auto* b = h.acquire (swapped);
    CHECK (b == a);
    CHECK (! swapped);
}

TEST_CASE ("HandOff: the replaced object is retired and freed by the message thread, never the audio thread", "[playback][handoff]")
{
    HandOff<Thing> h;
    std::weak_ptr<Thing> first;
    {
        auto p = std::make_shared<Thing> (Thing { 1 });
        first = p;
        h.publish (std::move (p));
    }
    bool swapped = false;
    h.acquire (swapped);
    h.publish (std::make_shared<Thing> (Thing { 2 }));
    CHECK (! first.expired());                 // audio thread still on #1
    CHECK (h.acquire (swapped)->value == 2);   // swaps, pushes #1 to the retire queue
    CHECK (! first.expired());                 // not freed on the audio side
    h.collectRetired();
    CHECK (first.expired());                   // freed on the message thread
}

TEST_CASE ("HandOff: an object published twice before the audio thread looks is dropped unseen", "[playback][handoff]")
{
    HandOff<Thing> h;
    std::weak_ptr<Thing> skipped;
    {
        auto p = std::make_shared<Thing> (Thing { 1 });
        skipped = p;
        h.publish (std::move (p));
    }
    h.publish (std::make_shared<Thing> (Thing { 2 }));
    CHECK (skipped.expired());
    bool swapped = false;
    CHECK (h.acquire (swapped)->value == 2);
}

TEST_CASE ("HandOff: republishing the still-pending object keeps it owned", "[playback][handoff]")
{
    HandOff<Thing> h;
    std::weak_ptr<Thing> w;
    {
        auto p = std::make_shared<Thing> (Thing { 7 });
        w = p;
        h.publish (p);
        h.publish (p);   // exchange() hands back the very same pointer
    }
    CHECK (! w.expired());           // HandOff must still own it; the audio thread has not seen it yet
    CHECK (h.liveCount() == 1);
    bool swapped = false;
    auto* a = h.acquire (swapped);
    REQUIRE (a != nullptr);
    CHECK (a->value == 7);
}
