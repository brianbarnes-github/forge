#pragma once

// The Songsmith piano-roll toolbar's grid-size selector: transient UI
// state (never persisted in SongDocument, per the plan) driving both
// SourceRollEditor::quantizeSelection's snap size and create's default
// note duration. Header-only, no JUCE dependency -- pure enum + arithmetic.
namespace lotro
{

enum class GridSize
{
    Off,
    Quarter,
    Eighth,
    Sixteenth
};

// Returns 0 for GridSize::Off ("no grid"), else the number of ticks
// spanned by that note value at the given ticksPerQuarter.
constexpr int gridSizeToTicks (GridSize size, int ticksPerQuarter) noexcept
{
    switch (size)
    {
        case GridSize::Quarter:   return ticksPerQuarter;
        case GridSize::Eighth:    return ticksPerQuarter / 2;
        case GridSize::Sixteenth: return ticksPerQuarter / 4;
        case GridSize::Off:
        default:                 return 0;
    }
}

} // namespace lotro
