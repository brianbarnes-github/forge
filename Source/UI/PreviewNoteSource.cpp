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

        // A folding note's ghost is painted at postPitch, potentially many
        // rows away from prePitch — the range this drives (PianoRollComponent's
        // vertical fit) must cover the ghost's row too, or it lands off-canvas.
        if (n.postPitch.has_value())
        {
            lo = std::min (lo, *n.postPitch);
            hi = std::max (hi, *n.postPitch);
        }
    }

    return { lo, hi + 1 };
}

} // namespace lotro
