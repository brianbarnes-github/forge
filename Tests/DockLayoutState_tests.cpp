#include "UI/Dock/DockLayoutState.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>

using namespace lotro::dock;
using Catch::Approx;

TEST_CASE ("dock: the default layout is today's page", "[dock][state]")
{
    const auto s = defaultLayout();
    CHECK (s.upperWeight == Approx (0.55));
    CHECK (s.lowerWeight == Approx (0.45));
    const auto* lotro = find (s, "lotroPreview");
    REQUIRE (lotro != nullptr);
    CHECK (lotro->mode == Mode::docked);
    CHECK (lotro->visible);
    CHECK (lotro->zone == Zone::bottom);
    const auto* ed = find (s, "midiEditor");
    REQUIRE (ed != nullptr);
    CHECK_FALSE (ed->visible);
    CHECK (ed->mode == Mode::floating);   // first open is floating
    CHECK (ed->zone == Zone::belowTracks);
}

TEST_CASE ("dock: show/hide keep the mode; float/dock keep visibility", "[dock][state]")
{
    auto s = defaultLayout();
    show (s, "midiEditor");
    CHECK (find (s, "midiEditor")->visible);
    CHECK (find (s, "midiEditor")->mode == Mode::floating);
    setDocked (s, "midiEditor");
    hide (s, "midiEditor");
    CHECK (find (s, "midiEditor")->mode == Mode::docked);
    CHECK_FALSE (find (s, "midiEditor")->visible);
    setFloating (s, "lotroPreview", FloatBounds { 10, 20, 640, 300 });
    CHECK (find (s, "lotroPreview")->visible);
    REQUIRE (find (s, "lotroPreview")->floatBounds.has_value());
    CHECK (find (s, "lotroPreview")->floatBounds->width == 640);
}

TEST_CASE ("dock: unknown ids are a no-op", "[dock][state]")
{
    auto s = defaultLayout();
    const auto before = toJson (s);
    show (s, "nope");
    hide (s, "nope");
    setDocked (s, "nope");
    setFloating (s, "nope", std::nullopt);
    moveToZone (s, "nope", Zone::bottom);
    CHECK (toJson (s) == before);
}

TEST_CASE ("dock: moveToZone appends last and renumbers both zones", "[dock][state]")
{
    auto s = defaultLayout();
    moveToZone (s, "midiEditor", Zone::bottom);
    CHECK (find (s, "midiEditor")->zone == Zone::bottom);
    CHECK (find (s, "midiEditor")->order > find (s, "lotroPreview")->order);
    CHECK (find (s, "lotroPreview")->order == 0);
}

TEST_CASE ("dock: JSON round-trips", "[dock][state]")
{
    auto s = defaultLayout();
    show (s, "midiEditor");
    setFloating (s, "midiEditor", FloatBounds { -5, 7, 900, 500 });
    s.upperWeight = 0.3;
    s.lowerWeight = 0.7;
    const auto back = fromJson (toJson (s));
    CHECK (back.upperWeight == Approx (0.3));
    CHECK (back.lowerWeight == Approx (0.7));
    CHECK (find (back, "midiEditor")->visible);
    CHECK (find (back, "midiEditor")->mode == Mode::floating);
    REQUIRE (find (back, "midiEditor")->floatBounds.has_value());
    CHECK (find (back, "midiEditor")->floatBounds->x == -5);
    CHECK (find (back, "midiEditor")->floatBounds->height == 500);
    CHECK (find (back, "lotroPreview")->zone == Zone::bottom);
}

TEST_CASE ("dock: corrupt, empty, wrong-typed or hostile JSON gives the defaults", "[dock][state]")
{
    for (const char* text : { "", "garbage", "{", "[]", "null", "{\"panels\": 5}",
                              "{\"upperWeight\": -1, \"lowerWeight\": 0, \"panels\": []}",
                              "{\"upperWeight\": \"wide\", \"panels\": [1, \"x\", null]}" })
    {
        const auto s = fromJson (text);
        CHECK (s.upperWeight > 0.0);
        CHECK (s.lowerWeight > 0.0);
        REQUIRE (find (s, "lotroPreview") != nullptr);
        REQUIRE (find (s, "midiEditor") != nullptr);
        CHECK (find (s, "lotroPreview")->visible);
        CHECK_FALSE (find (s, "midiEditor")->visible);
    }
}

TEST_CASE ("dock: normalise drops unknown ids, adds missing, clamps weights and bounds", "[dock][state]")
{
    LayoutState s;
    s.upperWeight = std::nan ("");
    s.panels.push_back ({ "ghost", Mode::docked, true, Zone::bottom, 0, 1.0, std::nullopt });
    s.panels.push_back ({ "midiEditor", Mode::floating, true, Zone::belowTracks, 0, -3.0,
                          FloatBounds { 0, 0, 0, 0 } });   // zero-size bounds are discarded
    const auto n = normalise (s, panelDefs());
    CHECK (find (n, "ghost") == nullptr);
    CHECK (find (n, "lotroPreview") != nullptr);
    CHECK (n.upperWeight > 0.0);
    CHECK (find (n, "midiEditor")->weight > 0.0);
    CHECK (find (n, "midiEditor")->visible);   // kept: it was a valid field
    CHECK_FALSE (find (n, "midiEditor")->floatBounds.has_value());
}

TEST_CASE ("dock: absurd numbers in the JSON neither overflow nor leave the panel unreachable", "[dock][state]")
{
    const auto s = fromJson ("{\"panels\": [{\"id\": \"midiEditor\", \"mode\": \"floating\", \"visible\": true,"
                             " \"zone\": \"belowTracks\", \"order\": 1e300, \"weight\": 1e300,"
                             " \"floatBounds\": {\"x\": 1e300, \"y\": -1e300, \"width\": 1e300, \"height\": 600}}]}");
    const auto* p = find (s, "midiEditor");
    REQUIRE (p != nullptr);
    CHECK (p->order >= 0);
    CHECK (p->order <= 1000);
    if (p->floatBounds)
    {
        CHECK (std::abs (p->floatBounds->x) <= 1000000);
        CHECK (std::abs (p->floatBounds->y) <= 1000000);
        CHECK (p->floatBounds->width <= 1000000);
    }
}

TEST_CASE ("dock: a panel entry with only an id keeps that panel's defaults", "[dock][state]")
{
    const auto s = fromJson ("{\"panels\": [{\"id\": \"lotroPreview\"}, {\"id\": \"midiEditor\"}]}");
    CHECK (find (s, "lotroPreview")->visible);
    CHECK (find (s, "lotroPreview")->mode == Mode::docked);
    CHECK (find (s, "lotroPreview")->zone == Zone::bottom);
    CHECK_FALSE (find (s, "midiEditor")->visible);
    CHECK (find (s, "midiEditor")->mode == Mode::floating);
    CHECK (find (s, "midiEditor")->zone == Zone::belowTracks);
}
