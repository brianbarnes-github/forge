#include "DurationConstraint.h"

namespace lotro
{

void applyDurationConstraint (Track& track, const Song& song, Diagnostics& diagnostics)
{
    (void) song;
    (void) diagnostics;

    std::vector<Note> rebuilt;
    rebuilt.reserve (track.notes.size());

    for (const auto& source : track.notes)
        if (source.durationTicks > 0)
            rebuilt.push_back (source);

    track.notes = std::move (rebuilt);
}

} // namespace lotro
