#include "PartStripTestAccess.h"
#include "UI/SongDocument.h"

#include <catch2/catch_test_macros.hpp>

using namespace lotro;

TEST_CASE ("PartStripComponent: clearSelection forgets the selection without firing onPartSelected", "[session-reset]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    SongDocument doc;
    auto part = doc.addPart ("Lute of Ages", "Part 1");
    const auto partId = (juce::int64) part.getProperty (SongIDs::partId);

    PartStripComponent strip (doc);
    int fired = 0;
    strip.onPartSelected = [&] (juce::int64) { ++fired; };

    PartStripComponentTestAccess::selectPart (strip, partId);
    REQUIRE (strip.getSelectedPartId() == partId);
    REQUIRE (fired == 1);

    strip.clearSelection();
    CHECK (strip.getSelectedPartId() == -1);
    CHECK (fired == 1);                         // clearing is silent
}
