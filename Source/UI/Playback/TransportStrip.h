#pragma once

#include "UI/Playback/PlaybackController.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <vector>

namespace lotro
{

// Go to start | Rewind | Play/Pause | Stop | Go to end, bound to one PlaybackController.
class TransportStrip : public juce::Component, private PlaybackController::Listener
{
public:
    static constexpr int height = 28;

    explicit TransportStrip (PlaybackController& controllerIn);
    ~TransportStrip() override;

    void resized() override;
    void paint (juce::Graphics& g) override;

    juce::TextButton& playButtonForTesting() { return playButton; }
    std::vector<juce::TextButton*> buttonsForTesting() { return { &startButton, &rewindButton, &playButton, &stopButton, &endButton }; }

private:
    void playbackStateChanged() override;
    void refreshPlayLabel();

    PlaybackController& controller;
    juce::TextButton startButton { "|<" }, rewindButton { "<<" }, playButton { "Play" },
                     stopButton { "Stop" }, endButton { ">|" };
};

} // namespace lotro
