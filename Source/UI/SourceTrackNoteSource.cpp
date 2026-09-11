#include "SourceTrackNoteSource.h"

#include <algorithm>
#include <limits>

namespace lotro
{

SourceTrackNoteSource::SourceTrackNoteSource (juce::ValueTree trackNode)
    : track (trackNode)
{
    jassert (track.hasType (SongIDs::MIDI_TRACK));
}

int SourceTrackNoteSource::getNumNotes() const
{
    return track.getNumChildren();
}

PianoRollNote SourceTrackNoteSource::getNote (int index) const
{
    auto noteNode = track.getChild (index);

    PianoRollNote note;
    note.pitch         = (int) noteNode.getProperty (SongIDs::pitch);
    note.startTick     = (int) noteNode.getProperty (SongIDs::startTick);
    note.durationTicks = (int) noteNode.getProperty (SongIDs::durationTicks);
    // Track colour, not per-note — read verbatim per A-R5, never recomputed.
    note.colourArgb    = (juce::uint32) (int) track.getProperty (SongIDs::colorArgb);
    return note;
}

juce::Range<int> SourceTrackNoteSource::getTickRange() const
{
    const int numNotes = getNumNotes();
    if (numNotes == 0)
        return { 0, 0 };

    int lo = std::numeric_limits<int>::max();
    int hi = std::numeric_limits<int>::min();

    for (int i = 0; i < numNotes; ++i)
    {
        auto note = getNote (i);
        lo = std::min (lo, note.startTick);
        hi = std::max (hi, note.startTick + note.durationTicks);
    }

    return { lo, hi };
}

juce::Range<int> SourceTrackNoteSource::getPitchRange() const
{
    const int numNotes = getNumNotes();
    if (numNotes == 0)
        return { 0, 0 };

    int lo = std::numeric_limits<int>::max();
    int hi = std::numeric_limits<int>::min();

    for (int i = 0; i < numNotes; ++i)
    {
        auto note = getNote (i);
        lo = std::min (lo, note.pitch);
        hi = std::max (hi, note.pitch);
    }

    // Exclusive upper bound (highest pitch + 1), matching the tick range's
    // half-open convention.
    return { lo, hi + 1 };
}

} // namespace lotro
