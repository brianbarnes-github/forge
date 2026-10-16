#pragma once

#include "MenuModel.h"

#include <vector>

// The MIDI editor panel's own menu bar as plain data, in the same MenuDesc form
// as the main menu so it is testable without JUCE. TrackEditorPanel converts it
// to its juce::MenuBarComponent and dispatches the ids. Items are added one
// feature at a time.
namespace lotro
{

// The editor bar's own ids; they never reach MainWindow::menuItemSelected.
enum EditorMenuCommandId : int
{
    EditorFileExportTrackMidi = 1,
    EditorFileClose,
    EditorOptionsSnapToGrid
};

struct EditorMenuState
{
    bool hasTrack = false;     // a track is loaded in the editor
    bool snapToGrid = true;    // Options > Snap to Grid is ticked
};

inline std::vector<MenuDesc> editorMenus (const EditorMenuState& s)
{
    using namespace menu_detail;
    return { { "File", {
                 item (EditorFileExportTrackMidi, "Export to New MIDI File...", {}, s.hasTrack),
                 separator(),
                 item (EditorFileClose, "Close Editor") } },
             { "Edit", {} }, { "Navigate", {} },
             { "Options", {
                 item (EditorOptionsSnapToGrid, "Snap to Grid", {}, true, s.snapToGrid) } },
             { "View", {} } };
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
