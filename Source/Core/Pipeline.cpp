#include "Pipeline.h"

#include "AutoInstrument.h"
#include "Constraints/ChordConstraint.h"
#include "Constraints/CollisionGuard.h"
#include "Constraints/DurationConstraint.h"
#include "Constraints/DynamicMapper.h"
#include "Constraints/RangeConstraint.h"
#include "Constraints/TempoCollapse.h"

namespace lotro
{

Config synthesiseConfig (const Song&                                  raw,
                         const std::string&                            inputPath,
                         const std::string&                            outputPath,
                         std::optional<double>                         tempo,
                         int                                           transpose,
                         const std::map<int, LotroInstrument>&         instrumentOverrides)
{
    Config cfg;
    cfg.input = inputPath;
    if (! outputPath.empty())
        cfg.output = outputPath;
    if (tempo.has_value())
        cfg.tempo = *tempo;
    cfg.transpose = transpose;

    for (size_t i = 0; i < raw.tracks.size(); ++i)
    {
        ConfigInstrument inst;
        inst.x       = (int) (i + 1);
        inst.sources = { (int) i };

        const auto picked = pickInstrumentForTrack (raw.tracks[i]);
        inst.name = std::string (displayName (picked));

        const auto overrideIt = instrumentOverrides.find ((int) i);
        if (overrideIt != instrumentOverrides.end())
            inst.name = std::string (displayName (overrideIt->second));

        cfg.instruments.push_back (inst);
    }
    return cfg;
}

void runPipeline (Song& song, Diagnostics& diagnostics)
{
    for (size_t trackIdx = 0; trackIdx < song.tracks.size(); ++trackIdx)
    {
        auto& track = song.tracks[trackIdx];
        if (! track.enabled) continue;

        const size_t before = diagnostics.size();
        applyRangeConstraint    (track, diagnostics);
        applyChordConstraint    (track, diagnostics);
        applyDurationConstraint (track, song, diagnostics);
        applyTempoCollapse      (track, song, diagnostics);
        applyCollisionGuard     (track, diagnostics);
        applyDynamicMapper      (track, diagnostics);

        for (size_t i = before; i < diagnostics.size(); ++i)
            if (diagnostics[i].trackIndex < 0)
                diagnostics[i].trackIndex = (int) trackIdx;
    }

    applyTempoCollapseToSongMaps (song);
}

} // namespace lotro
