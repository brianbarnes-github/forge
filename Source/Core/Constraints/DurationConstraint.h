#pragma once

#include "../Diagnostics.h"
#include "../Song.h"
#include "../Track.h"

namespace lotro
{

void applyDurationConstraint (Track& track, const Song& song, Diagnostics& diagnostics);

} // namespace lotro
