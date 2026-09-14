#include "PreviewNoteDiff.h"

#include <map>
#include <utility>

namespace lotro
{

std::vector<PreviewNote> diffPreviewNotes (const PreviewResult& result)
{
    std::map<std::pair<int, int>, int> pipelinedPitchByKey;
    for (const auto& track : result.pipelined.tracks)
        for (const auto& note : track.notes)
            pipelinedPitchByKey[{ note.sourceTrackIndex, note.sourceEventIndex }] = note.pitch;

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

            auto it = pipelinedPitchByKey.find ({ note.sourceTrackIndex, note.sourceEventIndex });
            if (it == pipelinedPitchByKey.end())
            {
                pn.state = NoteState::Dropped;
            }
            else if (it->second == note.pitch)
            {
                pn.state = NoteState::Normal;
            }
            else
            {
                pn.state    = NoteState::WillFold;
                pn.postPitch = it->second;
            }

            diff.push_back (pn);
        }
    }

    return diff;
}

} // namespace lotro
