#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

// Phase 5 — generalized from MainWindow::Body's original left/right-only
// private `Splitter` class. Drag-to-resize a fraction of the available space
// along either axis via a thin bar between two sibling components; this
// class does not own or parent those components (they stay children of
// whatever component the splitter itself is a sibling of), it only tracks
// pointers for layout, matching the original's "sits on top, intercepts
// clicks only via its bar" z-order trick — see MainWindow.cpp's usage.
namespace lotro
{

class SplitterComponent : public juce::Component
{
public:
    enum class Orientation { leftRight, topBottom };

    explicit SplitterComponent (Orientation orientationIn);

    // Neither pointer is owned; both must outlive this splitter.
    void setComponents (juce::Component* firstIn, juce::Component* secondIn);

    void resized() override;

    // Fraction of the available space given to `first` (leftRight: width;
    // topBottom: height), clamped to [0.15, 0.85].
    float getFraction() const noexcept { return fraction; }
    void setFraction (float newFraction);

private:
    class Bar : public juce::Component
    {
    public:
        explicit Bar (SplitterComponent& ownerIn) : owner (ownerIn) {}

        void paint (juce::Graphics& g) override { g.fillAll (juce::Colours::grey); }
        void mouseDrag (const juce::MouseEvent& e) override;
        void mouseEnter (const juce::MouseEvent&) override;

    private:
        SplitterComponent& owner;
    };

    Orientation orientation;
    juce::Component* first = nullptr;
    juce::Component* second = nullptr;
    Bar bar { *this };
    float fraction = 0.55f;

    static constexpr int barThickness = 6;
};

} // namespace lotro
