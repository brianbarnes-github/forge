#pragma once

#include "Note.h"
#include "LotroInstrument.h"

#include <string>
#include <vector>

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
};

struct DynamicChangeRef
{
    int startTick = 0;
    int marking   = 4; // index into DynamicMarking (0..7); 4 = mf
};

} // namespace lotro
