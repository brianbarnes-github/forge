#include "RangeConstraint.h"

#include <string>

namespace lotro
{

namespace
{
    // Octave-fold `pitch` into [low, high] by whole-octave shifts, preserving
    // pitch class. Returns true if it succeeded. For any 36-semitone (3-octave)
    // range — which every non-drum LOTRO instrument has — this always succeeds
    // for any integer pitch.
    bool foldInto (int& pitch, int low, int high) noexcept
    {
        while (pitch < low  && pitch + 12 <= high) pitch += 12;
        while (pitch > high && pitch - 12 >= low)  pitch -= 12;
        return pitch >= low && pitch <= high;
    }
}

// Every non-drum LOTRO instrument can play a 36-semitone range; different
// instruments start at different base octaves (see LotroInstrument.cpp and
// docs/Picture1.jpg). Fold source notes into that per-instrument native MIDI
// range, preserving pitch class. No drops — the range is wide enough that
// the fold always converges.
void applyRangeConstraint (Track& track, Diagnostics& diagnostics)
{
    if (track.instrument == LotroInstrument::Drums)
        return;

    const auto instrumentRange = rangeFor (track.instrument);
    const int  effectiveLow    = instrumentRange.midiLow;
    const int  effectiveHigh   = instrumentRange.midiHigh;

    auto readIndex  = track.notes.begin();
    auto writeIndex = track.notes.begin();

    for (; readIndex != track.notes.end(); ++readIndex)
    {
        Note note = *readIndex;
        note.pitch += track.transposeSemitones;

        if (! foldInto (note.pitch, effectiveLow, effectiveHigh))
        {
            // Defensive: should never happen for a 36-semitone range.
            Diagnostic d;
            d.severity         = Severity::Warning;
            d.source           = "RangeConstraint";
            d.message          = "Dropped note — could not fold into instrument range on '"
                               + track.name + "'";
            d.tick             = readIndex->startTick;
            d.pitch            = readIndex->pitch;
            d.sourceTrackIndex = readIndex->sourceTrackIndex;
            d.sourceEventIndex = readIndex->sourceEventIndex;
            diagnostics.push_back (std::move (d));
            continue;
        }

        *writeIndex = note;
        ++writeIndex;
    }

    track.notes.erase (writeIndex, track.notes.end());
}

} // namespace lotro
