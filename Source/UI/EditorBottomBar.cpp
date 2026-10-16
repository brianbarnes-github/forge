#include "EditorBottomBar.h"

#include "EditorGrid.h"
#include "SongsmithColours.h"

namespace lotro
{

namespace
{
    constexpr int leftMargin = 8;
    constexpr int labelWidth = 44;
    constexpr int sliderWidth = 160;
    constexpr int gap = 4;
    constexpr int readoutWidth = 44;
}

EditorBottomBar::EditorBottomBar()
{
    swingLabel.setColour (juce::Label::textColourId, juce::Colour (SongsmithColours::text));
    swingReadout.setColour (juce::Label::textColourId, juce::Colour (SongsmithColours::text));
    swingReadout.setJustificationType (juce::Justification::centredRight);
    swingReadout.setText (swingPercentText (0), juce::dontSendNotification);

    swingSlider.setRange (-100.0, 100.0, 1.0);
    swingSlider.setValue (0.0, juce::dontSendNotification);
    swingSlider.setDoubleClickReturnValue (true, 0.0);
    swingSlider.setWantsKeyboardFocus (false);   // like the transport's rate knob: keys stay with the roll
    swingSlider.setColour (juce::Slider::trackColourId, juce::Colour (SongsmithColours::accentCopper));
    swingSlider.setColour (juce::Slider::backgroundColourId, juce::Colour (SongsmithColours::columnDivider));
    swingSlider.setColour (juce::Slider::thumbColourId, juce::Colour (SongsmithColours::text));
    swingSlider.setTooltip ("Swing strength (double-click to reset to 0%)");
    swingSlider.onValueChange = [this]
    {
        const int percent = clampSwingPercent ((int) swingSlider.getValue());
        swingReadout.setText (swingPercentText (percent), juce::dontSendNotification);
        if (onSwingChanged)
            onSwingChanged (percent);
    };

    addAndMakeVisible (swingLabel);
    addAndMakeVisible (swingSlider);
    addAndMakeVisible (swingReadout);
    setSwingEnabled (false);
}

void EditorBottomBar::setSwing (int percent)
{
    const int clamped = clampSwingPercent (percent);
    swingSlider.setValue (clamped, juce::dontSendNotification);
    swingReadout.setText (swingPercentText (clamped), juce::dontSendNotification);
}

void EditorBottomBar::setSwingEnabled (bool enabled)
{
    swingLabel.setEnabled (enabled);
    swingSlider.setEnabled (enabled);
    swingReadout.setEnabled (enabled);
}

void EditorBottomBar::resized()
{
    auto area = getLocalBounds();
    area.removeFromLeft (leftMargin);
    swingLabel.setBounds (area.removeFromLeft (labelWidth));
    swingSlider.setBounds (area.removeFromLeft (sliderWidth));
    area.removeFromLeft (gap);
    swingReadout.setBounds (area.removeFromLeft (readoutWidth));
}

} // namespace lotro
