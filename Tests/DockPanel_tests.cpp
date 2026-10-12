#include "UI/Dock/DockFloatWindow.h"
#include "UI/Dock/DockPanel.h"

#include <catch2/catch_test_macros.hpp>

using namespace lotro::dock;

TEST_CASE ("dock panel: header on top, content below", "[dock][panel]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    juce::Component content;
    DockPanel panel ("p", "Panel", juce::Colours::white, content);
    panel.setSize (300, 200);
    CHECK (content.getParentComponent() == &panel);
    CHECK (content.getBounds() == juce::Rectangle<int> (0, DockPanel::headerHeight, 300, 200 - DockPanel::headerHeight));
}

TEST_CASE ("dock panel: float button toggles its text and calls back", "[dock][panel]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    juce::Component content;
    DockPanel panel ("p", "Panel", juce::Colours::white, content);
    panel.setSize (300, 200);

    int toggles = 0;
    panel.onFloatToggle = [&] { ++toggles; };
    CHECK_FALSE (panel.isFloating());
    CHECK (panel.floatButtonText() == "Float");
    panel.setFloating (true);
    CHECK (panel.isFloating());
    CHECK (panel.floatButtonText() == "Dock");

    panel.clickFloatButtonForTesting();
    CHECK (toggles == 1);

    int closes = 0;
    panel.onClose = [&] { ++closes; };
    panel.clickCloseButtonForTesting();
    CHECK (closes == 1);
}

TEST_CASE ("dock panel: the zone menu has the two zones", "[dock][panel]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    juce::Component content;
    DockPanel panel ("p", "Panel", juce::Colours::white, content);
    CHECK (panel.buildZoneMenu().getNumItems() == 2);
}

TEST_CASE ("dock panel: one content instance survives moving into a window and back", "[dock][panel]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    juce::Component content;
    DockPanel panel ("p", "Panel", juce::Colours::white, content);
    panel.setSize (300, 200);
    {
        DockFloatWindow window (panel, "Panel", std::nullopt, nullptr);
        CHECK (content.getParentComponent() == &panel);
        CHECK (panel.getParentComponent() != nullptr);
    }
    CHECK (content.getParentComponent() == &panel);
    CHECK (panel.getParentComponent() == nullptr);
}

TEST_CASE ("dock float window: resolveBounds keeps saved bounds reachable", "[dock][panel]")
{
    juce::Array<juce::Rectangle<int>> areas;
    areas.add ({ 0, 0, 1920, 1080 });
    const juce::Rectangle<int> primary { 0, 0, 1920, 1080 };

    const auto off = DockFloatWindow::resolveBounds (FloatBounds { -5000, -5000, 800, 600 }, areas, primary);
    CHECK (off.intersects (areas[0]));

    const auto none = DockFloatWindow::resolveBounds (std::nullopt, areas, primary);
    CHECK (none.getCentre() == primary.getCentre());

    const auto zero = DockFloatWindow::resolveBounds (FloatBounds { 0, 0, 0, 0 }, areas, primary);
    CHECK (zero == none);

    const auto good = DockFloatWindow::resolveBounds (FloatBounds { 100, 100, 640, 300 }, areas, primary);
    CHECK (good == juce::Rectangle<int> (100, 100, 640, 300));
}

TEST_CASE ("dock panel: the Always on top item only appears while floating and reflects its state", "[dock][panel]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    juce::Component content;
    DockPanel panel ("p", "Panel", juce::Colours::white, content);

    CHECK (panel.buildMenu().getNumItems() == 2);   // docked: the two zones only
    panel.setFloating (true);
    auto menu = panel.buildMenu();
    CHECK (menu.getNumItems() > 2);
    bool found = false, ticked = true;
    for (juce::PopupMenu::MenuItemIterator it (menu); it.next();)
        if (it.getItem().text == "Always on top") { found = true; ticked = it.getItem().isTicked; }
    CHECK (found);
    CHECK_FALSE (ticked);

    panel.setAlwaysOnTop (true);
    for (juce::PopupMenu::MenuItemIterator it (panel.buildMenu()); it.next();)
        if (it.getItem().text == "Always on top")
            CHECK (it.getItem().isTicked);
}

TEST_CASE ("dock panel: choosing Always on top asks to flip it; choosing a zone docks there", "[dock][panel]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    juce::Component content;
    DockPanel panel ("p", "Panel", juce::Colours::white, content);
    panel.setFloating (true);

    bool flipped = false;
    std::optional<Zone> zone;
    panel.onAlwaysOnTopToggle = [&] { flipped = true; };
    panel.onDockTo = [&] (Zone z) { zone = z; };

    panel.chooseMenuItemForTesting ("Always on top");
    CHECK (flipped);
    CHECK_FALSE (zone.has_value());
    panel.chooseMenuItemForTesting ("Bottom");
    REQUIRE (zone.has_value());
    CHECK (*zone == Zone::bottom);
}

TEST_CASE ("dock panel: a right click anywhere on the header, buttons included, is a context-menu request", "[dock][panel]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    juce::Component content;
    DockPanel panel ("p", "Panel", juce::Colours::white, content);
    panel.setSize (300, 200);

    CHECK (panel.isContextMenuTrigger (juce::Point<int> (10, 5), true));      // title
    CHECK (panel.isContextMenuTrigger (juce::Point<int> (290, 5), true));     // over the close button
    CHECK_FALSE (panel.isContextMenuTrigger (juce::Point<int> (10, 5), false));   // a left click
    CHECK_FALSE (panel.isContextMenuTrigger (juce::Point<int> (10, DockPanel::headerHeight + 5), true));   // content
}
