#include "UI/Dock/DockStack.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace lotro::dock;

namespace
{
juce::MouseEvent makeMouse (juce::Component& c, juce::Point<int> pos, juce::Point<int> mouseDownPos)
{
    const auto now = juce::Time::getCurrentTime();
    return juce::MouseEvent (juce::Desktop::getInstance().getMainMouseSource(), pos.toFloat(),
                             juce::ModifierKeys (juce::ModifierKeys::leftButtonModifier),
                             1.0f, 0.0f, 0.0f, 0.0f, 0.0f, &c, &c, now, mouseDownPos.toFloat(), now, 1,
                             pos != mouseDownPos);
}
} // namespace

TEST_CASE ("dock stack: weights lay children out top to bottom with a divider between", "[dock][stack-ui]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    juce::Component a, b;
    DockStack stack;
    stack.setItems ({ { "a", &a, 1.0, 10 }, { "b", &b, 1.0, 10 } });
    stack.setSize (200, 106);

    CHECK (a.getBounds() == juce::Rectangle<int> (0, 0, 200, 50));
    CHECK (b.getBounds() == juce::Rectangle<int> (0, 56, 200, 50));
    CHECK (stack.dividerCount() == 1);
    CHECK (stack.dividerBounds (0) == juce::Rectangle<int> (0, 50, 200, 6));
}

TEST_CASE ("dock stack: removing an item hides it and the survivor fills the height", "[dock][stack-ui]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    juce::Component a, b;
    DockStack stack;
    stack.setItems ({ { "a", &a, 1.0, 10 }, { "b", &b, 1.0, 10 } });
    stack.setSize (200, 106);

    stack.setItems ({ { "a", &a, 1.0, 10 } });
    CHECK_FALSE (b.isVisible());
    CHECK (a.getBounds() == juce::Rectangle<int> (0, 0, 200, 106));
    CHECK (stack.dividerCount() == 0);
}

TEST_CASE ("dock stack: dragging a divider reports new weights once, on release", "[dock][stack-ui]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    juce::Component a, b;
    DockStack stack;
    stack.setItems ({ { "a", &a, 1.0, 40 }, { "b", &b, 1.0, 40 } });
    stack.setSize (200, 206);
    stack.setVisible (true);

    int calls = 0;
    std::vector<DockStack::Item> reported;
    stack.onWeightsChanged = [&] (const std::vector<DockStack::Item>& items) { ++calls; reported = items; };

    const auto bar = stack.dividerBounds (0).getCentre();
    auto* target = stack.getComponentAt (bar);
    REQUIRE (target != nullptr);
    REQUIRE (target != &stack);

    const auto down = bar;
    const auto to = bar.translated (0, 30);
    target->mouseDown (makeMouse (*target, down, down));
    target->mouseDrag (makeMouse (*target, to, down));
    CHECK (calls == 0);
    CHECK (a.getHeight() == 130);
    CHECK (b.getHeight() == 70);
    target->mouseUp (makeMouse (*target, to, down));

    CHECK (calls == 1);
    REQUIRE (reported.size() == 2);
    CHECK (reported[0].weight == Catch::Approx (1.3));
    CHECK (reported[1].weight == Catch::Approx (0.7));
}
