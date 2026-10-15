#include "UI/Dock/StripButtons.h"
#include "UI/MenuModel.h"

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

TEST_CASE ("StripButtons: a command button takes its label and enabled state from the menu", "[dock][strip-buttons]")
{
    lotro::MenuState s;
    const auto abc = buttonView (cmd ("file.exportAbc"), s);
    REQUIRE (abc.has_value());
    CHECK (abc->label == "Export ABC");
    CHECK_FALSE (abc->enabled);              // no ABC yet
    s.hasAbc = true;
    CHECK (buttonView (cmd ("file.exportAbc"), s)->enabled);
    CHECK_FALSE (buttonView (cmd ("edit.undo"), s)->enabled);
    s.canUndo = true;
    CHECK (buttonView (cmd ("edit.undo"), s)->enabled);
}

TEST_CASE ("StripButtons: a panel-toggle button is pressed while its panel is visible", "[dock][strip-buttons]")
{
    lotro::MenuState s;
    s.mixerVisible = false;
    CHECK_FALSE (buttonView (cmd ("view.mixer"), s)->pressed);
    s.mixerVisible = true;
    CHECK (buttonView (cmd ("view.mixer"), s)->pressed);
    CHECK_FALSE (buttonView (cmd ("file.save"), s)->pressed);
}

TEST_CASE ("StripButtons: an unknown command key has no view and no command id", "[dock][strip-buttons]")
{
    const lotro::MenuState s;
    CHECK_FALSE (buttonView (cmd ("future.thing"), s).has_value());
    CHECK_FALSE (commandIdFor (cmd ("future.thing"), s).has_value());
}

TEST_CASE ("StripButtons: a command button resolves to its menu command id", "[dock][strip-buttons]")
{
    const lotro::MenuState s;
    CHECK (commandIdFor (cmd ("file.exportMidi"), s) == lotro::FileExportMidi);
    CHECK (commandIdFor (cmd ("view.layoutManage"), s) == lotro::ViewLayoutManage);
}

TEST_CASE ("StripButtons: a layout button resolves by name to the layout's apply id", "[dock][strip-buttons]")
{
    lotro::MenuState s;
    s.layoutNames = { "Wide", "Tall" };
    CHECK (commandIdFor (layout ("Tall"), s) == lotro::ViewLayoutApplyBase + 1);
    CHECK (commandIdFor (layout ("wide"), s) == lotro::ViewLayoutApplyBase);
    const auto v = buttonView (layout ("Tall"), s);
    REQUIRE (v.has_value());
    CHECK (v->label == "Tall");
    CHECK (v->enabled);
}

TEST_CASE ("StripButtons: a layout button whose layout is gone is shown disabled and runs nothing", "[dock][strip-buttons]")
{
    lotro::MenuState s;
    s.layoutNames = { "Wide" };
    const auto v = buttonView (layout ("Deleted"), s);
    REQUIRE (v.has_value());
    CHECK (v->label == "Deleted");
    CHECK_FALSE (v->enabled);
    CHECK_FALSE (commandIdFor (layout ("Deleted"), s).has_value());
}

TEST_CASE ("buttonView with prebuilt menus matches the single-argument overload", "[strip-buttons]")
{
    lotro::MenuState state;
    state.canUndo = true;
    state.mixerVisible = true;
    const auto menus = lotro::buildMenus (state);
    for (const auto& b : { StripButton { StripButton::Kind::command, "edit.undo" },
                           StripButton { StripButton::Kind::command, "edit.redo" },
                           StripButton { StripButton::Kind::command, "view.mixer" },
                           StripButton { StripButton::Kind::command, "nope" } })
    {
        const auto a = buttonView (b, state);
        const auto c = buttonView (b, state, menus);
        REQUIRE (a.has_value() == c.has_value());
        if (a)
        {
            CHECK (a->label == c->label);
            CHECK (a->enabled == c->enabled);
            CHECK (a->pressed == c->pressed);
        }
    }
}
