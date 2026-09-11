#include "SongsmithMainComponent.h"
#include "SongsmithColours.h"

namespace lotro
{

SongsmithMainComponent::UpperRegion::UpperRegion (juce::Label& headerIn, TrackListComponent& trackListIn,
                                                   PianoRollComponent& rollIn)
    : header (headerIn), trackList (trackListIn), roll (rollIn)
{
    addAndMakeVisible (header);
    addAndMakeVisible (trackList);
    addAndMakeVisible (roll);
}

void SongsmithMainComponent::UpperRegion::resized()
{
    auto area = getLocalBounds();
    header.setBounds (area.removeFromTop (sourceHeaderHeight));
    trackList.setBounds (area.removeFromLeft (trackListWidth));
    roll.setBounds (area);
}

SongsmithMainComponent::LowerRegion::LowerRegion (PartStripComponent& partStripIn,
                                                   DiagnosticListView& diagnosticsIn)
    : partStrip (partStripIn), diagnostics (diagnosticsIn)
{
    addAndMakeVisible (partStrip);
    addAndMakeVisible (diagnostics);
}

void SongsmithMainComponent::LowerRegion::resized()
{
    auto area = getLocalBounds();
    partStrip.setBounds (area.removeFromTop (partStripHeight));
    diagnostics.setBounds (area);
}

SongsmithMainComponent::SongsmithMainComponent (SongDocument& document)
    : doc (document),
      trackList (document),
      partStrip (document),
      upperRegion (sourceHeader, trackList, sourceRoll),
      lowerRegion (partStrip, diagnostics),
      splitter (SplitterComponent::Orientation::topBottom)
{
    sourceHeader.setText (
        juce::String::fromUTF8 ("\xe2\x96\xb2 MIDI SOURCE \xc2\xb7 drag tracks down to assign"),
        juce::dontSendNotification);
    sourceHeader.setColour (juce::Label::backgroundColourId, juce::Colour (SongsmithColours::panelHeader));
    sourceHeader.setColour (juce::Label::textColourId, juce::Colour (0xFF7FA8D0)); // mockup's blue label

    trackList.onTrackSelected = [this] (juce::int64 trackId) { trackSelected (trackId); };

    addAndMakeVisible (upperRegion);
    addAndMakeVisible (lowerRegion);

    // Splitter sits ON TOP of upperRegion/lowerRegion in z-order (same trick
    // as MainWindow::Body's splitter) — its own hit area is transparent to
    // clicks so upperRegion/lowerRegion get them, but its drag bar still
    // gets its own via the second `true`.
    splitter.setComponents (&upperRegion, &lowerRegion);
    splitter.setInterceptsMouseClicks (false, true);
    addAndMakeVisible (splitter);
}

void SongsmithMainComponent::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (SongsmithColours::background));
}

void SongsmithMainComponent::resized()
{
    splitter.setBounds (getLocalBounds());
}

void SongsmithMainComponent::trackSelected (juce::int64 trackId)
{
    auto trackNode = doc.findTrackById (trackId);
    if (! trackNode.isValid())
    {
        currentNoteSource.reset();
        sourceRoll.setNoteSource (nullptr, 480, {});
        return;
    }

    currentNoteSource = std::make_unique<SourceTrackNoteSource> (trackNode);
    const int ticksPerQuarter = (int) doc.getSourceMidiNode().getProperty (SongIDs::ticksPerQuarter, 480);
    sourceRoll.setNoteSource (currentNoteSource.get(), ticksPerQuarter, doc.getMeterMapNode());
}

} // namespace lotro
