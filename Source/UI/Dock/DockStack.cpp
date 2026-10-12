#include "UI/Dock/DockStack.h"

#include "UI/Dock/DockStackLayout.h"

#include <algorithm>

namespace lotro::dock
{
namespace
{
std::vector<StackItem> toStackItems (const std::vector<DockStack::Item>& items)
{
    std::vector<StackItem> out;
    out.reserve (items.size());
    for (const auto& it : items)
        out.push_back ({ it.weight, it.minSize });
    return out;
}
} // namespace

class DockStack::Bar : public juce::Component
{
public:
    Bar (DockStack& ownerIn, int indexIn) : owner (ownerIn), index (indexIn)
    {
        setMouseCursor (juce::MouseCursor::UpDownResizeCursor);
    }

    void paint (juce::Graphics& g) override { g.fillAll (juce::Colours::grey); }
    void mouseDown (const juce::MouseEvent&) override { owner.beginDrag(); }
    void mouseDrag (const juce::MouseEvent& e) override { owner.dragBar (index, e.getDistanceFromDragStartY()); }
    void mouseUp (const juce::MouseEvent& e) override { owner.endDrag (e.mouseWasDraggedSinceMouseDown()); }

private:
    DockStack& owner;
    int index;
};

DockStack::DockStack() = default;
DockStack::~DockStack() = default;

void DockStack::setItems (std::vector<Item> newItems)
{
    for (const auto& old : currentItems)
    {
        const bool kept = std::any_of (newItems.begin(), newItems.end(),
                                       [&] (const Item& n) { return n.component == old.component; });
        if (! kept && old.component != nullptr)
        {
            old.component->setVisible (false);
            if (old.component->getParentComponent() == this)
                removeChildComponent (old.component);
        }
    }

    currentItems = std::move (newItems);
    for (const auto& it : currentItems)
        if (it.component != nullptr)
            addAndMakeVisible (it.component);

    bars.clear();
    for (int i = 0; i < dividerCount(); ++i)
        addAndMakeVisible (bars.add (new Bar (*this, i)));

    resized();
}

juce::Rectangle<int> DockStack::dividerBounds (int index) const
{
    if (index < 0 || index >= dividerCount())
        return {};
    return bars[index]->getBounds();
}

void DockStack::resized()
{
    const auto sizes = computeStack (toStackItems (currentItems), getHeight(), dividerSize);
    int y = 0;
    for (size_t i = 0; i < currentItems.size(); ++i)
    {
        if (currentItems[i].component != nullptr)
            currentItems[i].component->setBounds (0, y, getWidth(), sizes[i]);
        y += sizes[i];
        if (i + 1 < currentItems.size())
        {
            bars[static_cast<int> (i)]->setBounds (0, y, getWidth(), dividerSize);
            y += dividerSize;
        }
    }
}

void DockStack::beginDrag()
{
    dragStart = currentItems;
}

void DockStack::dragBar (int index, int deltaY)
{
    if (dragStart.size() != currentItems.size())
        return;
    const auto weights = dragDivider (toStackItems (dragStart), getHeight(), dividerSize, index, deltaY);
    for (size_t i = 0; i < currentItems.size(); ++i)
        currentItems[i].weight = weights[i];
    resized();
}

void DockStack::endDrag (bool moved)
{
    dragStart.clear();
    if (moved && onWeightsChanged)
        onWeightsChanged (currentItems);
}

} // namespace lotro::dock
