// Verifies SplitterComponent positions its siblings in their shared
// parent's coordinate space, not the splitter's own zero-origin local
// space. SplitterComponent doesn't parent `first`/`second` -- they stay
// siblings of it under whatever component the splitter itself lives in
// (see the class comment in SplitterComponent.h) -- so when the splitter
// itself is offset within that shared parent (as the Songsmith UI's inner
// splitter is, sitting below the 110px part strip), its resized() must
// still hand siblings bounds relative to the shared parent, not to
// itself.

#include "UI/SplitterComponent.h"

#include <catch2/catch_test_macros.hpp>

using namespace lotro;

namespace
{
    // Mimics LowerRegion: a parent that reserves a fixed-height band above
    // the splitter (standing in for PartStripComponent) before handing the
    // splitter itself an offset region to lay out its siblings within.
    struct Harness : public juce::Component
    {
        static constexpr int reservedTop = 110;

        juce::Component first, second;
        SplitterComponent splitter { SplitterComponent::Orientation::topBottom };

        Harness()
        {
            addAndMakeVisible (first);
            addAndMakeVisible (second);
            addAndMakeVisible (splitter);
            splitter.setComponents (&first, &second);
        }

        void resized() override
        {
            auto area = getLocalBounds();
            area.removeFromTop (reservedTop);
            splitter.setBounds (area);
        }
    };
}

TEST_CASE ("SplitterComponent: siblings land in the shared parent's space when the splitter itself is offset",
           "[splitter]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    Harness harness;
    harness.setSize (300, 400);

    // The splitter occupies (0, 110, 300, 290) within harness. Its default
    // 0.55 fraction over that 290px height is round(290*0.55) = 160px.
    const int expectedFirstHeight = 160;

    CHECK (harness.first.getBounds() == juce::Rectangle<int> (0, Harness::reservedTop, 300, expectedFirstHeight));
    CHECK (harness.second.getBounds().getY()
           > Harness::reservedTop + expectedFirstHeight); // below first + the drag bar, not back at y=0
    CHECK (harness.second.getBounds().getBottom() == 400);
}
