#include "UI/Dock/DockFloatWindow.h"

#include "UI/SongsmithColours.h"
#include "UI/WindowPlacement.h"

namespace lotro::dock
{
namespace
{
constexpr int defaultWidth = 900;
constexpr int defaultHeight = 500;
} // namespace

DockFloatWindow::DockFloatWindow (DockPanel& panel, juce::String title, std::optional<FloatBounds> saved,
                                  juce::Component* centreAround)
    : juce::DocumentWindow (title, juce::Colour (SongsmithColours::background), juce::DocumentWindow::allButtons)
{
    setUsingNativeTitleBar (true);
    setResizable (true, false);
    setResizeLimits (200, 120, 10000, 10000);
    setContentNonOwned (&panel, false);

    const bool hasSaved = saved && saved->width > 0 && saved->height > 0;
    if (! hasSaved && centreAround != nullptr)
    {
        centreAroundComponent (centreAround, defaultWidth, defaultHeight);
    }
    else
    {
        juce::Array<juce::Rectangle<int>> areas;
        for (const auto& d : juce::Desktop::getInstance().getDisplays().displays)
            areas.add (d.userArea);
        const auto* primary = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay();
        setBounds (resolveBounds (saved, areas, primary != nullptr ? primary->userArea
                                                                     : juce::Rectangle<int> (0, 0, 1280, 720)));
    }
    setVisible (true);
}

DockFloatWindow::~DockFloatWindow()
{
    onBoundsChanged = nullptr;   // detaching the content resizes the window; that is not a user change
    onClosed = nullptr;
    setContentNonOwned (nullptr, false);
}

FloatBounds DockFloatWindow::currentBounds() const
{
    const auto b = getBounds();
    return { b.getX(), b.getY(), b.getWidth(), b.getHeight() };
}

void DockFloatWindow::moved()
{
    juce::DocumentWindow::moved();
    if (onBoundsChanged)
        onBoundsChanged();
}

void DockFloatWindow::resized()
{
    juce::DocumentWindow::resized();
    if (onBoundsChanged)
        onBoundsChanged();
}

void DockFloatWindow::closeButtonPressed()
{
    // Copy first: the callback usually destroys this window, and with it onClosed.
    const auto callback = onClosed;
    if (callback)
        callback();
}

juce::Rectangle<int> DockFloatWindow::resolveBounds (std::optional<FloatBounds> saved,
                                                     const juce::Array<juce::Rectangle<int>>& userAreas,
                                                     juce::Rectangle<int> primary)
{
    if (saved && saved->width > 0 && saved->height > 0)
        return WindowPlacement::resolve ({ saved->x, saved->y, saved->width, saved->height }, userAreas, primary);
    return juce::Rectangle<int> (defaultWidth, defaultHeight).withCentre (primary.getCentre());
}

} // namespace lotro::dock
