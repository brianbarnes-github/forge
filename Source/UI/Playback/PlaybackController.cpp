#include "UI/Playback/PlaybackController.h"

#include "UI/MeterSegments.h"

#include <algorithm>
#include <cmath>

namespace lotro
{

namespace
{
    bool sameRange (const std::optional<LoopRange>& a, const std::optional<LoopRange>& b) noexcept
    {
        if (a.has_value() != b.has_value())
            return false;
        // Exact identity on purpose (an unchanged range must not re-notify), written
        // without == to keep -Wfloat-equal quiet.
        const auto same = [] (double x, double y) { return ! (x < y) && ! (y < x); };
        return ! a.has_value() || (same (a->startTick, b->startTick) && same (a->endTick, b->endTick));
    }
}

PlaybackController::PlaybackController (SongDocument& document, EventSink& sink)
    : doc (document),
      rootNode (document.getTree()),
      sourceMidiNode (document.getSourceMidiNode()),
      tempoMapNode (document.getTempoMapNode()),
      playbackEngine (transport, sink)
{
    rootNode.addListener (this);
    rebuild();
    startTimerHz (30);
}

PlaybackController::~PlaybackController()
{
    stopTimer();
    cancelPendingUpdate();
    rootNode.removeListener (this);
}

bool PlaybackController::isPlaybackRelevant (const juce::ValueTree& tree) const
{
    return tree == sourceMidiNode || tree.isAChildOf (sourceMidiNode)
        || tree == tempoMapNode || tree.isAChildOf (tempoMapNode);
}

void PlaybackController::valueTreePropertyChanged (juce::ValueTree& tree, const juce::Identifier& property)
{
    if (property == SongIDs::playbackVolume)
    {
        applyTrackGain (tree);
        return;   // audition gain only: a rebuild would cut held notes
    }
    if (property == SongIDs::playbackPan)
    {
        applyTrackPan (tree);
        return;   // mixer pan only: a rebuild would cut held notes
    }
    // Cosmetic track properties never change what is heard; rebuilding would cut held notes.
    if (property == SongIDs::name || property == SongIDs::colorArgb)
        return;
    if (isPlaybackRelevant (tree))
        triggerAsyncUpdate();
}

void PlaybackController::applyTrackGain (const juce::ValueTree& track)
{
    if (snapshot == nullptr || ! track.hasType (SongIDs::MIDI_TRACK))
        return;
    const int index = snapshot->trackIndexForId ((juce::int64) track.getProperty (SongIDs::trackId, (juce::int64) -1));
    if (index >= 0)
        snapshot->setGainPercent (index, trackPlaybackVolume (track));
}

void PlaybackController::applyTrackPan (const juce::ValueTree& track)
{
    if (snapshot == nullptr || ! track.hasType (SongIDs::MIDI_TRACK))
        return;
    const int index = snapshot->trackIndexForId ((juce::int64) track.getProperty (SongIDs::trackId, (juce::int64) -1));
    if (index >= 0)
        snapshot->setPanOverride (index, trackPlaybackPan (track));
}

void PlaybackController::setMasterPercent (int percent)
{
    masterLevel = juce::jlimit (0, 100, percent);
    playbackEngine.setMasterPercent (masterLevel);
    listeners.call ([] (Listener& l) { l.masterChanged(); });
}

void PlaybackController::handleAsyncUpdate() { rebuild(); }

void PlaybackController::flushRebuild()
{
    // Only when an edit is pending: an unconditional rebuild would replace the
    // snapshot (cutting held notes) even though nothing changed.
    handleUpdateNowIfNeeded();
}

void PlaybackController::rebuild()
{
    snapshot = buildSnapshot (doc);
    muteSolo.apply (*snapshot);
    playbackEngine.publishSnapshot (snapshot);
    publishLoopSpan();   // re-derive the seconds: a tempo edit moves them; a shorter Song clamps them
}

void PlaybackController::applyMuteSolo()
{
    if (snapshot != nullptr)
        muteSolo.apply (*snapshot);
}

double PlaybackController::getPositionTicks() const
{
    return snapshot != nullptr ? snapshot->tempo().secondsToTicks (transport.getPositionSeconds()) : 0.0;
}

void PlaybackController::play()
{
    flushRebuild();   // never start from a snapshot that predates a pending edit
    if (snapshot == nullptr || ! (snapshot->endSeconds() > 0.0))
        return;
    if (onBeforePlay && ! onBeforePlay())
        return;
    if (const auto loop = effectiveLoopSpan())
    {
        // The start marker is ignored while a loop is in effect.
        const double position = transport.getPositionSeconds();
        if (! (position >= loop->startSeconds && position < loop->endSeconds))
            transport.seek (loop->startSeconds);
    }
    else if (markerTick.has_value())
    {
        transport.seek (snapshot->tempo().ticksToSeconds (*markerTick));
    }
    transport.play (snapshot->endSeconds());
    listeners.call ([] (Listener& l) { l.playbackStateChanged(); l.playbackPositionChanged(); });
}

void PlaybackController::pause()
{
    transport.pause();
    listeners.call ([] (Listener& l) { l.playbackStateChanged(); });
}

void PlaybackController::stop()
{
    transport.stop();
    listeners.call ([] (Listener& l) { l.playbackStateChanged(); l.playbackPositionChanged(); });
}

void PlaybackController::goToStart()
{
    transport.goToStart();
    listeners.call ([] (Listener& l) { l.playbackPositionChanged(); });
}

void PlaybackController::goToEnd()
{
    flushRebuild();
    if (snapshot != nullptr)
        transport.goToEnd (snapshot->endSeconds());
    listeners.call ([] (Listener& l) { l.playbackPositionChanged(); });
}

void PlaybackController::rewindOneBar()
{
    if (snapshot == nullptr)
        return;
    const auto segments = meterSegments (doc.getMeterChanges(), snapshot->tempo().getTicksPerQuarter());
    seekToTick (previousBarStart (segments, getPositionTicks()));
}

void PlaybackController::seekToTick (double tick)
{
    flushRebuild();
    if (snapshot == nullptr)
        return;
    transport.seek (snapshot->tempo().ticksToSeconds (tick));
    listeners.call ([] (Listener& l) { l.playbackPositionChanged(); });
}

void PlaybackController::setMarkerTick (double tick)
{
    // Same "nothing to play" test as play(): with no MIDI open there is no
    // timeline to mark.
    flushRebuild();
    if (snapshot == nullptr || ! (snapshot->endSeconds() > 0.0))
        return;
    markerTick = std::max (0.0, tick);
    listeners.call ([] (Listener& l) { l.playbackMarkerChanged(); });
}

void PlaybackController::clearMarker()
{
    if (! markerTick.has_value())
        return;
    markerTick.reset();
    listeners.call ([] (Listener& l) { l.playbackMarkerChanged(); });
}

void PlaybackController::setLoopRange (double startTick, double endTick)
{
    flushRebuild();
    if (snapshot == nullptr || ! (snapshot->endSeconds() > 0.0))
        return;
    const auto& tempo = snapshot->tempo();
    const double endLimit = tempo.secondsToTicks (snapshot->endSeconds());
    // kMinLoopSeconds in ticks at the range's start (tempo can vary along the Song).
    const double lo = std::clamp (std::min (startTick, endTick), 0.0, endLimit);
    const double minTicks = tempo.secondsToTicks (tempo.ticksToSeconds (lo) + kMinLoopSeconds) - lo;
    const auto normalised = normaliseLoopRange (startTick, endTick, endLimit, minTicks);
    if (! normalised.has_value() || sameRange (normalised, loopRange))
        return;
    loopRange = normalised;
    publishLoopSpan();
    listeners.call ([] (Listener& l) { l.playbackLoopChanged(); });
}

void PlaybackController::clearLoopRange()
{
    if (! loopRange.has_value())
        return;
    loopRange.reset();
    publishLoopSpan();
    listeners.call ([] (Listener& l) { l.playbackLoopChanged(); });
}

std::optional<LoopSpan> PlaybackController::effectiveLoopSpan() const
{
    if (! loopRange.has_value() || snapshot == nullptr)
        return std::nullopt;
    const auto& tempo = snapshot->tempo();
    const double start = tempo.ticksToSeconds (loopRange->startTick);
    // The engine wraps whenever the loop end falls inside a block, even past the
    // Song end, so the published end must never exceed the Song end.
    const double end = std::min (tempo.ticksToSeconds (loopRange->endTick), snapshot->endSeconds());
    // The tolerance absorbs tick/second round-trip error for a range set at exactly
    // the minimum; the stored ticks are kept either way (the loop returns if the Song grows).
    constexpr double tolerance = 1.0e-9;
    if (! (end - start >= kMinLoopSeconds - tolerance))
        return std::nullopt;
    return LoopSpan { start, end };
}

void PlaybackController::publishLoopSpan()
{
    playbackEngine.setLoopSpan (effectiveLoopSpan());
}

void PlaybackController::setMuted (juce::int64 trackId, bool muted)
{
    muteSolo.setMuted (trackId, muted);
    applyMuteSolo();
    listeners.call ([] (Listener& l) { l.muteSoloChanged(); });
}

void PlaybackController::setSoloed (juce::int64 trackId, bool soloed)
{
    muteSolo.setSoloed (trackId, soloed);
    applyMuteSolo();
    listeners.call ([] (Listener& l) { l.muteSoloChanged(); });
}

void PlaybackController::documentReplaced()
{
    transport.reset();   // not stop()+goToStart(): those would leave the old Song's play-start behind
    muteSolo.clear();
    markerTick.reset();
    loopRange.reset();   // before rebuild(), which publishes the (now absent) loop span
    cancelPendingUpdate();
    rebuild();   // unconditional: the whole document changed
    listeners.call ([] (Listener& l) { l.playbackStateChanged(); l.playbackPositionChanged(); l.muteSoloChanged(); l.playbackMarkerChanged(); l.playbackLoopChanged(); });
}

void PlaybackController::timerCallback()
{
    playbackEngine.collectGarbage();
    const double position = transport.getPositionSeconds();
    const bool playing = transport.isPlaying();
    if (playing != lastReportedPlaying)
    {
        lastReportedPlaying = playing;
        listeners.call ([] (Listener& l) { l.playbackStateChanged(); });
    }
    if (std::abs (position - lastReportedPosition) > 0.0)
    {
        lastReportedPosition = position;
        listeners.call ([] (Listener& l) { l.playbackPositionChanged(); });
    }
}

} // namespace lotro
