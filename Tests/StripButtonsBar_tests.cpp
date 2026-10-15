#include "UI/Playback/StripButtonsBar.h"

#include <catch2/catch_test_macros.hpp>

using namespace lotro;
using namespace lotro::dock;

namespace
{
StripButton cmd (const char* key) { return { StripButton::Kind::command, key }; }
StripButton layout (const char* name) { return { StripButton::Kind::applyLayout, name }; }

struct Rig
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    StripButtonsHost host;
    std::vector<int> ran;
    int persisted = 0;
    MenuState state;

    Rig()
    {
        state.layoutNames = { "Wide" };
        host.menuState = [this] { return state; };
        host.persist = [this] { ++persisted; };
        host.run = [this] (int id) { ran.push_back (id); };
    }
};
}

TEST_CASE ("visibleButtonCount: everything fits, or the tail moves behind the overflow button", "[strip-buttons][bar]")
{
    CHECK (visibleButtonCount ({ 50, 50, 50 }, 400, 4, 20) == 3);
    CHECK (visibleButtonCount ({ 50, 50, 50 }, 158, 4, 20) == 3);   // 50+4+50+4+50 = 158 exactly fits
    CHECK (visibleButtonCount ({ 50, 50, 50 }, 157, 4, 20) == 2);   // 50+4+50 + gap + overflow(20) = 128 <= 157
    CHECK (visibleButtonCount ({ 50, 50, 50 }, 60, 4, 20) == 0);    // not even one plus the overflow button
    CHECK (visibleButtonCount ({}, 100, 4, 20) == 0);
    CHECK (visibleButtonCount ({ 50 }, 0, 4, 20) == 0);
}

TEST_CASE ("StripButtonsBar: shows one button per host entry and hides unknown command keys", "[strip-buttons][bar]")
{
    Rig r;
    r.host.buttons = { cmd ("file.save"), cmd ("future.thing"), layout ("Wide") };
    StripButtonsBar bar (r.host);
    bar.setSize (600, 28);
    CHECK (bar.visibleButtonsForTesting() == 2);
    CHECK_FALSE (bar.hasOverflowForTesting());
}

TEST_CASE ("StripButtonsBar: clicking a command button runs its menu command id", "[strip-buttons][bar]")
{
    Rig r;
    r.state.hasAbc = true;
    r.host.buttons = { cmd ("file.exportAbc") };
    StripButtonsBar bar (r.host);
    bar.setSize (600, 28);
    bar.clickButtonForTesting (0);
    REQUIRE (r.ran.size() == 1);
    CHECK (r.ran[0] == FileExportAbc);
}

TEST_CASE ("StripButtonsBar: a layout button runs the layout's apply id", "[strip-buttons][bar]")
{
    Rig r;
    r.host.buttons = { layout ("Wide") };
    StripButtonsBar bar (r.host);
    bar.setSize (600, 28);
    bar.clickButtonForTesting (0);
    REQUIRE (r.ran.size() == 1);
    CHECK (r.ran[0] == ViewLayoutApplyBase);
}

TEST_CASE ("StripButtonsBar: a button is disabled while its command is disabled", "[strip-buttons][bar]")
{
    Rig r;
    r.host.buttons = { cmd ("edit.undo") };
    StripButtonsBar bar (r.host);
    bar.setSize (600, 28);
    CHECK_FALSE (bar.buttonForTesting (0).isEnabled());
    r.state.canUndo = true;
    bar.refresh();
    CHECK (bar.buttonForTesting (0).isEnabled());
}

TEST_CASE ("StripButtonsBar: a panel-toggle button shows as on while its panel is visible", "[strip-buttons][bar]")
{
    Rig r;
    r.host.buttons = { cmd ("view.mixer") };
    StripButtonsBar bar (r.host);
    bar.setSize (600, 28);
    CHECK_FALSE (bar.buttonForTesting (0).getToggleState());
    r.state.mixerVisible = true;
    bar.refresh();
    CHECK (bar.buttonForTesting (0).getToggleState());
}

TEST_CASE ("StripButtonsBar: the picker lists every group, greys out what is on the strip and layouts apply by name", "[strip-buttons][bar]")
{
    Rig r;
    r.host.buttons = { cmd ("file.save") };
    StripButtonsBar bar (r.host);
    bar.setSize (600, 28);
    const auto menu = bar.pickerMenuForTesting();

    juce::StringArray tops;
    juce::PopupMenu::MenuItemIterator it (menu);
    while (it.next())
        if (it.getItem().subMenu != nullptr)
            tops.add (it.getItem().text);
    CHECK (tops == juce::StringArray { "File", "Edit", "Song", "Transport", "View", "Apply Layout" });

    // "Save" is already on the strip: present but disabled.
    bool foundSave = false;
    juce::PopupMenu::MenuItemIterator all (menu, true);
    while (all.next())
        if (all.getItem().text == "Save")
        {
            foundSave = true;
            CHECK_FALSE (all.getItem().isEnabled);
        }
    CHECK (foundSave);
}

TEST_CASE ("StripButtonsBar: removing a button updates the host, persists and notifies the other bars", "[strip-buttons][bar]")
{
    Rig r;
    r.host.buttons = { cmd ("file.save"), cmd ("file.new") };
    StripButtonsBar a (r.host), b (r.host);
    a.setSize (600, 28);
    b.setSize (600, 28);
    a.removeButtonForTesting (0);
    REQUIRE (r.host.buttons.size() == 1);
    CHECK (r.host.buttons[0].key == "file.new");
    CHECK (r.persisted == 1);
    CHECK (b.visibleButtonsForTesting() == 1);   // the other bar followed
}

TEST_CASE ("StripButtonsBar: a narrow bar moves trailing buttons behind the overflow button", "[strip-buttons][bar]")
{
    Rig r;
    r.host.buttons = { cmd ("file.new"), cmd ("file.open"), cmd ("file.save"), cmd ("file.saveAs"), cmd ("file.exportMidi") };
    StripButtonsBar bar (r.host);
    bar.setSize (600, 28);
    CHECK_FALSE (bar.hasOverflowForTesting());
    bar.setSize (150, 28);
    CHECK (bar.hasOverflowForTesting());
    CHECK (bar.visibleButtonsForTesting() < 5);
    bar.setSize (10, 28);                         // narrower than anything: must not crash
    CHECK (bar.visibleButtonsForTesting() == 0);
}

TEST_CASE ("StripButtonsBar: the picker is fully disabled once ten buttons are on the strip", "[strip-buttons][bar]")
{
    Rig r;
    for (const auto* k : { "file.new", "file.open", "file.save", "file.saveAs", "file.importMidi",
                           "file.exportMidi", "file.exportAbc", "file.preferences", "edit.undo", "edit.redo" })
        r.host.buttons.push_back (cmd (k));
    StripButtonsBar bar (r.host);
    bar.setSize (600, 28);
    const auto fullMenu = bar.pickerMenuForTesting();
    juce::PopupMenu::MenuItemIterator all (fullMenu, true);
    while (all.next())
        if (all.getItem().itemID != 0)
            CHECK_FALSE (all.getItem().isEnabled);
}

TEST_CASE ("StripButtonsBar: a layout button whose layout was removed is disabled but can still be removed", "[strip-buttons][bar]")
{
    Rig r;
    r.host.buttons = { layout ("Gone") };
    StripButtonsBar bar (r.host);
    bar.setSize (600, 28);
    REQUIRE (bar.visibleButtonsForTesting() == 1);
    CHECK_FALSE (bar.buttonForTesting (0).isEnabled());
    bar.removeButtonForTesting (0);
    CHECK (r.host.buttons.empty());
}
