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
    const juce::String& getTitle() const noexcept { return title; }
    void setFloating (bool floating);   // button text only: "Float" / "Dock"
    bool isFloating() const noexcept { return floating; }
    void setAlwaysOnTop (bool on) noexcept { alwaysOnTop = on; }   // menu tick only

    std::function<void()> onFloatToggle;
    std::function<void()> onClose;
    std::function<void (Zone)> onDockTo;
    std::function<void()> onAlwaysOnTopToggle;   // from the menu, floating panels only

    juce::PopupMenu buildZoneMenu() const;   // "Below track list", "Bottom"
    juce::PopupMenu buildMenu() const;       // the zones, plus "Always on top" while floating
    // Where a right click opens the menu: anywhere on the header, buttons included.
    bool isContextMenuTrigger (juce::Point<int> headerPoint, bool isPopupClick) const noexcept;
    void chooseMenuItemForTesting (const juce::String& text);
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
    bool alwaysOnTop = false;

    void showMenu();
    void menuItemChosen (int id);
    juce::TextButton floatButton { "Float" };
    juce::TextButton menuButton { "..." };
    juce::TextButton closeButton { "x" };
};

} // namespace lotro::dock
