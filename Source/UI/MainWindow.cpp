#include "MainWindow.h"

namespace lotro
{

class MainWindow::Body : public juce::Component
{
public:
    Body()
    {
        addAndMakeVisible (editor);
        addAndMakeVisible (diagnostics);

        splitter.setComponents (&editor, &diagnostics);
        addAndMakeVisible (splitter);
    }

    void resized() override
    {
        splitter.setBounds (getLocalBounds());
    }

private:
    class Splitter : public juce::Component
    {
    public:
        void setComponents (juce::Component* l, juce::Component* r)
        {
            left = l; right = r;
        }

        void resized() override
        {
            const auto area = getLocalBounds();
            const int barWidth = 6;
            const int leftWidth = (int) std::lround (area.getWidth() * leftFraction);
            if (left)  left->setBounds  (area.withWidth (leftWidth));
            bar.setBounds (area.withX (leftWidth).withWidth (barWidth));
            if (right) right->setBounds (area.withTrimmedLeft (leftWidth + barWidth));
            addAndMakeVisible (bar);
        }

    private:
        class Bar : public juce::Component
        {
        public:
            void paint (juce::Graphics& g) override { g.fillAll (juce::Colours::grey); }
            void mouseDrag (const juce::MouseEvent& e) override
            {
                if (auto* parent = getParentComponent())
                {
                    auto* split = dynamic_cast<Splitter*> (parent);
                    if (split == nullptr) return;
                    const float w = (float) parent->getWidth();
                    if (w <= 0.0f) return;
                    split->leftFraction = juce::jlimit (0.15f, 0.85f,
                        e.getEventRelativeTo (parent).x / w);
                    parent->resized();
                }
            }
            void mouseEnter (const juce::MouseEvent&) override
            {
                setMouseCursor (juce::MouseCursor::LeftRightResizeCursor);
            }
        };

        juce::Component* left  = nullptr;
        juce::Component* right = nullptr;
        Bar              bar;
        float            leftFraction = 0.55f;
        friend class Bar;
    };

    EditorPane      editor;
    DiagnosticsPane diagnostics;
    Splitter        splitter;
};

MainWindow::MainWindow()
    : juce::DocumentWindow ("LOTRO ABC Converter UI",
                            juce::Colours::lightgrey,
                            juce::DocumentWindow::allButtons),
      body (std::make_unique<Body>()),
      menuBar (std::make_unique<juce::MenuBarComponent> (this))
{
    setUsingNativeTitleBar (true);
    setResizable (true, true);
    centreWithSize (1000, 700);

    auto host = std::make_unique<juce::Component>();
    host->addAndMakeVisible (*menuBar);
    host->addAndMakeVisible (*body);
    auto* hostRaw = host.release();

    setContentOwned (hostRaw, false);

    hostRaw->setSize (1000, 700);
    menuBar->setBounds (0, 0, hostRaw->getWidth(), 24);
    body->setBounds (0, 24, hostRaw->getWidth(), hostRaw->getHeight() - 24);

    setVisible (true);
}

MainWindow::~MainWindow() = default;

void MainWindow::closeButtonPressed()
{
    juce::JUCEApplication::getInstance()->systemRequestedQuit();
}

juce::StringArray MainWindow::getMenuBarNames()
{
    return { "File", "Edit" };
}

juce::PopupMenu MainWindow::getMenuForIndex (int topLevelMenuIndex, const juce::String&)
{
    juce::PopupMenu m;
    if (topLevelMenuIndex == 0)
    {
        m.addItem (FileOpenMidi,    "Open MIDI...",    true, false);
        m.addItem (FileOpenConfig,  "Open Config...",  true, false);
        m.addSeparator();
        m.addItem (FileSaveConfig,  "Save Config",     false, false);
        juce::PopupMenu saveAs;
        saveAs.addItem (FileSaveAsJson, "JSON (.json)", true, false);
        saveAs.addItem (FileSaveAsToml, "TOML (.toml)", true, false);
        saveAs.addItem (FileSaveAsXml,  "XML (.xml)",   true, false);
        m.addSubMenu ("Save Config As", saveAs);
        m.addSeparator();
        m.addItem (FileQuit, "Quit");
    }
    else if (topLevelMenuIndex == 1)
    {
        m.addItem (EditAddInstrument,    "Add Instrument",    false, false);
        m.addItem (EditRemoveSelected,   "Remove Selected",   false, false);
    }
    return m;
}

void MainWindow::menuItemSelected (int menuItemID, int)
{
    if (menuItemID == FileQuit)
        juce::JUCEApplication::getInstance()->systemRequestedQuit();
    // Other items are placeholders until later tasks wire them up.
}

} // namespace lotro
