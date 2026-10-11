#pragma once

// The Tempo/Meter window's position strip: the whole song as a thin bar (tick 0 to
// the song end), with every tempo / meter event as a notch and the selected event
// as a full-height green line. Display only. The mapping and bar-line functions
// are pure so they can be tested without painting.

#include <juce_gui_basics/juce_gui_basics.h>

#include "Core/Song.h" // MeterChange
#include "UI/MeterSegments.h"
#include "UI/TempoEdit.h"

#include <optional>
#include <vector>

namespace lotro
{

class SongDocument;

// The tick the strip's right edge stands for: the later of `lastNoteEnd` and the last
// event, but at least four bars of the first meter (an empty song is not degenerate).
int stripEndTick (int lastNoteEnd, const std::vector<TempoMeterEvent>& events,
                  const std::vector<MeterSegment>& segments, int ticksPerQuarter);

// Linear tick <-> x over [left, left + width]. A span of 0 maps everything to `left`.
double stripTickToX (int tick, int endTick, double left, double width);
int    stripXToTick (double x, int endTick, double left, double width);

// Bar-line ticks in [0, endTick], following every meter segment, keeping every Nth bar
// (N = 1, 2, 5, 10, 20, ...) so kept bars are at least `minPixels` apart across `width`.
std::vector<int> stripBarTicks (const std::vector<MeterSegment>& segments, int endTick,
                                double width, double minPixels = 4.0);

// End of the last note of any track (mirrors TrackListComponent::documentEndTick).
int lastNoteEndTick (const SongDocument& doc);

class PositionStrip : public juce::Component
{
public:
    static constexpr int height = 28;
    static constexpr int inset  = 8;   // left/right margin of the bar

    PositionStrip();

    void update (std::vector<TempoMeterEvent> events, const std::vector<MeterChange>& meterMap,
                 int ticksPerQuarter, int lastNoteEnd, std::optional<int> selectedTick);

    int                endTick() const noexcept      { return end; }
    std::optional<int> selectedTick() const noexcept { return selected; }
    float              xForTick (int tick) const;

    void paint (juce::Graphics&) override;

private:
    std::vector<TempoMeterEvent> events;
    std::vector<MeterSegment>    segments;
    std::optional<int>           selected;
    int                          end = 0;
};

} // namespace lotro
