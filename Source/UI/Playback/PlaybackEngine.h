#pragma once

#include "UI/Playback/EventSink.h"
#include "UI/Playback/HandOff.h"
#include "UI/Playback/PlaybackSnapshot.h"
#include "UI/Playback/Transport.h"

#include <juce_audio_basics/juce_audio_basics.h>

#include <memory>

namespace lotro
{

// Device-free playback core. renderBlock() is what the audio callback (and the
// tests) call. It never allocates, locks, frees or throws.
class PlaybackEngine
{
public:
    PlaybackEngine (Transport& transportIn, EventSink& sinkIn) : transport (transportIn), sink (sinkIn) {}

    void publishSnapshot (std::shared_ptr<PlaybackSnapshot> snapshot) { snapshots.publish (std::move (snapshot)); }
    void collectGarbage() { snapshots.collectRetired(); }

    void prepare (double sampleRateIn, int maxBlockSize);
    void renderBlock (juce::AudioBuffer<float>& buffer, int startSample, int numSamples) noexcept;

private:
    void chase (const PlaybackSnapshot& snapshot, double positionSeconds) noexcept;
    void releaseNewlyMutedTracks (const PlaybackSnapshot& snapshot) noexcept;

    Transport& transport;
    EventSink& sink;
    HandOff<PlaybackSnapshot> snapshots;
    double sampleRate = 44100.0;
    size_t nextEvent = 0;
    bool wasPlaying = false;
    unsigned seenSeekGeneration = 0;
    double expectedPosition = -1.0;   // audio thread only: where this engine left the playhead; -1 = unknown
};

} // namespace lotro
