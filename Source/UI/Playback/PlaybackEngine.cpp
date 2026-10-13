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

    constexpr int kPanMsb = 10, kPanLsb = 42, kResetAllControllers = 121;

    bool isPanControl (const PlaybackEvent& e) noexcept
    {
        return e.kind == PlaybackEventKind::Control && (e.data1 == kPanMsb || e.data1 == kPanLsb);
    }
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

    // Latch the pan overrides first so the replay and the re-apply below agree
    // even if the message thread changes one meanwhile (applyPanChanges picks
    // that up on the next block).
    for (int t = 0; t < snapshot.numTracks(); ++t)
    {
        const auto pan = snapshot.panOverride (t);
        snapshot.setAppliedPan (t, pan.has_value() ? *pan : PlaybackSnapshot::kNoPan);
    }

    for (const auto& e : snapshot.events())
    {
        if (e.seconds >= positionSeconds)
            break;
        if (e.kind == PlaybackEventKind::Control)
            deliverControl (snapshot, e);
        else if (e.kind == PlaybackEventKind::Program || e.kind == PlaybackEventKind::PitchBend)
            sink.handle (e);
    }

    // resetChannel centres pan (CC121), so the overrides go on after it.
    for (int t = 0; t < snapshot.numTracks(); ++t)
        if (const int pan = snapshot.appliedPan (t); pan != PlaybackSnapshot::kNoPan)
            for (int vch : snapshot.channelsOfTrack (t))
                sink.setChannelPan (vch, pan);
}

// A file Control event, as the track's pan override allows: an overridden
// track's own pan (CC10/CC42) is dropped, and a CC121, which centres pan in the
// synth, is followed by the override again.
void PlaybackEngine::deliverControl (const PlaybackSnapshot& snapshot, const PlaybackEvent& e) noexcept
{
    const int pan = snapshot.appliedPan (e.trackIndex);
    if (pan == PlaybackSnapshot::kNoPan)
    {
        sink.handle (e);
        return;
    }
    if (isPanControl (e))
        return;
    sink.handle (e);
    if (e.data1 == kResetAllControllers)
        sink.setChannelPan (e.virtualChannel, pan);
}

// Acts on a pan override set or cleared since the last block. While playing, a
// new override pans the track's channels at once, and a cleared one restores
// the file's own pan as of the playhead (the last CC10/CC42 before it, unless a
// CC121 came later; centre if none). While stopped only the bookkeeping moves:
// the next Play's chase applies the right state.
void PlaybackEngine::applyPanChanges (const PlaybackSnapshot& snapshot, double positionSeconds, bool playing) noexcept
{
    for (int t = 0; t < snapshot.numTracks(); ++t)
    {
        const auto wanted = snapshot.panOverride (t);
        const int target = wanted.has_value() ? *wanted : PlaybackSnapshot::kNoPan;
        if (target == snapshot.appliedPan (t))
            continue;
        snapshot.setAppliedPan (t, target);
        if (! playing)
            continue;

        if (target != PlaybackSnapshot::kNoPan)
        {
            for (int vch : snapshot.channelsOfTrack (t))
                sink.setChannelPan (vch, target);
            continue;
        }

        for (int vch : snapshot.channelsOfTrack (t))
        {
            const PlaybackEvent* msb = nullptr;
            const PlaybackEvent* lsb = nullptr;
            for (const auto& e : snapshot.events())
            {
                if (e.seconds >= positionSeconds)
                    break;
                if (e.kind != PlaybackEventKind::Control || e.virtualChannel != vch)
                    continue;
                if (e.data1 == kPanMsb)                    msb = &e;
                else if (e.data1 == kPanLsb)               lsb = &e;
                else if (e.data1 == kResetAllControllers)  msb = lsb = nullptr;
            }
            if (msb == nullptr && lsb == nullptr)
                sink.setChannelPan (vch, 0);
            if (msb != nullptr)
                sink.handle (*msb);
            if (lsb != nullptr)
                sink.handle (*lsb);
        }
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
    renderBlockUnscaled (buffer, startSample, numSamples);
    const int percent = masterPercent();
    if (percent != 100 && numSamples > 0)
        buffer.applyGain (startSample, numSamples, (float) percent / 100.0f);
}

void PlaybackEngine::renderBlockUnscaled (juce::AudioBuffer<float>& buffer, int startSample, int numSamples) noexcept
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

    const auto loop = loopCell.read();
    // A playhead this engine left inside the loop that now starts at or past
    // the loop end (the range was narrowed or moved, or a tempo edit moved the
    // edges) wraps at once. A seek past the end clears the latch (seeked), so
    // it still plays on. A new snapshot does not: a tempo edit is one.
    const bool wrapOnEntry = playing && wasPlaying && ! seeked && insideLoopLastBlock
                             && loop.has_value() && position >= loop->endSeconds;
    insideLoopLastBlock = false;

    if (swapped || seeked || (! playing && wasPlaying) || wrapOnEntry)
        sink.releaseAll();
    if (snapshot != nullptr)
        releaseNewlyMutedTracks (*snapshot);

    if (! playing)
    {
        if (snapshot != nullptr)
            applyPanChanges (*snapshot, position, false);
        wasPlaying = false;
        expectedPosition = -1.0;   // never a real position: the next Play re-chases
        sink.render (left, right, numSamples);   // lets release tails ring out
        return;
    }

    const double startPos = wrapOnEntry ? loop->startSeconds : position;
    if (swapped || seeked || ! wasPlaying || wrapOnEntry)
    {
        chase (*snapshot, startPos);
        nextEvent = snapshot->firstEventAtOrAfter (startPos);
    }
    applyPanChanges (*snapshot, startPos, true);   // after a chase: nothing left to do
    wasPlaying = true;

    // The block as segments: a segment ends at the block end or, with a loop,
    // at the loop end, where the playhead wraps back to the loop start
    // (sample-accurately, at most kMaxWrapsPerBlock times per block).
    double segPos = startPos;
    int done = 0;
    int wraps = wrapOnEntry ? 1 : 0;
    while (done < numSamples)
    {
        const auto step = nextLoopStep (segPos, numSamples - done, sampleRate, loop);
        if (step.wraps && wraps >= kMaxWrapsPerBlock)
        {
            // The cap binds: the rest of the block only lets the sink ring out,
            // with no events and the playhead held where the last wrap chased
            // it (the loop start), so it never runs on past the loop end.
            sink.render (left + done, right + done, numSamples - done);
            break;
        }
        const double segSeconds = (double) step.samples / sampleRate;
        // A wrapping segment delivers only what lies before the loop end: an
        // event at the end belongs after the loop, not to the segment's last
        // frame (the ceil'd segment may reach just past the end).
        const double eventsBefore = step.wraps ? loop->endSeconds : segPos + segSeconds;
        renderSegment (*snapshot, left + done, right + done, step.samples, segPos, eventsBefore);
        done += step.samples;
        if (step.wraps)
        {
            sink.releaseAll();
            chase (*snapshot, loop->startSeconds);
            nextEvent = snapshot->firstEventAtOrAfter (loop->startSeconds);
            segPos = loop->startSeconds;
            ++wraps;
        }
        else
        {
            segPos += segSeconds;
        }
    }

    const double endSeconds = snapshot->endSeconds();
    const bool advanced = transport.advanceTo (position, segPos, endSeconds);
    // If the advance was dropped the message thread moved the playhead: the
    // sentinel forces a re-chase on the next block.
    expectedPosition = advanced ? std::min (segPos, endSeconds) : -1.0;
    insideLoopLastBlock = advanced && loop.has_value()
                          && segPos >= loop->startSeconds && segPos < loop->endSeconds;
}

// Renders `samples` frames starting at segPos, delivering the events before
// eventsBeforeSeconds at their frame (clamped into the segment).
void PlaybackEngine::renderSegment (const PlaybackSnapshot& snapshot, float* left, float* right, int samples,
                                    double segPos, double eventsBeforeSeconds) noexcept
{
    const auto& events = snapshot.events();
    int rendered = 0;
    while (nextEvent < events.size() && events[nextEvent].seconds < eventsBeforeSeconds)
    {
        const auto& e = events[nextEvent];
        const int offset = juce::jlimit (rendered, samples, (int) std::llround ((e.seconds - segPos) * sampleRate));
        if (offset > rendered)
        {
            sink.render (left + rendered, right + rendered, offset - rendered);
            rendered = offset;
        }
        if (e.kind == PlaybackEventKind::Control)
            deliverControl (snapshot, e);
        else if (e.kind != PlaybackEventKind::NoteOn)
            sink.handle (e);
        else if (snapshot.isAudible (e.trackIndex))
        {
            const int scaled = scaleVelocity (e.data2, snapshot.gainPercent (e.trackIndex));
            if (scaled > 0)
            {
                PlaybackEvent heard = e;
                heard.data2 = scaled;
                sink.handle (heard);
            }
        }
        ++nextEvent;
    }
    if (rendered < samples)
        sink.render (left + rendered, right + rendered, samples - rendered);
}

} // namespace lotro
