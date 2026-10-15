#pragma once

#include "MenuModel.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <vector>

// Turns MenuModel-style items into a juce::PopupMenu; shared by the main menu bar
// and the MIDI editor's.
namespace lotro
{

inline void addMenuItems (juce::PopupMenu& m, const std::vector<MenuItemDesc>& items)
{
    for (const auto& d : items)
    {
        if (d.separator)
        {
            m.addSeparator();
        }
        else if (d.isSubmenu)
        {
            juce::PopupMenu sub;
            addMenuItems (sub, d.children);
            m.addSubMenu (d.label, sub, d.enabled);
        }
        else
        {
            juce::PopupMenu::Item i (d.label);
            i.itemID = d.id;
            i.isEnabled = d.enabled;
            i.isTicked = d.ticked;
            i.shortcutKeyDescription = d.shortcut;
            m.addItem (i);
        }
    }
}

} // namespace lotro
