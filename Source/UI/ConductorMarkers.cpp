#include "UI/ConductorMarkers.h"

#include <algorithm>
#include <array>
#include <climits>
#include <cstdio>
#include <iterator>

namespace lotro
{

std::string formatMarkerBpm (double bpm)
{
    char buffer[32];
    std::snprintf (buffer, sizeof buffer, "%.2f", bpm);
    std::string text (buffer);
    text.erase (text.find_last_not_of ('0') + 1);
    if (! text.empty() && text.back() == '.')
        text.pop_back();
    return text;
}

std::string markerLabel (const TempoMeterEvent& event)
{
    if (event.kind == TempoMeterKind::tempo)
        return "\xe2\x99\xa9=" + formatMarkerBpm (event.bpm);
    return std::to_string (event.numerator) + "/" + std::to_string (event.denominator);
}

std::vector<ConductorMarker> conductorMarkers (const std::vector<TempoMeterEvent>& events)
{
    std::vector<ConductorMarker> out;
    out.reserve (events.size());
    std::transform (events.begin(), events.end(), std::back_inserter (out),
                    [] (const TempoMeterEvent& e) { return ConductorMarker { e.kind, e.tick, markerLabel (e) }; });
    return out;
}

std::vector<PlacedMarker> placeMarkers (const std::vector<ConductorMarker>& markers,
                                        const std::function<int (int)>& xForTick,
                                        const std::function<int (const std::string&)>& textWidth,
                                        int labelInset, int labelGap, int stopAfterX)
{
    std::array<int, 2> shownRight { INT_MIN / 2, INT_MIN / 2 };   // per TempoMeterKind
    std::vector<PlacedMarker> out;
    out.reserve (markers.size());
    for (const auto& marker : markers)
    {
        const int x     = xForTick (marker.tick);
        if (x > stopAfterX)
            break;
        int&      right = shownRight[(size_t) marker.kind];
        const bool shown = x + labelInset >= right + labelGap;
        const int  width = shown ? textWidth (marker.label) : 0;
        if (shown)
            right = x + labelInset + width;
        out.push_back ({ marker, x, shown, width });
    }
    return out;
}

} // namespace lotro
