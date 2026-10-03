#include "MainWindow.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace lotro
{

class UiApp : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override       { return "LotroAbcConverterUi"; }
    const juce::String getApplicationVersion() override    { return "0.1"; }
    bool moreThanOneInstanceAllowed() override             { return true; }

    void initialise (const juce::String&) override
    {
        window = std::make_unique<MainWindow>();
        for (const auto& arg : getCommandLineParameterArray())
        {
            const auto file = juce::File::getCurrentWorkingDirectory().getChildFile (arg.unquoted());
            if (file.hasFileExtension (".songsmith") && file.existsAsFile())
            {
                window->openSongOnStartup (file);
                break;
            }
        }
    }
    void shutdown() override                               { window.reset(); }
    void systemRequestedQuit() override
    {
        if (window != nullptr) window->requestQuit();   // prompts if dirty, then quit()s
        else quit();
    }

private:
    std::unique_ptr<MainWindow> window;
};

} // namespace lotro

START_JUCE_APPLICATION (lotro::UiApp)
