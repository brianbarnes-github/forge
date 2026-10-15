#pragma once

#include "UI/Playback/PlaybackController.h"
#include "UI/Playback/TransportControls.h"
#include "UI/SongDocument.h"
#include "UI/ThreePaneSplitter.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <vector>

namespace lotro
{

enum class TransportIcon { goToStart, rewind, play, pause, stop, goToEnd };

// The icon's glyph as a filled path in the unit square (every glyph spans the
// full height, so they all render at the same size once fitted to a button).
juce::Path transportIconPath (TransportIcon icon);

// Three resizable areas (equal thirds to start). The middle one holds
// Go to start | Rewind | Play/Pause | Stop | Go to end, bound to one
// PlaybackController; the right one holds the transport controls; the left one
// is still empty.
class TransportStrip : public juce::Component, private PlaybackController::Listener
{
public:
    static constexpr int height = 28;

    TransportStrip (PlaybackController& controllerIn, SongDocument& documentIn);
    ~TransportStrip() override;

    void resized() override;
    void paint (juce::Graphics& g) override;

    juce::Button& playButtonForTesting() { return playButton; }
    TransportIcon playIconForTesting() const { return playIcon; }
    juce::Component& leftAreaForTesting() { return leftArea; }
    juce::Component& centreAreaForTesting() { return centreArea; }
    juce::Component& rightAreaForTesting() { return rightArea; }
    TransportControls& controlsForTesting() { return controls; }
    void dragDividerForTesting (int index, int x) { splitter.dragDivider (index, x); }
    std::vector<juce::Button*> buttonsForTesting() { return { &startButton, &rewindButton, &playButton, &stopButton, &endButton }; }

private:
    void playbackStateChanged() override;
    void refreshPlayIcon();
    void layoutButtons();

    // A plain area whose bounds the splitter sets; reports changes so the
    // buttons follow a divider drag (which does not resize the strip itself).
    struct Area : public juce::Component
    {
        std::function<void()> onBoundsChanged;
        void resized() override { if (onBoundsChanged) onBoundsChanged(); }
        void moved() override { if (onBoundsChanged) onBoundsChanged(); }
    };

    PlaybackController& controller;
    TransportIcon playIcon = TransportIcon::play;
    Area leftArea, centreArea, rightArea;   // before the buttons and the splitter, so both draw on top
    TransportControls controls;             // after rightArea, so it is destroyed first
    ThreePaneSplitter splitter;
    juce::DrawableButton startButton  { "start",  juce::DrawableButton::ImageFitted },
                         rewindButton { "rewind", juce::DrawableButton::ImageFitted },
                         playButton   { "play",   juce::DrawableButton::ImageFitted },
                         stopButton   { "stop",   juce::DrawableButton::ImageFitted },
                         endButton    { "end",    juce::DrawableButton::ImageFitted };
};

} // namespace lotro
