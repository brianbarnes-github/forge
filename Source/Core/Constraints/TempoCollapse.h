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

// Rescales the tick anchors on both `song.meterMap` and `song.tempoMap` from
// original-MIDI-tick space into main-tempo stream-tick space, so bar labelling
// and mid-song tempo/meter annotations align with where notes now sit after
// `applyTempoCollapse`. Call once per song, after every track has been through
// `applyTempoCollapse`. Safe to call when either map is empty.
void applyTempoCollapseToSongMaps (Song& song);

} // namespace lotro
