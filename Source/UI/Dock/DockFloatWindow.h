#pragma once

#include "UI/Dock/DockLayoutState.h"
#include "UI/Dock/DockPanel.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <optional>

namespace lotro::dock
{

// Hosts a DockPanel while it floats. The panel is not owned: the destructor
// detaches it so the panel (and its content) can move back into a DockStack.
class DockFloatWindow : public juce::DocumentWindow
{
public:
    DockFloatWindow (DockPanel& panel, juce::String title, std::optional<FloatBounds> saved,
                     juce::Component* centreAround);
    ~DockFloatWindow() override;

    FloatBounds currentBounds() const;
    std::function<void()> onClosed;
    std::function<void()> onBoundsChanged;   // the user moved or resized the window
    void closeButtonPressed() override;
    void moved() override;
    void resized() override;

    static juce::Rectangle<int> resolveBounds (std::optional<FloatBounds> saved,
                                               const juce::Array<juce::Rectangle<int>>& userAreas,
                                               juce::Rectangle<int> primary);
};

} // namespace lotro::dock
