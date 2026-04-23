#include "DiagnosticsPane.h"

namespace lotro
{

DiagnosticsPane::DiagnosticsPane() = default;

void DiagnosticsPane::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colours::whitesmoke);
    g.setColour (juce::Colours::darkgrey);
    g.setFont (16.0f);
    g.drawText ("Diagnostics pane (stub)", getLocalBounds(), juce::Justification::centred);
}

} // namespace lotro
