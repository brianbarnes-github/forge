#include "Config.h"
#include "LotroInstrument.h"

#include <set>
#include <string>

namespace lotro
{

std::string validateConfig (const Config& config, int midiTrackCount)
{
    if (config.input.empty())
        return "config: 'input' path is required";

    if (config.instruments.empty())
        return "config: 'instruments' array must have at least one entry";

    std::set<int> seenX;

    for (size_t idx = 0; idx < config.instruments.size(); ++idx)
    {
        const auto& inst = config.instruments[idx];
        const auto where = "instruments[" + std::to_string (idx) + "]: ";

        if (inst.x < 1)
            return where + "'x' must be >= 1 (got " + std::to_string (inst.x) + ")";

        if (! seenX.insert (inst.x).second)
            return where + "duplicate 'x' value " + std::to_string (inst.x);

        if (inst.name.empty())
            return where + "'name' is required";

        LotroInstrument parsed;
        const auto parseError = parseName (inst.name, parsed);
        if (! parseError.empty())
            return where + parseError;

        if (inst.sources.empty())
            return where + "'sources' array must have at least one entry";

        std::set<int> seenMidi;
        for (size_t si = 0; si < inst.sources.size(); ++si)
        {
            const auto& src = inst.sources[si];
            const auto sWhere = where + "sources[" + std::to_string (si) + "]: ";

            if (src.midiTrackIndex < 0)
                return sWhere + "'midiTrack' must be >= 0 (got "
                     + std::to_string (src.midiTrackIndex) + ")";
            if (src.midiTrackIndex >= midiTrackCount)
                return sWhere + "'midiTrack' index " + std::to_string (src.midiTrackIndex)
                     + " exceeds MIDI track count (" + std::to_string (midiTrackCount) + ")";
            if (! seenMidi.insert (src.midiTrackIndex).second)
                return sWhere + "duplicate MIDI track index " + std::to_string (src.midiTrackIndex)
                     + " in this instrument";

            // volumePercent is an adjustment: 0 = no change, +N louder, -N quieter.
            // <= -100 would silence or invert the velocity.
            if (src.volumePercent <= -100)
                return sWhere + "'volumePercent' must be greater than -100 (got "
                     + std::to_string (src.volumePercent) + ")";
        }

        if (inst.drumMap.has_value() && parsed != LotroInstrument::Drums)
            return where + "'drumMap' is only valid on name == \"Drums\"";
    }

    return {};
}

} // namespace lotro
