#pragma once

#include "UI/Playback/PlaybackController.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <vector>

namespace lotro
{

enum class TransportIcon { goToStart, rewind, play, pause, stop, goToEnd };

// The icon's glyph as a filled path in the unit square (every glyph spans the
// full height, so they all render at the same size once fitted to a button).
juce::Path transportIconPath (TransportIcon icon);

// Go to start | Rewind | Play/Pause | Stop | Go to end, bound to one PlaybackController.
class TransportStrip : public juce::Component, private PlaybackController::Listener
{
public:
    static constexpr int height = 28;

    explicit TransportStrip (PlaybackController& controllerIn);
    ~TransportStrip() override;

    void resized() override;
    void paint (juce::Graphics& g) override;

    juce::Button& playButtonForTesting() { return playButton; }
    TransportIcon playIconForTesting() const { return playIcon; }
    std::vector<juce::Button*> buttonsForTesting() { return { &startButton, &rewindButton, &playButton, &stopButton, &endButton }; }

private:
    void playbackStateChanged() override;
    void refreshPlayIcon();

    PlaybackController& controller;
    TransportIcon playIcon = TransportIcon::play;
    juce::DrawableButton startButton  { "start",  juce::DrawableButton::ImageFitted },
                         rewindButton { "rewind", juce::DrawableButton::ImageFitted },
                         playButton   { "play",   juce::DrawableButton::ImageFitted },
                         stopButton   { "stop",   juce::DrawableButton::ImageFitted },
                         endButton    { "end",    juce::DrawableButton::ImageFitted };
};

} // namespace lotro
