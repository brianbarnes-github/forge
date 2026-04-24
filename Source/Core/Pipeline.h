#pragma once

#include "Config.h"
#include "Diagnostics.h"
#include "LotroInstrument.h"
#include "Song.h"

#include <map>
#include <optional>
#include <string>

namespace lotro
{

// Builds a one-instrument-per-MIDI-track Config from a freshly imported Song.
// Used by the no-config CLI path and by the UI when a MIDI is loaded without
// an existing config. Every non-drum track is defaulted to LuteOfAges and
// the user is expected to pick the real instrument (no auto-picking, per
// the MIDI-is-source-of-truth principle). Channel-10 tracks imported as
// Drums by MidiImporter keep that identity because it's what the MIDI
// itself declares under the General MIDI convention. Entries in
// `instrumentOverrides` (keyed by raw MIDI track index) replace the
// default.
Config synthesiseConfig (const Song&                                  raw,
                         const std::string&                            inputPath,
                         const std::string&                            outputPath,
                         std::optional<double>                         tempo,
                         int                                           transpose,
                         const std::map<int, LotroInstrument>&         instrumentOverrides);

// Runs the per-track constraint pipeline (Range → Chord → Duration → Tempo →
// Collision → Dynamic) on every enabled track of `song`, then calls
// applyTempoCollapseToSongMaps once on the song. Diagnostics get the
// per-track index back-filled where the constraint pass left it as -1.
void runPipeline (Song& song, Diagnostics& diagnostics);

} // namespace lotro
