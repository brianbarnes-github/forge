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
    EditorOptionsSnapToGrid,
    EditorNavigateMarkerLeft,
    EditorNavigateMarkerRight,
    EditorNavigatePitchUp,
    EditorNavigatePitchDown,
    EditorNavigateSelectNearestNote
};

struct EditorMenuState
{
    bool hasTrack = false;     // a track is loaded in the editor
    bool snapToGrid = true;    // Options > Snap to Grid is ticked
    bool canStepMarker = false; // a track, playback and a grid (not Off)
    bool canStepPitch = false;  // a track and an active row (a canvas click picks one)
    bool canSelectNearestNote = false; // a track with notes, and playback for the time position
};

inline std::vector<MenuDesc> editorMenus (const EditorMenuState& s)
{
    using namespace menu_detail;
    return { { "File", {
                 item (EditorFileExportTrackMidi, "Export to New MIDI File...", {}, s.hasTrack),
                 separator(),
                 item (EditorFileClose, "Close Editor") } },
             { "Edit", {} },
             { "Navigate", {
                 item (EditorNavigateMarkerLeft,  "Move Marker Left by Grid",  "Left",  s.canStepMarker),
                 item (EditorNavigateMarkerRight, "Move Marker Right by Grid", "Right", s.canStepMarker),
                 separator(),
                 item (EditorNavigatePitchUp,   "Move Up a Semitone",   "Up",   s.canStepPitch),
                 item (EditorNavigatePitchDown, "Move Down a Semitone", "Down", s.canStepPitch),
                 separator(),
                 item (EditorNavigateSelectNearestNote, "Select Note Nearest Cursor", {}, s.canSelectNearestNote) } },
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
