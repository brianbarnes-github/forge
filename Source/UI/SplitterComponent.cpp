#include "SplitterComponent.h"

#include <cmath>

namespace lotro
{

SplitterComponent::SplitterComponent (Orientation orientationIn) : orientation (orientationIn)
{
    addAndMakeVisible (bar);

    // Positions its siblings, not its own children — without this, the
    // splitter's full bounds would swallow every mouse click before
    // first/second ever see it. The bar (a child) still gets clicks via the
    // second `true`.
    setInterceptsMouseClicks (false, true);
}

void SplitterComponent::setComponents (juce::Component* firstIn, juce::Component* secondIn)
{
    first = firstIn;
    second = secondIn;
}

void SplitterComponent::setFraction (float newFraction)
{
    fraction = juce::jlimit (0.15f, 0.85f, newFraction);
    resized();
}

void SplitterComponent::resized()
{
    const auto area = getLocalBounds();

    if (orientation == Orientation::leftRight)
    {
        const int firstExtent = (int) std::lround (area.getWidth() * fraction);
        if (first)  first->setBounds (area.withWidth (firstExtent));
        bar.setBounds (area.withX (firstExtent).withWidth (barThickness));
        if (second) second->setBounds (area.withTrimmedLeft (firstExtent + barThickness));
    }
    else
    {
        const int firstExtent = (int) std::lround (area.getHeight() * fraction);
        if (first)  first->setBounds (area.withHeight (firstExtent));
        bar.setBounds (area.withY (firstExtent).withHeight (barThickness));
        if (second) second->setBounds (area.withTrimmedTop (firstExtent + barThickness));
    }
}

void SplitterComponent::Bar::mouseDrag (const juce::MouseEvent& e)
{
    const auto relative = e.getEventRelativeTo (&owner);

    if (owner.orientation == Orientation::leftRight)
    {
        const float w = (float) owner.getWidth();
        if (w <= 0.0f) return;
        owner.setFraction ((float) relative.x / w);
    }
    else
    {
        const float h = (float) owner.getHeight();
        if (h <= 0.0f) return;
        owner.setFraction ((float) relative.y / h);
    }
}

void SplitterComponent::Bar::mouseEnter (const juce::MouseEvent&)
{
    setMouseCursor (owner.orientation == Orientation::leftRight
                        ? juce::MouseCursor::LeftRightResizeCursor
                        : juce::MouseCursor::UpDownResizeCursor);
}

} // namespace lotro
