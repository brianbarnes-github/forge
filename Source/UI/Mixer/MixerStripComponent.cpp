#include "UI/Mixer/MixerStripComponent.h"
#include "UI/SongsmithColours.h"

#include <utility>

namespace lotro
{

namespace
{
    constexpr int padding = 4;
    constexpr int knobSize = 36;
    constexpr int gap = 2;   // between knob / fader / buttons / label (kept small: the dock minimum is tight)
    constexpr int buttonRowHeight = 20;
    constexpr int buttonSize = 20;
    constexpr int nameHeight = 30;
}

void MixerStripComponent::configureFader (juce::Slider& s)
{
    s.setSliderStyle (juce::Slider::LinearVertical);
    s.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    s.setRange (0.0, 100.0, 1.0);
    s.setDoubleClickReturnValue (true, 100.0);
    // As the head's slider: wheel moves are never bracketed by onDragStart /
    // onDragEnd (one undo step per tick), and the wheel belongs to the viewport.
    s.setScrollWheelEnabled (false);
    // NN% in a bubble while dragging (a bubble needs no TooltipWindow).
    s.setTextValueSuffix ("%");
    s.setPopupDisplayEnabled (true, false, nullptr);
    s.setValue (100.0, juce::dontSendNotification);
}

MixerStripComponent::MixerStripComponent (MixerTarget& mixerTarget, juce::int64 channelId)
    : target (mixerTarget), id (channelId)
{
    panKnob.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    panKnob.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    panKnob.setRange (-100.0, 100.0, 1.0);
    panKnob.setValue (0.0, juce::dontSendNotification);
    // A wheel tick would silently turn a file-following pan into an override.
    panKnob.setScrollWheelEnabled (false);
    panKnob.setTooltip ("Pan");
    // JUCE brackets mouse drags with onDragStart / onDragEnd but not keyboard
    // steps, so any change outside a bracket starts its own gesture (as the head's slider).
    panKnob.onDragStart = [this] { panDragging = true; panDragHasChanged = false; };
    panKnob.onDragEnd = [this] { panDragging = false; };
    panKnob.onValueChange = [this]
    {
        const bool starts = ! panDragging || ! std::exchange (panDragHasChanged, true);
        target.setPan (id, (int) panKnob.getValue(), starts);
    };
    panKnob.onClear = [this] { target.setPan (id, std::nullopt, true); };
    addAndMakeVisible (panKnob);

    muteButton.setTooltip ("Mute");
    soloButton.setTooltip ("Solo");
    muteButton.onClick = [this] { target.setMuted (id, muteButton.getToggleState()); };
    soloButton.onClick = [this] { target.setSoloed (id, soloButton.getToggleState()); };
    addAndMakeVisible (muteButton);
    addAndMakeVisible (soloButton);

    configureFader (fader);
    fader.setTooltip ("Playback volume");
    fader.onDragStart = [this] { faderDragging = true; faderDragHasChanged = false; };
    fader.onDragEnd = [this] { faderDragging = false; };
    fader.onValueChange = [this]
    {
        const bool starts = ! faderDragging || ! std::exchange (faderDragHasChanged, true);
        target.setVolume (id, (int) fader.getValue(), starts);
    };
    addAndMakeVisible (fader);

    nameLabel.setJustificationType (juce::Justification::centred);
    nameLabel.setFont (juce::Font (juce::FontOptions (11.0f)));
    nameLabel.setMinimumHorizontalScale (0.7f);
    addAndMakeVisible (nameLabel);
}

void MixerStripComponent::refresh (const MixerChannel& c)
{
    jassert (c.id == id);
    name = c.name;

    nameLabel.setText (juce::String (c.number) + "\n" + c.name, juce::dontSendNotification);
    nameLabel.setColour (juce::Label::textColourId, juce::Colour (c.argb).withAlpha (1.0f));
    nameLabel.setTooltip (c.name);

    fader.setValue (c.volume, juce::dontSendNotification);
    panKnob.setValue (c.pan.value_or (0), juce::dontSendNotification);
    muteButton.setToggleState (c.muted, juce::dontSendNotification);
    soloButton.setToggleState (c.soloed, juce::dontSendNotification);

    // The track head's rule (TrackRowComponent::setMuteSolo): muted or silenced by solo dims.
    fader.setAlpha ((c.muted || c.silencedBySolo) ? 0.5f : 1.0f);
    repaint();
}

void MixerStripComponent::resized()
{
    auto area = getLocalBounds().reduced (padding);
    panKnob.setBounds (area.removeFromTop (knobSize).withSizeKeepingCentre (knobSize, knobSize));

    // Bottom-up: name label, then the mute/solo row; the fader takes what is left.
    nameLabel.setBounds (area.removeFromBottom (nameHeight));
    area.removeFromBottom (gap);
    auto buttons = area.removeFromBottom (buttonRowHeight)
                       .withSizeKeepingCentre (buttonSize * 2 + padding, buttonRowHeight);
    muteButton.setBounds (buttons.removeFromLeft (buttonSize));
    buttons.removeFromLeft (padding);
    soloButton.setBounds (buttons.removeFromLeft (buttonSize));
    area.removeFromBottom (gap);

    area.removeFromTop (gap);
    fader.setBounds (area);
}

void MixerStripComponent::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (SongsmithColours::background));
}

} // namespace lotro
