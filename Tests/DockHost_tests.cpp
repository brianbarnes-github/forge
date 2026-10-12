#include "UI/Dock/DockHost.h"

#include <catch2/catch_test_macros.hpp>

using namespace lotro::dock;

namespace
{
struct Fixture
{
    juce::Component upper, lower, previewContent, editorContent;
    DockStack topStack, bottomStack;
    DockHost host { topStack, bottomStack, upper, lower, nullptr };
    int changes = 0;

    Fixture()
    {
        host.addPanel (panelDefs()[0], juce::Colours::orange, previewContent);
        host.addPanel (panelDefs()[1], juce::Colours::white, editorContent);
        host.setLowerChromeHeight (138);
        host.onLayoutChanged = [this] (const LayoutState&) { ++changes; };
        topStack.setSize (1000, 700);
        host.apply (defaultLayout());
    }

    static std::vector<std::string> ids (const DockStack& s)
    {
        std::vector<std::string> out;
        for (const auto& it : s.items())
            out.push_back (it.id);
        return out;
    }
};
}

TEST_CASE ("dock host: the default layout docks the preview at the bottom and hides the editor", "[dock][host]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    Fixture f;
    CHECK (Fixture::ids (f.topStack) == std::vector<std::string> { "upper", "lower" });
    CHECK (Fixture::ids (f.bottomStack) == std::vector<std::string> { "lotroPreview" });
    CHECK (f.host.isVisible ("lotroPreview"));
    CHECK_FALSE (f.host.isVisible ("midiEditor"));
    CHECK (f.previewContent.getParentComponent() == f.host.panel ("lotroPreview"));
}

TEST_CASE ("dock host: showing the editor opens a floating window; docking it keeps the same content", "[dock][host]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    Fixture f;
    f.host.setVisible ("midiEditor", true);
    auto* editor = f.host.panel ("midiEditor");
    REQUIRE (editor != nullptr);
    CHECK (dynamic_cast<DockFloatWindow*> (editor->getTopLevelComponent()) != nullptr);
    CHECK (Fixture::ids (f.topStack) == std::vector<std::string> { "upper", "lower" });
    CHECK (f.host.state().panels.size() == 2);

    editor->onFloatToggle();   // dock it
    CHECK (Fixture::ids (f.topStack) == std::vector<std::string> { "upper", "midiEditor", "lower" });
    CHECK (editor->getParentComponent() == &f.topStack);
    CHECK (f.editorContent.getParentComponent() == editor);
    CHECK (find (f.host.state(), "midiEditor")->mode == Mode::docked);

    editor->onFloatToggle();   // and float it again
    CHECK (Fixture::ids (f.topStack) == std::vector<std::string> { "upper", "lower" });
    CHECK (dynamic_cast<DockFloatWindow*> (editor->getTopLevelComponent()) != nullptr);
    CHECK (f.editorContent.getParentComponent() == editor);
}

TEST_CASE ("dock host: hiding then showing keeps the mode, and changes are reported", "[dock][host]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    Fixture f;
    f.host.setVisible ("midiEditor", true);
    f.host.panel ("midiEditor")->onFloatToggle();   // docked
    const int before = f.changes;

    f.host.setVisible ("midiEditor", false);
    CHECK_FALSE (f.host.isVisible ("midiEditor"));
    CHECK (Fixture::ids (f.topStack) == std::vector<std::string> { "upper", "lower" });
    f.host.setVisible ("midiEditor", true);
    CHECK (Fixture::ids (f.topStack) == std::vector<std::string> { "upper", "midiEditor", "lower" });
    CHECK (f.changes == before + 2);

    const auto s = f.host.state();
    CHECK (find (s, "midiEditor")->visible);
    CHECK (find (s, "midiEditor")->mode == Mode::docked);
}

TEST_CASE ("dock host: closing a panel hides it; Dock-to moves it between zones", "[dock][host]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    Fixture f;
    f.host.setVisible ("midiEditor", true);
    f.host.panel ("midiEditor")->onDockTo (Zone::bottom);
    CHECK (Fixture::ids (f.bottomStack) == std::vector<std::string> { "lotroPreview", "midiEditor" });

    f.host.panel ("midiEditor")->onClose();
    CHECK_FALSE (f.host.isVisible ("midiEditor"));
    CHECK (Fixture::ids (f.bottomStack) == std::vector<std::string> { "lotroPreview" });
}

TEST_CASE ("dock host: floating the preview empties the bottom zone and collapses lower to its chrome", "[dock][host]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    Fixture f;
    const int lowerBefore = f.lower.getHeight();
    CHECK (lowerBefore > 138);

    f.host.panel ("lotroPreview")->onFloatToggle();
    CHECK (f.bottomStack.items().empty());
    CHECK (f.lower.getHeight() == 138);
    CHECK (f.upper.getHeight() + 6 + f.lower.getHeight() == 700);

    f.host.setLowerHasOtherContent (true);   // e.g. the diagnostics list was shown
    CHECK (f.lower.getHeight() > 138);

    f.host.setLowerHasOtherContent (false);
    f.host.panel ("lotroPreview")->onFloatToggle();   // dock it back
    CHECK (f.lower.getHeight() == lowerBefore);
}

TEST_CASE ("dock host: a divider drag is written back to the state and reported", "[dock][host]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    Fixture f;
    auto items = f.topStack.items();
    items[0].weight = 0.3;
    items[1].weight = 0.7;
    f.topStack.setItems (items);
    const int before = f.changes;
    f.topStack.onWeightsChanged (f.topStack.items());
    CHECK (f.changes == before + 1);
    CHECK (f.host.state().upperWeight == 0.3);
    CHECK (f.host.state().lowerWeight == 0.7);
}

TEST_CASE ("dock host: dragging while the lower region is collapsed keeps the upper:panel ratio and the saved lower weight", "[dock][host]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    Fixture f;
    f.host.setVisible ("midiEditor", true);
    f.host.panel ("midiEditor")->onFloatToggle();          // docked below the tracks
    f.host.panel ("lotroPreview")->onFloatToggle();        // bottom zone empty: lower collapses
    const double lowerBefore = f.host.state().lowerWeight;

    auto items = f.topStack.items();
    REQUIRE (items.size() == 3);
    items[0].weight = 3.0;      // upper
    items[1].weight = 1.0;      // midiEditor
    f.topStack.setItems (items);
    f.topStack.onWeightsChanged (f.topStack.items());

    const auto s = f.host.state();
    CHECK (s.lowerWeight == lowerBefore);
    CHECK (s.upperWeight / find (s, "midiEditor")->weight == 3.0);

    f.host.panel ("lotroPreview")->onFloatToggle();        // dock the preview back: lower un-collapses
    const auto after = f.host.state();
    CHECK (after.lowerWeight == lowerBefore);
    CHECK (after.upperWeight / find (after, "midiEditor")->weight == 3.0);
}

TEST_CASE ("dock host: applying a layout keeps the saved float bounds even when a window is open", "[dock][host]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    Fixture f;
    f.host.setVisible ("midiEditor", true);   // an open floating window at its default place

    auto layout = f.host.state();
    const auto primary = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay()->userArea;
    find (layout, "midiEditor")->floatBounds = FloatBounds { primary.getX() + 33, primary.getY() + 44, 500, 300 };
    f.host.apply (layout);

    const auto* applied = find (f.host.state(), "midiEditor");
    REQUIRE (applied->floatBounds.has_value());
    CHECK (applied->floatBounds->x == primary.getX() + 33);
    CHECK (applied->floatBounds->width == 500);
}
