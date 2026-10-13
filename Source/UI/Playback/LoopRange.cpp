#include "LoopRange.h"

#include <algorithm>
#include <cmath>

namespace lotro
{
std::optional<LoopRange> normaliseLoopRange (double a, double b, double endTickLimit, double minTicks)
{
    double lo = std::min (a, b);
    double hi = std::max (a, b);
    lo = std::clamp (lo, 0.0, endTickLimit);
    hi = std::clamp (hi, 0.0, endTickLimit);
    if (! (hi - lo >= minTicks))
        return std::nullopt;
    return LoopRange { lo, hi };
}

double snapLoopTick (double tick, int gridTicks, bool unsnapped) noexcept
{
    if (unsnapped || gridTicks <= 0)
        return tick;
    return std::round (tick / (double) gridTicks) * (double) gridTicks;
}

void LoopSpanCell::publish (std::optional<LoopSpan> span) noexcept
{
    version.fetch_add (1, std::memory_order_release);   // odd: write in progress
    active.store (span.has_value(), std::memory_order_relaxed);
    start.store (span ? span->startSeconds : 0.0, std::memory_order_relaxed);
    end.store (span ? span->endSeconds : 0.0, std::memory_order_relaxed);
    version.fetch_add (1, std::memory_order_release);   // even: stable
}

std::optional<LoopSpan> LoopSpanCell::read() noexcept
{
    for (int attempt = 0; attempt < 3; ++attempt)
    {
        const unsigned v1 = version.load (std::memory_order_acquire);
        if ((v1 & 1u) != 0u)
            continue;
        const bool isActive = active.load (std::memory_order_relaxed);
        const double s = start.load (std::memory_order_relaxed);
        const double e = end.load (std::memory_order_relaxed);
        std::atomic_thread_fence (std::memory_order_acquire);
        const unsigned v2 = version.load (std::memory_order_relaxed);
        if (v1 == v2)
        {
            if (isActive)
                lastGood = LoopSpan { s, e };
            else
                lastGood.reset();
            return lastGood;
        }
    }
    return lastGood;
}

LoopStep nextLoopStep (double pos, int remaining, double sr, std::optional<LoopSpan> loop) noexcept
{
    if (! loop.has_value() || pos >= loop->endSeconds || remaining < 1)
        return { remaining, false };
    const double blockSeconds = (double) remaining / sr;
    if (pos + blockSeconds < loop->endSeconds)
        return { remaining, false };
    const int toEnd = (int) std::ceil ((loop->endSeconds - pos) * sr - 1e-6);   // epsilon: absorb FP error (1.99 -> 480.0000000000x)
    return { std::clamp (toEnd, 1, remaining), true };
}
}   // namespace lotro
