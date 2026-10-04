#include "UI/Playback/PlaybackSnapshot.h"

#include "UI/SongDocument.h"

#include <algorithm>
#include <map>

namespace lotro
{

PlaybackSnapshot::PlaybackSnapshot (TempoMap tempoIn, std::vector<PlaybackEvent> eventsIn,
                                    std::vector<VirtualChannel> channelsIn, std::vector<juce::int64> trackIdsIn)
    : tempoMap (std::move (tempoIn)), eventList (std::move (eventsIn)),
      channelList (std::move (channelsIn)), trackIdList (std::move (trackIdsIn)),
      byTrack (trackIdList.size()),
      audible (new std::atomic<std::uint8_t>[trackIdList.size()]),
      applied (trackIdList.size(), 1)
{
    for (size_t i = 0; i < trackIdList.size(); ++i)
        audible[i].store (1, std::memory_order_relaxed);
    for (size_t c = 0; c < channelList.size(); ++c)
        byTrack[(size_t) channelList[c].trackIndex].push_back ((int) c);
    for (const auto& e : eventList)
        if (e.kind == PlaybackEventKind::NoteOff)
            endTime = std::max (endTime, e.seconds);
}

int PlaybackSnapshot::trackIndexForId (juce::int64 trackId) const noexcept
{
    for (size_t i = 0; i < trackIdList.size(); ++i)
        if (trackIdList[i] == trackId)
            return (int) i;
    return -1;
}

size_t PlaybackSnapshot::firstEventAtOrAfter (double seconds) const noexcept
{
    return (size_t) (std::lower_bound (eventList.begin(), eventList.end(), seconds,
                                       [] (const PlaybackEvent& e, double s) { return e.seconds < s; })
                     - eventList.begin());
}

TempoMap tempoMapFromDocument (const SongDocument& doc)
{
    std::vector<TempoPoint> points;
    const auto node = doc.getTempoMapNode();
    for (int i = 0; i < node.getNumChildren(); ++i)
    {
        const auto change = node.getChild (i);
        points.push_back ({ (int) change.getProperty (SongIDs::tick, 0), (double) change.getProperty (SongIDs::bpm, 0.0) });
    }
    return TempoMap (std::move (points), (int) doc.getSourceMidiNode().getProperty (SongIDs::ticksPerQuarter, 480));
}

std::shared_ptr<PlaybackSnapshot> buildSnapshot (const SongDocument& doc)
{
    const TempoMap tempo = tempoMapFromDocument (doc);

    std::vector<PlaybackEvent> events;
    std::vector<VirtualChannel> channels;
    std::vector<juce::int64> trackIds;
    std::map<std::pair<int, int>, int> channelIndex;   // (trackIndex, midiChannel) -> virtual channel

    auto channelFor = [&] (int trackIndex, juce::int64 trackId, int midiChannel) -> int
    {
        const auto key = std::make_pair (trackIndex, midiChannel);
        if (auto it = channelIndex.find (key); it != channelIndex.end())
            return it->second;
        if ((int) channels.size() >= kMaxVirtualChannels)
            return -1;
        const int vch = (int) channels.size();
        channels.push_back ({ trackId, trackIndex, midiChannel, midiChannel == 10 });
        channelIndex.emplace (key, vch);
        return vch;
    };

    auto push = [&] (int tick, PlaybackEventKind kind, int vch, int trackIndex, int d1, int d2)
    {
        events.push_back ({ tick, tempo.ticksToSeconds ((double) tick), kind, vch, trackIndex, d1, d2 });
    };

    for (int i = 0; i < doc.getNumTracks(); ++i)
        trackIds.push_back ((juce::int64) doc.getTrack (i).getProperty (SongIDs::trackId, (juce::int64) -1));

    // `events` collects the real events through `push`; the per-channel setup
    // events are prepended afterwards so that a stable sort keeps them ahead of
    // a track's own tick-0 program change.
    for (int i = 0; i < doc.getNumTracks(); ++i)
    {
        const auto track = doc.getTrack (i);
        if ((bool) track.getProperty (SongIDs::isConductor, false))
            continue;
        const auto trackId = trackIds[(size_t) i];

        const auto notes = SongDocument::getNotesNode (track);
        for (int n = 0; n < notes.getNumChildren(); ++n)
        {
            const auto note = notes.getChild (n);
            const int midiChannel = juce::jlimit (1, 16, (int) note.getProperty (SongIDs::channel, 1));
            const int vch = channelFor (i, trackId, midiChannel);
            if (vch < 0)
                continue;
            const int pitch = juce::jlimit (0, 127, (int) note.getProperty (SongIDs::pitch, 60));
            const int velocity = juce::jlimit (1, 127, (int) note.getProperty (SongIDs::velocity, 64));
            const int start = std::max (0, (int) note.getProperty (SongIDs::startTick, 0));
            const int duration = std::max (1, (int) note.getProperty (SongIDs::durationTicks, 1));
            push (start, PlaybackEventKind::NoteOn, vch, i, pitch, velocity);
            push (start + duration, PlaybackEventKind::NoteOff, vch, i, pitch, 0);
        }

        const auto eventNodes = SongDocument::getEventsNode (track);
        for (int n = 0; n < eventNodes.getNumChildren(); ++n)
        {
            const auto node = eventNodes.getChild (n);
            const auto* bytes = node.getProperty (SongIDs::data).getBinaryData();
            if (bytes == nullptr || bytes->getSize() < 2)
                continue;
            const auto* b = static_cast<const std::uint8_t*> (bytes->getData());
            const int status = b[0];
            if (status < 0x80 || status >= 0xF0)
                continue;   // SysEx, meta, system-common: not played
            const int vch = channelFor (i, trackId, (status & 0x0F) + 1);
            if (vch < 0)
                continue;
            const int tick = std::max (0, (int) node.getProperty (SongIDs::tick, 0));
            switch (status & 0xF0)
            {
                case 0xB0: if (bytes->getSize() >= 3) push (tick, PlaybackEventKind::Control, vch, i, b[1] & 0x7F, b[2] & 0x7F); break;
                case 0xC0: push (tick, PlaybackEventKind::Program, vch, i, b[1] & 0x7F, channels[(size_t) vch].isDrum ? 1 : 0); break;
                case 0xE0: if (bytes->getSize() >= 3) push (tick, PlaybackEventKind::PitchBend, vch, i, (b[1] & 0x7F) | ((b[2] & 0x7F) << 7), 0); break;
                default: break;   // note on/off (carried by NOTE nodes), polyphonic/channel pressure
            }
        }
    }

    std::vector<PlaybackEvent> all;
    for (size_t c = 0; c < channels.size(); ++c)
        all.push_back ({ 0, 0.0, PlaybackEventKind::Program, (int) c, channels[c].trackIndex, 0, channels[c].isDrum ? 1 : 0 });
    all.insert (all.end(), events.begin(), events.end());
    std::stable_sort (all.begin(), all.end(), [] (const PlaybackEvent& a, const PlaybackEvent& b)
    {
        if (a.tick != b.tick) return a.tick < b.tick;
        return (int) a.kind < (int) b.kind;
    });

    return std::make_shared<PlaybackSnapshot> (tempo, std::move (all), std::move (channels), std::move (trackIds));
}

} // namespace lotro
