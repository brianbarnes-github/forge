#pragma once

#include "MenuModel.h"

#include <vector>

// The MIDI editor panel's own menu bar as plain data, in the same MenuDesc form
// as the main menu so it is testable without JUCE. TrackEditorPanel converts it
// to its juce::MenuBarComponent. The menus are still empty; items are added one
// feature at a time.
namespace lotro
{

inline std::vector<MenuDesc> editorMenus()
{
    return { { "File", {} }, { "Edit", {} }, { "Navigate", {} }, { "Options", {} }, { "View", {} } };
}

// Left/Right from the open menu `current`: the next menu (step +1 or -1, wrapping)
// that has items, skipping empty ones; -1 when no other menu has any. An empty
// menu must never be opened (see TrackEditorPanel's menu bar).
inline int nextOpenableMenu (const std::vector<bool>& hasItems, int current, int step)
{
    const int n = (int) hasItems.size();
    for (int i = 1; i < n; ++i)
    {
        const int index = ((current + step * i) % n + n) % n;
        if (hasItems[(std::size_t) index])
            return index;
    }
    return -1;
}

} // namespace lotro
