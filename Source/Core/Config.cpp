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

        for (int src : inst.sources)
        {
            if (src < 0)
                return where + "source index " + std::to_string (src) + " is negative";
            if (src >= midiTrackCount)
                return where + "source index " + std::to_string (src)
                     + " exceeds MIDI track count (" + std::to_string (midiTrackCount) + ")";
        }

        // volumePercent is an adjustment: 0 = no change, +10 = +10%, -20 = -20%.
        // <= -100 would mean silence or worse (negative volume), which makes
        // no musical sense and almost certainly indicates a config mistake.
        if (inst.volumePercent <= -100)
            return where + "'volumePercent' must be greater than -100 (got "
                 + std::to_string (inst.volumePercent) + ")";

        if (inst.drumMap.has_value() && parsed != LotroInstrument::Drums)
            return where + "'drumMap' is only valid on name == \"Drums\"";
    }

    return {};
}

} // namespace lotro
