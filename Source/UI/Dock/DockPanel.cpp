#include "UI/Dock/DockPanel.h"

#include "UI/SongsmithColours.h"

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
    menuButton.onClick  = [this]
    {
        buildZoneMenu().showMenuAsync ({}, [safe = juce::Component::SafePointer<DockPanel> (this)] (int result)
        {
            if (safe == nullptr || ! safe->onDockTo)
                return;
            if (result == 1) safe->onDockTo (Zone::belowTracks);
            if (result == 2) safe->onDockTo (Zone::bottom);
        });
    };
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

void DockPanel::paint (juce::Graphics& g)
{
    auto header = getLocalBounds().removeFromTop (headerHeight);
    g.setColour (juce::Colour (SongsmithColours::panelHeader));
    g.fillRect (header);
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
    if (e.mods.isPopupMenu() && e.y < headerHeight)
        menuButton.triggerClick();
}

} // namespace lotro::dock
