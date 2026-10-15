#include "UI/Dock/SavedLayouts.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace lotro::dock;
using Catch::Approx;

namespace
{
LayoutState layoutWithUpper (double w)
{
    auto s = defaultLayout();
    s.upperWeight = w;
    s.lowerWeight = 1.0 - w;
    return s;
}
}

TEST_CASE ("SavedLayouts: new layouts are named Layout 1, 2, 3", "[dock][layouts]")
{
    SavedLayouts list;
    CHECK (addLayout (list, defaultLayout()) == "Layout 1");
    CHECK (addLayout (list, defaultLayout()) == "Layout 2");
    CHECK (addLayout (list, defaultLayout()) == "Layout 3");
    CHECK (list.size() == 3);
}

TEST_CASE ("SavedLayouts: a deleted number is reused and a renamed one is skipped", "[dock][layouts]")
{
    SavedLayouts list;
    for (int i = 0; i < 3; ++i) addLayout (list, defaultLayout());
    removeLayout (list, 1);                                   // Layout 2 goes
    CHECK (nextLayoutName (list) == "Layout 2");
    REQUIRE_FALSE (renameLayout (list, 1, "layout 2"));        // Layout 3 becomes "layout 2": in use, any case
    CHECK (nextLayoutName (list) == "Layout 3");
}

TEST_CASE ("SavedLayouts: rename trims, and refuses empty or duplicate names", "[dock][layouts]")
{
    SavedLayouts list;
    addLayout (list, defaultLayout());
    addLayout (list, defaultLayout());
    CHECK_FALSE (renameLayout (list, 0, "  Wide  "));
    CHECK (list[0].name == "Wide");
    CHECK (renameLayout (list, 0, "   ") == LayoutNameError::empty);
    CHECK (renameLayout (list, 1, "WIDE") == LayoutNameError::duplicate);
    CHECK (list[1].name == "Layout 2");
    CHECK_FALSE (renameLayout (list, 0, "WIDE"));              // its own name, other case
    CHECK (list[0].name == "WIDE");
    CHECK (renameLayout (list, 9, "x") == LayoutNameError::empty);   // out of range changes nothing
}

TEST_CASE ("SavedLayouts: JSON round-trips names, order and layouts", "[dock][layouts]")
{
    SavedLayouts list;
    addLayout (list, layoutWithUpper (0.3));
    addLayout (list, layoutWithUpper (0.7));
    REQUIRE_FALSE (renameLayout (list, 0, "Mixing"));

    const auto back = savedLayoutsFromJson (toJson (list));
    REQUIRE (back.size() == 2);
    CHECK (back[0].name == "Mixing");
    CHECK (back[0].layout.upperWeight == Approx (0.3));
    CHECK (back[1].name == "Layout 2");
    CHECK (back[1].layout.upperWeight == Approx (0.7));
}

TEST_CASE ("SavedLayouts: empty or corrupt JSON reads as no layouts, nameless entries are dropped", "[dock][layouts]")
{
    CHECK (savedLayoutsFromJson ("").empty());
    CHECK (savedLayoutsFromJson ("not json").empty());
    CHECK (savedLayoutsFromJson ("{}").empty());
    CHECK (savedLayoutsFromJson (R"([{"name":"","layout":{}},{"layout":{}},3])").empty());
    const auto kept = savedLayoutsFromJson (R"([{"name":"A","layout":{}}])");
    REQUIRE (kept.size() == 1);
    CHECK (kept[0].layout.panels.size() == defaultLayout().panels.size());   // a bad layout normalises to defaults
}
