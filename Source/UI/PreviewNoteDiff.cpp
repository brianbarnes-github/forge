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
    // by identical tick and pitch, then identical tick, then the leftovers in
    // start order (both sides sorted by tick, then pitch). The pipeline
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

        std::vector<const Note*> pendingNotes (notes.begin(), notes.end());
        std::vector<size_t> pendingCandidates (candidates.size());
        for (size_t i = 0; i < pendingCandidates.size(); ++i)
            pendingCandidates[i] = i;

        // Pairs equal-keyed entries one-to-one (both lists are sorted by
        // (tick, pitch), so keys ascend) and keeps what is left of each.
        const auto pairBy = [&] (auto noteKey, auto candidateKey)
        {
            std::vector<const Note*> restNotes;
            std::vector<size_t> restCandidates;
            size_t i = 0, j = 0;
            while (i < pendingNotes.size() && j < pendingCandidates.size())
            {
                const auto nk = noteKey (*pendingNotes[i]);
                const auto ck = candidateKey (candidates[pendingCandidates[j]]);
                if (nk == ck)
                {
                    pairedPitch[pendingNotes[i++]] = candidates[pendingCandidates[j++]].second;
                }
                else if (nk < ck)
                {
                    restNotes.push_back (pendingNotes[i++]);
                }
                else
                {
                    restCandidates.push_back (pendingCandidates[j++]);
                }
            }
            restNotes.insert (restNotes.end(), pendingNotes.begin() + (std::ptrdiff_t) i, pendingNotes.end());
            restCandidates.insert (restCandidates.end(), pendingCandidates.begin() + (std::ptrdiff_t) j, pendingCandidates.end());
            pendingNotes      = std::move (restNotes);
            pendingCandidates = std::move (restCandidates);
        };

        // Identical tick and pitch, then identical tick (a fold or a cap
        // drop changes pitch, not tick), then whatever is left in order.
        pairBy ([] (const Note& n) { return std::pair { n.startTick, n.pitch }; },
                [] (const std::pair<int, int>& c) { return c; });
        pairBy ([] (const Note& n) { return n.startTick; },
                [] (const std::pair<int, int>& c) { return c.first; });

        for (size_t k = 0; k < pendingNotes.size() && k < pendingCandidates.size(); ++k)
            pairedPitch[pendingNotes[k]] = candidates[pendingCandidates[k]].second;
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
