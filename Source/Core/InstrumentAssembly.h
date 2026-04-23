#pragma once

#include "Config.h"
#include "Diagnostics.h"
#include "Song.h"

namespace lotro
{

// Takes a raw Song just imported from a MIDI file plus a validated Config
// and produces a new Song whose `tracks` are the merged virtual tracks —
// one Track per ConfigInstrument, with notes gathered from every source,
// per-instrument transpose and volume already applied, x and name and
// drumMap populated from the Config.
//
// Any MIDI track not referenced by any instrument's `sources` emits an
// info-level Diagnostic and is dropped.
//
// The returned Song copies tempoMap, meterMap, title, transcriber,
// ticksPerQuarter, and drumMap (used as the song-level default for Drums
// instruments without their own drumMap) from `raw` and applies the
// Config's overrides where present.
Song assembleInstruments (const Song&         raw,
                          const Config&       config,
                          Diagnostics&        diagnostics);

} // namespace lotro
