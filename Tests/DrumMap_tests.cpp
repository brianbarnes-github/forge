#include "Core/DrumMap.h"

#include <catch2/catch_test_macros.hpp>

using lotro::mapDrumPitch;

TEST_CASE ("drum map: spec section 2.6 entries map to expected ABC notes", "[drummap]")
{
    const struct { int gm; const char* abc; } expected[] = {
        { 35, "C"  }, { 36, "D"  }, { 37, "E"  }, { 38, "F"  },
        { 39, "G"  }, { 40, "A"  }, { 41, "B"  }, { 42, "c"  },
        { 43, "d"  }, { 44, "e"  }, { 45, "f"  }, { 46, "g"  },
        { 47, "a"  }, { 48, "b"  }, { 49, "c'" }, { 50, "d'" },
        { 51, "e'" },
    };

    for (const auto& e : expected)
    {
        auto result = mapDrumPitch (e.gm);
        REQUIRE (result.has_value());
        CHECK (*result == e.abc);
    }
}

TEST_CASE ("drum map: extended GM percussion (52-59) folds onto closest-kin slots", "[drummap]")
{
    const struct { int gm; const char* abc; } extended[] = {
        { 52, "c'" },   // China Cymbal   → Crash
        { 53, "e'" },   // Ride Bell      → Ride
        { 54, "g"  },   // Tambourine     → Open Hi-Hat
        { 55, "c'" },   // Splash         → Crash
        { 56, "e'" },   // Cowbell        → Ride
        { 57, "c'" },   // Crash 2        → Crash
        { 59, "e'" },   // Ride 2         → Ride
    };
    for (const auto& e : extended)
    {
        auto result = mapDrumPitch (e.gm);
        REQUIRE (result.has_value());
        CHECK (*result == e.abc);
    }
}

TEST_CASE ("drum map: unmapped GM notes return nullopt", "[drummap]")
{
    CHECK_FALSE (mapDrumPitch (0).has_value());
    CHECK_FALSE (mapDrumPitch (34).has_value());
    CHECK_FALSE (mapDrumPitch (58).has_value());   // Vibraslap — intentionally unmapped
    CHECK_FALSE (mapDrumPitch (127).has_value());
}

TEST_CASE ("drum map: DrumMap class defaults empty, set/clear/lookup", "[drummap]")
{
    lotro::DrumMap m;
    CHECK (m.empty());

    m.set (35, "C");
    m.set (38, "F");
    CHECK (m.size() == 2);

    const auto hit = m.lookup (35);
    REQUIRE (hit.has_value());
    CHECK (*hit == "C");

    CHECK_FALSE (m.lookup (99).has_value());

    m.clear();
    CHECK (m.empty());
}

TEST_CASE ("drum map: defaultDrumMap() populates spec entries", "[drummap]")
{
    const auto m = lotro::defaultDrumMap();
    REQUIRE (m.size() >= 17);   // spec §2.6 is 17 entries; extended adds more
    CHECK (*m.lookup (35) == "C");
    CHECK (*m.lookup (38) == "F");
    CHECK (*m.lookup (49) == "c'");
    CHECK (*m.lookup (57) == "c'");   // Crash 2 → Crash slot, extended
}

TEST_CASE ("drum map: set() replaces existing entry", "[drummap]")
{
    auto m = lotro::defaultDrumMap();
    m.set (35, "c'");      // remap kick to crash slot — weird, but user's choice
    CHECK (*m.lookup (35) == "c'");
}
