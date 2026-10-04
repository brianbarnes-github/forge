#include "UI/Playback/PlaybackEngine.h"

#include <cmath>

namespace lotro
{

void PlaybackEngine::prepare (double sampleRateIn, int maxBlockSize)
{
    sampleRate = sampleRateIn > 0.0 ? sampleRateIn : 44100.0;
    sink.prepare (sampleRate, maxBlockSize);
}

void PlaybackEngine::chase (const PlaybackSnapshot& snapshot, double positionSeconds) noexcept
{
    for (const auto& e : snapshot.events())
    {
        if (e.seconds >= positionSeconds)
            break;
        if (e.kind == PlaybackEventKind::Program || e.kind == PlaybackEventKind::Control
            || e.kind == PlaybackEventKind::PitchBend)
            sink.handle (e);
    }
}

void PlaybackEngine::releaseNewlyMutedTracks (const PlaybackSnapshot& snapshot) noexcept
{
    for (int t = 0; t < snapshot.numTracks(); ++t)
    {
        const bool audible = snapshot.isAudible (t);
        if (! audible && snapshot.appliedAudible (t))
            for (int vch : snapshot.channelsOfTrack (t))
                sink.releaseChannel (vch);
        snapshot.setAppliedAudible (t, audible);
    }
}

void PlaybackEngine::renderBlock (juce::AudioBuffer<float>& buffer, int startSample, int numSamples) noexcept
{
    buffer.clear (startSample, numSamples);
    if (buffer.getNumChannels() == 0 || numSamples <= 0)
        return;

    float* left = buffer.getWritePointer (0, startSample);
    float* right = buffer.getNumChannels() > 1 ? buffer.getWritePointer (1, startSample) : left;

    bool swapped = false;
    PlaybackSnapshot* snapshot = snapshots.acquire (swapped);
    const bool sinkReplaced = sink.beginBlock();
    swapped = swapped || sinkReplaced;

    const bool playing = snapshot != nullptr && transport.isPlaying();
    const unsigned seekGeneration = transport.getSeekGeneration();
    const bool seeked = seekGeneration != seenSeekGeneration;
    seenSeekGeneration = seekGeneration;

    if (swapped || seeked || (! playing && wasPlaying))
        sink.releaseAll();
    if (snapshot != nullptr)
        releaseNewlyMutedTracks (*snapshot);

    if (! playing)
    {
        wasPlaying = false;
        sink.render (left, right, numSamples);   // lets release tails ring out
        return;
    }

    const double position = transport.getPositionSeconds();
    if (swapped || seeked || ! wasPlaying)
    {
        chase (*snapshot, position);
        nextEvent = snapshot->firstEventAtOrAfter (position);
    }
    wasPlaying = true;

    const double blockSeconds = (double) numSamples / sampleRate;
    const auto& events = snapshot->events();
    int rendered = 0;
    while (nextEvent < events.size() && events[nextEvent].seconds < position + blockSeconds)
    {
        const auto& e = events[nextEvent];
        const int offset = juce::jlimit (rendered, numSamples, (int) std::llround ((e.seconds - position) * sampleRate));
        if (offset > rendered)
        {
            sink.render (left + rendered, right + rendered, offset - rendered);
            rendered = offset;
        }
        if (e.kind != PlaybackEventKind::NoteOn || snapshot->isAudible (e.trackIndex))
            sink.handle (e);
        ++nextEvent;
    }
    if (rendered < numSamples)
        sink.render (left + rendered, right + rendered, numSamples - rendered);

    transport.advance (blockSeconds, snapshot->endSeconds());
}

} // namespace lotro
