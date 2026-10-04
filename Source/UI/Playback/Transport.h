#pragma once

#include <atomic>

namespace lotro
{

// The one playback clock shared by every window. State is atomic so the audio
// thread can advance it while the message thread reads it. Position is in
// seconds (see TempoMap).
//
// Thread affinity: play(), pause(), stop(), seek(), goToStart(), goToEnd() are
// message-thread only. advance() is audio-thread only: it moves the playhead
// from the position the block STARTED at, and is dropped (returns false,
// touches nothing) when the position is no longer that value, i.e. the message
// thread moved it during the block.
class Transport
{
public:
    bool isPlaying() const noexcept { return playing.load (std::memory_order_acquire); }
    double getPositionSeconds() const noexcept { return position.load (std::memory_order_acquire); }
    double getPlayStartSeconds() const noexcept { return playStart.load (std::memory_order_acquire); }
    unsigned getSeekGeneration() const noexcept { return seekGeneration.load (std::memory_order_acquire); }

    void play (double endSeconds) noexcept;
    void pause() noexcept;
    void stop() noexcept;
    void seek (double seconds) noexcept;
    void goToStart() noexcept { seek (0.0); }
    void goToEnd (double endSeconds) noexcept { seek (endSeconds); }
    // Message-thread only. Back to a fresh clock for a different Song: not
    // playing, position and play-start both 0 (so a later Stop does not jump to
    // the previous Song's play-start), seek generation bumped.
    void reset() noexcept;

    bool advance (double fromSeconds, double seconds, double endSeconds) noexcept;

private:
    std::atomic<bool> playing { false };
    std::atomic<double> position { 0.0 };
    std::atomic<double> playStart { 0.0 };
    std::atomic<unsigned> seekGeneration { 0 };
};

// Tick of the start of the bar containing `tick`, or of the previous bar when
// `tick` is exactly on a bar line. Bad meter values yield 0.
double previousBarTick (double tick, int ticksPerQuarter, int numerator, int denominator);

} // namespace lotro
