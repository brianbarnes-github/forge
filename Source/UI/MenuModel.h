#pragma once

#include "GridSize.h"

#include <algorithm>
#include <array>
#include <string>
#include <vector>

// The Songsmith main menu bar as plain data, so its structure (labels, shortcut
// hints, order, enabled/ticked state) is testable without JUCE or a MainWindow.
// MainWindow converts a MenuDesc to a juce::PopupMenu and dispatches the ids.
namespace lotro
{

enum MenuCommandId : int
{
    FileNew = 1,
    FileOpenSong,
    FileSave,
    FileSaveAs,
    FileImportMidi,
    FileExportAbc,
    FileExportMidi,
    FilePreferences,
    FileQuit,
    EditUndo,
    EditRedo,
    EditSplit,
    EditDelete,
    EditSelectAll,
    EditSelectAllTracks,
    EditSelectAllSections,
    EditQuantize,
    SongDefaultParts,
    SongRunConverter,
    SongSoundFont,
    TransportPlayPause,
    TransportStop,
    TransportGoToStart,
    TransportGoToEnd,
    TransportRewindOneBar,
    TransportClearMarker,
    ViewExportPanelToggle,
    ViewDiagnosticsToggle,
    HelpAbout,

    // Grid-size submenu items occupy EditGridSizeBase + each GridSize enum
    // value; MainWindow range-checks incoming ids against that span. Pinned well
    // clear of the ids above so appending an enumerator can't land inside it.
    EditGridSizeBase = 1000
};

// Every plain (non grid-size) command; the menu model's ids must all be in here,
// and MainWindow::menuItemSelected must handle each of them.
inline constexpr std::array<MenuCommandId, 29> allCommandIds {
    FileNew, FileOpenSong, FileSave, FileSaveAs, FileImportMidi, FileExportAbc,
    FileExportMidi, FilePreferences, FileQuit, EditUndo, EditRedo, EditSplit, EditDelete,
    EditSelectAll, EditSelectAllTracks, EditSelectAllSections, EditQuantize,
    SongDefaultParts, SongRunConverter, SongSoundFont, TransportPlayPause,
    TransportStop, TransportGoToStart, TransportGoToEnd, TransportRewindOneBar,
    TransportClearMarker, ViewExportPanelToggle, ViewDiagnosticsToggle, HelpAbout };

constexpr bool isGridSizeCommand (int id) noexcept
{
    return id >= EditGridSizeBase && id <= EditGridSizeBase + (int) GridSize::Sixteenth;
}

struct MenuState
{
    bool isDirty = false;
    bool isUntitled = false;
    bool canUndo = false;
    bool canRedo = false;
    int  trackCount = 0;                 // including the conductor
    bool hasAbc = false;
    bool trackEditorOpen = false;
    bool exportPanelVisible = false;
    bool diagnosticsVisible = false;
    bool isPlaying = false;
    bool hasMarker = false;
    bool hasSelectedSections = false;    // canvas selection non-empty
    bool canSplitAtMarker = false;
};

struct MenuItemDesc
{
    int         id = 0;                  // 0 for separators and submenus
    std::string label;
    std::string shortcut;                // display text only, e.g. "Ctrl+S"
    bool        enabled = true;
    bool        ticked = false;
    bool        separator = false;
    bool        isSubmenu = false;
    std::vector<MenuItemDesc> children;  // when isSubmenu
};

struct MenuDesc
{
    std::string name;
    std::vector<MenuItemDesc> items;
};

namespace menu_detail
{
    inline MenuItemDesc item (int id, std::string label, std::string shortcut = {},
                              bool enabled = true, bool ticked = false)
    {
        MenuItemDesc d;
        d.id = id;
        d.label = std::move (label);
        d.shortcut = std::move (shortcut);
        d.enabled = enabled;
        d.ticked = ticked;
        return d;
    }
    inline MenuItemDesc separator()
    {
        MenuItemDesc d;
        d.separator = true;
        return d;
    }
    inline MenuItemDesc submenu (std::string label, std::vector<MenuItemDesc> children, bool enabled = true)
    {
        MenuItemDesc d;
        d.label = std::move (label);
        d.enabled = enabled;
        d.isSubmenu = true;
        d.children = std::move (children);
        return d;
    }
}

inline std::vector<MenuDesc> buildMenus (const MenuState& s)
{
    using namespace menu_detail;
    std::vector<MenuDesc> menus;

    menus.push_back ({ "File", {
        item (FileNew,      "New",        "Ctrl+N"),
        item (FileOpenSong, "Open...",    "Ctrl+O"),
        separator(),
        item (FileSave,     "Save",       "Ctrl+S", s.isDirty || s.isUntitled),
        item (FileSaveAs,   "Save As...", "Ctrl+Shift+S"),
        separator(),
        submenu ("Import", { item (FileImportMidi, "MIDI...") }),
        // MIDI export is enabled once the song has anything besides its conductor.
        submenu ("Export", { item (FileExportMidi, "MIDI...", {}, s.trackCount > 1),
                             item (FileExportAbc,  "ABC...",  {}, s.hasAbc) }),
        separator(),
        item (FilePreferences, "Preferences..."),
        separator(),
        item (FileQuit,     "Quit",       "Ctrl+Q") } });

    menus.push_back ({ "Edit", {
        item (EditUndo, "Undo", "Ctrl+Z", s.canUndo),
        item (EditRedo, "Redo", "Ctrl+Y", s.canRedo),
        separator(),
        item (EditSplit,  "Split",  "S",   s.canSplitAtMarker),
        item (EditDelete, "Delete", "Del", s.hasSelectedSections),
        item (EditSelectAll,         "Select All",          "Ctrl+A"),
        item (EditSelectAllTracks,   "Select All Tracks"),
        item (EditSelectAllSections, "Select All Sections"),
        separator(),
        item (EditQuantize, "Quantize", {}, s.trackEditorOpen),
        submenu ("Grid Size", {
            item (EditGridSizeBase + (int) GridSize::Off,       "Off",  {}, s.trackEditorOpen),
            item (EditGridSizeBase + (int) GridSize::Quarter,   "1/4",  {}, s.trackEditorOpen),
            item (EditGridSizeBase + (int) GridSize::Eighth,    "1/8",  {}, s.trackEditorOpen),
            item (EditGridSizeBase + (int) GridSize::Sixteenth, "1/16", {}, s.trackEditorOpen) },
            s.trackEditorOpen) } });

    menus.push_back ({ "Song", {
        item (SongDefaultParts, "Default parts from tracks"),
        item (SongRunConverter, "Run Converter"),
        separator(),
        item (SongSoundFont,    "SoundFont...") } });

    menus.push_back ({ "Transport", {
        item (TransportPlayPause, s.isPlaying ? "Pause" : "Play", "Space"),
        item (TransportStop,      "Stop"),
        separator(),
        item (TransportGoToStart,     "Go to Start"),
        item (TransportGoToEnd,       "Go to End"),
        item (TransportRewindOneBar,  "Rewind One Bar"),
        separator(),
        item (TransportClearMarker,   "Clear Marker", {}, s.hasMarker) } });

    menus.push_back ({ "View", {
        item (ViewExportPanelToggle, "Export ABC panel",  {}, true, s.exportPanelVisible),
        item (ViewDiagnosticsToggle, "Diagnostics list",  {}, true, s.diagnosticsVisible) } });

    menus.push_back ({ "Help", { item (HelpAbout, "About...") } });
    return menus;
}

} // namespace lotro
