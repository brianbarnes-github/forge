#include "UI/Playback/PlaybackController.h"

#include <cmath>

namespace lotro
{

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
    // Cosmetic track properties never change what is heard; rebuilding would cut held notes.
    if (property == SongIDs::name || property == SongIDs::colorArgb)
        return;
    if (isPlaybackRelevant (tree))
        triggerAsyncUpdate();
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
    if (markerTick.has_value())
        transport.seek (snapshot->tempo().ticksToSeconds (*markerTick));
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
    int numerator = 4, denominator = 4;
    const auto meter = doc.getMeterMapNode().getChild (0);   // first entry only: one-meter-timeline convention
    if (meter.isValid())
    {
        numerator = (int) meter.getProperty (SongIDs::numerator, 4);
        denominator = (int) meter.getProperty (SongIDs::denominator, 4);
    }
    seekToTick (previousBarTick (getPositionTicks(), snapshot->tempo().getTicksPerQuarter(), numerator, denominator));
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
    cancelPendingUpdate();
    rebuild();   // unconditional: the whole document changed
    listeners.call ([] (Listener& l) { l.playbackStateChanged(); l.playbackPositionChanged(); l.muteSoloChanged(); l.playbackMarkerChanged(); });
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
