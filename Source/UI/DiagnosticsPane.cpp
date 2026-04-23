#include "DiagnosticsPane.h"

namespace lotro
{

namespace
{
    class HSplitterBar : public juce::Component
    {
    public:
        HSplitterBar (std::function<void(float)> onMove) : moved (std::move (onMove)) {}

        void paint (juce::Graphics& g) override { g.fillAll (juce::Colours::grey); }
        void mouseEnter (const juce::MouseEvent&) override
        {
            setMouseCursor (juce::MouseCursor::UpDownResizeCursor);
        }
        void mouseDrag (const juce::MouseEvent& e) override
        {
            if (auto* parent = getParentComponent())
            {
                const float h = (float) parent->getHeight();
                if (h <= 0.0f) return;
                const float fraction = juce::jlimit (0.15f, 0.85f,
                    e.getEventRelativeTo (parent).y / h);
                if (moved) moved (fraction);
            }
        }

    private:
        std::function<void(float)> moved;
    };
}

class DiagnosticsPane::Body : public juce::Component
{
public:
    Body()
        : diagList (std::make_unique<DiagnosticListView>()),
          abcView  (std::make_unique<AbcPreviewView>()),
          bar      (std::make_unique<HSplitterBar> ([this] (float f) { topFraction = f; resized(); }))
    {
        addAndMakeVisible (*diagList);
        addAndMakeVisible (*abcView);
        addAndMakeVisible (*bar);
    }

    void resized() override
    {
        const auto area = getLocalBounds();
        const int barH = 6;
        const int topH = (int) std::lround (area.getHeight() * topFraction);
        diagList->setBounds (area.withHeight (topH));
        bar->setBounds (area.withY (topH).withHeight (barH));
        abcView->setBounds (area.withTrimmedTop (topH + barH));
    }

    DiagnosticListView& list()    { return *diagList; }
    AbcPreviewView&     preview() { return *abcView;  }

private:
    std::unique_ptr<DiagnosticListView> diagList;
    std::unique_ptr<AbcPreviewView>     abcView;
    std::unique_ptr<HSplitterBar>       bar;
    float topFraction = 0.5f;
};

DiagnosticsPane::DiagnosticsPane()
    : body (std::make_unique<Body>())
{
    addAndMakeVisible (*body);
}

DiagnosticsPane::~DiagnosticsPane() = default;

void DiagnosticsPane::show (Diagnostics diagnostics, std::string abc)
{
    body->list().setDiagnostics (std::move (diagnostics));
    body->preview().setAbc (abc);
}

void DiagnosticsPane::resized()
{
    body->setBounds (getLocalBounds().reduced (4));
}

} // namespace lotro
