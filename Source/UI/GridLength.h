#pragma once

#include "GridSize.h"

#include <algorithm>
#include <cmath>

// The MIDI editor's Options > Grid Length and Grid Division: session-only state
// (never saved, never written back to the main window's Grid Size or the
// Preferences default) that sets the editor's grid spacing. Header-only, no JUCE.
namespace lotro
{

enum class GridLength
{
    Off,
    Whole,
    Half,
    Quarter,
    Eighth,
    Sixteenth,
    ThirtySecond,
    SixtyFourth
};

constexpr int gridLengthCount = (int) GridLength::SixtyFourth + 1;

enum class GridDivision
{
    Straight,
    Triplet,
    Dotted,
    Swing
};

constexpr int gridDivisionCount = (int) GridDivision::Swing + 1;

constexpr const char* gridLengthName (GridLength length) noexcept
{
    switch (length)
    {
        case GridLength::Whole:        return "1/1";
        case GridLength::Half:         return "1/2";
        case GridLength::Quarter:      return "1/4";
        case GridLength::Eighth:       return "1/8";
        case GridLength::Sixteenth:    return "1/16";
        case GridLength::ThirtySecond: return "1/32";
        case GridLength::SixtyFourth:  return "1/64";
        case GridLength::Off:
        default:                       return "Off";
    }
}

constexpr const char* gridDivisionName (GridDivision division) noexcept
{
    switch (division)
    {
        case GridDivision::Triplet: return "Triplet";
        case GridDivision::Dotted:  return "Dotted";
        case GridDivision::Swing:   return "Swing";
        case GridDivision::Straight:
        default:                    return "Straight";
    }
}

// The main window's Edit > Grid Size choice as a length.
constexpr GridLength gridLengthForSize (GridSize size) noexcept
{
    switch (size)
    {
        case GridSize::Quarter:   return GridLength::Quarter;
        case GridSize::Eighth:    return GridLength::Eighth;
        case GridSize::Sixteenth: return GridLength::Sixteenth;
        case GridSize::Off:
        default:                  return GridLength::Off;
    }
}

// The grid spacing in ticks: 0 for Off (or no PPQ), else the note value, two
// thirds of it for Triplet and one and a half times it for Dotted, rounded to a
// whole tick and never below one. Swing spaces the grid as Straight; the swing shift
// itself is EditorGrid's (EditorGrid.h).
inline int editorGridTicks (GridLength length, GridDivision division, int ticksPerQuarter) noexcept
{
    if (length == GridLength::Off || ticksPerQuarter <= 0)
        return 0;
    double ticks = 4.0 * (double) ticksPerQuarter / (double) (1 << ((int) length - (int) GridLength::Whole));
    if (division == GridDivision::Triplet)
        ticks *= 2.0 / 3.0;
    else if (division == GridDivision::Dotted)
        ticks *= 1.5;
    return std::max (1, (int) std::lround (ticks));
}

} // namespace lotro
