#include "UI/Playback/PlaybackEngine.h"

#include <algorithm>
#include <cmath>
#include <functional>

namespace lotro
{

namespace
{
    // Exact comparison on purpose: the playhead is stored and loaded unmodified,
    // so an unchanged position is bit-identical. Going through std::equal_to
    // keeps -Wfloat-equal quiet without a pragma.
    bool samePosition (double a, double b) noexcept { return std::equal_to<double>() (a, b); }
}

void PlaybackEngine::prepare (double sampleRateIn, int maxBlockSize)
{
    sampleRate = sampleRateIn > 0.0 ? sampleRateIn : 44100.0;
    sink.prepare (sampleRate, maxBlockSize);
}

void PlaybackEngine::chase (const PlaybackSnapshot& snapshot, double positionSeconds) noexcept
{
    // The synth keeps channel state for the whole session, so start from its
    // defaults: a replay of "the last CC/bend before the playhead" is only
    // correct if channels begin clean (a CC11 fade or a bend from earlier
    // playback would otherwise survive a Stop or a seek).
    for (size_t c = 0; c < snapshot.channels().size(); ++c)
        sink.resetChannel ((int) c);

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
    // Position first, then the generation: a seek that lands between the two
    // reads shows up as a generation change (or as a position that is not the
    // one we expected), never as a new position paired with a stale nextEvent.
    const double position = transport.getPositionSeconds();
    const unsigned seekGeneration = transport.getSeekGeneration();
    const bool seeked = seekGeneration != seenSeekGeneration
                        || (playing && ! samePosition (position, expectedPosition));
    seenSeekGeneration = seekGeneration;

    if (swapped || seeked || (! playing && wasPlaying))
        sink.releaseAll();
    if (snapshot != nullptr)
        releaseNewlyMutedTracks (*snapshot);

    if (! playing)
    {
        wasPlaying = false;
        expectedPosition = -1.0;   // never a real position: the next Play re-chases
        sink.render (left, right, numSamples);   // lets release tails ring out
        return;
    }

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
        if (e.kind != PlaybackEventKind::NoteOn)
            sink.handle (e);
        else if (snapshot->isAudible (e.trackIndex))
        {
            const int scaled = scaleVelocity (e.data2, snapshot->gainPercent (e.trackIndex));
            if (scaled > 0)
            {
                PlaybackEvent heard = e;
                heard.data2 = scaled;
                sink.handle (heard);
            }
        }
        ++nextEvent;
    }
    if (rendered < numSamples)
        sink.render (left + rendered, right + rendered, numSamples - rendered);

    const double endSeconds = snapshot->endSeconds();
    const bool advanced = transport.advance (position, blockSeconds, endSeconds);
    // If the advance was dropped the message thread moved the playhead: the
    // sentinel forces a re-chase on the next block.
    expectedPosition = advanced ? std::min (position + blockSeconds, endSeconds) : -1.0;
}

} // namespace lotro
