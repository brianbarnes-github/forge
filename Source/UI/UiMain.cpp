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

    void initialise (const juce::String&) override         { window = std::make_unique<MainWindow>(); }
    void shutdown() override                               { window.reset(); }
    void systemRequestedQuit() override                    { quit(); }

private:
    std::unique_ptr<MainWindow> window;
};

} // namespace lotro

START_JUCE_APPLICATION (lotro::UiApp)
