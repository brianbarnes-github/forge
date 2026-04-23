#pragma once

#include "../Diagnostics.h"
#include "../Song.h"
#include "../Track.h"

namespace lotro
{

// Rescales a track's notes from original-MIDI-tick space into main-tempo
// stream-tick space. Both `startTick` and `durationTicks` are scaled so that
// inter-note gaps (rests) and note lengths all reflect perceived time under
// the fixed `Q:` from `song.tempoMap.front()`. Must be called once per track.
void applyTempoCollapse (Track& track, const Song& song, Diagnostics& diagnostics);

// Rescales `song.meterMap[i].tick` values from original-MIDI-tick space into
// main-tempo stream-tick space so bar labelling aligns with where notes now
// sit after `applyTempoCollapse`. Call once per song, after every track has
// been through `applyTempoCollapse`. Safe to call when `meterMap` is empty.
void applyTempoCollapseToMeterMap (Song& song);

} // namespace lotro
