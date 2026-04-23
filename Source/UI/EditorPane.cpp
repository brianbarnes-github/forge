#include "EditorPane.h"

namespace lotro
{

EditorPane::EditorPane()  = default;

void EditorPane::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colours::white);
    g.setColour (juce::Colours::darkgrey);
    g.setFont (16.0f);
    g.drawText ("Editor pane (stub)", getLocalBounds(), juce::Justification::centred);
}

} // namespace lotro
