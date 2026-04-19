#include "AutoInstrument.h"

#include <array>

namespace lotro
{

namespace
{
    // Tiebreak order when multiple instruments have equal coverage.
    // Top entries are the "natural melody" instruments — most users expect
    // a plain conversion to pick a lute first. Theorbo moves up when the
    // range is bass-heavy (it covers 24–60 natively, which no other
    // instrument can). Flute / Clarinet win when the range is high.
    constexpr std::array<LotroInstrument, 18> preferenceOrder = {{
        LotroInstrument::LuteOfAges,
        LotroInstrument::Harp,
        LotroInstrument::BasicLute,
        LotroInstrument::Bassoon,
        LotroInstrument::Theorbo,
        LotroInstrument::Clarinet,
        LotroInstrument::Horn,
        LotroInstrument::Pibgorn,
        LotroInstrument::Flute,
        LotroInstrument::Fiddle,
        LotroInstrument::StudentFiddle,
        LotroInstrument::BardicFiddle,
        LotroInstrument::LonelyMountainFiddle,
        LotroInstrument::SprightlyFiddle,
        LotroInstrument::BarndanceFiddle,
        LotroInstrument::TravellersTrusty,
        LotroInstrument::Cowbell,
        LotroInstrument::MoorCowbell,
    }};
}

LotroInstrument pickInstrumentForTrack (const Track& track)
{
    if (track.instrument == LotroInstrument::Drums)
        return LotroInstrument::Drums;

    if (track.notes.empty())
        return track.instrument;

    int             bestScore      = -1;
    LotroInstrument bestInstrument = track.instrument;

    for (auto candidate : preferenceOrder)
    {
        const auto range = rangeFor (candidate);
        if (range.midiLow == 0 && range.midiHigh == 0)
            continue;   // Drums or otherwise unscoreable

        int score = 0;
        for (const auto& note : track.notes)
            if (note.pitch >= range.midiLow && note.pitch <= range.midiHigh)
                ++score;

        if (score > bestScore)
        {
            bestScore      = score;
            bestInstrument = candidate;
        }
    }

    return bestInstrument;
}

} // namespace lotro
