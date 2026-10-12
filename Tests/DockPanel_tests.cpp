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
