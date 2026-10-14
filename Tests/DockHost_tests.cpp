#include "UI/Dock/DockHost.h"

#include <catch2/catch_approx.hpp>
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

TEST_CASE ("dock host: a layout that shows a panel nobody registered leaves it out of the stacks", "[dock][host][mixer]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    Fixture f;   // registers lotroPreview and midiEditor only, as a host without playback has no mixer
    auto s = defaultLayout();
    show (s, "mixer");
    f.host.apply (s);
    CHECK (Fixture::ids (f.bottomStack) == std::vector<std::string> { "lotroPreview" });
    CHECK (f.host.panel ("mixer") == nullptr);
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
    CHECK (f.host.state().panels.size() == 5);

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

TEST_CASE ("dock host: a floating window forwards unhandled Esc to the host hook", "[dock][host]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    Fixture f;
    int calls = 0;
    f.host.onFloatingKey = [&calls] (const juce::KeyPress& k)
    {
        ++calls;
        return k == juce::KeyPress (juce::KeyPress::escapeKey);
    };
    f.host.setVisible ("midiEditor", true);
    auto* window = dynamic_cast<DockFloatWindow*> (f.host.panel ("midiEditor")->getTopLevelComponent());
    REQUIRE (window != nullptr);

    CHECK (window->keyPressed (juce::KeyPress (juce::KeyPress::escapeKey)));
    CHECK (calls == 1);
    CHECK_FALSE (window->keyPressed (juce::KeyPress ('x')));
    CHECK (calls == 2);
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

TEST_CASE ("dock host: hiding the source region gives its space to lower; showing it restores the split", "[dock][host][source]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    Fixture f;
    CHECK (f.host.isUpperVisible());
    const int upperBefore = f.upper.getHeight(), lowerBefore = f.lower.getHeight();
    const int changesBefore = f.changes;

    f.host.setUpperVisible (false);
    CHECK_FALSE (f.host.isUpperVisible());
    CHECK (Fixture::ids (f.topStack) == std::vector<std::string> { "lower" });
    CHECK_FALSE (f.upper.isVisible());
    CHECK (f.lower.getHeight() == 700);
    CHECK_FALSE (f.host.state().upperVisible);
    CHECK (f.changes > changesBefore);

    f.host.setUpperVisible (true);
    CHECK (Fixture::ids (f.topStack) == std::vector<std::string> { "upper", "lower" });
    CHECK (f.upper.isVisible());
    CHECK (f.upper.getHeight() == upperBefore);
    CHECK (f.lower.getHeight() == lowerBefore);
}

TEST_CASE ("dock host: with the source region hidden and the bottom zone empty, lower still fills the window", "[dock][host][source]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    Fixture f;
    f.host.panel ("lotroPreview")->onFloatToggle();   // empties the bottom zone: lower would collapse
    f.host.setUpperVisible (false);
    CHECK (f.lower.getHeight() == 700);

    f.host.setUpperVisible (true);
    CHECK (f.lower.getHeight() == 138);   // collapsed to its chrome again
}

TEST_CASE ("dock host: apply honours a saved hidden source region and floating panels still work", "[dock][host][source]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    Fixture f;
    auto s = defaultLayout();
    s.upperVisible = false;
    f.host.apply (s);
    CHECK_FALSE (f.host.isUpperVisible());
    CHECK (Fixture::ids (f.topStack) == std::vector<std::string> { "lower" });

    f.host.setVisible ("midiEditor", true);   // floats; the top stack is unchanged
    CHECK (Fixture::ids (f.topStack) == std::vector<std::string> { "lower" });
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

namespace
{
juce::MouseEvent barMouse (juce::Component& c, juce::Point<int> pos, juce::Point<int> down)
{
    const auto now = juce::Time::getCurrentTime();
    return juce::MouseEvent (juce::Desktop::getInstance().getMainMouseSource(), pos.toFloat(),
                             juce::ModifierKeys (juce::ModifierKeys::leftButtonModifier),
                             1.0f, 0.0f, 0.0f, 0.0f, 0.0f, &c, &c, now, down.toFloat(), now, 1, pos != down);
}
}

TEST_CASE ("dock host: closing a floating panel with its x keeps the position it was moved to", "[dock][host]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    Fixture f;
    f.host.setVisible ("midiEditor", true);
    auto* window = dynamic_cast<DockFloatWindow*> (f.host.panel ("midiEditor")->getTopLevelComponent());
    REQUIRE (window != nullptr);
    const auto primary = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay()->userArea;
    window->setBounds (primary.getX() + 70, primary.getY() + 80, 520, 330);

    f.host.panel ("midiEditor")->onClose();

    const auto* saved = find (f.host.state(), "midiEditor");
    REQUIRE (saved->floatBounds.has_value());
    CHECK (saved->floatBounds->width == 520);
    CHECK (saved->floatBounds->height == 330);
}

TEST_CASE ("dock host: real divider drags while lower is collapsed do not shrink the saved weights", "[dock][host]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    Fixture f;
    f.topStack.setVisible (true);
    f.host.setVisible ("midiEditor", true);
    f.host.panel ("midiEditor")->onFloatToggle();          // docked below the tracks
    f.host.panel ("lotroPreview")->onFloatToggle();        // lower collapses
    const auto before = f.host.state();
    const double sumBefore = before.upperWeight + find (before, "midiEditor")->weight;

    for (int i = 0; i < 3; ++i)
    {
        const auto bar = f.topStack.dividerBounds (0).getCentre();
        auto* target = f.topStack.getComponentAt (bar);
        REQUIRE (target != nullptr);
        REQUIRE (target != &f.topStack);
        target->mouseDown (barMouse (*target, bar, bar));
        target->mouseDrag (barMouse (*target, bar.translated (0, 10), bar));
        target->mouseUp (barMouse (*target, bar.translated (0, 10), bar));
        f.host.setLowerHasOtherContent (false);            // a rebuild, as at every restart
    }

    const auto after = f.host.state();
    CHECK (after.upperWeight + find (after, "midiEditor")->weight == Catch::Approx (sumBefore).margin (1e-9));
    CHECK (after.lowerWeight == before.lowerWeight);
}

TEST_CASE ("dock host: always-on-top reaches the floating window and survives a dock and float cycle", "[dock][host]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    Fixture f;
    f.host.setVisible ("midiEditor", true);
    auto* editor = f.host.panel ("midiEditor");
    auto windowOf = [&] { return dynamic_cast<DockFloatWindow*> (editor->getTopLevelComponent()); };
    REQUIRE (windowOf() != nullptr);
    CHECK_FALSE (windowOf()->isAlwaysOnTop());

    int saves = 0;
    f.host.onLayoutChanged = [&] (const LayoutState&) { ++saves; };
    editor->onAlwaysOnTopToggle();
    CHECK (windowOf()->isAlwaysOnTop());
    CHECK (find (f.host.state(), "midiEditor")->alwaysOnTop);
    CHECK (saves == 1);

    editor->onFloatToggle();   // dock: the setting is kept
    editor->onFloatToggle();   // float again: a new window
    REQUIRE (windowOf() != nullptr);
    CHECK (windowOf()->isAlwaysOnTop());

    editor->onAlwaysOnTopToggle();
    CHECK_FALSE (windowOf()->isAlwaysOnTop());
}

TEST_CASE ("dock host: apply gives a restored floating window its saved always-on-top", "[dock][host]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    Fixture f;
    auto layout = f.host.state();
    show (layout, "midiEditor");
    setAlwaysOnTop (layout, "midiEditor", true);
    f.host.apply (layout);
    auto* window = dynamic_cast<DockFloatWindow*> (f.host.panel ("midiEditor")->getTopLevelComponent());
    REQUIRE (window != nullptr);
    CHECK (window->isAlwaysOnTop());
}
