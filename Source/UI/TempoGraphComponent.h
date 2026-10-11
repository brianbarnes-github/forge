#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "SongDocument.h"
#include "TempoMeterGraphModel.h"
#include "TempoMeterTable.h"
#include "TimelineViewState.h"

#include <functional>
#include <vector>

namespace lotro
{
    // The tempo graph of the Tempo/Meter window: a stepped line of the tempo events
    // against the same horizontal time axis as the main timeline (it reads the shared
    // TimelineViewState). Read-only for now; editing arrives with onSelect / onEdited.
    class TempoGraphComponent : public juce::Component
    {
    public:
        TempoGraphComponent (SongDocument&, const TimelineViewState&);

        // The listing the table shows (tempo points are the kind == tempo entries).
        void setEvents (std::vector<TempoMeterEvent> events);
        // Highlighted points = the tempo entries among `selected`.
        void setSelected (std::vector<TempoMeterEvent> selected);

        // Fired when the user clicks a point / adds one. The window selects that row.
        std::function<void (TempoMeterKind, int tick)> onSelect;
        // Fired after every edit attempt so the window can show describe().
        std::function<void (const TableEditResult&)> onEdited;

        int xForTick (int tick) const;
        int yForBpm (double bpm) const;
        juce::Rectangle<int> plotArea() const;   // local bounds reduced (0, 8); no left gutter

        void paint (juce::Graphics&) override;

        int paintCountForTesting() const noexcept { return paintCount; }
        int repaintRequestsForTesting() const noexcept { return repaintRequests; }
        const std::vector<TempoMeterEvent>& eventsForTesting() const noexcept { return events; }
        const std::vector<TempoMeterEvent>& selectedForTesting() const noexcept { return selectedTempos; }

        // Asks for a repaint (counted, so tests can observe it without a peer).
        void requestRepaint();

    private:
        SongDocument&            doc;
        const TimelineViewState& view;
        std::vector<TempoMeterEvent> events;
        std::vector<TempoMeterEvent> selectedTempos;
        BpmRange                 range;
        int                      paintCount = 0;
        int                      repaintRequests = 0;
    };
}
