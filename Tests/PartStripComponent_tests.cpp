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

TEST_CASE ("PartStripComponent: cards keep one set width however many parts there are", "[part-strip]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    SongDocument doc;
    PartStripComponent strip (doc);
    strip.setBounds (0, 0, 1000, 110);

    doc.addPart ("Lute of Ages", "Part 1");
    PartStripComponentTestAccess::rebuild (strip);
    REQUIRE (PartStripComponentTestAccess::slotCount (strip) == 1);
    CHECK (PartStripComponentTestAccess::slotBounds (strip, 0).getWidth() == PartStripComponent::slotWidth);

    doc.addPart ("Harp", "Part 2");
    doc.addPart ("Flute", "Part 3");
    PartStripComponentTestAccess::rebuild (strip);
    REQUIRE (PartStripComponentTestAccess::slotCount (strip) == 3);
    for (int i = 0; i < 3; ++i)
        CHECK (PartStripComponentTestAccess::slotBounds (strip, i).getWidth() == PartStripComponent::slotWidth);
    CHECK (PartStripComponentTestAccess::slotBounds (strip, 1).getX() > PartStripComponentTestAccess::slotBounds (strip, 0).getRight());
}

TEST_CASE ("PartStripComponent: cards that overflow the strip scroll instead of shrinking", "[part-strip]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    SongDocument doc;
    PartStripComponent strip (doc);
    strip.setBounds (0, 0, 300, 110);
    for (int i = 0; i < 4; ++i)
        doc.addPart ("Lute of Ages", "Part " + juce::String (i + 1));
    PartStripComponentTestAccess::rebuild (strip);

    REQUIRE (PartStripComponentTestAccess::slotCount (strip) == 4);
    CHECK (PartStripComponentTestAccess::rowWidth (strip) > 300);
    CHECK (PartStripComponentTestAccess::slotBounds (strip, 3).getWidth() == PartStripComponent::slotWidth);
}
