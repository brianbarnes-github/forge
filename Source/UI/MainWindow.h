#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "Core/ConfigLoader.h"

#include <memory>

namespace lotro
{

class EditorPane;
class DiagnosticsPane;

class MainWindow : public juce::DocumentWindow,
                   public juce::MenuBarModel,
                   public juce::FileDragAndDropTarget
{
public:
    MainWindow();
    ~MainWindow() override;

    void closeButtonPressed() override;

    juce::StringArray getMenuBarNames() override;
    juce::PopupMenu   getMenuForIndex (int topLevelMenuIndex,
                                       const juce::String& menuName) override;
    void              menuItemSelected (int menuItemID, int topLevelMenuIndex) override;

    // FileDragAndDropTarget
    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void filesDropped           (const juce::StringArray& files, int x, int y) override;

private:
    enum CommandId
    {
        FileOpenMidi    = 1,
        FileOpenConfig,
        FileSaveAsJson,
        FileSaveAsToml,
        FileSaveAsXml,
        FileSaveAbc,
        FileQuit
    };

    class Body;
    std::unique_ptr<Body>                   body;
    std::unique_ptr<juce::MenuBarComponent> menuBar;
    std::unique_ptr<juce::FileChooser>      fileChooser;

    void openMidiViaDialog();
    void openMidiFromPath (const juce::File& file);
    void openConfigViaDialog();
    void openConfigFromPath (const juce::File& file);
    void runConversion();
    void saveConfigAs (ConfigFormat format);
    void saveAbcAs();

    std::string lastAbc;   // populated by runConversion(); consumed by saveAbcAs()
};

} // namespace lotro
