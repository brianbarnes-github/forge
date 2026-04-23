#include "TempoCollapse.h"

#include <cmath>
#include <string>

namespace lotro
{

namespace
{
    double localBpmAt (const std::vector<TempoChange>& tempoMap, int tick, double mainBpm) noexcept
    {
        double bpm = mainBpm;
        for (const auto& change : tempoMap)
        {
            if (change.tick <= tick) bpm = change.bpm;
            else                     break;
        }
        return bpm;
    }

    // Cumulative transform from original MIDI ticks to main-tempo stream
    // ticks. Walks tempo segments in order, scaling each segment's length
    // by `mainBpm / segBpm` so the perceived duration of that segment is
    // preserved under the fixed `Q:` emitted from tempoMap.front().
    int scaleTickToMainTempo (int                             originalTick,
                              const std::vector<TempoChange>& tempoMap,
                              double                          mainBpm) noexcept
    {
        if (originalTick <= 0 || mainBpm <= 0.0)
            return originalTick;

        double accumulated = 0.0;
        int    prevSegTick = 0;
        double prevSegBpm  = mainBpm;

        for (const auto& change : tempoMap)
        {
            if (change.tick >= originalTick)
                break;
            if (prevSegBpm > 0.0)
                accumulated += (change.tick - prevSegTick) * (mainBpm / prevSegBpm);
            prevSegTick = change.tick;
            prevSegBpm  = change.bpm;
        }

        if (prevSegBpm > 0.0)
            accumulated += (originalTick - prevSegTick) * (mainBpm / prevSegBpm);

        return (int) std::lround (accumulated);
    }
}

void applyTempoCollapse (Track& track, const Song& song, Diagnostics& diagnostics)
{
    if (song.tempoMap.empty())
        return;

    const double mainBpm = song.tempoMap.front().bpm;
    if (mainBpm <= 0.0)
        return;

    for (auto& note : track.notes)
    {
        const int    originalStartTick = note.startTick;
        const double localBpm          = localBpmAt (song.tempoMap, originalStartTick, mainBpm);

        if (localBpm > 0.0)
        {
            const double scale = mainBpm / localBpm;
            if (std::abs (scale - 1.0) >= 1e-9)
            {
                const int rescaled = (int) std::lround ((double) note.durationTicks * scale);
                if (rescaled <= 0)
                {
                    Diagnostic d;
                    d.severity         = Severity::Warning;
                    d.source           = "TempoCollapse";
                    d.message          = "Tempo scaling collapsed note to 0 duration on '" + track.name + "'";
                    d.tick             = originalStartTick;
                    d.pitch            = note.pitch;
                    d.sourceTrackIndex = note.sourceTrackIndex;
                    d.sourceEventIndex = note.sourceEventIndex;
                    diagnostics.push_back (std::move (d));
                    note.durationTicks = 1;
                }
                else
                {
                    note.durationTicks = rescaled;
                }
            }
        }

        note.startTick = scaleTickToMainTempo (originalStartTick, song.tempoMap, mainBpm);
    }
}

void applyTempoCollapseToSongMaps (Song& song)
{
    if (song.tempoMap.empty())
        return;

    const double mainBpm = song.tempoMap.front().bpm;
    if (mainBpm <= 0.0)
        return;

    // Snapshot the tempo map before we mutate it — scaleTickToMainTempo walks
    // it using original ticks, so rescaling tempoMap in-place would corrupt
    // the scaling of later entries.
    const auto original = song.tempoMap;

    for (auto& meter : song.meterMap)
        meter.tick = scaleTickToMainTempo (meter.tick, original, mainBpm);

    for (auto& change : song.tempoMap)
        change.tick = scaleTickToMainTempo (change.tick, original, mainBpm);
}

} // namespace lotro
