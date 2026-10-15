#include "UI/Playback/TransportStrip.h"

#include "UI/SongsmithColours.h"

#include <algorithm>

namespace lotro
{

namespace
{
    void addBar (juce::Path& p, float x, float w) { p.addRectangle (x, 0.0f, w, 1.0f); }

    void addTriangle (juce::Path& p, float tipX, float baseX)
    {
        p.addTriangle (baseX, 0.0f, tipX, 0.5f, baseX, 1.0f);
    }

    void setIcon (juce::DrawableButton& button, TransportIcon icon)
    {
        auto make = [icon] (juce::uint32 argb)
        {
            juce::DrawablePath d;
            d.setPath (transportIconPath (icon));
            d.setFill (juce::FillType (juce::Colour (argb)));
            return d;
        };
        const auto normal = make (SongsmithColours::text);
        const auto over   = make (SongsmithColours::selectionHighlight);
        const auto down   = make (SongsmithColours::accentAmber);
        button.setImages (&normal, &over, &down);
    }
}

juce::Path transportIconPath (TransportIcon icon)
{
    juce::Path p;
    switch (icon)
    {
        case TransportIcon::goToStart: addBar (p, 0.0f, 0.15f); addTriangle (p, 0.25f, 1.0f); break;
        case TransportIcon::rewind:    addTriangle (p, 0.0f, 0.5f); addTriangle (p, 0.5f, 1.0f); break;
        case TransportIcon::play:      addTriangle (p, 1.0f, 0.15f); break;
        case TransportIcon::pause:     addBar (p, 0.1f, 0.3f); addBar (p, 0.6f, 0.3f); break;
        case TransportIcon::stop:      p.addRectangle (0.0f, 0.0f, 1.0f, 1.0f); break;
        case TransportIcon::goToEnd:   addTriangle (p, 0.75f, 0.0f); addBar (p, 0.85f, 0.15f); break;
    }
    return p;
}

namespace
{
    constexpr int buttonWidth = 36, buttonGap = 4, buttonCount = 5, sideAreaMinWidth = 24;
    constexpr int buttonRowWidth = buttonCount * buttonWidth + (buttonCount - 1) * buttonGap;
}

void TransportStrip::setButtonsHost (StripButtonsHost* host)
{
    buttonsBar.reset();
    if (host == nullptr)
        return;
    buttonsBar = std::make_unique<StripButtonsBar> (*host);
    leftArea.addAndMakeVisible (*buttonsBar);
    buttonsBar->setBounds (leftArea.getLocalBounds());
}

TransportStrip::TransportStrip (PlaybackController& controllerIn, SongDocument& documentIn)
    : controller (controllerIn),
      controls (controllerIn, documentIn),
      splitter ({ sideAreaMinWidth, buttonRowWidth, sideAreaMinWidth })
{
    for (auto* a : { &leftArea, &centreArea, &rightArea })
        addAndMakeVisible (*a);
    leftArea.onBoundsChanged = [this] { if (buttonsBar != nullptr) buttonsBar->setBounds (leftArea.getLocalBounds()); };
    centreArea.onBoundsChanged = [this] { layoutButtons(); };
    rightArea.addAndMakeVisible (controls);
    rightArea.onBoundsChanged = [this] { controls.setBounds (0, 0, std::max (TransportControls::preferredWidth, rightArea.getWidth()), rightArea.getHeight()); };

    for (auto* b : buttonsForTesting())
    {
        b->setWantsKeyboardFocus (false);
        addAndMakeVisible (*b);
    }
    setIcon (startButton,  TransportIcon::goToStart);
    setIcon (rewindButton, TransportIcon::rewind);
    setIcon (stopButton,   TransportIcon::stop);
    setIcon (endButton,    TransportIcon::goToEnd);
    for (auto* b : { &startButton, &rewindButton, &playButton, &stopButton, &endButton })
        b->setEdgeIndent (6);
    startButton.setTooltip ("Go to beginning");
    rewindButton.setTooltip ("Rewind one bar");
    playButton.setTooltip ("Play / Pause (Space)");
    stopButton.setTooltip ("Stop");
    endButton.setTooltip ("Go to end");

    startButton.onClick  = [this] { controller.goToStart(); };
    rewindButton.onClick = [this] { controller.rewindOneBar(); };
    playButton.onClick   = [this] { controller.togglePlayPause(); refreshPlayIcon(); };
    stopButton.onClick   = [this] { controller.stop(); refreshPlayIcon(); };
    endButton.onClick    = [this] { controller.goToEnd(); };

    addAndMakeVisible (splitter);   // last: its bars sit above the areas
    splitter.setComponents (&leftArea, &centreArea, &rightArea);

    controller.addListener (this);
    refreshPlayIcon();
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
    splitter.setBounds (getLocalBounds());   // lays out the three areas, which re-centres the buttons
    layoutButtons();
}

void TransportStrip::layoutButtons()
{
    // The row of buttons, centred in the middle area.
    const auto buttons = buttonsForTesting();
    auto row = centreArea.getBounds().reduced (0, 3).withSizeKeepingCentre (buttonRowWidth, getHeight() - 6);
    for (auto* b : buttons)
    {
        b->setBounds (row.removeFromLeft (buttonWidth));
        row.removeFromLeft (buttonGap);
    }
}

void TransportStrip::playbackStateChanged() { refreshPlayIcon(); }

void TransportStrip::refreshPlayIcon()
{
    playIcon = controller.isPlaying() ? TransportIcon::pause : TransportIcon::play;
    setIcon (playButton, playIcon);
}

} // namespace lotro
