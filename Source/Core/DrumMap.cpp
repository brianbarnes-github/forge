#include "DrumMap.h"

#include <array>

namespace lotro
{

namespace
{
    struct Entry
    {
        int              gmNote;
        std::string_view abc;
    };

    constexpr std::array<Entry, 24> defaultMap = {{
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
        // Extended GM percussion (52-59). Folded onto closest-kin LOTRO slots
        // so unusual hits (crashes, cowbells, rides) don't silently drop.
        { 52, "c'" },   // China Cymbal     → Crash 1 slot
        { 53, "e'" },   // Ride Bell        → Ride 1 slot
        { 54, "g"  },   // Tambourine       → Open Hi-Hat slot
        { 55, "c'" },   // Splash Cymbal    → Crash 1 slot
        { 56, "e'" },   // Cowbell          → Ride 1 slot
        { 57, "c'" },   // Crash Cymbal 2   → Crash 1 slot
        { 59, "e'" },   // Ride Cymbal 2    → Ride 1 slot
        // GM 58 (Vibraslap) intentionally unmapped — rare and tonally unique.
    }};
}

std::optional<std::string_view> mapDrumPitch (int generalMidiNote) noexcept
{
    for (const auto& entry : defaultMap)
        if (entry.gmNote == generalMidiNote)
            return entry.abc;
    return std::nullopt;
}

} // namespace lotro
