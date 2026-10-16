#include "UI/Playback/TransportStrip.h"

#include "UI/SongsmithColours.h"
#include "UI/Theme/ThemePaint.h"

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
        const auto over   = make (SongsmithColours::accentCopper);
        const auto down   = make (juce::Colour (SongsmithColours::accentCopper).darker (0.2f).getARGB());
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
    for (auto* b : std::initializer_list<juce::DrawableButton*> { &startButton, &rewindButton, &playButton, &stopButton, &endButton })
        b->setEdgeIndent (6);
    // Transparent, so PlayButton's own fill and glow show beneath the icon.
    playButton.setColour (juce::DrawableButton::backgroundColourId, juce::Colours::transparentBlack);
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
    ThemePaint::paintPanelGradient (g, getLocalBounds());
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
    playButton.setPlaying (controller.isPlaying());
}

void PlayButton::paintButton (juce::Graphics& g, bool highlighted, bool down)
{
    const auto copper = juce::Colour (SongsmithColours::accentCopper);
    const auto r = getLocalBounds().toFloat().reduced (2.0f);
    constexpr float radius = 6.0f;
    if (playing)
    {
        for (int ring = 2; ring >= 1; --ring)
        {
            g.setColour (copper.withAlpha (0.12f / (float) ring));
            g.fillRoundedRectangle (r.expanded ((float) ring), radius + (float) ring);
        }
        g.setColour (copper.withAlpha (0.15f));
        g.fillRoundedRectangle (r, radius);
    }
    g.setColour (highlighted ? copper.brighter (0.2f) : copper);
    g.drawRoundedRectangle (r, radius, 1.5f);
    juce::DrawableButton::paintButton (g, highlighted, down);   // the icon
}

} // namespace lotro
