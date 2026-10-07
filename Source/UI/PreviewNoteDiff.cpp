#include "PreviewNoteDiff.h"

#include <algorithm>
#include <map>
#include <utility>

namespace lotro
{

std::vector<PreviewNote> diffPreviewNotes (const PreviewResult& result)
{
    using Key = std::pair<int, int>;

    // Notes that share a provenance key (the two halves of a cut note, or
    // editor-created notes, all keyed -1/-1) are matched in start-tick order.
    std::map<Key, std::vector<std::pair<int, int>>> pipelinedByKey;   // (startTick, pitch)
    for (const auto& track : result.pipelined.tracks)
        for (const auto& note : track.notes)
            pipelinedByKey[{ note.sourceTrackIndex, note.sourceEventIndex }].push_back ({ note.startTick, note.pitch });
    for (auto& [key, list] : pipelinedByKey)
        std::sort (list.begin(), list.end());

    std::map<Key, std::vector<int>> assembledStarts;
    for (const auto& track : result.assembled.tracks)
        for (const auto& note : track.notes)
            assembledStarts[{ note.sourceTrackIndex, note.sourceEventIndex }].push_back (note.startTick);
    for (auto& [key, list] : assembledStarts)
        std::sort (list.begin(), list.end());

    std::vector<PreviewNote> diff;

    for (const auto& track : result.assembled.tracks)
    {
        for (const auto& note : track.notes)
        {
            PreviewNote pn;
            pn.sourceTrackIndex = note.sourceTrackIndex;
            pn.sourceEventIndex = note.sourceEventIndex;
            pn.prePitch         = note.pitch;
            pn.startTick        = note.startTick;
            pn.durationTicks    = note.durationTicks;
            pn.velocity         = note.velocity;

            const Key key { note.sourceTrackIndex, note.sourceEventIndex };
            const auto& starts = assembledStarts.at (key);
            const auto rank = (size_t) (std::lower_bound (starts.begin(), starts.end(), note.startTick) - starts.begin());

            const auto it = pipelinedByKey.find (key);
            if (it == pipelinedByKey.end() || rank >= it->second.size())
            {
                pn.state = NoteState::Dropped;
            }
            else if (it->second[rank].second == note.pitch)
            {
                pn.state = NoteState::Normal;
            }
            else
            {
                pn.state    = NoteState::WillFold;
                pn.postPitch = it->second[rank].second;
            }

            diff.push_back (pn);
        }
    }

    return diff;
}

} // namespace lotro
