#include "PreviewNoteSource.h"
#include "SongsmithColours.h"

#include <algorithm>
#include <limits>

namespace lotro
{

PreviewNoteSource::PreviewNoteSource (std::vector<PreviewNote> notesIn)
    : notes (std::move (notesIn))
{
}

int PreviewNoteSource::getNumNotes() const
{
    return (int) notes.size();
}

PianoRollNote PreviewNoteSource::getNote (int index) const
{
    const auto& n = notes[(size_t) index];

    PianoRollNote note;
    note.pitch            = n.prePitch;
    note.startTick        = n.startTick;
    note.durationTicks    = n.durationTicks;
    note.sourceTrackIndex = n.sourceTrackIndex;
    note.sourceEventIndex = n.sourceEventIndex;
    note.state            = n.state;
    note.postPitch        = n.postPitch;
    note.colourArgb       = n.state == NoteState::Normal
                                 ? SongsmithColours::previewNoteNormal
                                 : SongsmithColours::outOfRangeFill;
    return note;
}

juce::Range<int> PreviewNoteSource::getTickRange() const
{
    if (notes.empty())
        return { 0, 0 };

    int lo = std::numeric_limits<int>::max();
    int hi = std::numeric_limits<int>::min();

    for (const auto& n : notes)
    {
        lo = std::min (lo, n.startTick);
        hi = std::max (hi, n.startTick + n.durationTicks);
    }

    return { lo, hi };
}

juce::Range<int> PreviewNoteSource::getPitchRange() const
{
    if (notes.empty())
        return { 0, 0 };

    int lo = std::numeric_limits<int>::max();
    int hi = std::numeric_limits<int>::min();

    for (const auto& n : notes)
    {
        lo = std::min (lo, n.prePitch);
        hi = std::max (hi, n.prePitch);
    }

    return { lo, hi + 1 };
}

} // namespace lotro
