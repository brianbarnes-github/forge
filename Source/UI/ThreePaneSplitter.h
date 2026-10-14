#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>

// Three sibling panes side by side, divided by two draggable bars. Like
// SplitterComponent it does not own or parent the panes: they stay children
// of whatever component the splitter is a sibling of, and the splitter only
// sets their bounds. Dragging a bar moves that bar alone; the pane on its far
// side is untouched.
namespace lotro
{

struct ThreePaneGeometry
{
    std::array<juce::Rectangle<int>, 3> panes;
    std::array<juce::Rectangle<int>, 2> bars;
};

// Pure layout. `fractions` are the two bars' left edges as fractions of the
// area's width. They are clamped so every pane keeps at least its minimum
// width; if the area is too narrow for the minimums the layout degrades
// without producing negative widths.
ThreePaneGeometry computeThreePaneGeometry (juce::Rectangle<int> area,
                                            std::array<float, 2> fractions,
                                            std::array<int, 3> minWidths,
                                            int barThickness);

class ThreePaneSplitter : public juce::Component
{
public:
    static constexpr int barThickness = 3;

    explicit ThreePaneSplitter (std::array<int, 3> minWidthsIn);

    // None of the pointers is owned; all must outlive this splitter.
    void setComponents (juce::Component* firstIn, juce::Component* secondIn, juce::Component* thirdIn);
    void setMinWidths (std::array<int, 3> minWidthsIn);

    void resized() override;

    // The bars' left edges as fractions of the width; start as equal thirds.
    std::array<float, 2> getFractions() const noexcept { return fractions; }

    // Moves bar `index` (0 or 1) so its left edge is at `xInSplitter`, clamped.
    void dragDivider (int index, int xInSplitter);

private:
    class Bar : public juce::Component
    {
    public:
        Bar (ThreePaneSplitter& ownerIn, int indexIn) : owner (ownerIn), index (indexIn)
        {
            setMouseCursor (juce::MouseCursor::LeftRightResizeCursor);
        }

        void paint (juce::Graphics& g) override { g.fillAll (juce::Colours::grey); }
        void mouseDrag (const juce::MouseEvent& e) override;

    private:
        ThreePaneSplitter& owner;
        int index;
    };

    std::array<int, 3> minWidths;
    std::array<juce::Component*, 3> panes { nullptr, nullptr, nullptr };
    Bar bar0 { *this, 0 }, bar1 { *this, 1 };
    std::array<float, 2> fractions { 1.0f / 3.0f, 2.0f / 3.0f };
};

} // namespace lotro
