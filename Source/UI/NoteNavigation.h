#pragma once

#include <cmath>
#include <cstdlib>
#include <vector>

// Picking a note from the MIDI editor's cursor (the time position and the
// active row). Header-only, no JUCE.
namespace lotro
{

struct NoteStart
{
    int startTick = 0;
    int pitch = 0;
};

// The note whose start is nearest `tick`, before or after it, on any pitch
// (Navigate > Select Note Nearest Cursor). Equal distances go to the pitch
// nearest `activePitch` (-1 for none), then to the higher pitch. Notes whose
// `excluded` flag is set are skipped (Add Note Nearest Cursor to Selection
// skips the selected ones); an empty `excluded` skips none. -1 when no note
// is left.
inline int nearestNoteIndex (const std::vector<NoteStart>& notes, double tick, int activePitch,
                             const std::vector<bool>& excluded = {})
{
    int best = -1;
    for (int i = 0; i < (int) notes.size(); ++i)
    {
        if ((std::size_t) i < excluded.size() && excluded[(std::size_t) i])
            continue;
        if (best < 0)
        {
            best = i;
            continue;
        }
        const auto& n = notes[(std::size_t) i];
        const auto& b = notes[(std::size_t) best];
        const double distance = std::abs (n.startTick - tick);
        const double bestDistance = std::abs (b.startTick - tick);
        if (distance != bestDistance)
        {
            if (distance < bestDistance)
                best = i;
            continue;
        }
        if (activePitch >= 0)
        {
            const int rowDistance = std::abs (n.pitch - activePitch);
            const int bestRowDistance = std::abs (b.pitch - activePitch);
            if (rowDistance != bestRowDistance)
            {
                if (rowDistance < bestRowDistance)
                    best = i;
                continue;
            }
        }
        if (n.pitch > b.pitch)
            best = i;
    }
    return best;
}

} // namespace lotro
