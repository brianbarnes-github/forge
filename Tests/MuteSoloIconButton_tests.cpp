#include "UI/MuteSoloIconButton.h"

#include <catch2/catch_test_macros.hpp>

using namespace lotro;

namespace
{
    juce::Image render (MuteSoloIconButton& b)
    {
        juce::Image img (juce::Image::ARGB, b.getWidth(), b.getHeight(), true);
        juce::Graphics g (img);
        b.paintEntireComponent (g, true);
        return img;
    }
}

TEST_CASE ("MuteSoloIconButton fills its body with the off colour, then the on colour", "[mute-solo-button][render]")
{
    const juce::Colour onColour = juce::Colours::orangered;
    MuteSoloIconButton button (MuteSoloIconButton::Kind::Mute, onColour);
    button.setSize (24, 24);

    button.setToggleState (false, juce::dontSendNotification);
    const auto off = render (button).getPixelAt (12, 2);
    CHECK (off == juce::Colour (0xFF4A4A4A));

    button.setToggleState (true, juce::dontSendNotification);
    const auto on = render (button).getPixelAt (12, 2);
    CHECK (on == onColour);
    CHECK (on != off);
}
