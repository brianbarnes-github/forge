#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "SongDocument.h"
#include "TempoMeterGraphModel.h"
#include "TempoMeterTable.h"
#include "TimelineViewState.h"

#include <functional>
#include <optional>
#include <utility>
#include <vector>

namespace lotro
{
    // The tempo graph of the Tempo/Meter window: a stepped line of the tempo events
    // against the same horizontal time axis as the main timeline (it reads the shared
    // TimelineViewState). Click a point to select it, click empty space to add a tempo
    // there (snapped to the beat), drag a point to move it: the drag only previews, and
    // mouse-up commits it as ONE TempoEdit (one undo step) - or nothing if unchanged.
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
        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseUp (const juce::MouseEvent&) override;
        // Never consumed: Delete / undo keys bubble to the window's handler.
        bool keyPressed (const juce::KeyPress&) override;

        int paintCountForTesting() const noexcept { return paintCount; }
        int repaintRequestsForTesting() const noexcept { return repaintRequests; }
        const std::vector<TempoMeterEvent>& eventsForTesting() const noexcept { return events; }
        const std::vector<TempoMeterEvent>& selectedForTesting() const noexcept { return selectedTempos; }
        // (tick, bpm) the in-flight drag would commit; nullopt when not dragging.
        std::optional<std::pair<int, double>> dragPreviewForTesting() const;
        bool isDraggingForTesting() const noexcept { return drag.has_value(); }

        // Asks for a repaint (counted, so tests can observe it without a peer).
        void requestRepaint();

    private:
        struct Drag
        {
            TempoMeterEvent   event;         // the point as listed at mouse-down
            juce::Point<int>  down;
            bool              moved = false; // passed the 3 px threshold
            BpmRange          range;         // frozen at mouse-down
            int               previewTick = 0;
            double            previewBpm  = 0.0;
        };

        void updatePreview (const juce::MouseEvent&);
        int  snappedTickForX (int x) const;

        SongDocument&            doc;
        const TimelineViewState& view;
        std::vector<TempoMeterEvent> events;
        std::vector<TempoMeterEvent> selectedTempos;
        BpmRange                 range;
        int                      paintCount = 0;
        int                      repaintRequests = 0;
        std::optional<Drag>      drag;
    };
}
