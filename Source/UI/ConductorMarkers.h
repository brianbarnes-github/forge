#pragma once

// Pure model behind the Conductor row's tempo and meter markers: the label text
// for each event and which labels fit without overlapping (spec 2026-10-10,
// section 4). No painting and no JUCE types in this API.

#include "UI/TempoEdit.h"

#include <climits>
#include <functional>
#include <string>
#include <vector>

namespace lotro
{

struct ConductorMarker
{
    TempoMeterKind kind = TempoMeterKind::tempo;
    int            tick = 0;
    std::string    label;
};

struct PlacedMarker
{
    ConductorMarker marker;
    int             x          = 0;
    bool            labelShown = false;
    int             labelWidth = 0;       // textWidth of the label; 0 unless shown
};

std::string formatMarkerBpm (double bpm);                       // "120", "93.75"
std::string markerLabel (const TempoMeterEvent&);            // "120" or "3/4"
std::vector<ConductorMarker> conductorMarkers (const std::vector<TempoMeterEvent>&);   // same order as input

// x = xForTick (tick). A label is shown when it starts at least `labelGap` px after the
// previous *shown* label of the same kind ends (a label starts at x + labelInset).
// Markers must be tick-sorted; the walk stops at the first one with x > stopAfterX, so a
// caller painting a clip need not measure labels to its right. Earlier decisions are unchanged.
std::vector<PlacedMarker> placeMarkers (const std::vector<ConductorMarker>&,
                                        const std::function<int (int)>& xForTick,
                                        const std::function<int (const std::string&)>& textWidth,
                                        int labelInset = 3, int labelGap = 4, int stopAfterX = INT_MAX);

} // namespace lotro
