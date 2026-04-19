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
        const double localBpm = localBpmAt (song.tempoMap, note.startTick, mainBpm);
        if (localBpm <= 0.0)
            continue;

        const double scale = mainBpm / localBpm;
        if (std::abs (scale - 1.0) < 1e-9)
            continue;

        const int rescaled = (int) std::lround ((double) note.durationTicks * scale);
        if (rescaled <= 0)
        {
            Diagnostic d;
            d.severity = Severity::Warning;
            d.source   = "TempoCollapse";
            d.message  = "Tempo scaling collapsed note to 0 duration on '" + track.name + "'";
            d.tick     = note.startTick;
            d.pitch    = note.pitch;
            diagnostics.push_back (std::move (d));
            note.durationTicks = 1;
        }
        else
        {
            note.durationTicks = rescaled;
        }
    }
}

} // namespace lotro
