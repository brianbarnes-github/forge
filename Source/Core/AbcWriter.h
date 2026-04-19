#pragma once

#include "Song.h"

#include <string>

namespace lotro
{

std::string writeAbc (const Song& song);

std::string abcPitchToken (int midiPitch);

std::string abcDurationToken (int durationTicks, int ticksPerQuarter);

} // namespace lotro
