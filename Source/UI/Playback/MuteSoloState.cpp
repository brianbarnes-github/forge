#include "UI/Playback/MuteSoloState.h"

namespace lotro
{

void MuteSoloState::setMuted (juce::int64 trackId, bool value)
{
    if (value) muted.insert (trackId); else muted.erase (trackId);
}

void MuteSoloState::setSoloed (juce::int64 trackId, bool value)
{
    if (value) soloed.insert (trackId); else soloed.erase (trackId);
}

bool MuteSoloState::isAudible (juce::int64 trackId) const
{
    if (isMuted (trackId))
        return false;
    return ! anySolo() || isSoloed (trackId);
}

void MuteSoloState::clear()
{
    muted.clear();
    soloed.clear();
}

void MuteSoloState::apply (PlaybackSnapshot& snapshot) const
{
    for (int i = 0; i < snapshot.numTracks(); ++i)
        snapshot.setAudible (i, isAudible (snapshot.trackIds()[(size_t) i]));
}

} // namespace lotro
