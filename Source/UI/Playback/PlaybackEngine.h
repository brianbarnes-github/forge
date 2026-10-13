#pragma once

#include "UI/Playback/EventSink.h"
#include "UI/Playback/HandOff.h"
#include "UI/Playback/LoopRange.h"
#include "UI/Playback/PlaybackSnapshot.h"
#include "UI/Playback/Transport.h"

#include <juce_audio_basics/juce_audio_basics.h>

#include <atomic>
#include <memory>
#include <optional>

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

    // Master output level, 0..100 (linear; unity at 100). Any thread.
    void setMasterPercent (int percent) noexcept { master.store (juce::jlimit (0, 100, percent), std::memory_order_release); }
    int masterPercent() const noexcept { return master.load (std::memory_order_acquire); }

    // The loop span the playhead wraps within (nullopt = no loop). Message
    // thread (the single writer); the audio thread reads it once per block.
    void setLoopSpan (std::optional<LoopSpan> span) noexcept
    {
        lastPublishedSpan = span;
        loopCell.publish (span);
    }

    // TEST-ONLY: the span last published by setLoopSpan. Message thread; it
    // reads the writer-side copy, never the cell, so it is safe while the
    // audio thread renders.
    std::optional<LoopSpan> loopSpanForTesting() const noexcept { return lastPublishedSpan; }

private:
    void chase (const PlaybackSnapshot& snapshot, double positionSeconds) noexcept;
    void releaseNewlyMutedTracks (const PlaybackSnapshot& snapshot) noexcept;
    void applyPanChanges (const PlaybackSnapshot& snapshot, double positionSeconds, bool playing) noexcept;
    void deliverControl (const PlaybackSnapshot& snapshot, const PlaybackEvent& e) noexcept;
    void renderBlockUnscaled (juce::AudioBuffer<float>& buffer, int startSample, int numSamples) noexcept;
    void renderSegment (const PlaybackSnapshot& snapshot, float* left, float* right, int samples,
                        double segPos, double eventsBeforeSeconds) noexcept;

    Transport& transport;
    EventSink& sink;
    HandOff<PlaybackSnapshot> snapshots;
    double sampleRate = 44100.0;
    size_t nextEvent = 0;
    bool wasPlaying = false;
    unsigned seenSeekGeneration = 0;
    std::atomic<int> master { 100 };
    LoopSpanCell loopCell;
    std::optional<LoopSpan> lastPublishedSpan;   // message thread only: what setLoopSpan last published
    double expectedPosition = -1.0;   // audio thread only: where this engine left the playhead; -1 = unknown
    bool insideLoopLastBlock = false; // audio thread only: the last block left the playhead inside the loop
};

} // namespace lotro
