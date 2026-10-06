#pragma once

#include "UI/Playback/EventSink.h"
#include "UI/Playback/MuteSoloState.h"
#include "UI/Playback/PlaybackEngine.h"
#include "UI/Playback/PlaybackSnapshot.h"
#include "UI/Playback/Transport.h"
#include "UI/SongDocument.h"

#include <juce_events/juce_events.h>

#include <functional>
#include <memory>
#include <optional>

namespace lotro
{

// Message-thread owner of playback: the Transport, mute/solo, the engine, and
// the snapshot lifecycle. Every UI component talks to this, never to the audio
// side directly.
class PlaybackController : private juce::ValueTree::Listener,
                           private juce::AsyncUpdater,
                           private juce::Timer
{
public:
    class Listener
    {
    public:
        virtual ~Listener() = default;
        virtual void playbackPositionChanged() {}
        virtual void playbackStateChanged() {}
        virtual void muteSoloChanged() {}
        virtual void playbackMarkerChanged() {}
    };

    PlaybackController (SongDocument& document, EventSink& sink);
    ~PlaybackController() override;

    PlaybackEngine& engine() noexcept { return playbackEngine; }

    void addListener (Listener* l) { listeners.add (l); }
    void removeListener (Listener* l) { listeners.remove (l); }

    // Return false to veto Play (MainWindow shows why).
    std::function<bool()> onBeforePlay;

    void play();
    void pause();
    void stop();
    void togglePlayPause() { if (isPlaying()) pause(); else play(); }
    void goToStart();
    void goToEnd();
    void rewindOneBar();
    void seekToTick (double tick);

    // The start marker: where Play begins (after Pause or Stop too), whatever
    // the playhead is doing. Session-only; held in ticks so it follows tempo
    // edits. With no marker, Play starts from the playhead.
    void setMarkerTick (double tick);
    void clearMarker();
    std::optional<double> getMarkerTick() const { return markerTick; }

    bool isPlaying() const { return transport.isPlaying(); }
    double getPositionSeconds() const { return transport.getPositionSeconds(); }
    double getPositionTicks() const;

    void setMuted (juce::int64 trackId, bool muted);
    void setSoloed (juce::int64 trackId, bool soloed);
    bool isMuted (juce::int64 trackId) const { return muteSolo.isMuted (trackId); }
    bool isSoloed (juce::int64 trackId) const { return muteSolo.isSoloed (trackId); }
    bool isSilencedBySolo (juce::int64 trackId) const { return muteSolo.isSilencedBySolo (trackId); }

    const SongDocument& document() const noexcept { return doc; }

    void documentReplaced();
    void flushRebuild();
    std::shared_ptr<PlaybackSnapshot> currentSnapshot() const { return snapshot; }

private:
    bool isPlaybackRelevant (const juce::ValueTree& tree) const;
    void rebuild();
    void applyMuteSolo();
    void handleAsyncUpdate() override;
    void timerCallback() override;

    void valueTreePropertyChanged (juce::ValueTree& tree, const juce::Identifier& property) override;
    void valueTreeChildAdded (juce::ValueTree& parent, juce::ValueTree&) override { if (isPlaybackRelevant (parent)) triggerAsyncUpdate(); }
    void valueTreeChildRemoved (juce::ValueTree& parent, juce::ValueTree&, int) override { if (isPlaybackRelevant (parent)) triggerAsyncUpdate(); }
    void valueTreeChildOrderChanged (juce::ValueTree& parent, int, int) override { if (isPlaybackRelevant (parent)) triggerAsyncUpdate(); }
    void valueTreeParentChanged (juce::ValueTree&) override {}

    SongDocument& doc;
    // Persistent handles: ValueTree::addListener registers on the HANDLE, so
    // these must outlive the registration (see juce-valuetree-conventions).
    juce::ValueTree rootNode;
    juce::ValueTree sourceMidiNode;
    juce::ValueTree tempoMapNode;

    Transport transport;
    MuteSoloState muteSolo;
    PlaybackEngine playbackEngine;
    std::shared_ptr<PlaybackSnapshot> snapshot;
    juce::ListenerList<Listener> listeners;

    std::optional<double> markerTick;
    double lastReportedPosition = -1.0;
    bool lastReportedPlaying = false;
};

} // namespace lotro
