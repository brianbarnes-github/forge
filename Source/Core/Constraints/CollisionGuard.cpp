#include "CollisionGuard.h"

#include <algorithm>
#include <map>
#include <string>

namespace lotro
{

void applyCollisionGuard (Track& track, Diagnostics& diagnostics)
{
    std::map<int, std::vector<Note*>> byPitch;
    for (auto& note : track.notes)
        byPitch[note.pitch].push_back (&note);

    std::vector<Note*> toDrop;

    for (auto& [pitch, notes] : byPitch)
    {
        std::sort (notes.begin(), notes.end(),
                   [] (const Note* a, const Note* b) { return a->startTick < b->startTick; });

        for (size_t i = 1; i < notes.size(); ++i)
        {
            auto* prev = notes[i - 1];
            auto* curr = notes[i];
            const int prevEnd = prev->startTick + prev->durationTicks;
            if (prevEnd <= curr->startTick)
                continue;

            const int trimmedDuration = curr->startTick - prev->startTick - 1;
            if (trimmedDuration <= 0)
            {
                Diagnostic d;
                d.severity = Severity::Warning;
                d.source   = "CollisionGuard";
                d.message  = "Dropped same-pitch collision note on '" + track.name + "'";
                d.tick     = prev->startTick;
                d.pitch    = pitch;
                diagnostics.push_back (std::move (d));
                toDrop.push_back (prev);
            }
            else
            {
                prev->durationTicks = trimmedDuration;
            }
        }
    }

    if (toDrop.empty())
        return;

    track.notes.erase (
        std::remove_if (track.notes.begin(), track.notes.end(),
                        [&toDrop] (const Note& n)
                        {
                            return std::any_of (toDrop.begin(), toDrop.end(),
                                                [&n] (const Note* p) { return p == &n; });
                        }),
        track.notes.end());
}

} // namespace lotro
