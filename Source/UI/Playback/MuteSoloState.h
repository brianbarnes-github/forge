#pragma once

#include "UI/Playback/PlaybackSnapshot.h"

#include <juce_core/juce_core.h>

#include <set>

namespace lotro
{

// Message-thread owner of the session's mute/solo flags, keyed by trackId.
// Not part of the Song: never saved, never undoable.
class MuteSoloState
{
public:
    void setMuted (juce::int64 trackId, bool muted);
    void setSoloed (juce::int64 trackId, bool soloed);
    bool isMuted (juce::int64 trackId) const { return muted.count (trackId) != 0; }
    bool isSoloed (juce::int64 trackId) const { return soloed.count (trackId) != 0; }
    bool anySolo() const { return ! soloed.empty(); }

    // Mute beats solo; solo is additive.
    bool isAudible (juce::int64 trackId) const;
    // True when the track is silent only because some other track is soloed.
    bool isSilencedBySolo (juce::int64 trackId) const { return anySolo() && ! isSoloed (trackId) && ! isMuted (trackId); }

    void clear();

    // Writes every track's audible flag into `snapshot`.
    void apply (PlaybackSnapshot& snapshot) const;

private:
    std::set<juce::int64> muted;
    std::set<juce::int64> soloed;
};

} // namespace lotro
