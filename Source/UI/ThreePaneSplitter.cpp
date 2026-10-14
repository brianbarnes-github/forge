#include "ThreePaneSplitter.h"

#include <algorithm>
#include <cmath>

namespace lotro
{

ThreePaneGeometry computeThreePaneGeometry (juce::Rectangle<int> area,
                                            std::array<float, 2> fractions,
                                            std::array<int, 3> minWidths,
                                            int barThickness)
{
    const int w = area.getWidth();

    // Second bar first (it bounds how far the first can go), then the first.
    const int secondLo = minWidths[0] + barThickness + minWidths[1];
    const int secondHi = w - barThickness - minWidths[2];
    const int second = juce::jlimit (0, std::max (0, w - barThickness),
                                     juce::jlimit (secondLo, std::max (secondLo, secondHi),
                                                   (int) std::lround (fractions[1] * w)));

    const int firstHi = second - barThickness - minWidths[1];
    const int first = juce::jlimit (0, std::max (0, second - barThickness),
                                    juce::jlimit (minWidths[0], std::max (minWidths[0], firstHi),
                                                  (int) std::lround (fractions[0] * w)));

    ThreePaneGeometry g;
    const auto span = [&] (int from, int to) { return juce::Rectangle<int> (area.getX() + from, area.getY(), std::max (0, to - from), area.getHeight()); };
    g.panes[0] = span (0, first);
    g.bars[0]  = span (first, first + barThickness);
    g.panes[1] = span (first + barThickness, second);
    g.bars[1]  = span (second, second + barThickness);
    g.panes[2] = span (second + barThickness, w);
    return g;
}

ThreePaneSplitter::ThreePaneSplitter (std::array<int, 3> minWidthsIn) : minWidths (minWidthsIn)
{
    addAndMakeVisible (bar0);
    addAndMakeVisible (bar1);

    // Positions its siblings, not its own children: without this the splitter's
    // full bounds would swallow every click meant for them. The bars (children)
    // still get theirs via the second `true`.
    setInterceptsMouseClicks (false, true);
}

void ThreePaneSplitter::setComponents (juce::Component* firstIn, juce::Component* secondIn, juce::Component* thirdIn)
{
    panes = { firstIn, secondIn, thirdIn };
    resized();
}

void ThreePaneSplitter::setMinWidths (std::array<int, 3> minWidthsIn)
{
    minWidths = minWidthsIn;
    resized();
}

void ThreePaneSplitter::resized()
{
    // getBounds(), not getLocalBounds(): the panes are siblings in our parent's
    // coordinate space (see SplitterComponent::resized).
    const auto g = computeThreePaneGeometry (getBounds(), fractions, minWidths, barThickness);

    for (size_t i = 0; i < 3; ++i)
        if (panes[i] != nullptr)
            panes[i]->setBounds (g.panes[i]);

    // The bars are our children, so they use local coordinates.
    bar0.setBounds (g.bars[0].translated (-getX(), -getY()));
    bar1.setBounds (g.bars[1].translated (-getX(), -getY()));
}

void ThreePaneSplitter::dragDivider (int index, int xInSplitter)
{
    const int w = getWidth();
    if (w <= 0 || (index != 0 && index != 1)) return;

    // Commit the clamped positions first so the bar that is not being dragged
    // stays exactly where it is on screen, even if a narrow strip had clamped it.
    const auto g = computeThreePaneGeometry (getLocalBounds(), fractions, minWidths, barThickness);
    fractions = { (float) g.bars[0].getX() / (float) w, (float) g.bars[1].getX() / (float) w };

    fractions[(size_t) index] = (float) xInSplitter / (float) w;
    resized();
}

void ThreePaneSplitter::Bar::mouseDrag (const juce::MouseEvent& e)
{
    owner.dragDivider (index, e.getEventRelativeTo (&owner).x);
}

} // namespace lotro
