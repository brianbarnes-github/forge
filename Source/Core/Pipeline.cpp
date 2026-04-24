#include "Pipeline.h"

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
        inst.sources = { ConfigSource{ (int) i, 0, 0 } };

        // No auto-picking per-track: every non-drum track defaults to the
        // same fixed instrument and the user is expected to pick the real
        // one. Channel-10 tracks imported as Drums keep that identity
        // because it's what the MIDI itself declares (General MIDI
        // channel-10 convention), not a converter decision.
        const LotroInstrument defaulted =
            (raw.tracks[i].instrument == LotroInstrument::Drums)
                ? LotroInstrument::Drums
                : LotroInstrument::LuteOfAges;
        inst.name = std::string (displayName (defaulted));

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
