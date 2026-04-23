#pragma once

#include "EditorPane.h"
#include "DiagnosticsPane.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace lotro
{

class MainWindow : public juce::DocumentWindow,
                   public juce::MenuBarModel
{
public:
    MainWindow();
    ~MainWindow() override;

    void closeButtonPressed() override;

    // MenuBarModel
    juce::StringArray getMenuBarNames() override;
    juce::PopupMenu   getMenuForIndex (int topLevelMenuIndex,
                                       const juce::String& menuName) override;
    void              menuItemSelected (int menuItemID, int topLevelMenuIndex) override;

private:
    enum CommandId
    {
        FileOpenMidi    = 1,
        FileOpenConfig,
        FileSaveConfig,
        FileSaveAsJson,
        FileSaveAsToml,
        FileSaveAsXml,
        FileQuit,
        EditAddInstrument,
        EditRemoveSelected
    };

    class Body;
    std::unique_ptr<Body> body;
    std::unique_ptr<juce::MenuBarComponent> menuBar;
};

} // namespace lotro
