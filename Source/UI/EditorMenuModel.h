#pragma once

#include "GridLength.h"
#include "MenuModel.h"
#include "NotePreview.h"
#include "ScaleSnap.h"

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
    EditorNavigateSelectNearestNote,
    EditorNavigateAddNearestNote,
    EditorNavigateSelectPreviousNote,
    EditorNavigateSelectNextNote,
    EditorNavigateSelectPreviousSamePitch,
    EditorNavigateSelectNextSamePitch,
    EditorNavigateSelectAllSamePitch,
    EditorNavigateAddPreviousNote,
    EditorNavigateAddNextNote,
    EditorNavigateCursorToSelectionStart,
    EditorOptionsSnapToKeyEnabled,
    EditorOptionsPreviewOnInsert,
    EditorOptionsPreviewOnEdit,
    EditorOptionsPreviewOnVelocity,
    // Options > Snap to Key > Root: EditorSnapRootBase + the index into snapRoots().
    EditorSnapRootBase = 1000,
    // Options > Snap to Key > Type (and its Chords): EditorSnapScaleBase + (int) SnapScale.
    EditorSnapScaleBase = 1100,
    // Options > Grid Length: EditorGridLengthBase + (int) GridLength.
    EditorGridLengthBase = 1200,
    // Options > Grid Division: EditorGridDivisionBase + (int) GridDivision.
    EditorGridDivisionBase = 1300
};

constexpr bool isEditorSnapRootCommand (int id) noexcept
{
    return id >= EditorSnapRootBase && id < EditorSnapRootBase + snapRootCount;
}

constexpr bool isEditorSnapScaleCommand (int id) noexcept
{
    return id >= EditorSnapScaleBase && id < EditorSnapScaleBase + snapScaleCount;
}

constexpr bool isEditorGridLengthCommand (int id) noexcept
{
    return id >= EditorGridLengthBase && id < EditorGridLengthBase + gridLengthCount;
}

constexpr bool isEditorGridDivisionCommand (int id) noexcept
{
    return id >= EditorGridDivisionBase && id < EditorGridDivisionBase + gridDivisionCount;
}

struct EditorMenuState
{
    bool hasTrack = false;     // a track is loaded in the editor
    bool snapToGrid = true;    // Options > Snap to Grid is ticked
    bool canStepMarker = false; // a track, playback and a grid (not Off)
    bool canStepPitch = false;  // a track and an active row (a canvas click picks one)
    bool canSelectNearestNote = false; // a track with notes, and playback for the time position
    bool canAddNearestNote = false;    // as canSelectNearestNote, with a note not yet selected
    bool canSelectPreviousNote = false; // as canSelectNearestNote, with a note before the cursor
    bool canSelectNextNote = false;     // as canSelectNearestNote, with a note after the cursor
    bool canSelectPreviousSamePitch = false; // as canSelectNearestNote, one note selected, an earlier one of its pitch
    bool canSelectNextSamePitch = false;     // as canSelectNearestNote, one note selected, a later one of its pitch
    bool canSelectAllSamePitch = false;      // a track, one note selected, another of its pitch
    bool canAddPreviousNote = false;         // as canSelectNearestNote, a selection, a note before it
    bool canAddNextNote = false;             // as canSelectNearestNote, a selection, a note after it
    bool canMoveCursorToSelectionStart = false; // a track, playback, and a selection
    bool snapToKey = false;     // Options > Snap to Key > Enabled is ticked
    int snapRootIndex = -1;     // the current root (snapRoots()), -1 with no key
    int snapScaleIndex = -1;    // the current (int) SnapScale, -1 with no key
    NotePreviewSettings notePreview; // Options > Note Preview's ticks
    GridLength gridLength = GridLength::Off;              // Options > Grid Length's tick
    GridDivision gridDivision = GridDivision::Straight;   // Options > Grid Division's tick
};

namespace menu_detail
{
    // Always enabled, so the checkboxes can be set before a track is loaded.
    inline MenuItemDesc notePreviewSubmenu (const EditorMenuState& s)
    {
        return submenu ("Note Preview", {
            item (EditorOptionsPreviewOnInsert, "Preview notes when inserting", {}, true, s.notePreview.onInsert),
            item (EditorOptionsPreviewOnEdit, "Preview notes when editing", {}, true, s.notePreview.onEdit),
            item (EditorOptionsPreviewOnVelocity, "Preview notes when velocity changes", {}, true, s.notePreview.onVelocity) });
    }

    // Both need a track: a newly opened editor starts again from the default grid.
    inline MenuItemDesc gridLengthSubmenu (const EditorMenuState& s)
    {
        const auto lengthItem = [&s] (GridLength length)
        {
            return item (EditorGridLengthBase + (int) length, gridLengthName (length), {}, s.hasTrack,
                         length == s.gridLength);
        };
        std::vector<MenuItemDesc> items { lengthItem (GridLength::Off), separator() };
        for (int i = (int) GridLength::Whole; i < gridLengthCount; ++i)
            items.push_back (lengthItem ((GridLength) i));
        return submenu ("Grid Length", std::move (items));
    }

    inline MenuItemDesc gridDivisionSubmenu (const EditorMenuState& s)
    {
        std::vector<MenuItemDesc> items;
        for (int i = 0; i < gridDivisionCount; ++i)
            items.push_back (item (EditorGridDivisionBase + i, gridDivisionName ((GridDivision) i), {}, s.hasTrack,
                                   (GridDivision) i == s.gridDivision));
        return submenu ("Grid Division", std::move (items));
    }

    // Enabled and Type need a key (the song's or a picked root); Root is always open.
    inline MenuItemDesc snapToKeySubmenu (const EditorMenuState& s)
    {
        const bool hasKey = s.snapRootIndex >= 0;

        std::vector<MenuItemDesc> roots;
        for (int i = 0; i < snapRootCount; ++i)
            roots.push_back (item (EditorSnapRootBase + i, snapRoots()[(std::size_t) i].name, {}, true,
                                   i == s.snapRootIndex));

        const auto scaleItem = [&s] (int i)
        {
            return item (EditorSnapScaleBase + i, snapScaleName ((SnapScale) i), {}, true, i == s.snapScaleIndex);
        };
        std::vector<MenuItemDesc> types;
        for (int i = 0; i < snapFirstChord; ++i)
            types.push_back (scaleItem (i));
        std::vector<MenuItemDesc> chords;
        for (int i = snapFirstChord; i < snapScaleCount; ++i)
            chords.push_back (scaleItem (i));
        types.push_back (submenu ("Chords", std::move (chords)));

        return submenu ("Snap to Key", {
            item (EditorOptionsSnapToKeyEnabled, "Enabled", {}, hasKey, s.snapToKey),
            submenu ("Root", std::move (roots)),
            submenu ("Type", std::move (types), hasKey) });
    }
}

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
                 item (EditorNavigateSelectNearestNote, "Select Note Nearest Cursor", {}, s.canSelectNearestNote),
                 item (EditorNavigateAddNearestNote, "Add Note Nearest Cursor to Selection", {}, s.canAddNearestNote),
                 separator(),
                 item (EditorNavigateSelectPreviousNote, "Select Previous Note", {}, s.canSelectPreviousNote),
                 item (EditorNavigateSelectNextNote, "Select Next Note", {}, s.canSelectNextNote),
                 separator(),
                 item (EditorNavigateSelectPreviousSamePitch, "Select Previous Note of the Same Pitch", {},
                       s.canSelectPreviousSamePitch),
                 item (EditorNavigateSelectNextSamePitch, "Select Next Note of the Same Pitch", {},
                       s.canSelectNextSamePitch),
                 item (EditorNavigateSelectAllSamePitch, "Select All Notes of the Same Pitch", {},
                       s.canSelectAllSamePitch),
                 separator(),
                 item (EditorNavigateAddPreviousNote, "Add Previous Note to Selection", {}, s.canAddPreviousNote),
                 item (EditorNavigateAddNextNote, "Add Next Note to Selection", {}, s.canAddNextNote),
                 item (EditorNavigateCursorToSelectionStart, "Move Cursor to Start of Selection", {},
                       s.canMoveCursorToSelectionStart) } },
             { "Options", {
                 item (EditorOptionsSnapToGrid, "Snap to Grid", {}, true, s.snapToGrid),
                 snapToKeySubmenu (s),
                 notePreviewSubmenu (s),
                 separator(),
                 gridLengthSubmenu (s),
                 gridDivisionSubmenu (s) } },
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
