#include "UI/Playback/Transport.h"

#include <algorithm>
#include <cmath>

namespace lotro
{

void Transport::play (double endSeconds) noexcept
{
    if (! (endSeconds > 0.0))
        return;
    if (position.load() >= endSeconds)
        position.store (0.0);
    playStart.store (position.load());
    playing.store (true, std::memory_order_release);
}

void Transport::pause() noexcept
{
    playing.store (false, std::memory_order_release);
}

void Transport::stop() noexcept
{
    playing.store (false, std::memory_order_release);
    position.store (playStart.load());
    seekGeneration.fetch_add (1);
}

void Transport::seek (double seconds) noexcept
{
    position.store (std::max (0.0, seconds), std::memory_order_release);
    seekGeneration.fetch_add (1);
}

void Transport::reset() noexcept
{
    playing.store (false, std::memory_order_release);
    playStart.store (0.0, std::memory_order_release);
    position.store (0.0, std::memory_order_release);
    seekGeneration.fetch_add (1);
}

bool Transport::advance (double fromSeconds, double seconds, double endSeconds) noexcept
{
    const double next = std::min (fromSeconds + seconds, endSeconds);
    double expected = fromSeconds;

    // A single CAS against the position the block started from: if the message
    // thread moved the playhead since, the update is dropped.
    if (! position.compare_exchange_strong (expected, next, std::memory_order_release, std::memory_order_relaxed))
        return false;

    if (next >= endSeconds)
        playing.store (false, std::memory_order_release);
    return true;
}

double previousBarTick (double tick, int ticksPerQuarter, int numerator, int denominator)
{
    if (ticksPerQuarter <= 0 || numerator <= 0 || denominator <= 0)
        return 0.0;
    const double barTicks = (double) ticksPerQuarter * 4.0 * (double) numerator / (double) denominator;
    const double bars = std::ceil (tick / barTicks - 1e-9);
    return std::max (0.0, (bars - 1.0) * barTicks);
}

} // namespace lotro
