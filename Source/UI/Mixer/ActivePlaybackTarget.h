#pragma once

#include "UI/Mixer/MixerTarget.h"

namespace lotro
{

// Which MixerTarget the mixer panel drives.
class ActivePlaybackTarget
{
public:
    explicit ActivePlaybackTarget (MixerTarget& initial) : target (&initial) {}
    MixerTarget& get() const noexcept { return *target; }
    // Later: setActive (MixerTarget&) + a change listener; today there is exactly one target.
private:
    MixerTarget* target;
};

} // namespace lotro
