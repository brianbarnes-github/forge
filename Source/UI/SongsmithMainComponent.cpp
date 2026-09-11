#include "SongsmithMainComponent.h"
#include "SongsmithColours.h"

#include <cmath>

namespace lotro
{

SongsmithMainComponent::SongsmithMainComponent (SongDocument& document)
    : doc (document), trackList (document), partStrip (document)
{
    sourceHeader.setText (
        juce::String::fromUTF8 ("\xe2\x96\xb2 MIDI SOURCE \xc2\xb7 drag tracks down to assign"),
        juce::dontSendNotification);
    sourceHeader.setColour (juce::Label::backgroundColourId, juce::Colour (SongsmithColours::panelHeader));
    sourceHeader.setColour (juce::Label::textColourId, juce::Colour (0xFF7FA8D0)); // mockup's blue label
    addAndMakeVisible (sourceHeader);

    addAndMakeVisible (trackList);

    sourcePlaceholder.setText (juce::String::fromUTF8 ("Source piano roll \xe2\x80\x94 Phase 5"),
                                juce::dontSendNotification);
    sourcePlaceholder.setColour (juce::Label::textColourId, juce::Colour (SongsmithColours::textMuted));
    sourcePlaceholder.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (sourcePlaceholder);

    addAndMakeVisible (partStrip);

    addAndMakeVisible (diagnostics);
}

void SongsmithMainComponent::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (SongsmithColours::background));
}

void SongsmithMainComponent::resized()
{
    auto area = getLocalBounds();

    sourceHeader.setBounds (area.removeFromTop (sourceHeaderHeight));

    // Fixed proportions for now (40% source region / fixed-height part strip
    // / remainder diagnostics) rather than a user-resizable splitter.
    // MainWindow::Body's Splitter class is horizontal-only today; Phase 5's
    // piano roll is what will actually need a resizable top/bottom split,
    // so generalising Splitter is deferred to that phase.
    const int sourceHeight = (int) std::lround (area.getHeight() * 0.40);
    auto sourceArea = area.removeFromTop (sourceHeight);
    trackList.setBounds (sourceArea.removeFromLeft (trackListWidth));
    sourcePlaceholder.setBounds (sourceArea);

    partStrip.setBounds (area.removeFromTop (partStripHeight));

    diagnostics.setBounds (area);
}

} // namespace lotro
