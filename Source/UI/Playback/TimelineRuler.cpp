#include "UI/Playback/TimelineRuler.h"

#include "UI/SongsmithColours.h"

namespace lotro
{

void TimelineRuler::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (SongsmithColours::background).brighter (0.1f));
    g.setColour (juce::Colours::black.withAlpha (0.5f));
    g.drawHorizontalLine (getHeight() - 1, 0.0f, (float) getWidth());
}

} // namespace lotro
