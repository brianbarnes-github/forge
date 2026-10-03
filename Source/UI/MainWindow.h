#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "DiscardGuard.h"
#include "SongDocument.h"
#include "SongSession.h"

#include <functional>
#include <memory>

namespace lotro
{

class MainWindow : public juce::DocumentWindow,
                   public juce::MenuBarModel,
                   public juce::FileDragAndDropTarget
{
public:
    MainWindow();
    ~MainWindow() override;

    void closeButtonPressed() override;
    bool keyPressed (const juce::KeyPress&) override;

    juce::StringArray getMenuBarNames() override;
    juce::PopupMenu   getMenuForIndex (int topLevelMenuIndex,
                                       const juce::String& menuName) override;
    void              menuItemSelected (int menuItemID, int topLevelMenuIndex) override;

    // FileDragAndDropTarget
    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void filesDropped           (const juce::StringArray& files, int x, int y) override;

    // Opens without asking about unsaved changes -- callers guard.
    void openSongFromPath (const juce::File& file);
    // Guarded: asks about unsaved changes first (drops, later the command line).
    void requestOpenSong (const juce::File& file);
    // Guarded quit.
    void requestQuit();

private:
    enum CommandId
    {
        FileNew = 1,
        FileOpenSong,
        FileClose,
        FileSave,
        FileSaveAs,
        FileImportMidi,
        FileExportAbc,
        FileExportMidi,
        FileQuit,
        EditUndo,
        EditRedo,
        SongDefaultParts,
        SongRunConverter,
        ViewExportPanelToggle,
        ViewDiagnosticsToggle,
        EditQuantize,
        HelpAbout,

        // Grid-size submenu items occupy EditGridSizeBase + each GridSize
        // enum value, and menuItemSelected range-checks incoming ids against
        // that span. Pinned well clear of the ids above rather than left to
        // be last in declaration order, so appending an enumerator here
        // can't silently land inside the grid-size range.
        EditGridSizeBase = 1000
    };

    class Body;
    // Declared before `body` so it's constructed first (member init order
    // follows declaration order) — Body's SongsmithMainComponent needs a
    // reference to it at construction time.
    SongDocument                             songDocument;
    // Before `body`; its onChanged is wired in the constructor body.
    SongSession                              session { songDocument };
    std::unique_ptr<Body>                   body;
    std::unique_ptr<juce::MenuBarComponent> menuBar;
    std::unique_ptr<juce::FileChooser>      fileChooser;

    // Per-user app settings (window position so far), in the OS's usual
    // per-user settings folder, e.g. %APPDATA%\SongSmith\SongSmith.settings.
    std::unique_ptr<juce::PropertiesFile>   settings;

    void openMidiViaDialog();
    void openMidiFromPath (const juce::File& file);
    void runConversion();
    void saveAbcAs();
    void exportMidiAs();

    void refreshTitle();
    void resetToEmptySong();
    void openSongViaDialog();
    void saveSong (std::function<void (bool)> done = {});
    void saveSongAs (std::function<void (bool)> done = {});
    void writeSongTo (const juce::File& file, std::function<void (bool)> done);
    DiscardGuardHooks guardHooks();
    void guarded (std::function<void()> action);
    void afterDocumentReplaced();

    std::string lastAbc;   // populated by runConversion(); consumed by saveAbcAs()
};

} // namespace lotro
