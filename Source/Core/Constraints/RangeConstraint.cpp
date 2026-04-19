#include "RangeConstraint.h"

#include <algorithm>
#include <string>

namespace lotro
{

namespace
{
    // LOTRO ABC pitch envelope: C, (MIDI 48) through c' (MIDI 84).
    constexpr int abcLowMidi  = 48;
    constexpr int abcHighMidi = 84;

    bool transposeInto (int& pitch, int low, int high) noexcept
    {
        while (pitch < low  && pitch + 12 <= high) pitch += 12;
        while (pitch > high && pitch - 12 >= low)  pitch -= 12;
        return pitch >= low && pitch <= high;
    }
}

void applyRangeConstraint (Track& track, Diagnostics& diagnostics)
{
    if (track.instrument == LotroInstrument::Drums)
        return;

    const auto instrumentRange = rangeFor (track.instrument);

    const int effectiveLow  = std::max (instrumentRange.midiLow,  abcLowMidi);
    const int effectiveHigh = std::min (instrumentRange.midiHigh, abcHighMidi);

    auto readIndex  = track.notes.begin();
    auto writeIndex = track.notes.begin();

    for (; readIndex != track.notes.end(); ++readIndex)
    {
        Note note = *readIndex;
        note.pitch += track.transposeSemitones;

        bool ok = transposeInto (note.pitch, effectiveLow, effectiveHigh);
        if (! ok)
            ok = transposeInto (note.pitch, abcLowMidi, abcHighMidi);

        if (! ok)
        {
            Diagnostic d;
            d.severity = Severity::Warning;
            d.source   = "RangeConstraint";
            d.message  = "Dropped note — could not fit into ABC envelope C,..c' on '"
                       + track.name + "'";
            d.tick     = readIndex->startTick;
            d.pitch    = readIndex->pitch;
            diagnostics.push_back (std::move (d));
            continue;
        }

        *writeIndex = note;
        ++writeIndex;
    }

    track.notes.erase (writeIndex, track.notes.end());
}

} // namespace lotro
