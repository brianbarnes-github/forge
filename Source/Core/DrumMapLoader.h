#pragma once

#include "DrumMap.h"

#include <string>

namespace lotro
{

// Loads drum-map entries from a JSON file and applies them to `target`.
// Existing entries not mentioned in the file are preserved (merge semantics),
// so users can override just the drums they care about without restating
// the whole spec §2.6 table.
//
// Expected JSON format — a flat object keyed by GM pitch as a decimal
// string, valued by the LOTRO ABC drum-slot token:
//
//     {
//       "35": "C",       comment lines not supported by JSON, naturally
//       "36": "D",
//       "49": "c'",
//       ...
//     }
//
// Returns an empty string on success. Otherwise returns a human-readable
// error message and leaves `target` unchanged.
std::string loadDrumMapFromFile (const std::string& path, DrumMap& target);

} // namespace lotro
