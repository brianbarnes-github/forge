#include "UI/Dock/StripButtons.h"

#include <catch2/catch_test_macros.hpp>

using namespace lotro::dock;

namespace
{
StripButton cmd (const char* key) { return { StripButton::Kind::command, key }; }
StripButton layout (const char* name) { return { StripButton::Kind::applyLayout, name }; }
}

TEST_CASE ("StripButtons: add refuses duplicates and empty keys, layout names compare case-insensitively", "[dock][strip-buttons]")
{
    StripButtons list;
    CHECK (addButton (list, cmd ("file.exportMidi")));
    CHECK_FALSE (addButton (list, cmd ("file.exportMidi")));
    CHECK_FALSE (addButton (list, cmd ("")));
    CHECK (addButton (list, layout ("Wide")));
    CHECK_FALSE (addButton (list, layout ("wide")));
    CHECK (addButton (list, cmd ("Wide")));            // a command key is not a layout name
    CHECK (list.size() == 3);
    CHECK (contains (list, layout ("WIDE")));
    CHECK_FALSE (contains (list, cmd ("file.save")));
}

TEST_CASE ("StripButtons: the strip holds at most ten buttons", "[dock][strip-buttons]")
{
    StripButtons list;
    for (std::size_t i = 0; i < maxStripButtons; ++i)
        REQUIRE (addButton (list, layout (("L" + std::to_string (i)).c_str())));
    CHECK_FALSE (addButton (list, cmd ("file.save")));
    CHECK (list.size() == 10);
}

TEST_CASE ("StripButtons: remove drops one button and ignores a bad index", "[dock][strip-buttons]")
{
    StripButtons list { cmd ("a"), cmd ("b"), cmd ("c") };
    removeButton (list, 1);
    CHECK (list.size() == 2);
    CHECK (list[1].key == "c");
    removeButton (list, 9);
    CHECK (list.size() == 2);
}

TEST_CASE ("StripButtons: renaming a layout renames its button, including a case-only change", "[dock][strip-buttons]")
{
    StripButtons list { cmd ("file.save"), layout ("layout 1"), layout ("Other") };
    renameLayoutButton (list, "layout 1", "Layout 1");
    CHECK (list[1].key == "Layout 1");
    renameLayoutButton (list, "Other", "Mixing");
    CHECK (list[2].key == "Mixing");
    renameLayoutButton (list, "file.save", "x");       // a command button is never touched
    CHECK (list[0].key == "file.save");
    renameLayoutButton (list, "nope", "x");
    CHECK (list.size() == 3);
}

TEST_CASE ("StripButtons: deleting a layout removes only its button", "[dock][strip-buttons]")
{
    StripButtons list { layout ("Wide"), cmd ("Wide"), layout ("Tall") };
    removeLayoutButton (list, "wide");
    REQUIRE (list.size() == 2);
    CHECK (list[0].kind == StripButton::Kind::command);
    CHECK (list[1].key == "Tall");
}

TEST_CASE ("StripButtons: JSON round-trips in order", "[dock][strip-buttons]")
{
    const StripButtons list { cmd ("file.exportMidi"), layout ("Wide"), cmd ("view.mixer") };
    const auto back = stripButtonsFromJson (toJson (list));
    REQUIRE (back.size() == 3);
    for (std::size_t i = 0; i < 3; ++i)
        CHECK (sameButton (back[i], list[i]));
}

TEST_CASE ("StripButtons: corrupt, wrong-shaped or partly bad JSON loads what is valid", "[dock][strip-buttons]")
{
    CHECK (stripButtonsFromJson ("").empty());
    CHECK (stripButtonsFromJson ("not json").empty());
    CHECK (stripButtonsFromJson ("{\"kind\":\"command\"}").empty());
    const auto some = stripButtonsFromJson (
        "[{\"kind\":\"command\",\"key\":\"file.save\"},"
        "{\"kind\":\"mystery\",\"key\":\"x\"},"                // unknown kind: dropped
        "{\"kind\":\"command\",\"key\":\"\"},"                 // empty key: dropped
        "{\"kind\":\"command\",\"key\":\"file.save\"},"        // duplicate: dropped
        "{\"kind\":\"command\",\"key\":\"future.thing\"},"     // unknown command key: KEPT for a newer build
        "7]");
    REQUIRE (some.size() == 2);
    CHECK (some[0].key == "file.save");
    CHECK (some[1].key == "future.thing");
}

TEST_CASE ("StripButtons: loading more than the cap keeps the first ten", "[dock][strip-buttons]")
{
    std::string json = "[";
    for (int i = 0; i < 15; ++i)
        json += std::string (i ? "," : "") + "{\"kind\":\"applyLayout\",\"key\":\"L" + std::to_string (i) + "\"}";
    json += "]";
    CHECK (stripButtonsFromJson (json).size() == maxStripButtons);
}
