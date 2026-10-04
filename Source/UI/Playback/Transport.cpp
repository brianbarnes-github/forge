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

void Transport::advance (double seconds, double endSeconds) noexcept
{
    const double next = position.load() + seconds;
    if (next >= endSeconds)
    {
        position.store (endSeconds, std::memory_order_release);
        playing.store (false, std::memory_order_release);
    }
    else
    {
        position.store (next, std::memory_order_release);
    }
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
