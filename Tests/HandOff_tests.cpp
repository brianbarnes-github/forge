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

TEST_CASE ("HandOff: republishing the current object and acquiring it again keeps it alive", "[playback][handoff]")
{
    HandOff<Thing> h;
    std::weak_ptr<Thing> w;
    bool swapped = false;
    Thing* x = nullptr;
    {
        auto p = std::make_shared<Thing> (Thing { 5 });
        w = p;
        h.publish (p);
        x = h.acquire (swapped);
        h.publish (p);
    }
    CHECK (h.acquire (swapped) == x);   // same object again: pushed to the retire queue, still current
    h.collectRetired();
    REQUIRE (! w.expired());            // the audio thread is still using it
    CHECK (h.acquire (swapped) == x);
    CHECK (x->value == 5);
}

TEST_CASE ("HandOff: a current object republished then superseded before pickup is not freed early", "[playback][handoff]")
{
    HandOff<Thing> h;
    std::weak_ptr<Thing> w;
    bool swapped = false;
    Thing* x = nullptr;
    {
        auto p = std::make_shared<Thing> (Thing { 5 });
        w = p;
        h.publish (p);
        x = h.acquire (swapped);
        h.publish (p);
    }
    h.publish (std::make_shared<Thing> (Thing { 6 }));   // pending X handed back and dropped, X is still current
    REQUIRE (! w.expired());
    CHECK (x->value == 5);
    CHECK (h.acquire (swapped)->value == 6);              // audio moves on, X retired
    CHECK (! w.expired());                                // not freed on the audio side
    h.collectRetired();
    CHECK (w.expired());                                  // now, and only now, freed
}

TEST_CASE ("HandOff: repeatedly republishing one object cycles without losing or leaking it", "[playback][handoff]")
{
    HandOff<Thing> h;
    std::weak_ptr<Thing> w;
    bool swapped = false;
    Thing* x = nullptr;
    {
        auto p = std::make_shared<Thing> (Thing { 9 });
        w = p;
        for (int i = 0; i < 50; ++i)
        {
            h.publish (p);
            x = h.acquire (swapped);
        }
    }
    h.collectRetired();
    REQUIRE (! w.expired());
    CHECK (x->value == 9);
    CHECK (h.liveCount() == 1);
    h.publish (std::make_shared<Thing> (Thing { 10 }));
    h.acquire (swapped);
    h.collectRetired();
    CHECK (w.expired());
    CHECK (h.liveCount() == 1);
}
