#pragma once

#include "../Diagnostics.h"
#include "../Track.h"

namespace lotro
{

constexpr int maxChordSize = 6;

void applyChordConstraint (Track& track, Diagnostics& diagnostics);

} // namespace lotro
