#pragma once

#include "SongDocument.h"
#include "Core/Config.h"
#include "Core/Song.h"

#include <vector>

namespace lotro
{

// Import path: appends imported's tracks (and, per Ruling 2, its
// tempo/meter maps) as new document state, minting fresh trackIds,
// copying Note fields verbatim. Does NOT touch Parts/Assignments, and
// does NOT touch SONG.inputMidiPath (the caller sets that separately).
void appendImportedSong (SongDocument& doc, const Song& imported, int importBatch);

// Export path (also used later for scoped live preview). Builds a
// forge_core Config from Song-level metadata + the given parts (empty =
// all parts = full export; one or more entries = filtered subset, e.g.
// single-part preview in a later phase). Builds the Config and a
// flattened raw Song together, in lockstep, since Config.midiTrackIndex
// is positional into that raw Song.
struct BuiltConfigAndSong
{
    Config config;
    Song   rawSong;
};

BuiltConfigAndSong buildConfigAndRawSong (const SongDocument& doc,
                                          const std::vector<juce::int64>& partIds = {});

} // namespace lotro
