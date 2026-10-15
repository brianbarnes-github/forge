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

juce::MouseEvent mouseEvent (juce::Component& c, bool rightButton)
{
    const auto mods = rightButton ? juce::ModifierKeys (juce::ModifierKeys::rightButtonModifier)
                                  : juce::ModifierKeys (juce::ModifierKeys::leftButtonModifier);
    const auto t = juce::Time::getCurrentTime();
    return { juce::Desktop::getInstance().getMainMouseSource(), juce::Point<float> (3.0f, 3.0f), mods,
             juce::MouseInputSource::defaultPressure, juce::MouseInputSource::defaultOrientation,
             juce::MouseInputSource::defaultRotation, juce::MouseInputSource::defaultTiltX,
             juce::MouseInputSource::defaultTiltY, &c, &c, t, juce::Point<float> (3.0f, 3.0f), t, 1, false };
}

void press (juce::Component& c, bool rightButton)
{
    c.mouseDown (mouseEvent (c, rightButton));
    c.mouseUp (mouseEvent (c, rightButton));
}

// The id of the picker item with this text, found through the real menu.
int pickerIdFor (const StripButtonsBar& bar, const juce::String& text)
{
    const auto menu = bar.pickerMenuForTesting();
    juce::PopupMenu::MenuItemIterator it (menu, true);
    while (it.next())
        if (it.getItem().text == text && it.getItem().itemID != 0)
            return it.getItem().itemID;
    return 0;
}
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

TEST_CASE ("StripButtonsBar: a left click runs the command; a right click opens the remove path and never runs it", "[strip-buttons][bar][right-click]")
{
    Rig r;
    r.state.canUndo = true;
    r.host.buttons = { cmd ("edit.undo") };
    StripButtonsBar bar (r.host);
    bar.setSize (600, 28);
    std::vector<std::string> removeRequests;
    bar.removeMenuHookForTesting = [&] (const StripButton& b) { removeRequests.push_back (b.key); };

    auto& button = bar.buttonForTesting (0);
    REQUIRE (button.isEnabled());

    press (button, false);
    CHECK (r.ran.size() == 1);
    CHECK (removeRequests.empty());

    press (button, true);
    CHECK (r.ran.size() == 1);   // the right click did not click the button
    CHECK (removeRequests == std::vector<std::string> { "edit.undo" });
}

TEST_CASE ("StripButtonsBar: a disabled button can still be right-clicked for removal, and does not run", "[strip-buttons][bar][right-click]")
{
    Rig r;
    r.host.buttons = { cmd ("edit.undo"), layout ("Gone") };   // nothing to undo; layout does not exist
    StripButtonsBar bar (r.host);
    bar.setSize (600, 28);
    std::vector<std::string> removeRequests;
    bar.removeMenuHookForTesting = [&] (const StripButton& b) { removeRequests.push_back (b.key); };

    for (int i = 0; i < 2; ++i)
    {
        REQUIRE_FALSE (bar.buttonForTesting (i).isEnabled());
        press (bar.buttonForTesting (i), true);
    }
    CHECK (removeRequests == std::vector<std::string> { "edit.undo", "Gone" });
    CHECK (r.ran.empty());
}

TEST_CASE ("StripButtonsBar: choosing picker items adds a command or a layout, persists once and refreshes every bar", "[strip-buttons][bar][picker]")
{
    Rig r;
    StripButtonsBar a (r.host), b (r.host);
    a.setSize (600, 28);
    b.setSize (600, 28);

    const int saveId = pickerIdFor (a, "Save");
    REQUIRE (saveId != 0);
    a.pickerItemChosen (saveId);
    REQUIRE (r.host.buttons.size() == 1);
    CHECK (r.host.buttons[0].kind == StripButton::Kind::command);
    CHECK (r.host.buttons[0].key == "file.save");
    CHECK (r.persisted == 1);
    CHECK (a.visibleButtonsForTesting() == 1);
    CHECK (b.visibleButtonsForTesting() == 1);

    const int wideId = pickerIdFor (a, "Wide");
    REQUIRE (wideId != 0);
    a.pickerItemChosen (wideId);
    REQUIRE (r.host.buttons.size() == 2);
    CHECK (r.host.buttons[1].kind == StripButton::Kind::applyLayout);
    CHECK (r.host.buttons[1].key == "Wide");
    CHECK (r.persisted == 2);
    CHECK (b.visibleButtonsForTesting() == 2);

    a.pickerItemChosen (0);        // dismissed
    a.pickerItemChosen (999);      // not a command index
    a.pickerItemChosen (1005);     // not a layout index
    CHECK (r.host.buttons.size() == 2);
    CHECK (r.persisted == 2);
}

TEST_CASE ("StripButtonsBar: a picker choice is refused once the strip is full", "[strip-buttons][bar][picker]")
{
    Rig r;
    for (const auto* k : { "file.new", "file.open", "file.save", "file.saveAs", "file.importMidi",
                           "file.exportMidi", "file.exportAbc", "file.preferences", "edit.undo", "edit.redo" })
        r.host.buttons.push_back (cmd (k));
    StripButtonsBar bar (r.host);
    bar.setSize (600, 28);
    bar.pickerItemChosen (1);
    bar.pickerItemChosen (1000);
    CHECK (r.host.buttons.size() == 10);
    CHECK (r.persisted == 0);
}

TEST_CASE ("StripButtonsBar: an overflow item runs the command of the matching hidden button", "[strip-buttons][bar][overflow]")
{
    Rig r;
    r.host.buttons = { cmd ("file.new"), cmd ("file.open"), cmd ("file.save"), cmd ("file.saveAs"), cmd ("file.exportMidi") };
    StripButtonsBar bar (r.host);
    bar.setSize (150, 28);
    const int shown = bar.visibleButtonsForTesting();
    REQUIRE (bar.hasOverflowForTesting());
    REQUIRE (shown < 4);

    bar.overflowItemChosen (0);                                       // dismissed
    bar.overflowItemChosen (5 - shown + 1);                           // past the end
    CHECK (r.ran.empty());

    bar.overflowItemChosen (2);   // the second hidden button
    REQUIRE (r.ran.size() == 1);
    const auto expected = commandIdFor (r.host.buttons[(std::size_t) shown + 1], r.state);
    REQUIRE (expected.has_value());
    CHECK (r.ran[0] == *expected);
}
