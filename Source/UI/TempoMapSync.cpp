#include "UI/TempoMapSync.h"

namespace lotro
{

double bpmFromMicroseconds (std::uint32_t microsecondsPerQuarter)
{
    return 60.0 / ((double) microsecondsPerQuarter / 1000000.0);
}

DerivedMaps deriveMaps (const std::vector<RawMidiEvent>& events)
{
    DerivedMaps maps;
    for (const auto& event : events)
    {
        const auto& b = event.bytes;
        if (b.size() >= 5 && b[0] == 0xFF && b[1] == 0x51)
        {
            const std::uint32_t us = ((std::uint32_t) b[2] << 16) | ((std::uint32_t) b[3] << 8) | (std::uint32_t) b[4];
            if (us > 0)
                maps.tempo.push_back ({ event.tick, bpmFromMicroseconds (us) });
        }
        else if (b.size() >= 4 && b[0] == 0xFF && b[1] == 0x58 && b[3] <= 15)
        {
            maps.meter.push_back ({ event.tick, (int) b[2], 1 << b[3] });
        }
    }
    if (maps.tempo.empty())
        maps.tempo.push_back ({ 0, TempoMap::defaultBpm });
    if (maps.meter.empty())
        maps.meter.push_back ({});
    return maps;
}

} // namespace lotro
