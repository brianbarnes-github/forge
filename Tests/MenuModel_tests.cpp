// Pins the Songsmith menu bar's structure: labels, shortcut hints, order, and
// enabled/ticked state as a function of MenuState.

#include "UI/EditorMenuModel.h"

#include "UI/MenuModel.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
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
    CHECK (labels (file) == V { "New", "Open...", "-", "Save", "Save As...", "-", "Import>", "Export>", "-", "Preferences...", "-", "Quit" });
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

TEST_CASE ("MenuModel: Preferences sits between Export and Quit, always enabled, no shortcut", "[menu-model]")
{
    for (const bool dirty : { false, true })
    {
        MenuState s; s.isDirty = dirty;
        const auto file = menuNamed (buildMenus (s), "File");
        const auto prefs = itemWith (file, FilePreferences);
        CHECK (prefs.label == "Preferences...");
        CHECK (prefs.shortcut.empty());
        CHECK (prefs.enabled);
    }
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
                                "Select All Sections", "-", "Quantize", "Grid Size>", "-",
                                "Tempo and Meter..." });
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
                               edit.items[edit.items.size() - 3]);   // Grid Size sits before the separator and Tempo and Meter
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

TEST_CASE ("MenuModel: Grid Size ticks the open editor's Grid Length when it is one of its sizes, and nothing otherwise", "[menu-model][grid-length]")
{
    const auto ticks = [] (std::optional<GridLength> length)
    {
        MenuState s;
        s.trackEditorOpen = length.has_value();
        s.editorGridLength = length;
        const auto edit = menuNamed (buildMenus (s), "Edit");
        std::vector<bool> out;
        for (const auto& c : edit.items[edit.items.size() - 3].children)
            out.push_back (c.ticked);
        return out;
    };
    CHECK (ticks (std::nullopt) == std::vector<bool> { false, false, false, false });   // no editor loaded
    CHECK (ticks (GridLength::Off) == std::vector<bool> { true, false, false, false });
    CHECK (ticks (GridLength::Quarter) == std::vector<bool> { false, true, false, false });
    CHECK (ticks (GridLength::Eighth) == std::vector<bool> { false, false, true, false });
    CHECK (ticks (GridLength::Sixteenth) == std::vector<bool> { false, false, false, true });
    CHECK (ticks (GridLength::Whole) == std::vector<bool> { false, false, false, false });        // not a Grid Size
    CHECK (ticks (GridLength::SixtyFourth) == std::vector<bool> { false, false, false, false });
}

TEST_CASE ("MenuModel: Song menu opens with Set Key and has no shortcuts", "[menu-model]")
{
    const auto song = menuNamed (buildMenus ({}), "Song");
    CHECK (labels (song) == V { "Set Key>", "Default parts from tracks", "Run Converter", "-", "SoundFont..." });
    for (const auto& i : song.items)
        CHECK (i.shortcut.empty());
}

TEST_CASE ("MenuModel: Set Key holds Detect Key, a divider, then all 24 keys with the song's ticked", "[menu-model][key]")
{
    const auto setKey = [] (int trackCount, int songKeyIndex)
    {
        MenuState s; s.trackCount = trackCount; s.songKeyIndex = songKeyIndex;
        const auto song = menuNamed (buildMenus (s), "Song");
        REQUIRE (song.items.front().isSubmenu);
        CHECK (song.items.front().label == "Set Key");
        return song.items.front();
    };

    const auto sub = setKey (3, 7);
    CHECK (sub.enabled);
    REQUIRE (sub.children.size() == 26);
    CHECK (sub.children[0].id == SongDetectKey);
    CHECK (sub.children[0].label == "Detect Key");
    CHECK (sub.children[0].enabled);
    CHECK (sub.children[1].separator);
    for (int i = 0; i < keyCount; ++i)
    {
        const auto& k = sub.children[(std::size_t) i + 2];
        CAPTURE (i);
        CHECK (k.id == SongKeyBase + i);
        CHECK (isSongKeyCommand (k.id));
        CHECK (k.label == keyAt (i).name());
        CHECK (k.ticked == (i == 7));
        CHECK (k.enabled);
    }
    CHECK (sub.children[2].label == "C major");
    CHECK (sub.children[13].label == "B major");
    CHECK (sub.children[14].label == "C minor");
    CHECK (sub.children[25].label == "B minor");
    CHECK_FALSE (isSongKeyCommand (SongKeyBase - 1));
    CHECK_FALSE (isSongKeyCommand (SongKeyBase + keyCount));

    for (const auto& k : setKey (3, -1).children)
        CHECK_FALSE (k.ticked);

    // Only the conductor (or nothing): nothing to judge or label.
    CHECK_FALSE (setKey (1, -1).enabled);
    CHECK_FALSE (setKey (1, -1).children[0].enabled);
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
    CHECK (labels (view (false, false)) == V { "Export ABC panel", "Diagnostics list", "MIDI Source", "MIDI Editor", "LOTRO Preview", "Tracks", "Parts", "Mixer", "-", "Layouts>" });
    CHECK_FALSE (itemWith (view (false, true), ViewExportPanelToggle).ticked);
    CHECK (itemWith (view (false, true), ViewDiagnosticsToggle).ticked);
    CHECK (itemWith (view (true, false), ViewExportPanelToggle).ticked);
    CHECK_FALSE (itemWith (view (true, false), ViewDiagnosticsToggle).ticked);
}

TEST_CASE ("MenuModel: the panel items are enabled and ticked from their own flags", "[menu-model]")
{
    const auto view = [] (bool editor, bool preview)
    {
        MenuState s; s.midiEditorVisible = editor; s.lotroPreviewVisible = preview;
        return menuNamed (buildMenus (s), "View");
    };
    CHECK (itemWith (view (false, false), ViewMidiEditorToggle).enabled);
    CHECK (itemWith (view (false, false), ViewLotroPreviewToggle).enabled);
    CHECK (itemWith (view (true, false), ViewMidiEditorToggle).ticked);
    CHECK_FALSE (itemWith (view (true, false), ViewLotroPreviewToggle).ticked);
    CHECK (itemWith (view (false, true), ViewLotroPreviewToggle).ticked);
    CHECK_FALSE (itemWith (view (false, true), ViewMidiEditorToggle).ticked);
}

TEST_CASE ("MenuModel: the Parts item is enabled and ticked from its own flag", "[menu-model]")
{
    MenuState s;
    CHECK (itemWith (menuNamed (buildMenus (s), "View"), ViewPartsToggle).enabled);
    CHECK_FALSE (itemWith (menuNamed (buildMenus (s), "View"), ViewPartsToggle).ticked);
    s.partsVisible = true;
    CHECK (itemWith (menuNamed (buildMenus (s), "View"), ViewPartsToggle).ticked);
}

TEST_CASE ("MenuModel: the Mixer item follows Parts and is ticked from its own flag", "[menu-model][mixer]")
{
    MenuState s;
    CHECK (labels (menuNamed (buildMenus (s), "View"))
           == V { "Export ABC panel", "Diagnostics list", "MIDI Source", "MIDI Editor", "LOTRO Preview", "Tracks", "Parts", "Mixer", "-", "Layouts>" });
    CHECK (itemWith (menuNamed (buildMenus (s), "View"), ViewMixerToggle).enabled);
    CHECK_FALSE (itemWith (menuNamed (buildMenus (s), "View"), ViewMixerToggle).ticked);
    s.mixerVisible = true;
    CHECK (itemWith (menuNamed (buildMenus (s), "View"), ViewMixerToggle).ticked);
    CHECK_FALSE (itemWith (menuNamed (buildMenus (s), "View"), ViewPartsToggle).ticked);
}

TEST_CASE ("MenuModel: the MIDI Source item is ticked by default and follows its flag", "[menu-model][source]")
{
    MenuState s;
    CHECK (itemWith (menuNamed (buildMenus (s), "View"), ViewSourceToggle).enabled);
    CHECK (itemWith (menuNamed (buildMenus (s), "View"), ViewSourceToggle).ticked);
    s.sourceVisible = false;
    CHECK_FALSE (itemWith (menuNamed (buildMenus (s), "View"), ViewSourceToggle).ticked);
}

TEST_CASE ("MenuModel: the Tracks item sits before Parts and is ticked from its own flag", "[menu-model][tracks]")
{
    MenuState s;
    CHECK (itemWith (menuNamed (buildMenus (s), "View"), ViewTracksToggle).enabled);
    CHECK_FALSE (itemWith (menuNamed (buildMenus (s), "View"), ViewTracksToggle).ticked);
    s.tracksVisible = true;
    CHECK (itemWith (menuNamed (buildMenus (s), "View"), ViewTracksToggle).ticked);
    CHECK_FALSE (itemWith (menuNamed (buildMenus (s), "View"), ViewPartsToggle).ticked);
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
            CHECK ((known.count (i.id) == 1 || isGridSizeCommand (i.id) || isLayoutApplyCommand (i.id) || isSongKeyCommand (i.id)));
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

TEST_CASE ("MenuModel: Tempo and Meter... needs a time base", "[menu-model]")
{
    CHECK_FALSE (itemWith (menuNamed (buildMenus ({}), "Edit"), EditTempoMeter).enabled);
    MenuState s; s.hasTimeBase = true;
    CHECK (itemWith (menuNamed (buildMenus (s), "Edit"), EditTempoMeter).enabled);
    CHECK (allCommandIds.size() == 39);
}

TEST_CASE ("MenuModel: View ends with a Layouts submenu holding Save, each saved layout and Manage", "[menu-model]")
{
    const auto layouts = [] (std::vector<std::string> names)
    {
        MenuState s; s.layoutNames = std::move (names);
        const auto view = menuNamed (buildMenus (s), "View");
        REQUIRE (view.items.back().isSubmenu);
        CHECK (view.items.back().label == "Layouts");
        return MenuDesc { "Layouts", view.items.back().children };
    };
    const auto none = layouts ({});
    CHECK (labels (none) == V { "Save Layout", "-", "Manage Layouts..." });
    CHECK (itemWith (none, ViewLayoutSave).enabled);
    CHECK_FALSE (itemWith (none, ViewLayoutManage).enabled);

    const auto two = layouts ({ "Wide", "Layout 2" });
    CHECK (labels (two) == V { "Save Layout", "-", "Wide", "Layout 2", "-", "Manage Layouts..." });
    CHECK (itemWith (two, ViewLayoutApplyBase).label == "Wide");
    CHECK (itemWith (two, ViewLayoutApplyBase + 1).label == "Layout 2");
    CHECK (itemWith (two, ViewLayoutManage).enabled);
    CHECK (isLayoutApplyCommand (ViewLayoutApplyBase + 1));
    CHECK_FALSE (isLayoutApplyCommand (ViewLayoutApplyBase + maxSavedLayouts));
}

TEST_CASE ("MenuModel: every pickable strip command is a real command with a unique key", "[menu-model][strip-buttons]")
{
    std::set<std::string> keys;
    for (const auto& c : stripCommands)
    {
        CHECK (std::find (allCommandIds.begin(), allCommandIds.end(), c.id) != allCommandIds.end());
        CHECK (keys.insert (c.key).second);
        CHECK (std::string (c.label).size() > 0);
    }
}

TEST_CASE ("MenuModel: Quit, About and the centre transport buttons are not pickable", "[menu-model][strip-buttons]")
{
    for (const auto id : { FileQuit, HelpAbout, TransportPlayPause, TransportStop, TransportGoToStart, TransportGoToEnd })
        for (const auto& c : stripCommands)
            CHECK (c.id != id);
}

TEST_CASE ("MenuModel: findStripCommand looks a key up and rejects an unknown one", "[menu-model][strip-buttons]")
{
    REQUIRE (findStripCommand ("file.exportMidi") != nullptr);
    CHECK (findStripCommand ("file.exportMidi")->id == FileExportMidi);
    CHECK (findStripCommand ("future.thing") == nullptr);
}

TEST_CASE ("MenuModel: findMenuItem finds an item inside a submenu", "[menu-model][strip-buttons]")
{
    MenuState s; s.hasAbc = true;
    const auto menus = buildMenus (s);
    const auto* abc = findMenuItem (menus, FileExportAbc);
    REQUIRE (abc != nullptr);
    CHECK (abc->enabled);
    CHECK (findMenuItem (menus, 99999) == nullptr);
}

TEST_CASE ("EditorMenuModel: the MIDI editor's menu bar is File, Edit, Navigate, Options, View; Edit has no items yet", "[menu-model][editor-menu]")
{
    const auto menus = editorMenus ({});
    std::vector<std::string> names;
    for (const auto& m : menus)
        names.push_back (m.name);
    CHECK (names == std::vector<std::string> { "File", "Edit", "Navigate", "Options", "View" });
    CHECK (menus[1].items.empty());
    CHECK_FALSE (menus[2].items.empty());
    CHECK_FALSE (menus[3].items.empty());
    CHECK_FALSE (menus[4].items.empty());
}

TEST_CASE ("EditorMenuModel: View is Piano Notes and MIDI Numbers, enabled with or without a track, the current one ticked", "[menu-model][editor-menu][keyboard-labels]")
{
    EditorMenuState s;
    const auto piano = menuNamed (editorMenus (s), "View");
    CHECK (labels (piano) == V { "Piano Notes", "MIDI Numbers" });
    CHECK (piano.items[0].id == EditorViewPianoNotes);
    CHECK (piano.items[1].id == EditorViewMidiNumbers);
    CHECK (piano.items[0].enabled);   // no track loaded
    CHECK (piano.items[1].enabled);
    CHECK (piano.items[0].ticked);
    CHECK_FALSE (piano.items[1].ticked);

    s.keyboardLabels = KeyboardLabels::midiNumbers;
    const auto midi = menuNamed (editorMenus (s), "View");
    CHECK_FALSE (midi.items[0].ticked);
    CHECK (midi.items[1].ticked);
}

TEST_CASE ("EditorMenuModel: File is Export to New MIDI File, a separator, Close Editor; export needs a track", "[menu-model][editor-menu]")
{
    EditorMenuState s;
    const auto file = menuNamed (editorMenus (s), "File");
    CHECK (labels (file) == std::vector<std::string> { "Export to New MIDI File...", "-", "Close Editor" });
    CHECK (file.items[0].id == EditorFileExportTrackMidi);
    CHECK (file.items[2].id == EditorFileClose);
    CHECK_FALSE (file.items[0].enabled);   // no track loaded
    CHECK (file.items[2].enabled);

    s.hasTrack = true;
    CHECK (menuNamed (editorMenus (s), "File").items[0].enabled);
}

TEST_CASE ("EditorMenuModel: Options is a Snap to Grid item ticked by the snap state, enabled with or without a track, then Snap to Key and Note Preview, then Grid Length and Grid Division, then Note Length and Note Division", "[menu-model][editor-menu]")
{
    EditorMenuState s;
    s.snapToGrid = false;
    const auto off = menuNamed (editorMenus (s), "Options");
    CHECK (labels (off) == std::vector<std::string> { "Snap to Grid", "Snap to Key>", "Note Preview>",
                                                      "-", "Grid Length>", "Grid Division>",
                                                      "-", "Note Length>", "Note Division>" });
    CHECK (off.items[0].id == EditorOptionsSnapToGrid);
    CHECK (off.items[0].enabled);   // no track loaded
    CHECK_FALSE (off.items[0].ticked);

    s.snapToGrid = true;
    CHECK (menuNamed (editorMenus (s), "Options").items[0].ticked);
}

TEST_CASE ("EditorMenuModel: Navigate is Move Marker Left / Right by Grid on the arrow keys, enabled when the marker can step", "[menu-model][editor-menu]")
{
    EditorMenuState s;
    const auto nav = menuNamed (editorMenus (s), "Navigate");
    CHECK (labels (nav) == std::vector<std::string> { "Move Marker Left by Grid", "Move Marker Right by Grid",
                                                      "-", "Move Up a Semitone", "Move Down a Semitone",
                                                      "-", "Select Note Nearest Cursor",
                                                      "Add Note Nearest Cursor to Selection",
                                                      "-", "Select Previous Note", "Select Next Note",
                                                      "-", "Select Previous Note of the Same Pitch",
                                                      "Select Next Note of the Same Pitch",
                                                      "Select All Notes of the Same Pitch",
                                                      "-", "Add Previous Note to Selection",
                                                      "Add Next Note to Selection",
                                                      "Move Cursor to Start of Selection" });
    CHECK (nav.items[0].id == EditorNavigateMarkerLeft);
    CHECK (nav.items[1].id == EditorNavigateMarkerRight);
    CHECK (nav.items[0].shortcut == "Left");
    CHECK (nav.items[1].shortcut == "Right");
    CHECK_FALSE (nav.items[0].enabled);
    CHECK_FALSE (nav.items[1].enabled);

    s.canStepMarker = true;
    const auto on = menuNamed (editorMenus (s), "Navigate");
    CHECK (on.items[0].enabled);
    CHECK (on.items[1].enabled);
}

TEST_CASE ("EditorMenuModel: Navigate's Move Up / Down a Semitone are on Up and Down, enabled when a row is active", "[menu-model][editor-menu][active-row]")
{
    EditorMenuState s;
    const auto nav = menuNamed (editorMenus (s), "Navigate");
    CHECK (nav.items[3].id == EditorNavigatePitchUp);
    CHECK (nav.items[4].id == EditorNavigatePitchDown);
    CHECK (nav.items[3].shortcut == "Up");
    CHECK (nav.items[4].shortcut == "Down");
    CHECK_FALSE (nav.items[3].enabled);
    CHECK_FALSE (nav.items[4].enabled);

    s.canStepPitch = true;
    const auto on = menuNamed (editorMenus (s), "Navigate");
    CHECK (on.items[3].enabled);
    CHECK (on.items[4].enabled);
    CHECK_FALSE (on.items[0].enabled);   // independent of the marker steps
}

TEST_CASE ("EditorMenuModel: Navigate's Select Note Nearest Cursor has no shortcut and is enabled when there is a note to pick", "[menu-model][editor-menu][note-nav]")
{
    EditorMenuState s;
    const auto nav = menuNamed (editorMenus (s), "Navigate");
    CHECK (nav.items[6].id == EditorNavigateSelectNearestNote);
    CHECK (nav.items[6].shortcut.empty());
    CHECK_FALSE (nav.items[6].enabled);

    s.canSelectNearestNote = true;
    CHECK (menuNamed (editorMenus (s), "Navigate").items[6].enabled);
}

TEST_CASE ("EditorMenuModel: Navigate's Add Note Nearest Cursor to Selection has no shortcut and is enabled when a note is left to add", "[menu-model][editor-menu][note-nav]")
{
    EditorMenuState s;
    s.canSelectNearestNote = true;
    const auto nav = menuNamed (editorMenus (s), "Navigate");
    CHECK (nav.items[7].id == EditorNavigateAddNearestNote);
    CHECK (nav.items[7].shortcut.empty());
    CHECK_FALSE (nav.items[7].enabled);   // its own flag, not Select's

    s.canAddNearestNote = true;
    CHECK (menuNamed (editorMenus (s), "Navigate").items[7].enabled);
}

TEST_CASE ("EditorMenuModel: Navigate's Select Previous / Next Note have no shortcut and are enabled when there is a note that way", "[menu-model][editor-menu][note-nav]")
{
    EditorMenuState s;
    s.canSelectNearestNote = true;
    const auto nav = menuNamed (editorMenus (s), "Navigate");
    CHECK (nav.items[9].id == EditorNavigateSelectPreviousNote);
    CHECK (nav.items[10].id == EditorNavigateSelectNextNote);
    CHECK (nav.items[9].shortcut.empty());
    CHECK (nav.items[10].shortcut.empty());
    CHECK_FALSE (nav.items[9].enabled);    // their own flags
    CHECK_FALSE (nav.items[10].enabled);

    s.canSelectPreviousNote = true;
    const auto previousOnly = menuNamed (editorMenus (s), "Navigate");
    CHECK (previousOnly.items[9].enabled);
    CHECK_FALSE (previousOnly.items[10].enabled);

    s.canSelectPreviousNote = false;
    s.canSelectNextNote = true;
    const auto nextOnly = menuNamed (editorMenus (s), "Navigate");
    CHECK_FALSE (nextOnly.items[9].enabled);
    CHECK (nextOnly.items[10].enabled);
}

TEST_CASE ("EditorMenuModel: Navigate's Same Pitch items have no shortcut and follow their own flags", "[menu-model][editor-menu][same-pitch]")
{
    EditorMenuState s;
    s.canSelectNearestNote = true;
    s.canSelectPreviousNote = true;
    s.canSelectNextNote = true;
    const auto nav = menuNamed (editorMenus (s), "Navigate");
    CHECK (nav.items[12].id == EditorNavigateSelectPreviousSamePitch);
    CHECK (nav.items[13].id == EditorNavigateSelectNextSamePitch);
    CHECK (nav.items[14].id == EditorNavigateSelectAllSamePitch);
    for (int i : { 12, 13, 14 })
    {
        CHECK (nav.items[(std::size_t) i].shortcut.empty());
        CHECK_FALSE (nav.items[(std::size_t) i].enabled);
    }

    s.canSelectPreviousSamePitch = true;
    const auto previous = menuNamed (editorMenus (s), "Navigate");
    CHECK (previous.items[12].enabled);
    CHECK_FALSE (previous.items[13].enabled);
    CHECK_FALSE (previous.items[14].enabled);

    s.canSelectPreviousSamePitch = false;
    s.canSelectNextSamePitch = true;
    const auto next = menuNamed (editorMenus (s), "Navigate");
    CHECK_FALSE (next.items[12].enabled);
    CHECK (next.items[13].enabled);
    CHECK_FALSE (next.items[14].enabled);

    s.canSelectNextSamePitch = false;
    s.canSelectAllSamePitch = true;
    const auto all = menuNamed (editorMenus (s), "Navigate");
    CHECK_FALSE (all.items[12].enabled);
    CHECK_FALSE (all.items[13].enabled);
    CHECK (all.items[14].enabled);
}

TEST_CASE ("EditorMenuModel: Navigate's Add Previous / Next Note and Move Cursor to Start of Selection have no shortcut and follow their own flags", "[menu-model][editor-menu][extend-selection]")
{
    EditorMenuState s;
    s.canSelectNearestNote = true;
    s.canSelectAllSamePitch = true;
    const auto nav = menuNamed (editorMenus (s), "Navigate");
    CHECK (nav.items[16].id == EditorNavigateAddPreviousNote);
    CHECK (nav.items[17].id == EditorNavigateAddNextNote);
    CHECK (nav.items[18].id == EditorNavigateCursorToSelectionStart);
    for (int i : { 16, 17, 18 })
    {
        CHECK (nav.items[(std::size_t) i].shortcut.empty());
        CHECK_FALSE (nav.items[(std::size_t) i].enabled);
    }

    s.canAddPreviousNote = true;
    const auto previous = menuNamed (editorMenus (s), "Navigate");
    CHECK (previous.items[16].enabled);
    CHECK_FALSE (previous.items[17].enabled);
    CHECK_FALSE (previous.items[18].enabled);

    s.canAddPreviousNote = false;
    s.canAddNextNote = true;
    const auto next = menuNamed (editorMenus (s), "Navigate");
    CHECK_FALSE (next.items[16].enabled);
    CHECK (next.items[17].enabled);
    CHECK_FALSE (next.items[18].enabled);

    s.canAddNextNote = false;
    s.canMoveCursorToSelectionStart = true;
    const auto cursor = menuNamed (editorMenus (s), "Navigate");
    CHECK_FALSE (cursor.items[16].enabled);
    CHECK_FALSE (cursor.items[17].enabled);
    CHECK (cursor.items[18].enabled);
}

TEST_CASE ("EditorMenuModel: arrow keys step to the next menu that has items, wrapping, or stay put", "[menu-model][editor-menu]")
{
    const std::vector<bool> hasItems { true, false, false, true, false };
    CHECK (nextOpenableMenu (hasItems, 0, +1) == 3);
    CHECK (nextOpenableMenu (hasItems, 0, -1) == 3);   // wraps left past the empty View
    CHECK (nextOpenableMenu (hasItems, 3, +1) == 0);   // wraps right
    CHECK (nextOpenableMenu ({ false, true, false }, 1, +1) == -1);   // only the open one has items
    CHECK (nextOpenableMenu ({ false, false, false }, 0, +1) == -1);
    CHECK (nextOpenableMenu ({}, 0, +1) == -1);
}

namespace
{
    MenuItemDesc snapToKeyMenu (const EditorMenuState& s)
    {
        const auto options = menuNamed (editorMenus (s), "Options");
        REQUIRE (options.items.size() == 9);
        return options.items[1];
    }

    MenuItemDesc childNamed (const MenuItemDesc& m, const std::string& label)
    {
        for (const auto& c : m.children)
            if (c.label == label)
                return c;
        FAIL ("no child " << label);
        return {};
    }

    std::vector<std::string> childLabels (const MenuItemDesc& m)
    {
        return labels (MenuDesc { m.label, m.children });
    }
}

TEST_CASE ("EditorMenuModel: Snap to Key holds Enabled, Root and Type; Type lists the scales then Chords", "[menu-model][editor-menu][snap-to-key]")
{
    const auto snap = snapToKeyMenu ({});
    CHECK (snap.isSubmenu);
    CHECK (snap.label == "Snap to Key");
    CHECK (snap.enabled);
    CHECK (childLabels (snap) == V { "Enabled", "Root>", "Type>" });
    CHECK (childNamed (snap, "Enabled").id == EditorOptionsSnapToKeyEnabled);

    const auto root = childNamed (snap, "Root");
    REQUIRE ((int) root.children.size() == snapRootCount);
    for (int i = 0; i < snapRootCount; ++i)
    {
        CAPTURE (i);
        CHECK (root.children[(std::size_t) i].label == snapRoots()[(std::size_t) i].name);
        CHECK (root.children[(std::size_t) i].id == EditorSnapRootBase + i);
    }

    const auto type = childNamed (snap, "Type");
    CHECK (childLabels (type) == V { "Major", "Natural minor", "Melodic minor", "Harmonic minor", "Major pentatonic",
                                     "Minor pentatonic", "Major blues", "Minor blues", "Chords>" });
    for (int i = 0; i < snapFirstChord; ++i)
        CHECK (type.children[(std::size_t) i].id == EditorSnapScaleBase + i);

    const auto chords = childNamed (type, "Chords");
    CHECK (childLabels (chords) == V { "Major triad", "Minor triad", "Major seventh", "Minor seventh", "Dominant seventh",
                                       "Diminished triad", "Diminished seventh", "Augmented fifth triad" });
    for (int i = snapFirstChord; i < snapScaleCount; ++i)
        CHECK (chords.children[(std::size_t) (i - snapFirstChord)].id == EditorSnapScaleBase + i);
}

TEST_CASE ("EditorMenuModel: with no key, Snap to Key's Enabled and Type are greyed and Root stays open", "[menu-model][editor-menu][snap-to-key]")
{
    EditorMenuState s;
    s.snapRootIndex = -1;
    s.snapToKey = true;
    const auto snap = snapToKeyMenu (s);
    CHECK_FALSE (childNamed (snap, "Enabled").enabled);
    CHECK_FALSE (childNamed (snap, "Type").enabled);
    const auto root = childNamed (snap, "Root");
    CHECK (root.enabled);
    for (const auto& c : root.children)
    {
        CHECK (c.enabled);
        CHECK_FALSE (c.ticked);
    }
}

TEST_CASE ("EditorMenuModel: Snap to Key ticks Enabled, the exact root spelling and the current type", "[menu-model][editor-menu][snap-to-key]")
{
    EditorMenuState s;
    s.snapToKey = true;
    s.snapRootIndex = 3;   // A♯
    s.snapScaleIndex = (int) SnapScale::dominantSeventh;
    const auto snap = snapToKeyMenu (s);

    const auto enabled = childNamed (snap, "Enabled");
    CHECK (enabled.enabled);
    CHECK (enabled.ticked);

    const auto root = childNamed (snap, "Root");
    for (int i = 0; i < snapRootCount; ++i)
        CHECK (root.children[(std::size_t) i].ticked == (i == 3));   // B♭ (2) shares the pitch class but is not ticked

    const auto type = childNamed (snap, "Type");
    CHECK (type.enabled);
    for (int i = 0; i < snapFirstChord; ++i)
        CHECK_FALSE (type.children[(std::size_t) i].ticked);
    const auto chords = childNamed (type, "Chords");
    for (int i = snapFirstChord; i < snapScaleCount; ++i)
        CHECK (chords.children[(std::size_t) (i - snapFirstChord)].ticked == (i == (int) SnapScale::dominantSeventh));

    s.snapToKey = false;
    CHECK_FALSE (childNamed (snapToKeyMenu (s), "Enabled").ticked);
}

TEST_CASE ("EditorMenuModel: the Snap to Key root and type id ranges", "[menu-model][editor-menu][snap-to-key]")
{
    CHECK (isEditorSnapRootCommand (EditorSnapRootBase));
    CHECK (isEditorSnapRootCommand (EditorSnapRootBase + snapRootCount - 1));
    CHECK_FALSE (isEditorSnapRootCommand (EditorSnapRootBase + snapRootCount));
    CHECK_FALSE (isEditorSnapRootCommand (EditorSnapRootBase - 1));
    CHECK (isEditorSnapScaleCommand (EditorSnapScaleBase));
    CHECK (isEditorSnapScaleCommand (EditorSnapScaleBase + snapScaleCount - 1));
    CHECK_FALSE (isEditorSnapScaleCommand (EditorSnapScaleBase + snapScaleCount));
    CHECK (EditorSnapRootBase == 1000);
    CHECK (EditorSnapScaleBase == 1100);
}

TEST_CASE ("EditorMenuModel: Note Preview holds three checkboxes, enabled with or without a track, ticked by the settings", "[menu-model][editor-menu][note-preview]")
{
    const auto previewMenu = [] (const EditorMenuState& s)
    {
        const auto options = menuNamed (editorMenus (s), "Options");
        REQUIRE (options.items.size() == 9);
        return options.items[2];
    };

    const auto menu = previewMenu ({});
    CHECK (menu.isSubmenu);
    CHECK (menu.label == "Note Preview");
    CHECK (menu.enabled);
    REQUIRE (childLabels (menu) == V { "Preview notes when inserting", "Preview notes when editing",
                                       "Preview notes when velocity changes" });
    const int ids[] = { EditorOptionsPreviewOnInsert, EditorOptionsPreviewOnEdit, EditorOptionsPreviewOnVelocity };
    for (size_t i = 0; i < 3; ++i)
    {
        CAPTURE (i);
        CHECK (menu.children[i].id == ids[i]);
        CHECK (menu.children[i].enabled);   // no track loaded
        CHECK_FALSE (menu.children[i].ticked);
    }

    for (size_t on = 0; on < 3; ++on)
    {
        EditorMenuState s;
        s.hasTrack = true;
        s.notePreview.onInsert = on == 0;
        s.notePreview.onEdit = on == 1;
        s.notePreview.onVelocity = on == 2;
        const auto m = previewMenu (s);
        for (size_t i = 0; i < 3; ++i)
        {
            CAPTURE (on, i);
            CHECK (m.children[i].enabled);
            CHECK (m.children[i].ticked == (i == on));
        }
    }
}

TEST_CASE ("EditorMenuModel: Grid Length lists Off then whole to 1/64, ticked by the current length and enabled with a track", "[menu-model][editor-menu][grid-length]")
{
    const auto lengthMenu = [] (const EditorMenuState& s)
    {
        const auto options = menuNamed (editorMenus (s), "Options");
        REQUIRE (options.items.size() == 9);
        return options.items[4];
    };

    EditorMenuState s;
    const auto none = lengthMenu (s);
    CHECK (none.isSubmenu);
    CHECK (none.label == "Grid Length");
    REQUIRE (childLabels (none) == V { "Off", "-", "1/1", "1/2", "1/4", "1/8", "1/16", "1/32", "1/64" });
    for (const auto& c : none.children)
        if (! c.separator)
            CHECK_FALSE (c.enabled);   // no track loaded

    s.hasTrack = true;
    s.gridLength = GridLength::Eighth;
    const auto menu = lengthMenu (s);
    const GridLength lengths[] = { GridLength::Off, GridLength::Whole, GridLength::Half, GridLength::Quarter,
                                   GridLength::Eighth, GridLength::Sixteenth, GridLength::ThirtySecond,
                                   GridLength::SixtyFourth };
    std::vector<MenuItemDesc> items;
    for (const auto& c : menu.children)
        if (! c.separator)
            items.push_back (c);
    REQUIRE (items.size() == 8);
    for (size_t i = 0; i < 8; ++i)
    {
        CAPTURE (i);
        CHECK (items[i].id == EditorGridLengthBase + (int) lengths[i]);
        CHECK (isEditorGridLengthCommand (items[i].id));
        CHECK (items[i].enabled);
        CHECK (items[i].ticked == (lengths[i] == GridLength::Eighth));
    }
    CHECK (EditorGridLengthBase == 1200);
    CHECK_FALSE (isEditorGridLengthCommand (EditorGridLengthBase + gridLengthCount));
}

TEST_CASE ("EditorMenuModel: Grid Division lists Straight, Triplet, Dotted and Swing, ticked by the current division and enabled with a track", "[menu-model][editor-menu][grid-length]")
{
    const auto divisionMenu = [] (const EditorMenuState& s)
    {
        const auto options = menuNamed (editorMenus (s), "Options");
        REQUIRE (options.items.size() == 9);
        return options.items[5];
    };

    EditorMenuState s;
    const auto none = divisionMenu (s);
    CHECK (none.isSubmenu);
    CHECK (none.label == "Grid Division");
    REQUIRE (childLabels (none) == V { "Straight", "Triplet", "Dotted", "Swing" });
    CHECK (none.children[0].ticked);   // Straight unless changed
    for (const auto& c : none.children)
        CHECK_FALSE (c.enabled);       // no track loaded

    s.hasTrack = true;
    s.gridDivision = GridDivision::Swing;
    const auto menu = divisionMenu (s);
    const GridDivision divisions[] = { GridDivision::Straight, GridDivision::Triplet, GridDivision::Dotted,
                                       GridDivision::Swing };
    for (size_t i = 0; i < 4; ++i)
    {
        CAPTURE (i);
        CHECK (menu.children[i].id == EditorGridDivisionBase + (int) divisions[i]);
        CHECK (isEditorGridDivisionCommand (menu.children[i].id));
        CHECK (menu.children[i].enabled);
        CHECK (menu.children[i].ticked == (divisions[i] == GridDivision::Swing));
    }
    CHECK (EditorGridDivisionBase == 1300);
    CHECK_FALSE (isEditorGridDivisionCommand (EditorGridDivisionBase + gridDivisionCount));
}

TEST_CASE ("EditorMenuModel: Note Length lists Off then whole to 1/64, ticked by the current note length and enabled with a track", "[menu-model][editor-menu][note-length]")
{
    const auto lengthMenu = [] (const EditorMenuState& s)
    {
        const auto options = menuNamed (editorMenus (s), "Options");
        REQUIRE (options.items.size() == 9);
        return options.items[7];
    };

    EditorMenuState s;
    const auto none = lengthMenu (s);
    CHECK (none.isSubmenu);
    CHECK (none.label == "Note Length");
    REQUIRE (childLabels (none) == V { "Off", "-", "1/1", "1/2", "1/4", "1/8", "1/16", "1/32", "1/64" });
    CHECK (none.children[0].ticked);   // Off unless changed
    for (const auto& c : none.children)
        if (! c.separator)
            CHECK_FALSE (c.enabled);   // no track loaded

    s.hasTrack = true;
    s.noteLength = GridLength::Sixteenth;
    const auto menu = lengthMenu (s);
    std::vector<MenuItemDesc> items;
    for (const auto& c : menu.children)
        if (! c.separator)
            items.push_back (c);
    REQUIRE (items.size() == 8);
    for (size_t i = 0; i < 8; ++i)
    {
        CAPTURE (i);
        CHECK (items[i].id == EditorNoteLengthBase + (int) i);
        CHECK (isEditorNoteLengthCommand (items[i].id));
        CHECK_FALSE (isEditorGridLengthCommand (items[i].id));
        CHECK (items[i].enabled);
        CHECK (items[i].ticked == ((GridLength) i == GridLength::Sixteenth));
    }
    CHECK (EditorNoteLengthBase == 1400);
    CHECK_FALSE (isEditorNoteLengthCommand (EditorNoteLengthBase + gridLengthCount));
}

TEST_CASE ("EditorMenuModel: Note Division lists Straight, Triplet and Dotted (no Swing), ticked by the current note division and enabled with a track", "[menu-model][editor-menu][note-length]")
{
    const auto divisionMenu = [] (const EditorMenuState& s)
    {
        const auto options = menuNamed (editorMenus (s), "Options");
        REQUIRE (options.items.size() == 9);
        return options.items[8];
    };

    EditorMenuState s;
    const auto none = divisionMenu (s);
    CHECK (none.isSubmenu);
    CHECK (none.label == "Note Division");
    REQUIRE (childLabels (none) == V { "Straight", "Triplet", "Dotted" });
    CHECK (none.children[0].ticked);   // Straight unless changed
    for (const auto& c : none.children)
        CHECK_FALSE (c.enabled);       // no track loaded

    s.hasTrack = true;
    s.noteDivision = GridDivision::Dotted;
    const auto menu = divisionMenu (s);
    for (size_t i = 0; i < 3; ++i)
    {
        CAPTURE (i);
        CHECK (menu.children[i].id == EditorNoteDivisionBase + (int) i);
        CHECK (isEditorNoteDivisionCommand (menu.children[i].id));
        CHECK (menu.children[i].enabled);
        CHECK (menu.children[i].ticked == ((GridDivision) i == GridDivision::Dotted));
    }
    CHECK (EditorNoteDivisionBase == 1500);
    CHECK_FALSE (isEditorNoteDivisionCommand (EditorNoteDivisionBase + (int) GridDivision::Swing));
}
