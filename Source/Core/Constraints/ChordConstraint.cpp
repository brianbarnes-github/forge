#include "ChordConstraint.h"

#include <algorithm>
#include <map>
#include <string>

namespace lotro
{

void applyChordConstraint (Track& track, Diagnostics& diagnostics)
{
    std::map<int, std::vector<Note>> grouped;
    for (const auto& note : track.notes)
        grouped[note.startTick].push_back (note);

    std::vector<Note> rebuilt;
    rebuilt.reserve (track.notes.size());

    for (auto& [startTick, chord] : grouped)
    {
        if (chord.size() > maxChordSize)
        {
            std::stable_sort (chord.begin(), chord.end(),
                              [] (const Note& a, const Note& b)
                              {
                                  if (a.velocity != b.velocity) return a.velocity > b.velocity;
                                  return a.pitch > b.pitch;
                              });

            Diagnostic d;
            d.severity = Severity::Warning;
            d.source   = "ChordConstraint";
            d.message  = "Trimmed chord from " + std::to_string (chord.size())
                       + " to " + std::to_string (maxChordSize)
                       + " notes on '" + track.name + "'";
            d.tick     = startTick;
            diagnostics.push_back (std::move (d));

            chord.resize (maxChordSize);
        }

        std::stable_sort (chord.begin(), chord.end(),
                          [] (const Note& a, const Note& b) { return a.pitch < b.pitch; });

        for (const auto& n : chord)
            rebuilt.push_back (n);
    }

    track.notes = std::move (rebuilt);
}

} // namespace lotro
