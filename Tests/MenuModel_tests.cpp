// Pins the Songsmith menu bar's structure: labels, shortcut hints, order, and
// enabled/ticked state as a function of MenuState.

#include "UI/MenuModel.h"

#include <catch2/catch_test_macros.hpp>

#include <set>

using namespace lotro;

namespace
{
    MenuDesc menuNamed (const std::vector<MenuDesc>& m, const std::string& name)
    {
        const auto it = std::find_if (m.begin(), m.end(), [&] (const auto& d) { return d.name == name; });
        REQUIRE (it != m.end());
        return *it;
    }

    // "-" stands for a separator; "name>" for a submenu.
    std::vector<std::string> labels (const MenuDesc& m)
    {
        std::vector<std::string> out;
        for (const auto& i : m.items)
            out.push_back (i.separator ? "-" : i.isSubmenu ? i.label + ">" : i.label);
        return out;
    }

    MenuItemDesc itemWith (const MenuDesc& m, int id)
    {
        for (const auto& i : m.items)
        {
            if (i.id == id) return i;
            for (const auto& c : i.children)
                if (c.id == id) return c;
        }
        FAIL ("no such command id");
        return {};
    }

    using V = std::vector<std::string>;
}

TEST_CASE ("MenuModel: top-level menus are File, Edit, Song, Transport, View, Help in that order", "[menu-model]")
{
    const auto menus = buildMenus ({});
    REQUIRE (menus.size() == 6);
    CHECK (menus[0].name == "File");
    CHECK (menus[1].name == "Edit");
    CHECK (menus[2].name == "Song");
    CHECK (menus[3].name == "Transport");
    CHECK (menus[4].name == "View");
    CHECK (menus[5].name == "Help");
}

TEST_CASE ("MenuModel: File menu layout, shortcuts, and no Close", "[menu-model]")
{
    const auto menus = buildMenus ({});
    const auto file = menuNamed (menus, "File");
    CHECK (labels (file) == V { "New", "Open...", "-", "Save", "Save As...", "-", "Import>", "Export>", "-", "Quit" });
    CHECK (itemWith (file, FileNew).shortcut == "Ctrl+N");
    CHECK (itemWith (file, FileOpenSong).shortcut == "Ctrl+O");
    CHECK (itemWith (file, FileSave).shortcut == "Ctrl+S");
    CHECK (itemWith (file, FileSaveAs).shortcut == "Ctrl+Shift+S");
    CHECK (itemWith (file, FileQuit).shortcut == "Ctrl+Q");
    CHECK (itemWith (file, FileImportMidi).label == "MIDI...");
    CHECK (itemWith (file, FileExportMidi).label == "MIDI...");
    CHECK (itemWith (file, FileExportAbc).label == "ABC...");
    for (const auto& m : menus)
        for (const auto& i : m.items)
            CHECK (i.label != "Close");
}

TEST_CASE ("MenuModel: Save is enabled when dirty or untitled, not when clean", "[menu-model]")
{
    const auto save = [] (bool dirty, bool untitled)
    {
        MenuState s; s.isDirty = dirty; s.isUntitled = untitled;
        return itemWith (menuNamed (buildMenus (s), "File"), FileSave).enabled;
    };
    CHECK_FALSE (save (false, false));
    CHECK (save (true, false));
    CHECK (save (false, true));
    CHECK (save (true, true));
}

TEST_CASE ("MenuModel: Export MIDI needs more than the conductor, Export ABC needs ABC", "[menu-model]")
{
    const auto file = [] (int tracks, bool abc)
    {
        MenuState s; s.trackCount = tracks; s.hasAbc = abc;
        return menuNamed (buildMenus (s), "File");
    };
    CHECK_FALSE (itemWith (file (0, false), FileExportMidi).enabled);
    CHECK_FALSE (itemWith (file (1, false), FileExportMidi).enabled);
    CHECK (itemWith (file (2, false), FileExportMidi).enabled);
    CHECK_FALSE (itemWith (file (2, false), FileExportAbc).enabled);
    CHECK (itemWith (file (1, true), FileExportAbc).enabled);
    CHECK_FALSE (itemWith (file (1, true), FileExportMidi).enabled);   // independent
    CHECK (itemWith (file (0, false), FileNew).enabled);
    CHECK (itemWith (file (0, false), FileSaveAs).enabled);
    CHECK (itemWith (file (0, false), FileQuit).enabled);
}

TEST_CASE ("MenuModel: Edit menu layout and shortcut hints", "[menu-model]")
{
    const auto edit = menuNamed (buildMenus ({}), "Edit");
    CHECK (labels (edit) == V { "Undo", "Redo", "-", "Split", "Delete", "Select All", "Select All Tracks",
                                "Select All Sections", "-", "Quantize", "Grid Size>" });
    CHECK (itemWith (edit, EditUndo).shortcut == "Ctrl+Z");
    CHECK (itemWith (edit, EditRedo).shortcut == "Ctrl+Y");
    CHECK (itemWith (edit, EditSplit).shortcut == "S");
    CHECK (itemWith (edit, EditDelete).shortcut == "Del");
    CHECK (itemWith (edit, EditSelectAll).shortcut == "Ctrl+A");
    CHECK (itemWith (edit, EditSelectAllTracks).shortcut.empty());
    CHECK (itemWith (edit, EditSelectAllSections).shortcut.empty());
    CHECK (itemWith (edit, EditQuantize).shortcut.empty());
    // Select All variants are always available.
    CHECK (itemWith (edit, EditSelectAll).enabled);
    CHECK (itemWith (edit, EditSelectAllTracks).enabled);
    CHECK (itemWith (edit, EditSelectAllSections).enabled);
}

TEST_CASE ("MenuModel: Undo, Redo, Split and Delete follow their own state flags", "[menu-model]")
{
    const auto edit = [] (MenuState s) { return menuNamed (buildMenus (s), "Edit"); };
    {
        MenuState s; s.canUndo = true;
        CHECK (itemWith (edit (s), EditUndo).enabled);
        CHECK_FALSE (itemWith (edit (s), EditRedo).enabled);
    }
    {
        MenuState s; s.canRedo = true;
        CHECK_FALSE (itemWith (edit (s), EditUndo).enabled);
        CHECK (itemWith (edit (s), EditRedo).enabled);
    }
    {
        MenuState s; s.canSplitAtMarker = true;
        CHECK (itemWith (edit (s), EditSplit).enabled);
        CHECK_FALSE (itemWith (edit (s), EditDelete).enabled);   // split does not imply a selection flag
    }
    {
        MenuState s; s.hasSelectedSections = true;
        CHECK (itemWith (edit (s), EditDelete).enabled);
        CHECK_FALSE (itemWith (edit (s), EditSplit).enabled);
    }
    CHECK_FALSE (itemWith (edit ({}), EditSplit).enabled);
    CHECK_FALSE (itemWith (edit ({}), EditDelete).enabled);
}

TEST_CASE ("MenuModel: Quantize and the Grid Size submenu need an open track editor", "[menu-model]")
{
    const auto grid = [] (bool open)
    {
        MenuState s; s.trackEditorOpen = open;
        const auto edit = menuNamed (buildMenus (s), "Edit");
        return std::make_pair (itemWith (edit, EditQuantize).enabled,
                               edit.items.back());
    };
    for (const bool open : { false, true })
    {
        const auto [quantize, sub] = grid (open);
        CHECK (quantize == open);
        REQUIRE (sub.isSubmenu);
        CHECK (sub.label == "Grid Size");
        CHECK (sub.enabled == open);
        REQUIRE (sub.children.size() == 4);
        const char* names[] = { "Off", "1/4", "1/8", "1/16" };
        for (int i = 0; i < 4; ++i)
        {
            CHECK (sub.children[(size_t) i].label == names[i]);
            CHECK (sub.children[(size_t) i].id == EditGridSizeBase + i);
            CHECK (isGridSizeCommand (sub.children[(size_t) i].id));
            CHECK (sub.children[(size_t) i].enabled == open);
        }
    }
    CHECK_FALSE (isGridSizeCommand (EditGridSizeBase - 1));
    CHECK_FALSE (isGridSizeCommand (EditGridSizeBase + 4));
}

TEST_CASE ("MenuModel: Song menu is unchanged and has no shortcuts", "[menu-model]")
{
    const auto song = menuNamed (buildMenus ({}), "Song");
    CHECK (labels (song) == V { "Default parts from tracks", "Run Converter", "-", "SoundFont..." });
    for (const auto& i : song.items)
        CHECK (i.shortcut.empty());
}

TEST_CASE ("MenuModel: Transport menu layout, Play/Pause label and Clear Marker state", "[menu-model]")
{
    const auto transport = [] (bool playing, bool marker)
    {
        MenuState s; s.isPlaying = playing; s.hasMarker = marker;
        return menuNamed (buildMenus (s), "Transport");
    };
    CHECK (labels (transport (false, false)) == V { "Play", "Stop", "-", "Go to Start", "Go to End", "Rewind One Bar", "-", "Clear Marker" });
    CHECK (labels (transport (true, false)) == V { "Pause", "Stop", "-", "Go to Start", "Go to End", "Rewind One Bar", "-", "Clear Marker" });
    CHECK (itemWith (transport (true, false), TransportPlayPause).id == itemWith (transport (false, false), TransportPlayPause).id);
    CHECK (itemWith (transport (true, true), TransportPlayPause).shortcut == "Space");
    CHECK (itemWith (transport (false, false), TransportPlayPause).shortcut == "Space");
    for (const int id : { TransportStop, TransportGoToStart, TransportGoToEnd, TransportRewindOneBar, TransportClearMarker })
        CHECK (itemWith (transport (false, false), id).shortcut.empty());
    CHECK_FALSE (itemWith (transport (false, false), TransportClearMarker).enabled);
    CHECK (itemWith (transport (false, true), TransportClearMarker).enabled);
    CHECK (itemWith (transport (false, false), TransportPlayPause).enabled);
    CHECK (itemWith (transport (false, false), TransportStop).enabled);
}

TEST_CASE ("MenuModel: View items are ticked from their visibility flags independently", "[menu-model]")
{
    const auto view = [] (bool exportPanel, bool diag)
    {
        MenuState s; s.exportPanelVisible = exportPanel; s.diagnosticsVisible = diag;
        return menuNamed (buildMenus (s), "View");
    };
    CHECK (labels (view (false, false)) == V { "Export ABC panel", "Diagnostics list" });
    CHECK_FALSE (itemWith (view (false, true), ViewExportPanelToggle).ticked);
    CHECK (itemWith (view (false, true), ViewDiagnosticsToggle).ticked);
    CHECK (itemWith (view (true, false), ViewExportPanelToggle).ticked);
    CHECK_FALSE (itemWith (view (true, false), ViewDiagnosticsToggle).ticked);
}

TEST_CASE ("MenuModel: Help menu", "[menu-model]")
{
    CHECK (labels (menuNamed (buildMenus ({}), "Help")) == V { "About..." });
}

TEST_CASE ("MenuModel: every command id is unique and listed in allCommandIds", "[menu-model]")
{
    MenuState everything;
    everything.isDirty = everything.canUndo = everything.canRedo = everything.hasAbc = true;
    everything.trackCount = 3;
    const std::set<int> known (allCommandIds.begin(), allCommandIds.end());
    CHECK (known.size() == allCommandIds.size());

    std::multiset<int> seen;
    const auto walk = [&] (auto&& self, const std::vector<MenuItemDesc>& items) -> void
    {
        for (const auto& i : items)
        {
            if (i.separator) { CHECK (i.id == 0); continue; }
            if (i.isSubmenu) { CHECK (i.id == 0); self (self, i.children); continue; }
            seen.insert (i.id);
            CHECK ((known.count (i.id) == 1 || isGridSizeCommand (i.id)));
            CHECK_FALSE (i.label.empty());
        }
    };
    for (const auto& m : buildMenus (everything))
        walk (walk, m.items);

    for (const int id : seen)
        CHECK (seen.count (id) == 1);
    // Every plain command is reachable from some menu.
    for (const int id : known)
        CHECK (seen.count (id) == 1);
}
