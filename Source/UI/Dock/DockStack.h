#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <string>
#include <vector>

namespace lotro::dock
{

// A vertical stack of components sharing the height by weight, with a draggable
// divider bar between neighbours. Children are not owned.
class DockStack : public juce::Component
{
public:
    struct Item { std::string id; juce::Component* component; double weight; int minSize; };

    static constexpr int dividerSize = 6;

    DockStack();
    ~DockStack() override;

    // Re-parents the components into this stack; components dropped from the
    // previous list are hidden and detached.
    void setItems (std::vector<Item>);
    const std::vector<Item>& items() const noexcept { return currentItems; }

    // After a divider drag ends.
    std::function<void (const std::vector<Item>&)> onWeightsChanged;

    int dividerCount() const noexcept { return currentItems.empty() ? 0 : static_cast<int> (currentItems.size()) - 1; }
    juce::Rectangle<int> dividerBounds (int index) const;

    void resized() override;

private:
    class Bar;

    void beginDrag();
    void dragBar (int index, int deltaY);
    void endDrag (bool moved);

    std::vector<Item> currentItems;
    std::vector<Item> dragStart;
    juce::OwnedArray<Bar> bars;
};

} // namespace lotro::dock
