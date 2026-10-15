#pragma once

#include "GridSize.h"

#include <algorithm>
#include <array>
#include <string>
#include <string_view>
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
    ViewSourceToggle,
    ViewMidiEditorToggle,
    ViewLotroPreviewToggle,
    ViewTracksToggle,
    ViewPartsToggle,
    ViewMixerToggle,
    ViewLayoutSave,
    ViewLayoutManage,
    EditTempoMeter,
    HelpAbout,

    // Grid-size submenu items occupy EditGridSizeBase + each GridSize enum
    // value; MainWindow range-checks incoming ids against that span. Pinned well
    // clear of the ids above so appending an enumerator can't land inside it.
    EditGridSizeBase = 1000,

    // Layouts submenu: ViewLayoutApplyBase + the layout's index in the saved list.
    ViewLayoutApplyBase = 2000
};

inline constexpr int maxSavedLayouts = 100;

// Every plain (non grid-size) command; the menu model's ids must all be in here,
// and MainWindow::menuItemSelected must handle each of them.
inline constexpr std::array<MenuCommandId, 38> allCommandIds {
    FileNew, FileOpenSong, FileSave, FileSaveAs, FileImportMidi, FileExportAbc,
    FileExportMidi, FilePreferences, FileQuit, EditUndo, EditRedo, EditSplit, EditDelete,
    EditSelectAll, EditSelectAllTracks, EditSelectAllSections, EditQuantize,
    SongDefaultParts, SongRunConverter, SongSoundFont, TransportPlayPause,
    TransportStop, TransportGoToStart, TransportGoToEnd, TransportRewindOneBar,
    TransportClearMarker, ViewExportPanelToggle, ViewDiagnosticsToggle, ViewSourceToggle, ViewMidiEditorToggle,
    ViewLotroPreviewToggle, ViewTracksToggle, ViewPartsToggle, ViewMixerToggle, ViewLayoutSave, ViewLayoutManage, EditTempoMeter, HelpAbout };

constexpr bool isGridSizeCommand (int id) noexcept
{
    return id >= EditGridSizeBase && id <= EditGridSizeBase + (int) GridSize::Sixteenth;
}

constexpr bool isLayoutApplyCommand (int id) noexcept
{
    return id >= ViewLayoutApplyBase && id < ViewLayoutApplyBase + maxSavedLayouts;
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
    bool sourceVisible = true;
    bool midiEditorVisible = false;
    bool lotroPreviewVisible = false;
    bool tracksVisible = false;
    bool partsVisible = false;
    bool mixerVisible = false;
    bool isPlaying = false;
    bool hasMarker = false;
    bool hasSelectedSections = false;    // canvas selection non-empty
    bool canSplitAtMarker = false;
    bool hasTimeBase = false;            // the Song has a tempo/meter time base to edit
    std::vector<std::string> layoutNames;   // the saved dock layouts, in order
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

namespace menu_detail
{
    // Save, then each saved layout (click to apply), then Manage; the lists stay apart by separators.
    inline std::vector<MenuItemDesc> layoutItems (const std::vector<std::string>& names)
    {
        std::vector<MenuItemDesc> items { item (ViewLayoutSave, "Save Layout") };
        if (! names.empty())
            items.push_back (separator());
        for (std::size_t i = 0; i < names.size() && (int) i < maxSavedLayouts; ++i)
            items.push_back (item (ViewLayoutApplyBase + (int) i, names[i]));
        items.push_back (separator());
        items.push_back (item (ViewLayoutManage, "Manage Layouts...", {}, ! names.empty()));
        return items;
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
            s.trackEditorOpen),
        separator(),
        item (EditTempoMeter, "Tempo and Meter...", {}, s.hasTimeBase) } });

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
        item (ViewDiagnosticsToggle, "Diagnostics list",  {}, true, s.diagnosticsVisible),
        item (ViewSourceToggle,      "MIDI Source",       {}, true, s.sourceVisible),
        item (ViewMidiEditorToggle,  "MIDI Editor",       {}, true, s.midiEditorVisible),
        item (ViewLotroPreviewToggle, "LOTRO Preview",    {}, true, s.lotroPreviewVisible),
        item (ViewTracksToggle,       "Tracks",           {}, true, s.tracksVisible),
        item (ViewPartsToggle,        "Parts",            {}, true, s.partsVisible),
        item (ViewMixerToggle,        "Mixer",            {}, true, s.mixerVisible),
        separator(),
        submenu ("Layouts", layoutItems (s.layoutNames)) } });

    menus.push_back ({ "Help", { item (HelpAbout, "About...") } });
    return menus;
}

// The commands the transport strip's "+" menu offers, in picker order. `key` is the stable
// string saved in AppSettings (never reuse or change one); `label` is the button text.
struct StripCommand
{
    const char*   group;
    const char*   label;
    const char*   key;
    MenuCommandId id;
};

inline constexpr std::array<StripCommand, 32> stripCommands {{
    { "File", "New",            "file.new",            FileNew },
    { "File", "Open",           "file.open",           FileOpenSong },
    { "File", "Save",           "file.save",           FileSave },
    { "File", "Save As",        "file.saveAs",         FileSaveAs },
    { "File", "Import MIDI",    "file.importMidi",     FileImportMidi },
    { "File", "Export MIDI",    "file.exportMidi",     FileExportMidi },
    { "File", "Export ABC",     "file.exportAbc",      FileExportAbc },
    { "File", "Preferences",    "file.preferences",    FilePreferences },
    { "Edit", "Undo",           "edit.undo",           EditUndo },
    { "Edit", "Redo",           "edit.redo",           EditRedo },
    { "Edit", "Split",          "edit.split",          EditSplit },
    { "Edit", "Delete",         "edit.delete",         EditDelete },
    { "Edit", "Quantize",       "edit.quantize",       EditQuantize },
    { "Edit", "Tempo/Meter",    "edit.tempoMeter",     EditTempoMeter },
    { "Edit", "Select All",     "edit.selectAll",      EditSelectAll },
    { "Edit", "Select Tracks",  "edit.selectAllTracks", EditSelectAllTracks },
    { "Edit", "Select Sections", "edit.selectAllSections", EditSelectAllSections },
    { "Song", "Default Parts",  "song.defaultParts",   SongDefaultParts },
    { "Song", "Run Converter",  "song.runConverter",   SongRunConverter },
    { "Song", "SoundFont",      "song.soundFont",      SongSoundFont },
    { "Transport", "Rewind Bar",   "transport.rewindOneBar", TransportRewindOneBar },
    { "Transport", "Clear Marker", "transport.clearMarker",  TransportClearMarker },
    { "View", "Export Panel",   "view.exportPanel",    ViewExportPanelToggle },
    { "View", "Diagnostics",    "view.diagnostics",    ViewDiagnosticsToggle },
    { "View", "Source",         "view.source",         ViewSourceToggle },
    { "View", "MIDI Editor",    "view.midiEditor",     ViewMidiEditorToggle },
    { "View", "LOTRO Preview",  "view.lotroPreview",   ViewLotroPreviewToggle },
    { "View", "Tracks",         "view.tracks",         ViewTracksToggle },
    { "View", "Parts",          "view.parts",          ViewPartsToggle },
    { "View", "Mixer",          "view.mixer",          ViewMixerToggle },
    { "View", "Save Layout",    "view.layoutSave",     ViewLayoutSave },
    { "View", "Manage Layouts", "view.layoutManage",   ViewLayoutManage },
}};

inline const StripCommand* findStripCommand (std::string_view key)
{
    for (const auto& c : stripCommands)
        if (key == c.key)
            return &c;
    return nullptr;
}

// The menu item with this id, searching submenus; nullptr if there is none.
inline const MenuItemDesc* findMenuItem (const std::vector<MenuItemDesc>& items, int id)
{
    for (const auto& i : items)
    {
        if (! i.isSubmenu && ! i.separator && i.id == id)
            return &i;
        if (i.isSubmenu)
            if (const auto* inner = findMenuItem (i.children, id))
                return inner;
    }
    return nullptr;
}

inline const MenuItemDesc* findMenuItem (const std::vector<MenuDesc>& menus, int id)
{
    for (const auto& m : menus)
        if (const auto* i = findMenuItem (m.items, id))
            return i;
    return nullptr;
}

} // namespace lotro
