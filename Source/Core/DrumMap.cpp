#include "DrumMap.h"

#include <array>
#include <string_view>

namespace lotro
{

namespace
{
    struct DefaultEntry
    {
        int              gmNote;
        std::string_view abc;
    };

    constexpr std::array<DefaultEntry, 24> specDefaults = {{
        { 35, "C"  },   // Bass Drum 1
        { 36, "D"  },   // Bass Drum 2
        { 37, "E"  },   // Side Stick
        { 38, "F"  },   // Snare
        { 39, "G"  },   // Hand Clap
        { 40, "A"  },   // Snare 2
        { 41, "B"  },   // Low Floor Tom
        { 42, "c"  },   // Closed Hi-Hat
        { 43, "d"  },   // High Floor Tom
        { 44, "e"  },   // Pedal Hi-Hat
        { 45, "f"  },   // Low Tom
        { 46, "g"  },   // Open Hi-Hat
        { 47, "a"  },   // Low-Mid Tom
        { 48, "b"  },   // Hi-Mid Tom
        { 49, "c'" },   // Crash Cymbal 1
        { 50, "d'" },   // High Tom
        { 51, "e'" },   // Ride Cymbal 1
        // Extended GM percussion folded onto closest-kin LOTRO slots so
        // unusual hits (crashes, cowbells, rides) don't silently drop.
        { 52, "c'" },   // China Cymbal      → Crash 1
        { 53, "e'" },   // Ride Bell         → Ride 1
        { 54, "g"  },   // Tambourine        → Open Hi-Hat
        { 55, "c'" },   // Splash Cymbal     → Crash 1
        { 56, "e'" },   // Cowbell           → Ride 1
        { 57, "c'" },   // Crash Cymbal 2    → Crash 1
        { 59, "e'" },   // Ride Cymbal 2     → Ride 1
        // GM 58 (Vibraslap) is intentionally unmapped — rare and tonally unique.
    }};
}

void DrumMap::set (int gmNote, std::string_view abc)
{
    entries[gmNote] = std::string (abc);
}

void DrumMap::clear()
{
    entries.clear();
}

std::optional<std::string_view> DrumMap::lookup (int gmNote) const
{
    const auto it = entries.find (gmNote);
    if (it == entries.end())
        return std::nullopt;
    return std::string_view (it->second);
}

DrumMap defaultDrumMap()
{
    DrumMap map;
    for (const auto& e : specDefaults)
        map.set (e.gmNote, e.abc);
    return map;
}

std::optional<std::string_view> mapDrumPitch (int generalMidiNote) noexcept
{
    // Process-wide default. Initialised once on first call; never mutated,
    // so safe to share across threads.
    static const DrumMap defaults = defaultDrumMap();
    return defaults.lookup (generalMidiNote);
}

} // namespace lotro
