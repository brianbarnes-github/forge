#pragma once

#include <atomic>
#include <optional>

namespace lotro
{
struct LoopRange
{
    double startTick = 0.0;
    double endTick = 0.0;   // startTick < endTick
};

constexpr double kMinLoopSeconds = 0.05;
constexpr int kMaxWrapsPerBlock = 4;

// Orders the edges, clamps to [0, endTickLimit], and rejects a range shorter
// than minTicks (returns nullopt). minTicks > 0.
std::optional<LoopRange> normaliseLoopRange (double a, double b, double endTickLimit, double minTicks);

// Nearest multiple of gridTicks (>= 0). gridTicks <= 0 or unsnapped = the tick unchanged.
double snapLoopTick (double tick, int gridTicks, bool unsnapped) noexcept;

// The range in seconds. start < end, both >= 0.
struct LoopSpan
{
    double startSeconds = 0.0;
    double endSeconds = 0.0;
};

// Single-writer (the message thread), single-reader (the audio thread) cell. read() never blocks:
// it returns the last consistent value it saw (nullopt = no loop).
class LoopSpanCell
{
public:
    void publish (std::optional<LoopSpan> span) noexcept;   // message thread
    std::optional<LoopSpan> read() noexcept;                // audio thread

private:
    std::atomic<unsigned> version { 0 };
    std::atomic<double> start { 0.0 };
    std::atomic<double> end { 0.0 };
    std::atomic<bool> active { false };
    std::optional<LoopSpan> lastGood;   // audio-thread-only cache
};

struct LoopStep
{
    int samples = 0;
    bool wraps = false;
};

// How many of `remainingSamples` to render before either the block ends or the
// playhead reaches loopEnd (then wraps = true). pos >= loopEnd, or no loop, means
// render everything with no wrap. samples is always >= 1 when remainingSamples >= 1.
LoopStep nextLoopStep (double positionSeconds, int remainingSamples, double sampleRate,
                       std::optional<LoopSpan> loop) noexcept;
}   // namespace lotro
