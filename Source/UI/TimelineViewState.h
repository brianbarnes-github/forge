#pragma once

#include <juce_core/juce_core.h>

namespace lotro
{
    // Shared horizontal zoom/scroll state for the per-track note-timeline
    // previews in TrackListComponent. One instance is owned by
    // TrackListComponent and read by every row's TrackNotePreview, so all
    // rows stay in lockstep (2026-09-15 upper-region-track-timeline design:
    // "Shared across all rows").
    //
    // Deliberately independent of TrackEditorWindow's own zoom/scroll —
    // the floating editor keeps its own, unrelated state.
    class TimelineViewState
    {
    public:
        TimelineViewState() = default;

        void setPixelsPerTick (double pixelsPerTickIn) noexcept;
        double getPixelsPerTick() const noexcept { return pixelsPerTick; }

        void setScrollOffsetTicks (double ticks) noexcept;
        double getScrollOffsetTicks() const noexcept { return scrollOffsetTicks; }

        // Zooms so the tick currently under anchorX stays under anchorX.
        void zoomBy (double factor, int anchorX) noexcept;

        // Pans by a raw pixel delta (positive = content moves left, i.e. view
        // scrolls forward in time), clamped so the offset never goes negative.
        void scrollByPixels (int deltaX) noexcept;

        int xForTick (int tick) const noexcept;
        int tickForX (int x) const noexcept;

    private:
        double pixelsPerTick = 0.1;
        double scrollOffsetTicks = 0.0;

        static constexpr double minPixelsPerTick = 0.001;
        static constexpr double maxPixelsPerTick = 2.5;
    };
}
