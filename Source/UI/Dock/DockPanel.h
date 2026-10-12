#pragma once

#include "UI/Dock/DockLayoutState.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <string>

namespace lotro::dock
{

// A titled header (Float/Dock, zone menu, Close) over one content component.
// The content is not owned; it stays a child of this panel whether the panel is
// docked in a DockStack or floating in a DockFloatWindow.
class DockPanel : public juce::Component
{
public:
    static constexpr int headerHeight = 20;

    DockPanel (std::string id, juce::String title, juce::Colour titleColour, juce::Component& content);

    const std::string& id() const noexcept { return panelId; }
    void setTitle (const juce::String&);
    void setFloating (bool floating);   // button text only: "Float" / "Dock"
    bool isFloating() const noexcept { return floating; }

    std::function<void()> onFloatToggle;
    std::function<void()> onClose;
    std::function<void (Zone)> onDockTo;

    juce::PopupMenu buildZoneMenu() const;   // "Below track list", "Bottom"
    juce::Component& content() noexcept { return contentRef; }

    juce::String floatButtonText() const { return floatButton.getButtonText(); }
    void clickFloatButtonForTesting() { floatButton.onClick(); }
    void clickCloseButtonForTesting() { closeButton.onClick(); }

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    std::string panelId;
    juce::String title;
    juce::Colour titleColour;
    juce::Component& contentRef;
    bool floating = false;
    juce::TextButton floatButton { "Float" };
    juce::TextButton menuButton { "..." };
    juce::TextButton closeButton { "x" };
};

} // namespace lotro::dock
