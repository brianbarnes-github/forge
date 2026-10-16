#include "UI/Dock/DockPanel.h"

#include "UI/Theme/ThemePaint.h"

namespace lotro::dock
{

DockPanel::DockPanel (std::string id, juce::String titleIn, juce::Colour colour, juce::Component& content)
    : panelId (std::move (id)), title (std::move (titleIn)), titleColour (colour), contentRef (content)
{
    addAndMakeVisible (contentRef);
    addAndMakeVisible (floatButton);
    addAndMakeVisible (menuButton);
    addAndMakeVisible (closeButton);
    // As the transport strip does: a click must not take focus from the content, or Space stops reaching it.
    for (auto* b : { &floatButton, &menuButton, &closeButton })
        b->setWantsKeyboardFocus (false);

    floatButton.onClick = [this] { if (onFloatToggle) onFloatToggle(); };
    closeButton.onClick = [this] { if (onClose) onClose(); };
    menuButton.onClick  = [this] { showMenu(); };

    // A right click over the title or any header button opens the same menu: the buttons would
    // otherwise swallow it. Content below the header keeps its own context menus.
    addMouseListener (this, true);
}

void DockPanel::showMenu()
{
    buildMenu().showMenuAsync ({}, [safe = juce::Component::SafePointer<DockPanel> (this)] (int result)
    {
        if (safe != nullptr)
            safe->menuItemChosen (result);
    });
}

void DockPanel::menuItemChosen (int id)
{
    if (id == 1 && onDockTo) onDockTo (Zone::belowTracks);
    if (id == 2 && onDockTo) onDockTo (Zone::bottom);
    if (id == 3 && onAlwaysOnTopToggle) onAlwaysOnTopToggle();
}

void DockPanel::chooseMenuItemForTesting (const juce::String& text)
{
    auto menu = buildMenu();
    for (juce::PopupMenu::MenuItemIterator it (menu); it.next();)
        if (it.getItem().text == text)
            menuItemChosen (it.getItem().itemID);
}

bool DockPanel::isContextMenuTrigger (juce::Point<int> p, bool isPopupClick) const noexcept
{
    return isPopupClick && p.y >= 0 && p.y < headerHeight;
}

void DockPanel::setTitle (const juce::String& newTitle)
{
    title = newTitle;
    repaint (getLocalBounds().removeFromTop (headerHeight));
}

void DockPanel::setFloating (bool nowFloating)
{
    floating = nowFloating;
    floatButton.setButtonText (floating ? "Dock" : "Float");
}

juce::PopupMenu DockPanel::buildZoneMenu() const
{
    juce::PopupMenu menu;
    menu.addItem (1, "Below track list");
    menu.addItem (2, "Bottom");
    return menu;
}

juce::PopupMenu DockPanel::buildMenu() const
{
    auto menu = buildZoneMenu();
    if (floating)
    {
        menu.addSeparator();
        menu.addItem (3, "Always on top", true, alwaysOnTop);
    }
    return menu;
}

void DockPanel::paint (juce::Graphics& g)
{
    auto header = getLocalBounds().removeFromTop (headerHeight);
    ThemePaint::paintPanelGradient (g, header);
    g.setColour (titleColour);
    g.setFont (juce::FontOptions (11.0f));
    g.drawText (title, header.withTrimmedLeft (6).withTrimmedRight (110), juce::Justification::centredLeft, true);
}

void DockPanel::resized()
{
    auto area = getLocalBounds();
    auto header = area.removeFromTop (headerHeight);
    closeButton.setBounds (header.removeFromRight (22).reduced (1));
    menuButton.setBounds (header.removeFromRight (26).reduced (1));
    floatButton.setBounds (header.removeFromRight (46).reduced (1));
    contentRef.setBounds (area);
}

void DockPanel::mouseUp (const juce::MouseEvent& e)
{
    // Also receives the mouse-ups of its children (addMouseListener above).
    if (isContextMenuTrigger (e.getEventRelativeTo (this).getPosition(), e.mods.isPopupMenu()))
        showMenu();
}

} // namespace lotro::dock
