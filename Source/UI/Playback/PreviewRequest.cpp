#include "UI/Playback/PreviewRequest.h"

#include "UI/SongDocument.h"

#include <algorithm>

namespace lotro
{

std::optional<PreviewRequest> makePreviewRequest (juce::int64 trackId, const std::vector<juce::ValueTree>& notes)
{
    if (notes.empty() || (int) notes.size() > kMaxPreviewNotes)
        return std::nullopt;

    PreviewRequest request;
    for (const auto& note : notes)
    {
        // Read as buildSnapshot reads a NOTE, so the preview matches playback.
        PreviewNote p;
        p.trackId = trackId;
        p.midiChannel = juce::jlimit (1, 16, (int) note.getProperty (SongIDs::channel, 1));
        p.pitch = juce::jlimit (0, 127, (int) note.getProperty (SongIDs::pitch, 60));
        p.velocity = juce::jlimit (1, 127, (int) note.getProperty (SongIDs::velocity, 64));
        p.startTick = std::max (0, (int) note.getProperty (SongIDs::startTick, 0));
        p.endTick = p.startTick + std::max (1, (int) note.getProperty (SongIDs::durationTicks, 1));

        if (request.count > 0 && p.startTick != request.notes[0].startTick)
            return std::nullopt;
        request.notes[(size_t) request.count++] = p;
    }
    return request;
}

} // namespace lotro
