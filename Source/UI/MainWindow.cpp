#include "MainWindow.h"
#include "AboutBox.h"
#include "DiagnosticsPane.h"
#include "DiscardGuard.h"
#include "GridSize.h"
#include "MidiExport.h"
#include "RawMidi.h"
#include "SongFile.h"
#include "SongModelBridge.h"
#include "SongSession.h"
#include "SongsmithMainComponent.h"
#include "WindowPlacement.h"

#include "Playback/PlaybackError.h"

#include "Core/AbcWriter.h"
#include "Core/Config.h"
#include "Core/InstrumentAssembly.h"
#include "Core/Pipeline.h"

#include <fstream>

namespace lotro
{

namespace
{
    constexpr const char* mainWindowPlacementKey = "mainWindowPlacement";
    constexpr const char* songExtension = ".songsmith";

    void showError (const juce::String& title, const juce::String& message)
    {
        juce::NativeMessageBox::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, title, message);
    }

    juce::PropertiesFile::Options settingsOptions()
    {
        juce::PropertiesFile::Options options;
        options.applicationName = "SongSmith";
        options.folderName = "SongSmith";
        options.filenameSuffix = ".settings";
        options.osxLibrarySubFolder = "Application Support";
        return options;
    }

    // Choose a destination, append the extension if missing, then confirm an
    // overwrite ourselves (the native warning would check the typed name, not
    // the name with the extension appended). `write` runs only once confirmed,
    // and only if the window still exists.
    void chooseAndConfirm (MainWindow* owner, std::unique_ptr<juce::FileChooser>& holder,
                           const juce::String& title, const juce::File& defaultFile,
                           const juce::String& wildcard, const juce::StringArray& accepted,
                           const juce::String& defaultExt, std::function<void (const juce::File&)> write)
    {
        holder = std::make_unique<juce::FileChooser> (title, defaultFile, wildcard);
        holder->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles,
            [safe = juce::Component::SafePointer<MainWindow> (owner), accepted, defaultExt, write]
            (const juce::FileChooser& fc)
            {
                if (safe == nullptr) return;
                auto file = fc.getResult();
                if (file == juce::File()) return;
                file = withExtensionIfMissing (file, accepted, defaultExt);

                if (! file.existsAsFile()) { write (file); return; }

                juce::NativeMessageBox::showAsync (
                    juce::MessageBoxOptions()
                        .withIconType (juce::MessageBoxIconType::WarningIcon)
                        .withTitle ("Replace file?")
                        .withMessage (file.getFileName() + " already exists. Replace it?")
                        .withButton ("Replace").withButton ("Cancel")
                        .withAssociatedComponent (safe.getComponent()),
                    [safe, file, write] (int button) { if (button == 0 && safe != nullptr) write (file); });
            });
    }
}

class MainWindow::Body : public juce::Component
{
public:
    Body (SongDocument& doc, PlaybackController& playback) : songsmith (doc, &playback)
    {
        // Both songsmith and exportPanel are children throughout this Body's
        // lifetime; only one is ever visible (toggled by
        // setExportPanelVisible), so switching never needs to reparent
        // anything. Songsmith is the default; the export panel is shown by
        // Song -> Run Converter or View -> Export ABC panel.
        addChildComponent (songsmith);
        songsmith.setVisible (true);

        addChildComponent (exportPanel);
        exportPanel.setVisible (false);
    }

    void resized() override
    {
        auto area = getLocalBounds();
        songsmith.setBounds (area);
        exportPanel.setBounds (area);
    }

    SongsmithMainComponent& getSongsmith()    { return songsmith; }
    DiagnosticsPane&        getExportPanel()  { return exportPanel; }

    bool isExportPanelVisible() const { return exportPanel.isVisible(); }
    void setExportPanelVisible (bool shouldBeVisible)
    {
        exportPanel.setVisible (shouldBeVisible);
        songsmith.setVisible (! shouldBeVisible);
    }

private:
    SongsmithMainComponent songsmith;
    DiagnosticsPane        exportPanel;
};

MainWindow::MainWindow()
    : juce::DocumentWindow ("Songsmith",
                            juce::Colours::lightgrey,
                            juce::DocumentWindow::allButtons),
      body (std::make_unique<Body> (songDocument, playback)),
      menuBar (std::make_unique<juce::MenuBarComponent> (this)),
      settings (std::make_unique<juce::PropertiesFile> (settingsOptions()))
{
    // JUCE's own title bar (false) rather than the native X11/WSLg one.
    // Works around a WSLg quirk where the WM re-positions the window on
    // focus events, causing visible drift when the user first clicks on it.
    setUsingNativeTitleBar (false);
    setResizable (true, true);
    setWantsKeyboardFocus (true);

    // Host component: JUCE calls its resized() whenever the window's content
    // area changes (WM resize, initial mapping, drag-resize). It lays the
    // menu bar and body out to fill. With setContentOwned(..., true), JUCE
    // keeps host->getBounds() in sync with the content area, so there's no
    // manual coupling between the window's size and the children's layout.
    class Host : public juce::Component
    {
    public:
        Host (juce::MenuBarComponent& menuIn, juce::Component& bodyIn)
            : menu (menuIn), bodyRef (bodyIn)
        {
            addAndMakeVisible (menu);
            addAndMakeVisible (bodyRef);
        }

        void resized() override
        {
            menu.setBounds (0, 0, getWidth(), 24);
            bodyRef.setBounds (0, 24, getWidth(), getHeight() - 24);
        }

    private:
        juce::MenuBarComponent& menu;
        juce::Component&        bodyRef;
    };

    auto* host = new Host (*menuBar, *body);
    host->setSize (1000, 700);
    setContentOwned (host, /*useBoundsForComponent=*/true);

    // Where it was last closed -- or centred on the primary monitor if that
    // monitor is gone (WindowPlacement::resolve) or nothing was saved yet.
    if (! WindowPlacement::restoreWindow (*settings, mainWindowPlacementKey, *this))
        centreWithSize (getWidth(), getHeight());
    setVisible (true);
    WindowPlacement::applyMaximised (*settings, mainWindowPlacementKey, *this);

    // Fires synchronously inside ValueTree callbacks, so refreshTitle() must
    // never touch the tree.
    session.onChanged = [this] { refreshTitle(); };
    refreshTitle();

    playback.onBeforePlay = [this] { return ensurePlaybackReady(); };
    loadStartupSoundFont();
}

MainWindow::~MainWindow()
{
    // The audio thread must stop before the engine and synth are destroyed.
    // (Members are destroyed in reverse order, so this is also guaranteed by
    // declaration order; resetting here makes it explicit.)
    synthGc.stopTimer();
    audioOutput.reset();

    // Every quit path (close button, File -> Quit, OS shutdown) destroys the
    // window, so saving here covers them all.
    WindowPlacement::saveWindow (*settings, mainWindowPlacementKey, *this);
    settings->saveIfNeeded();
}

void MainWindow::closeButtonPressed()
{
    requestQuit();
}

juce::StringArray MainWindow::getMenuBarNames() { return { "File", "Edit", "Song", "View", "Help" }; }

juce::PopupMenu MainWindow::getMenuForIndex (int topLevelMenuIndex, const juce::String&)
{
    juce::PopupMenu m;
    if (topLevelMenuIndex == 0) // File
    {
        const auto item = [&m] (int id, const juce::String& text, const juce::String& shortcut, bool enabled)
        {
            juce::PopupMenu::Item i (text);
            i.itemID = id;
            i.isEnabled = enabled;
            i.shortcutKeyDescription = shortcut;
            m.addItem (i);
        };
        item (FileNew,      "New",         "Ctrl+N",       true);
        item (FileOpenSong, "Open...",     "Ctrl+O",       true);
        item (FileClose,    "Close",       "",             true);
        m.addSeparator();
        item (FileSave,     "Save",        "Ctrl+S",       session.isDirty() || session.isUntitled());
        item (FileSaveAs,   "Save As...",  "Ctrl+Shift+S", true);
        m.addSeparator();
        juce::PopupMenu importMenu;
        importMenu.addItem (FileImportMidi, "MIDI...");
        m.addSubMenu ("Import", importMenu);
        juce::PopupMenu exportMenu;
        // MIDI is enabled once the song has anything besides its conductor.
        exportMenu.addItem (FileExportMidi, "MIDI...", songDocument.getNumTracks() > 1);
        exportMenu.addItem (FileExportAbc,  "ABC...",  ! lastAbc.empty());
        m.addSubMenu ("Export", exportMenu);
        m.addSeparator();
        m.addItem (FileQuit, "Quit");
    }
    else if (topLevelMenuIndex == 1) // Edit
    {
        // No keyboard shortcuts yet (Phase 8). Recomputed fresh every time
        // the menu opens, so canUndo()/canRedo() don't need an explicit
        // menuItemsChanged() poke elsewhere.
        m.addItem (EditUndo, "Undo", songDocument.canUndo(), false);
        m.addItem (EditRedo, "Redo", songDocument.canRedo(), false);

        m.addSeparator();
        const bool editorOpen = body->getSongsmith().isTrackEditorOpen();
        m.addItem (EditQuantize, "Quantize", editorOpen);

        juce::PopupMenu gridMenu;
        gridMenu.addItem (EditGridSizeBase + (int) GridSize::Off,       "Off",  editorOpen);
        gridMenu.addItem (EditGridSizeBase + (int) GridSize::Quarter,   "1/4",  editorOpen);
        gridMenu.addItem (EditGridSizeBase + (int) GridSize::Eighth,    "1/8",  editorOpen);
        gridMenu.addItem (EditGridSizeBase + (int) GridSize::Sixteenth, "1/16", editorOpen);
        m.addSubMenu ("Grid Size", gridMenu, editorOpen);
    }
    else if (topLevelMenuIndex == 2) // Song
    {
        m.addItem (SongDefaultParts, "Default parts from tracks", true, false);
        m.addItem (SongRunConverter, "Run Converter", true, false);
        m.addSeparator();
        m.addItem (SongSoundFont, "SoundFont...");
    }
    else if (topLevelMenuIndex == 3) // View
    {
        m.addItem (ViewExportPanelToggle, "Export ABC panel", true, body->isExportPanelVisible());
        m.addItem (ViewDiagnosticsToggle, "Diagnostics list", true, body->getSongsmith().isDiagnosticsVisible());
    }
    else if (topLevelMenuIndex == 4) // Help
    {
        m.addItem (HelpAbout, "About...");
    }
    return m;
}

void MainWindow::menuItemSelected (int menuItemID, int)
{
    if (menuItemID >= EditGridSizeBase && menuItemID <= EditGridSizeBase + (int) GridSize::Sixteenth)
    {
        body->getSongsmith().setActiveEditorGridSize ((GridSize) (menuItemID - EditGridSizeBase));
        return;
    }

    switch (menuItemID)
    {
        case FileNew:
        case FileClose:       guarded ([this] { resetToEmptySong(); });                       return;
        case FileOpenSong:    guarded ([this] { openSongViaDialog(); });                       return;
        case FileSave:        saveSong();                                                      return;
        case FileSaveAs:      saveSongAs();                                                    return;
        case FileImportMidi:  openMidiViaDialog();                                            return;
        case FileExportAbc:   saveAbcAs();                                                    return;
        case FileExportMidi:  exportMidiAs();                                                 return;
        case FileQuit:        requestQuit();                                                   return;
        case EditUndo:        songDocument.undo();                                            return;
        case EditRedo:        songDocument.redo();                                            return;
        case EditQuantize:    body->getSongsmith().quantizeActiveEditor();                    return;
        case HelpAbout:       showAboutDialog (this);                                         return;
        case SongSoundFont:   chooseSoundFont();                                              return;
        case SongDefaultParts: synthesiseDefaultParts (songDocument);                         return;
        case SongRunConverter:
            runConversion();
            body->setExportPanelVisible (true);
            return;
        case ViewExportPanelToggle:
            body->setExportPanelVisible (! body->isExportPanelVisible());
            menuItemsChanged();
            return;
        case ViewDiagnosticsToggle:
            body->getSongsmith().setDiagnosticsVisible (! body->getSongsmith().isDiagnosticsVisible());
            menuItemsChanged();
            return;
        default: return;
    }
}

bool MainWindow::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (const auto& f : files)
    {
        const auto ext = juce::File (f).getFileExtension().toLowerCase();
        if (ext == ".mid" || ext == ".midi" || ext == songExtension) return true;
    }
    return false;
}

void MainWindow::filesDropped (const juce::StringArray& files, int, int)
{
    for (const auto& f : files)
    {
        const auto file = juce::File (f);
        const auto ext = file.getFileExtension().toLowerCase();
        if (ext == ".mid" || ext == ".midi") { openMidiFromPath (file);   return; }
        if (ext == songExtension)            { requestOpenSong (file);    return; }
    }
}

void MainWindow::openMidiViaDialog()
{
    fileChooser = std::make_unique<juce::FileChooser> (
        "Choose a MIDI file", juce::File(), "*.mid;*.midi");

    fileChooser->launchAsync (juce::FileBrowserComponent::openMode
                            | juce::FileBrowserComponent::canSelectFiles,
        [this] (const juce::FileChooser& fc)
        {
            const auto file = fc.getResult();
            if (file == juce::File()) return;
            openMidiFromPath (file);
        });
}

void MainWindow::openMidiFromPath (const juce::File& file)
{
    Diagnostics diags;
    importMidiFile (songDocument, file, songDocument.mintImportBatch(), diags);
    body->getSongsmith().getDiagnostics().setDiagnostics (std::move (diags));
    body->getSongsmith().fitTrackTimelineToDocument();
}

void MainWindow::refreshTitle()
{
    setName (session.displayTitle());
    menuItemsChanged();
}

DiscardGuardHooks MainWindow::guardHooks()
{
    // Quit routes through these hooks, so a callback can outlive the window.
    const juce::Component::SafePointer<MainWindow> safe (this);
    return {
        // prompt: Save / Don't Save / Cancel
        [safe] (std::function<void (DiscardChoice)> done)
        {
            if (safe == nullptr) return;
            juce::NativeMessageBox::showAsync (
                juce::MessageBoxOptions()
                    .withIconType (juce::MessageBoxIconType::QuestionIcon)
                    .withTitle ("Unsaved changes")
                    .withMessage ("Save changes to " + safe->session.getName() + " before continuing?")
                    .withButton ("Save")
                    .withButton ("Don't Save")
                    .withButton ("Cancel")
                    .withAssociatedComponent (safe.getComponent()),
                [done] (int button)
                {
                    done (button == 0 ? DiscardChoice::Save
                        : button == 1 ? DiscardChoice::DontSave
                                      : DiscardChoice::Cancel);
                });
        },
        // save: Save, or Save As for an untitled Song
        [safe] (std::function<void (bool)> done)
        {
            if (safe != nullptr) safe->saveSong (std::move (done));
        }
    };
}

void MainWindow::guarded (std::function<void()> action)
{
    confirmDiscardChanges (session.isDirty(), guardHooks(), std::move (action));
}

void MainWindow::requestQuit()
{
    guarded ([] { juce::JUCEApplication::quit(); });
}

void MainWindow::afterDocumentReplaced()
{
    playback.documentReplaced();
    body->getSongsmith().documentReplaced();
    body->getSongsmith().getDiagnostics().setDiagnostics ({});
    lastAbc.clear();
    body->getExportPanel().show ({}, {});
    body->setExportPanelVisible (false);
    menuItemsChanged();
}

void MainWindow::resetToEmptySong()
{
    songDocument.resetToEmpty();
    session.markNew();
    afterDocumentReplaced();
}

void MainWindow::openSongFromPath (const juce::File& file)
{
    try
    {
        songDocument.replaceContents (loadSongFile (file));   // validates fully first; document untouched on throw
    }
    catch (const SongFileError& e)
    {
        showError ("Could not open Song", juce::String (e.what()));
        return;
    }
    session.markClean (file);
    afterDocumentReplaced();
}

void MainWindow::openSongOnStartup (const juce::File& file)
{
    openSongFromPath (file);   // nothing is open yet, so no guard
}

void MainWindow::requestOpenSong (const juce::File& file)
{
    guarded ([this, file] { openSongFromPath (file); });
}

void MainWindow::openSongViaDialog()
{
    fileChooser = std::make_unique<juce::FileChooser> ("Open Song", juce::File(), juce::String ("*") + songExtension);
    fileChooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [this] (const juce::FileChooser& fc)
        {
            const auto file = fc.getResult();
            if (file != juce::File())
                openSongFromPath (file);   // already behind the guard (File > Open runs guarded() first)
        });
}

void MainWindow::writeSongTo (const juce::File& file, std::function<void (bool)> done)
{
    const bool ok = saveSongFile (songDocument, file);
    if (ok)
        session.markClean (file);
    else
        showError ("Save failed", "Could not write: " + file.getFullPathName());
    if (done)
        done (ok);
}

void MainWindow::saveSong (std::function<void (bool)> done)
{
    if (session.isUntitled())
        saveSongAs (std::move (done));
    else
        writeSongTo (session.getFile(), std::move (done));
}

void MainWindow::saveSongAs (std::function<void (bool)> done)
{
    const auto defaultFile = defaultExportFile (session.getFile(), songExtension,
                                                juce::File::getSpecialLocation (juce::File::userDocumentsDirectory));
    fileChooser = std::make_unique<juce::FileChooser> ("Save Song", defaultFile, juce::String ("*") + songExtension);

    // No warnAboutOverwriting: the extension is appended AFTER the chooser
    // returns, so the native warning would check the wrong name. We ask ourselves.
    fileChooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles,
        [this, done] (const juce::FileChooser& fc)
        {
            auto file = fc.getResult();
            if (file == juce::File())
            {
                if (done) done (false);
                return;
            }
            file = withExtensionIfMissing (file, { songExtension }, songExtension);

            if (file.existsAsFile() && file != session.getFile())
            {
                // Built outside the capture list: MSVC resolves `this` in a
                // nested lambda's init-capture to the closure, not the window.
                const juce::Component::SafePointer<MainWindow> safe (this);
                juce::NativeMessageBox::showAsync (
                    juce::MessageBoxOptions()
                        .withIconType (juce::MessageBoxIconType::WarningIcon)
                        .withTitle ("Replace file?")
                        .withMessage (file.getFileName() + " already exists. Replace it?")
                        .withButton ("Replace")
                        .withButton ("Cancel")
                        .withAssociatedComponent (this),
                    [safe, file, done] (int button)
                    {
                        if (button == 0 && safe != nullptr) safe->writeSongTo (file, done);
                        else if (done)                      done (false);
                    });
                return;
            }
            writeSongTo (file, done);
        });
}

bool MainWindow::keyPressed (const juce::KeyPress& key)
{
    const auto cmd = juce::ModifierKeys::commandModifier;
    const auto cmdShift = juce::ModifierKeys::commandModifier | juce::ModifierKeys::shiftModifier;
    // Plain Space only (KeyPress equality includes modifiers). Text fields and
    // other focused children get the key first; the transport buttons are
    // non-focusable, so Space cannot re-click one.
    if (key == juce::KeyPress (juce::KeyPress::spaceKey)) { playback.togglePlayPause(); return true; }
    if (key == juce::KeyPress ('n', cmd, 0))      { menuItemSelected (FileNew, 0);      return true; }
    if (key == juce::KeyPress ('o', cmd, 0))      { menuItemSelected (FileOpenSong, 0); return true; }
    if (key == juce::KeyPress ('s', cmd, 0))      { if (session.isDirty() || session.isUntitled()) menuItemSelected (FileSave, 0);   return true; }
    if (key == juce::KeyPress ('s', cmdShift, 0)) { menuItemSelected (FileSaveAs, 0);   return true; }

    // Track-canvas keys. Never while an editable component has focus (it normally
    // consumes these first; this guards the ones it does not).
    if (dynamic_cast<juce::TextInputTarget*> (juce::Component::getCurrentlyFocusedComponent()) != nullptr)
        return false;
    // Plain S only; Ctrl/Cmd+S is Save above. Return true only when something happened.
    if (key == juce::KeyPress ('s'))                   return body->getSongsmith().splitSections();
    if (key == juce::KeyPress (juce::KeyPress::deleteKey) || key == juce::KeyPress (juce::KeyPress::backspaceKey))
                                                       return body->getSongsmith().deleteSections();
    if (key == juce::KeyPress ('a', cmd, 0))           { body->getSongsmith().selectAllTracks(); return true; }
    return false;
}

void MainWindow::loadStartupSoundFont()
{
    const juce::String configuredPath = settings->getValue ("soundFontPath");
    const juce::File configured = configuredPath.isNotEmpty() ? juce::File (configuredPath) : juce::File();
    const auto bundled = juce::File::getSpecialLocation (juce::File::currentExecutableFile).getSiblingFile ("resources").getChildFile ("TimGM6mb.sf2");
    for (const auto& candidate : { configured, bundled })
    {
        if (candidate == juce::File() || ! candidate.existsAsFile())
            continue;
        try { synth.loadSoundFont (candidate); return; }
        catch (const PlaybackError&) { /* fall through to the next candidate */ }
    }
    // No SoundFont: the app still starts; Play explains (ensurePlaybackReady).
}

bool MainWindow::ensurePlaybackReady()
{
    if (! synth.hasSoundFont())
    {
        showError ("No SoundFont", "Choose a SoundFont (.sf2) via Song > SoundFont... to enable playback.");
        return false;
    }
    if (audioOutput == nullptr)
    {
        try { audioOutput = std::make_unique<AudioOutput> (playback.engine()); }
        catch (const PlaybackError& e) { showError ("Audio unavailable", e.what()); return false; }
        catch (const std::exception& e) { showError ("Audio unavailable", e.what()); return false; }
    }
    return true;
}

void MainWindow::chooseSoundFont()
{
    fileChooser = std::make_unique<juce::FileChooser> ("Choose a SoundFont", juce::File(), "*.sf2");
    fileChooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [safe = juce::Component::SafePointer<MainWindow> (this)] (const juce::FileChooser& fc)
        {
            if (safe == nullptr) return;
            const auto file = fc.getResult();
            if (file == juce::File()) return;
            // Message thread. A failed load leaves the previous SoundFont active
            // (SynthVoice restores its state), and the setting is only written on success.
            try
            {
                safe->synth.loadSoundFont (file);
                safe->settings->setValue ("soundFontPath", file.getFullPathName());
                safe->settings->saveIfNeeded();
            }
            catch (const PlaybackError& e) { showError ("SoundFont", e.what()); }
            catch (const std::exception& e) { showError ("SoundFont", e.what()); }
        });
}

void MainWindow::runConversion()
{
    auto built = buildConfigAndRawSong (songDocument); // no partIds => full export

    Diagnostics diagnostics;

    // A part with zero assignments (e.g. freshly created via the part
    // strip's "+ Add", before any track is dragged onto it) builds a
    // ConfigInstrument with an empty `sources` array, which validateConfig
    // rejects — but that should only drop that one part from the export,
    // not abort every other correctly-configured part. (The scoped preview
    // path, computePartPreview, is unaffected — it never calls
    // validateConfig, so an empty part previews as empty rather than
    // erroring.)
    dropUnassignedInstruments (built.config, diagnostics);

    const auto validErr = validateConfig (built.config, (int) built.rawSong.tracks.size());
    if (! validErr.empty())
    {
        Diagnostic err;
        err.severity = Severity::Error;
        err.source   = "Config";
        err.message  = validErr;
        diagnostics.push_back (err);
        body->getExportPanel().show (std::move (diagnostics), {});
        return;
    }

    Song assembled;
    std::string abc;
    try
    {
        assembled = assembleInstruments (built.rawSong, built.config, diagnostics);
        runPipeline (assembled, diagnostics);
        abc = writeAbc (assembled);
    }
    catch (const std::exception& e)
    {
        Diagnostic err;
        err.severity = Severity::Error;
        err.source   = "Pipeline";
        err.message  = e.what();
        diagnostics.push_back (err);
        body->getExportPanel().show (std::move (diagnostics), {});
        return;
    }

    lastAbc = abc;
    body->getExportPanel().show (std::move (diagnostics), std::move (abc));
}

void MainWindow::saveAbcAs()
{
    if (lastAbc.empty())
    {
        juce::NativeMessageBox::showMessageBoxAsync (
            juce::MessageBoxIconType::InfoIcon,
            "No ABC to save",
            "Run Converter first to generate ABC output.");
        return;
    }

    const auto defaultFile = defaultExportFile (session.getFile(), ".abc",
                                                juce::File::getSpecialLocation (juce::File::userDocumentsDirectory));
    chooseAndConfirm (this, fileChooser, "Export ABC", defaultFile, "*.abc", { ".abc" }, ".abc",
        [this] (const juce::File& file)
        {
            if (! file.replaceWithText (juce::String (lastAbc)))
            {
                juce::NativeMessageBox::showMessageBoxAsync (
                    juce::MessageBoxIconType::WarningIcon,
                    "Save ABC failed",
                    "Could not write: " + file.getFullPathName());
            }
        });
}

void MainWindow::exportMidiAs()
{
    const auto defaultFile = defaultExportFile (session.getFile(), ".mid",
                                                juce::File::getSpecialLocation (juce::File::userDocumentsDirectory));
    chooseAndConfirm (this, fileChooser, "Export MIDI", defaultFile, "*.mid;*.midi", { ".mid", ".midi" }, ".mid",
        [this] (const juce::File& file)
        {
            // Build the whole byte buffer before touching the destination, so a
            // MidiExportError leaves any existing file intact.
            std::vector<std::uint8_t> bytes;
            try
            {
                bytes = writeMidiBytes (buildRawMidiFile (songDocument));
            }
            catch (const MidiExportError& e)
            {
                juce::NativeMessageBox::showMessageBoxAsync (
                    juce::MessageBoxIconType::WarningIcon,
                    "Export MIDI failed",
                    "Could not export: " + juce::String (e.what()));
                return;
            }

            if (! file.replaceWithData (bytes.data(), bytes.size()))
            {
                juce::NativeMessageBox::showMessageBoxAsync (
                    juce::MessageBoxIconType::WarningIcon,
                    "Export MIDI failed",
                    "Could not write: " + file.getFullPathName());
            }
        });
}

} // namespace lotro
