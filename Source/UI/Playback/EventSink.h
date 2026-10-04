#pragma once

#include "UI/Playback/PlaybackSnapshot.h"

namespace lotro
{

// Where the engine delivers events and asks for audio. Implemented by the
// real synth sink and by the test RecordingSink. All methods except prepare()
// are called on the audio thread and must not allocate, lock or throw.
class EventSink
{
public:
    virtual ~EventSink() = default;
    virtual void prepare (double sampleRate, int maxBlockSize) = 0;
    virtual bool beginBlock() noexcept = 0;                    // true if the sink's own state was replaced (engine must re-chase)
    virtual void handle (const PlaybackEvent&) noexcept = 0;
    virtual void releaseChannel (int virtualChannel) noexcept = 0;   // note-off with release tails
    virtual void releaseAll() noexcept = 0;
    // Restores one virtual channel's controller state (volume, expression, pan,
    // pitch wheel, sustain, pitch range, tuning, RPN) to the synth's initial
    // values. The engine calls it for every channel of the snapshot after
    // releaseAll() and before chasing, so a chase never inherits controllers
    // left over from earlier playback. Does not touch preset/bank.
    virtual void resetChannel (int virtualChannel) noexcept = 0;
    virtual void render (float* left, float* right, int numFrames) noexcept = 0;   // overwrites
};

} // namespace lotro
