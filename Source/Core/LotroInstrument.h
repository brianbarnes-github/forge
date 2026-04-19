#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace lotro
{

enum class LotroInstrument
{
    LuteOfAges,
    BasicLute,
    Harp,
    Theorbo,
    Flute,
    Clarinet,
    Horn,
    Pibgorn,
    Bassoon,
    Cowbell,
    MoorCowbell,
    StudentFiddle,
    Fiddle,
    BardicFiddle,
    LonelyMountainFiddle,
    SprightlyFiddle,
    BarndanceFiddle,
    TravellersTrusty,
    Drums,
};

struct InstrumentRange
{
    int midiLow  = 0;
    int midiHigh = 0;
};

InstrumentRange rangeFor (LotroInstrument instrument) noexcept;

std::string_view displayName (LotroInstrument instrument) noexcept;

// Returns empty string on success; otherwise a human-readable error message.
std::string parseName (std::string_view name, LotroInstrument& out);

std::vector<std::string_view> allInstrumentNames();

} // namespace lotro
