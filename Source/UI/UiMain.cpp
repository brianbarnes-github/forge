#include <juce_gui_basics/juce_gui_basics.h>

namespace lotro
{

class HelloWindow : public juce::DocumentWindow
{
public:
    HelloWindow()
        : juce::DocumentWindow ("LOTRO ABC Converter UI",
                                juce::Colours::lightgrey,
                                juce::DocumentWindow::allButtons)
    {
        setUsingNativeTitleBar (true);
        setResizable (true, true);
        centreWithSize (800, 600);
        setVisible (true);
    }

    void closeButtonPressed() override
    {
        juce::JUCEApplication::getInstance()->systemRequestedQuit();
    }
};

class UiApp : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override       { return "LotroAbcConverterUi"; }
    const juce::String getApplicationVersion() override    { return "0.1"; }
    bool moreThanOneInstanceAllowed() override             { return true; }

    void initialise (const juce::String&) override         { window = std::make_unique<HelloWindow>(); }
    void shutdown() override                               { window.reset(); }
    void systemRequestedQuit() override                    { quit(); }

private:
    std::unique_ptr<HelloWindow> window;
};

} // namespace lotro

START_JUCE_APPLICATION (lotro::UiApp)
