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

} // namespace lotro
