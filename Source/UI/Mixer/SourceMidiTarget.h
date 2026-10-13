#pragma once

#include "UI/Mixer/MixerTarget.h"
#include "UI/Playback/PlaybackController.h"
#include "UI/SongDocument.h"

#include <juce_data_structures/juce_data_structures.h>
#include <juce_events/juce_events.h>

namespace lotro
{

// The mixer's view of the source MIDI: channels are the document's non-conductor
// tracks; volume/pan are saved on the tracks, mute/solo/master live in the controller.
class SourceMidiTarget : public MixerTarget,
                         private juce::ValueTree::Listener,
                         private PlaybackController::Listener,
                         private juce::AsyncUpdater
{
public:
    SourceMidiTarget (SongDocument&, PlaybackController&);
    ~SourceMidiTarget() override;

    std::vector<MixerChannel> channels() const override;
    void setVolume (juce::int64 id, int percent, bool startsGesture) override;
    void setPan (juce::int64 id, std::optional<int> pan, bool startsGesture) override;
    void setMuted (juce::int64 id, bool muted) override;
    void setSoloed (juce::int64 id, bool soloed) override;
    int  masterPercent() const override;
    void setMasterPercent (int percent) override;
    void addListener (MixerTarget::Listener* l) override { listeners.add (l); }
    void removeListener (MixerTarget::Listener* l) override { listeners.remove (l); }

    void flushPendingForTesting() { handleUpdateNowIfNeeded(); }

private:
    void handleAsyncUpdate() override;
    void muteSoloChanged() override { triggerAsyncUpdate(); }
    void masterChanged() override { triggerAsyncUpdate(); }

    void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override { triggerAsyncUpdate(); }
    void valueTreeChildAdded (juce::ValueTree&, juce::ValueTree&) override { triggerAsyncUpdate(); }
    void valueTreeChildRemoved (juce::ValueTree&, juce::ValueTree&, int) override { triggerAsyncUpdate(); }
    void valueTreeChildOrderChanged (juce::ValueTree&, int, int) override { triggerAsyncUpdate(); }
    void valueTreeParentChanged (juce::ValueTree&) override {}

    SongDocument& doc;
    PlaybackController& controller;
    // Persistent handle: addListener registers on the handle (see juce-valuetree-conventions).
    juce::ValueTree rootNode;
    juce::ListenerList<MixerTarget::Listener> listeners;
};

} // namespace lotro
