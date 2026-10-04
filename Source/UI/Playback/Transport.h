#pragma once

#include <atomic>

namespace lotro
{

// The one playback clock shared by every window. State is atomic so the audio
// thread can advance it while the message thread reads it. Position is in
// seconds (see TempoMap).
//
// Thread affinity: play(), pause(), stop(), seek(), goToStart(), goToEnd() are
// message-thread only. advance() is audio-thread only. If the message thread
// changes position during a block, that block's advance() becomes a no-op
// (compare_exchange_strong fails, and we drop the update without retrying).
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

    void advance (double seconds, double endSeconds) noexcept;

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
