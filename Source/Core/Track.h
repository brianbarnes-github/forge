#pragma once

#include "Note.h"
#include "LotroInstrument.h"
#include "DrumMap.h"

#include <string>
#include <vector>
#include <optional>

namespace lotro
{

struct DynamicChangeRef;

struct Track
{
    std::string                   name;
    std::vector<Note>             notes;
    std::vector<DynamicChangeRef> dynamicChanges;
    LotroInstrument               instrument          = LotroInstrument::LuteOfAges;
    bool                          enabled             = true;
    int                           transposeSemitones  = 0;
    int                           sourceMidiChannel   = 0;

    // ABC X: index. 0 means "auto-assign in emission order" (no-config or
    // legacy path); nonzero means the user picked this specific value via
    // Config. Writer sorts tracks ascending by x before emission.
    int x = 0;

    // Optional per-track drum-map override. Populated by InstrumentAssembly
    // for Drums instruments that have a drumMap path in their ConfigInstrument.
    // Empty means "fall back to Song::drumMap at emission time."
    std::optional<DrumMap> drumMap;
};

struct DynamicChangeRef
{
    int startTick = 0;
    int marking   = 4; // index into DynamicMarking (0..7); 4 = mf
};

} // namespace lotro
