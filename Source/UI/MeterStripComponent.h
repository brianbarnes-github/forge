#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "SongDocument.h"
#include "TempoMeterTable.h"
#include "TimelineViewState.h"

#include <functional>
#include <optional>
#include <vector>

namespace lotro
{
    // The meter strip of the Tempo/Meter window, under the tempo graph: one block per
    // meter event, from its tick to the next meter (the last to the right edge), on the
    // main timeline's horizontal axis (the shared TimelineViewState). Click a block to
    // select it; press on its left edge or in its first 40 px and drag to move it to
    // another bar (previewed; mouse-up commits ONE edit, or nothing if unchanged);
    // double-click to add a meter at the nearest bar, carrying the meter in force there.
    class MeterStripComponent : public juce::Component
    {
    public:
        MeterStripComponent (SongDocument&, const TimelineViewState&);

        static constexpr int preferredHeight = 28;

        // The listing the table shows (blocks = the kind == meter entries).
        void setEvents (std::vector<TempoMeterEvent> events);
        // Highlighted blocks = the meter entries among `selected`.
        void setSelected (std::vector<TempoMeterEvent> selected);

        // Fired when the user clicks a block / adds one / moves one. The window selects that row.
        std::function<void (TempoMeterKind, int tick)> onSelect;
        // Fired after every edit attempt so the window can show describe().
        std::function<void (const TableEditResult&)> onEdited;

        int xForTick (int tick) const;

        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseUp (const juce::MouseEvent&) override;
        void mouseDoubleClick (const juce::MouseEvent&) override;
        // Never consumed: Delete / undo keys bubble to the window's handler.
        bool keyPressed (const juce::KeyPress&) override;

        int paintCountForTesting() const noexcept { return paintCount; }
        int repaintRequestsForTesting() const noexcept { return repaintRequests; }
        const std::vector<TempoMeterEvent>& eventsForTesting() const noexcept { return events; }
        const std::vector<TempoMeterEvent>& selectedForTesting() const noexcept { return selectedMeters; }
        // The bar tick the in-flight drag would commit; nullopt when not dragging.
        std::optional<int> dragPreviewForTesting() const;
        bool isDraggingForTesting() const noexcept { return drag.has_value(); }

        // Asks for a repaint (counted, so tests can observe it without a peer).
        void requestRepaint();

    private:
        struct Drag
        {
            TempoMeterEvent event;          // the meter as listed at mouse-down
            int             downX = 0;
            bool            moved = false;  // passed the 3 px horizontal threshold
            int             previewTick = 0;
        };

        struct Hit
        {
            size_t index = 0;      // into `events`
            bool   grab  = false;  // on the left edge or in the first 40 px: may start a drag
        };

        std::optional<Hit> hitAt (int x) const;
        void updatePreview (const juce::MouseEvent&);
        int  ticksPerQuarter() const;

        SongDocument&                doc;
        const TimelineViewState&     view;
        std::vector<TempoMeterEvent> events;          // meter entries only, tick-sorted
        std::vector<TempoMeterEvent> selectedMeters;
        int                          paintCount = 0;
        int                          repaintRequests = 0;
        std::optional<Drag>          drag;
    };
}
