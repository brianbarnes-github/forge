#pragma once

// Note colours for View > Color Note by (no JUCE): the track's colour, a
// velocity heat map, a channel table or a pitch-class table.

#include "NoteDisplay.h"

#include <cstdint>

namespace lotro
{

// Opaque 0xFFRRGGBB for a note under `mode`. Track returns trackArgb unchanged;
// velocity clamps to 0..127 and channel to 1..16.
std::uint32_t noteColourFor (NoteColourMode mode, int pitch, int velocity, int channel, std::uint32_t trackArgb);

} // namespace lotro
