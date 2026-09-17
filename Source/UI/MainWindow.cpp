#include "MainWindow.h"
#include "DiagnosticsPane.h"
#include "GridSize.h"
#include "SongModelBridge.h"
#include "SongsmithMainComponent.h"

#include "Core/AbcWriter.h"
#include "Core/Config.h"
#include "Core/ConfigWriter.h"
#include "Core/InstrumentAssembly.h"
#include "Core/Pipeline.h"

#include <fstream>

namespace lotro
{

class MainWindow::Body : public juce::Component
{
public:
    explicit Body (SongDocument& doc) : songsmith (doc)
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
        songsmith.setBounds (getLocalBounds());
        exportPanel.setBounds (getLocalBounds());
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
        // There is still no path from a loaded Config file into
        // SongDocument's ValueTree (see openConfigFromPath) — kept visible
        // per the plan's "keep File menu" decision, but permanently
        // disabled until that translation exists.
        m.addSubMenu ("Save Config As", saveAs, false);
        m.addItem (FileSaveAbc, "Save ABC As...", ! lastAbc.empty(), false);
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
    }
    else if (topLevelMenuIndex == 3) // View
    {
        m.addItem (ViewExportPanelToggle, "Export ABC panel", true, body->isExportPanelVisible());
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
        case FileOpenMidi:    openMidiViaDialog();                                           return;
        case FileOpenConfig:  openConfigViaDialog();                                          return;
        case FileSaveAsJson:  saveConfigAs (ConfigFormat::Json);                              return;
        case FileSaveAsToml:  saveConfigAs (ConfigFormat::Toml);                              return;
        case FileSaveAsXml:   saveConfigAs (ConfigFormat::Xml);                               return;
        case FileSaveAbc:     saveAbcAs();                                                    return;
        case FileQuit:        juce::JUCEApplication::getInstance()->systemRequestedQuit();   return;
        case EditUndo:        songDocument.undo();                                            return;
        case EditRedo:        songDocument.redo();                                            return;
        case EditQuantize:    body->getSongsmith().quantizeActiveEditor();                    return;
        case SongDefaultParts: synthesiseDefaultParts (songDocument);                         return;
        case SongRunConverter:
            runConversion();
            body->setExportPanelVisible (true);
            return;
        case ViewExportPanelToggle:
            body->setExportPanelVisible (! body->isExportPanelVisible());
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
    Diagnostics diags;
    importMidiFile (songDocument, file, nextImportBatch++, diags);
    body->getSongsmith().getDiagnostics().setDiagnostics (std::move (diags));
}

void MainWindow::saveConfigAs (ConfigFormat format)
{
    // Save Config As is disabled in the menu (getMenuForIndex) — there is
    // still no path from SongDocument's ValueTree into a Config file, so
    // this early return is defence in depth against reaching it any other
    // way, matching openConfigFromPath's own guard below.
    juce::NativeMessageBox::showMessageBoxAsync (
        juce::MessageBoxIconType::InfoIcon,
        "Not supported",
        "Save Config is not supported yet");
    juce::ignoreUnused (format);
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

void MainWindow::openConfigFromPath (const juce::File&)
{
    // There is still no path from a loaded Config file into SongDocument's
    // ValueTree — building one is out of scope for this phase. Both the
    // File -> Open Config... dialog and dropped config files funnel through
    // here, so this is the single place that early return needs to live.
    juce::NativeMessageBox::showMessageBoxAsync (
        juce::MessageBoxIconType::InfoIcon,
        "Not supported",
        "Config files are not supported yet");
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

    // Default the dialog's initial filename to <input-stem>.abc so a saved
    // ABC lands next to the MIDI's name by default.
    const auto inputMidiPath = songDocument.getTree().getProperty (SongIDs::inputMidiPath).toString();
    juce::File defaultPath;
    if (inputMidiPath.isNotEmpty())
        defaultPath = juce::File (inputMidiPath).withFileExtension (".abc");

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
