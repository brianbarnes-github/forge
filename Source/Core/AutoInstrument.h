#pragma once

#include "LotroInstrument.h"
#include "Track.h"

namespace lotro
{

// Pick a LOTRO instrument that best fits the track's pitch range — the
// instrument whose native MIDI range covers the most notes without needing
// octave transposition. Drums tracks are returned unchanged (their identity
// is set by MidiImporter from channel 10). Empty tracks keep their current
// instrument, since there's nothing to score against.
//
// Tiebreakers favour the most common "melody" instruments first (LuteOfAges,
// Harp, Theorbo) so a plain conversion produces sensible defaults without
// surprising the user. The explicit --instrument CLI flag still wins.
LotroInstrument pickInstrumentForTrack (const Track& track);

} // namespace lotro
