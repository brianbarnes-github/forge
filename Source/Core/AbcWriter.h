#pragma once

#include "LotroInstrument.h"
#include "Song.h"

#include <string>

namespace lotro
{

std::string writeAbc (const Song& song);

// Standard-ABC letter conversion (treats MIDI 48 as C, — equivalent to
// passing LotroInstrument::Clarinet, whose midiLow=48 is the identity
// shift). Kept as a single-arg overload for callers that don't care
// about per-instrument shift semantics.
std::string abcPitchToken (int midiPitch);

// Per-instrument letter conversion: shifts `midiPitch` so that the
// instrument's midiLow becomes "C,". Output always lands in [C, .. c']
// for any pitch in the instrument's native range.
std::string abcPitchToken (int midiPitch, LotroInstrument instrument);

std::string abcDurationToken (int durationTicks, int ticksPerQuarter);

} // namespace lotro
