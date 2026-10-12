#pragma once

#include "UI/PartStripComponent.h"

namespace lotro
{
    struct PartStripComponentTestAccess
    {
        static void selectPart (PartStripComponent& c, juce::int64 partId) { c.selectPart (partId); }
        static void rebuild (PartStripComponent& c) { c.rebuild(); }
        static int slotCount (const PartStripComponent& c) { return c.row.slots.size(); }
        static juce::Rectangle<int> slotBounds (const PartStripComponent& c, int i) { return c.row.slots[i]->getBounds(); }
        static int rowWidth (const PartStripComponent& c) { return c.row.getWidth(); }
        static int appearanceRepaints (const PartStripComponent& c) { return c.appearanceRepaintCount; }
    };
}
