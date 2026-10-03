#pragma once

#include "UI/PartStripComponent.h"

namespace lotro
{
    struct PartStripComponentTestAccess
    {
        static void selectPart (PartStripComponent& c, juce::int64 partId) { c.selectPart (partId); }
    };
}
