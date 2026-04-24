#include "LotroInstrument.h"

#include <array>
#include <string>

namespace lotro
{

namespace
{
    struct InstrumentInfo
    {
        LotroInstrument  id;
        std::string_view name;
        int              midiLow;
        int              midiHigh;
    };

    constexpr std::array<InstrumentInfo, 19> instruments = {{
        { LotroInstrument::LuteOfAges,           "LuteOfAges",           36, 72 },
        { LotroInstrument::BasicLute,            "BasicLute",            36, 72 },
        { LotroInstrument::Harp,                 "Harp",                 36, 72 },
        { LotroInstrument::Theorbo,              "Theorbo",              24, 60 },
        { LotroInstrument::Flute,                "Flute",                60, 96 },
        { LotroInstrument::Clarinet,             "Clarinet",             48, 84 },
        { LotroInstrument::Horn,                 "Horn",                 48, 84 },
        { LotroInstrument::Pibgorn,              "Pibgorn",              48, 84 },
        { LotroInstrument::Bassoon,              "Bassoon",              36, 72 },
        { LotroInstrument::Cowbell,              "Cowbell",              36, 72 },
        { LotroInstrument::MoorCowbell,          "MoorCowbell",          36, 72 },
        { LotroInstrument::StudentFiddle,        "StudentFiddle",        60, 96 },
        { LotroInstrument::Fiddle,               "Fiddle",               60, 96 },
        { LotroInstrument::BardicFiddle,         "BardicFiddle",         60, 96 },
        { LotroInstrument::LonelyMountainFiddle, "LonelyMountainFiddle", 60, 96 },
        { LotroInstrument::SprightlyFiddle,      "SprightlyFiddle",      60, 96 },
        { LotroInstrument::BarndanceFiddle,      "BarndanceFiddle",      60, 96 },
        { LotroInstrument::TravellersTrusty,     "TravellersTrusty",     60, 96 },
        { LotroInstrument::Drums,                "Drums",                 0,  0 },
    }};

    const InstrumentInfo& lookup (LotroInstrument id) noexcept
    {
        for (const auto& info : instruments)
            if (info.id == id)
                return info;
        return instruments.front();
    }
}

InstrumentRange rangeFor (LotroInstrument instrument) noexcept
{
    const auto& info = lookup (instrument);
    return { info.midiLow, info.midiHigh };
}

std::string_view displayName (LotroInstrument instrument) noexcept
{
    return lookup (instrument).name;
}

std::string parseName (std::string_view name, LotroInstrument& out)
{
    for (const auto& info : instruments)
    {
        if (name == info.name)
        {
            out = info.id;
            return {};
        }
    }

    std::string msg = "Unknown instrument: ";
    msg.append (name);
    msg += ". Valid names:";
    for (const auto& info : instruments)
    {
        msg += ' ';
        msg.append (info.name);
    }
    return msg;
}

std::vector<std::string_view> allInstrumentNames()
{
    std::vector<std::string_view> names;
    names.reserve (instruments.size());
    for (const auto& info : instruments)
        names.push_back (info.name);
    return names;
}

} // namespace lotro
