#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace lotro
{

// A drawn mute (speaker) or solo (headphones) toggle button, shared by the
// track head and the mixer strip.
class MuteSoloIconButton : public juce::Button
{
public:
    enum class Kind { Mute, Solo };

    MuteSoloIconButton (Kind k, juce::Colour onColour) : juce::Button ({}), kind (k), onColourValue (onColour)
    {
        setClickingTogglesState (true);
        setWantsKeyboardFocus (false);
    }

    void paintButton (juce::Graphics& g, bool isOver, bool isDown) override;

private:
    Kind kind;
    juce::Colour onColourValue;
};

}
