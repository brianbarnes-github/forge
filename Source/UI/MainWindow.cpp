#include "MainWindow.h"
#include "EditorPane.h"
#include "DiagnosticsPane.h"
#include "SongModelBridge.h"
#include "SongsmithMainComponent.h"
#include "SplitterComponent.h"

#include "Core/AbcWriter.h"
#include "Core/Config.h"
#include "Core/ConfigWriter.h"
#include "Core/InstrumentAssembly.h"
#include "Core/MidiImporter.h"
#include "Core/Pipeline.h"

#include <fstream>

namespace lotro
{

class MainWindow::Body : public juce::Component
{
public:
    explicit Body (SongDocument& doc)
        : splitter (SplitterComponent::Orientation::leftRight), songsmith (doc)
    {
        addAndMakeVisible (editor);
        addAndMakeVisible (diagnostics);
        splitter.setComponents (&editor, &diagnostics);
        // Both the classic splitter and the Songsmith view are children
        // throughout this Body's lifetime; only one is ever visible
        // (toggled by setSongsmithMode), so switching modes never needs to
        // reparent anything. Songsmith is the default on startup (amendment
        // (a)); the View menu's "Classic editor" item, off by default,
        // switches back to the splitter.
        addChildComponent (splitter);
        splitter.setVisible (false);

        addChildComponent (songsmith);
        songsmith.setVisible (true);
    }

    void resized() override
    {
        splitter.setBounds (getLocalBounds());
        songsmith.setBounds (getLocalBounds());
    }

    EditorPane&      getEditor()      { return editor; }
    DiagnosticsPane& getDiagnostics() { return diagnostics; }
    SongsmithMainComponent& getSongsmith() { return songsmith; }

    void setSongsmithMode (bool enabled)
    {
        splitter.setVisible (! enabled);
        songsmith.setVisible (enabled);
    }
    bool isSongsmithMode() const { return songsmith.isVisible(); }

private:
    EditorPane      editor;
    DiagnosticsPane diagnostics;
    SplitterComponent splitter;
    SongsmithMainComponent songsmith;
};

MainWindow::MainWindow()
    : juce::DocumentWindow ("Forge",
                            juce::Colours::lightgrey,
                            juce::DocumentWindow::allButtons),
      body (std::make_unique<Body> (songDocument)),
      menuBar (std::make_unique<juce::MenuBarComponent> (this))
{
    // JUCE's own title bar (false) rather than the native X11/WSLg one.
    // Works around a WSLg quirk where the WM re-positions the window on
    // focus events, causing visible drift when the user first clicks on it.
    setUsingNativeTitleBar (false);
    setResizable (true, true);

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

    centreWithSize (getWidth(), getHeight());
    setVisible (true);

    body->getEditor().onRunRequested = [this] { runConversion(); };
}

MainWindow::~MainWindow() = default;

void MainWindow::closeButtonPressed()
{
    juce::JUCEApplication::getInstance()->systemRequestedQuit();
}

juce::StringArray MainWindow::getMenuBarNames() { return { "File", "Edit", "Song", "View" }; }

juce::PopupMenu MainWindow::getMenuForIndex (int topLevelMenuIndex, const juce::String&)
{
    juce::PopupMenu m;
    if (topLevelMenuIndex == 0) // File
    {
        m.addItem (FileOpenMidi,    "Open MIDI...",    true, false);
        m.addItem (FileOpenConfig,  "Open Config...",  true, false);
        m.addSeparator();
        juce::PopupMenu saveAs;
        saveAs.addItem (FileSaveAsJson, "JSON (.json)", true, false);
        saveAs.addItem (FileSaveAsToml, "TOML (.toml)", true, false);
        saveAs.addItem (FileSaveAsXml,  "XML (.xml)",   true, false);
        // Save Config As reads the classic EditorPane's Config, which is
        // never fed anything in Songsmith mode — disabled there so the
        // dialog can never be opened to overwrite a real file with a
        // near-empty classic Config (same defect class as Open Config,
        // see saveConfigAs()'s own early return below).
        m.addSubMenu ("Save Config As", saveAs, ! body->isSongsmithMode());
        // Save ABC As reads `lastAbc`, which only Run Converter (classic
        // mode) ever populates — enabled only in classic mode, same
        // rationale as Save Config As above.
        m.addItem (FileSaveAbc, "Save ABC As...", ! body->isSongsmithMode() && ! lastAbc.empty(), false);
        m.addSeparator();
        m.addItem (FileQuit, "Quit");
    }
    else if (topLevelMenuIndex == 1) // Edit
    {
        // No keyboard shortcuts yet (Phase 8). Recomputed fresh every time
        // the menu opens, so canUndo()/canRedo() don't need an explicit
        // menuItemsChanged() poke elsewhere. Mode-gated like Song/File's
        // Songsmith-only items below: songDocument is invisible in classic
        // mode, so undoing/redoing it there would silently mutate a
        // document the user isn't looking at.
        const bool songsmith = body->isSongsmithMode();
        m.addItem (EditUndo, "Undo", songsmith && songDocument.canUndo(), false);
        m.addItem (EditRedo, "Redo", songsmith && songDocument.canRedo(), false);
    }
    else if (topLevelMenuIndex == 2) // Song
    {
        // Visible in both modes; only meaningful (and enabled) in Songsmith
        // mode, which is the only mode with a songDocument to act on.
        m.addItem (SongDefaultParts, "Default parts from tracks", body->isSongsmithMode(), false);
    }
    else if (topLevelMenuIndex == 3) // View
    {
        // Songsmith is the default view; this item is OFF by default and,
        // when ticked, switches to the classic EditorPane/DiagnosticsPane
        // layout (kept fully functional until it is deleted in Phase 6).
        m.addItem (ViewClassicEditorToggle, "Classic editor", true, ! body->isSongsmithMode());
    }
    return m;
}

void MainWindow::menuItemSelected (int menuItemID, int)
{
    switch (menuItemID)
    {
        case FileOpenMidi:    openMidiViaDialog();                                           return;
        case FileOpenConfig:  openConfigViaDialog();                                          return;
        case FileSaveAsJson:  saveConfigAs (ConfigFormat::Json);                              return;
        case FileSaveAsToml:  saveConfigAs (ConfigFormat::Toml);                              return;
        case FileSaveAsXml:   saveConfigAs (ConfigFormat::Xml);                               return;
        case FileSaveAbc:     saveAbcAs();                                                    return;
        case FileQuit:        juce::JUCEApplication::getInstance()->systemRequestedQuit();   return;
        case EditUndo:        songDocument.undo();                                            return;
        case EditRedo:        songDocument.redo();                                            return;
        case SongDefaultParts:
            if (body->isSongsmithMode())
                synthesiseDefaultParts (songDocument);
            return;
        case ViewClassicEditorToggle: toggleClassicEditorMode();                              return;
        default:                                                                              return;
    }
}

void MainWindow::toggleClassicEditorMode()
{
    body->setSongsmithMode (! body->isSongsmithMode());
    menuItemsChanged();
}

bool MainWindow::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (const auto& f : files)
    {
        const auto ext = juce::File (f).getFileExtension().toLowerCase();
        if (ext == ".mid" || ext == ".midi") return true;
        if (ext == ".json" || ext == ".toml" || ext == ".xml") return true;
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
        if (ext == ".json" || ext == ".toml" || ext == ".xml")
        {
            openConfigFromPath (file);
            return;
        }
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
    if (body->isSongsmithMode())
    {
        Diagnostics diags;
        importMidiFile (songDocument, file, nextImportBatch++, diags);
        body->getSongsmith().getDiagnostics().setDiagnostics (std::move (diags));
        return;
    }

    std::ifstream stream (file.getFullPathName().toStdString(), std::ios::binary);
    if (! stream)
    {
        juce::NativeMessageBox::showMessageBoxAsync (
            juce::MessageBoxIconType::WarningIcon,
            "Open MIDI failed", "Could not read: " + file.getFullPathName());
        return;
    }

    Diagnostics importDiags;
    Song raw;
    try
    {
        raw = importMidi (stream, file.getFileNameWithoutExtension().toStdString(), importDiags);
    }
    catch (const std::exception& e)
    {
        juce::NativeMessageBox::showMessageBoxAsync (
            juce::MessageBoxIconType::WarningIcon,
            "Open MIDI failed", juce::String (e.what()));
        return;
    }

    auto cfg = synthesiseConfig (raw,
        file.getFullPathName().toStdString(),
        file.withFileExtension (".abc").getFullPathName().toStdString(),
        std::nullopt, 0, {});

    body->getEditor().loadFromMidi (std::move (raw), std::move (cfg));
}

void MainWindow::saveConfigAs (ConfigFormat format)
{
    // Same defect class as openConfigFromPath's guard below: the classic
    // EditorPane's Config is never fed anything in Songsmith mode, so
    // without this the user could pick an existing config file and have it
    // silently overwritten with a near-empty one. The menu item is also
    // disabled in Songsmith mode (getMenuForIndex), so this is defence in
    // depth against reaching saveConfigAs any other way.
    if (body->isSongsmithMode())
    {
        juce::NativeMessageBox::showMessageBoxAsync (
            juce::MessageBoxIconType::InfoIcon,
            "Not supported",
            "Save Config is not supported in Songsmith mode yet");
        return;
    }

    const juce::String ext =
        (format == ConfigFormat::Json) ? ".json"
      : (format == ConfigFormat::Toml) ? ".toml"
      :                                    ".xml";

    fileChooser = std::make_unique<juce::FileChooser> (
        "Save Config", juce::File(), "*" + ext);

    fileChooser->launchAsync (juce::FileBrowserComponent::saveMode
                            | juce::FileBrowserComponent::canSelectFiles
                            | juce::FileBrowserComponent::warnAboutOverwriting,
        [this, format, ext] (const juce::FileChooser& fc)
        {
            auto file = fc.getResult();
            if (file == juce::File()) return;
            if (! file.getFileName().endsWithIgnoreCase (ext))
                file = file.withFileExtension (ext);

            const auto& cfg = body->getEditor().getConfig();
            const auto err = writeConfigToFile (file.getFullPathName().toStdString(),
                                                format, cfg);
            if (! err.empty())
            {
                juce::NativeMessageBox::showMessageBoxAsync (
                    juce::MessageBoxIconType::WarningIcon,
                    "Save failed", juce::String (err));
            }
        });
}

void MainWindow::openConfigViaDialog()
{
    fileChooser = std::make_unique<juce::FileChooser> (
        "Choose a Config", juce::File(), "*.json;*.toml;*.xml");

    fileChooser->launchAsync (juce::FileBrowserComponent::openMode
                            | juce::FileBrowserComponent::canSelectFiles,
        [this] (const juce::FileChooser& fc)
        {
            const auto file = fc.getResult();
            if (file == juce::File()) return;
            openConfigFromPath (file);
        });
}

void MainWindow::openConfigFromPath (const juce::File& file)
{
    // Config files load into the classic EditorPane, which isn't visible (or
    // wired up) in Songsmith mode. Both the File -> Open Config... dialog and
    // dropped config files funnel through here, so this is the single place
    // that branch needs to live.
    if (body->isSongsmithMode())
    {
        juce::NativeMessageBox::showMessageBoxAsync (
            juce::MessageBoxIconType::InfoIcon,
            "Not supported",
            "Config files are not supported in Songsmith mode yet");
        return;
    }

    Config cfg;
    Diagnostics migDiag;
    const auto loadErr = loadConfigFromFile (file.getFullPathName().toStdString(),
                                             ConfigFormat::Auto, cfg, migDiag);
    if (! loadErr.empty())
    {
        juce::NativeMessageBox::showMessageBoxAsync (
            juce::MessageBoxIconType::WarningIcon,
            "Open Config failed", juce::String (loadErr));
        return;
    }

    if (cfg.input.empty())
    {
        juce::NativeMessageBox::showMessageBoxAsync (
            juce::MessageBoxIconType::WarningIcon,
            "Open Config failed",
            "Config has no 'input' field — cannot load the referenced MIDI.");
        return;
    }

    const auto configDir = file.getParentDirectory();
    const auto midiFile  = configDir.getChildFile (juce::String (cfg.input));

    std::ifstream stream (midiFile.getFullPathName().toStdString(), std::ios::binary);
    if (! stream)
    {
        juce::NativeMessageBox::showMessageBoxAsync (
            juce::MessageBoxIconType::WarningIcon,
            "Open Config failed",
            "Could not read MIDI referenced by the config: " + midiFile.getFullPathName());
        return;
    }

    Diagnostics importDiags;
    Song raw;
    try
    {
        raw = importMidi (stream, midiFile.getFileNameWithoutExtension().toStdString(), importDiags);
    }
    catch (const std::exception& e)
    {
        juce::NativeMessageBox::showMessageBoxAsync (
            juce::MessageBoxIconType::WarningIcon,
            "Open Config failed", juce::String (e.what()));
        return;
    }

    body->getEditor().loadFromMidi (std::move (raw), std::move (cfg));
}

void MainWindow::runConversion()
{
    auto& editor = body->getEditor();
    const auto& cfg = editor.getConfig();
    const auto& raw = editor.getRawSong();

    Diagnostics diagnostics;

    const auto validErr = validateConfig (cfg, (int) raw.tracks.size());
    if (! validErr.empty())
    {
        Diagnostic err;
        err.severity = Severity::Error;
        err.source   = "Config";
        err.message  = validErr;
        diagnostics.push_back (err);
        body->getDiagnostics().show (std::move (diagnostics), {});
        return;
    }

    Song assembled;
    std::string abc;
    try
    {
        assembled = assembleInstruments (raw, cfg, diagnostics);
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
        body->getDiagnostics().show (std::move (diagnostics), {});
        return;
    }

    lastAbc = abc;
    body->getDiagnostics().show (std::move (diagnostics), std::move (abc));
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

    // Default the dialog's initial filename to <input-stem>.abc so a saved
    // ABC lands next to the MIDI's name by default.
    const auto& cfg = body->getEditor().getConfig();
    juce::File defaultPath;
    if (! cfg.input.empty())
        defaultPath = juce::File (juce::String (cfg.input)).withFileExtension (".abc");

    fileChooser = std::make_unique<juce::FileChooser> (
        "Save ABC", defaultPath, "*.abc");

    fileChooser->launchAsync (juce::FileBrowserComponent::saveMode
                            | juce::FileBrowserComponent::canSelectFiles
                            | juce::FileBrowserComponent::warnAboutOverwriting,
        [this] (const juce::FileChooser& fc)
        {
            auto file = fc.getResult();
            if (file == juce::File()) return;
            if (! file.getFileName().endsWithIgnoreCase (".abc"))
                file = file.withFileExtension (".abc");

            if (! file.replaceWithText (juce::String (lastAbc)))
            {
                juce::NativeMessageBox::showMessageBoxAsync (
                    juce::MessageBoxIconType::WarningIcon,
                    "Save ABC failed",
                    "Could not write: " + file.getFullPathName());
            }
        });
}

} // namespace lotro
