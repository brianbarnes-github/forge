#include "UI/Playback/TransportStrip.h"

#include "UI/SongsmithColours.h"

namespace lotro
{

TransportStrip::TransportStrip (PlaybackController& controllerIn) : controller (controllerIn)
{
    for (auto* b : buttonsForTesting())
    {
        b->setWantsKeyboardFocus (false);
        addAndMakeVisible (*b);
    }
    startButton.setTooltip ("Go to beginning");
    rewindButton.setTooltip ("Rewind one bar");
    playButton.setTooltip ("Play / Pause (Space)");
    stopButton.setTooltip ("Stop");
    endButton.setTooltip ("Go to end");

    startButton.onClick  = [this] { controller.goToStart(); };
    rewindButton.onClick = [this] { controller.rewindOneBar(); };
    playButton.onClick   = [this] { controller.togglePlayPause(); refreshPlayLabel(); };
    stopButton.onClick   = [this] { controller.stop(); refreshPlayLabel(); };
    endButton.onClick    = [this] { controller.goToEnd(); };

    controller.addListener (this);
    refreshPlayLabel();
}

TransportStrip::~TransportStrip()
{
    controller.removeListener (this);
}

void TransportStrip::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (SongsmithColours::background));
}

void TransportStrip::resized()
{
    auto area = getLocalBounds().reduced (4, 3);
    const int w = 56;
    for (auto* b : buttonsForTesting())
    {
        b->setBounds (area.removeFromLeft (w));
        area.removeFromLeft (4);
    }
}

void TransportStrip::playbackStateChanged() { refreshPlayLabel(); }

void TransportStrip::refreshPlayLabel()
{
    playButton.setButtonText (controller.isPlaying() ? "Pause" : "Play");
}

} // namespace lotro
