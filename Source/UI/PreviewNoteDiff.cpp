#include "PreviewNoteDiff.h"

#include <algorithm>
#include <map>
#include <optional>
#include <tuple>
#include <utility>

namespace lotro
{

std::vector<PreviewNote> diffPreviewNotes (const PreviewResult& result)
{
    using Key = std::pair<int, int>;

    // Notes that share a provenance key (the two halves of a cut note, or
    // editor-created notes, all keyed -1/-1) are paired within their group:
    // first by identical start tick (occurrence order, sorted by tick then
    // pitch on both sides), then the leftovers in start order. The pipeline
    // can erase individual members, so rank alone would mis-pair survivors.
    std::map<Key, std::vector<std::pair<int, int>>> pipelinedByKey;   // (startTick, pitch)
    for (const auto& track : result.pipelined.tracks)
        for (const auto& note : track.notes)
            pipelinedByKey[{ note.sourceTrackIndex, note.sourceEventIndex }].push_back ({ note.startTick, note.pitch });
    for (auto& [key, list] : pipelinedByKey)
        std::sort (list.begin(), list.end());

    std::map<Key, std::vector<const Note*>> assembledByKey;
    for (const auto& track : result.assembled.tracks)
        for (const auto& note : track.notes)
            assembledByKey[{ note.sourceTrackIndex, note.sourceEventIndex }].push_back (&note);

    std::map<const Note*, std::optional<int>> pairedPitch;   // absent: Dropped
    for (auto& [key, notes] : assembledByKey)
    {
        std::stable_sort (notes.begin(), notes.end(), [] (const Note* l, const Note* r)
        {
            return std::tie (l->startTick, l->pitch) < std::tie (r->startTick, r->pitch);
        });

        const auto found = pipelinedByKey.find (key);
        if (found == pipelinedByKey.end())
            continue;
        const auto& candidates = found->second;

        std::vector<bool> taken (candidates.size(), false);
        std::vector<const Note*> unpaired;
        size_t from = 0;   // candidates are sorted, so exact-tick matches advance monotonically
        for (const auto* note : notes)
        {
            size_t i = from;
            while (i < candidates.size() && (taken[i] || candidates[i].first < note->startTick))
                ++i;
            if (i < candidates.size() && candidates[i].first == note->startTick)
            {
                taken[i] = true;
                pairedPitch[note] = candidates[i].second;
                from = i;
            }
            else
            {
                unpaired.push_back (note);
            }
        }

        size_t next = 0;
        for (const auto* note : unpaired)
        {
            while (next < candidates.size() && taken[next])
                ++next;
            if (next >= candidates.size())
                break;
            taken[next] = true;
            pairedPitch[note] = candidates[next].second;
        }
    }

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

            const auto paired = pairedPitch.find (&note);
            if (paired == pairedPitch.end())
            {
                pn.state = NoteState::Dropped;
            }
            else if (*paired->second == note.pitch)
            {
                pn.state = NoteState::Normal;
            }
            else
            {
                pn.state    = NoteState::WillFold;
                pn.postPitch = paired->second;
            }

            diff.push_back (pn);
        }
    }

    return diff;
}

} // namespace lotro
