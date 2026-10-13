#include "UI/Mixer/MixerStripComponent.h"
#include "UI/SongsmithColours.h"

#include <utility>

namespace lotro
{

namespace
{
    constexpr int padding = 4;
    constexpr int knobSize = 36;
    constexpr int markerHeight = 12;
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

    midiMarker.setText ("MIDI", juce::dontSendNotification);
    midiMarker.setJustificationType (juce::Justification::centred);
    midiMarker.setFont (juce::Font (juce::FontOptions (9.0f)));
    midiMarker.setColour (juce::Label::textColourId, juce::Colour (SongsmithColours::textMuted));
    midiMarker.setTooltip ("Pan follows the MIDI file. Move the knob to override; double-click to follow the file again.");
    addAndMakeVisible (midiMarker);

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
    midiMarker.setVisible (! c.pan.has_value());
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
    midiMarker.setBounds (area.removeFromTop (markerHeight));

    auto buttons = area.removeFromTop (buttonRowHeight);
    const int buttonsWidth = buttonSize * 2 + padding;
    buttons = buttons.withSizeKeepingCentre (buttonsWidth, buttonRowHeight);
    muteButton.setBounds (buttons.removeFromLeft (buttonSize));
    buttons.removeFromLeft (padding);
    soloButton.setBounds (buttons.removeFromLeft (buttonSize));

    nameLabel.setBounds (area.removeFromBottom (nameHeight));
    area.removeFromTop (padding);
    fader.setBounds (area);
}

void MixerStripComponent::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (SongsmithColours::background));
}

} // namespace lotro
