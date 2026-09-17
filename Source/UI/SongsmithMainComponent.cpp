#include "SongsmithMainComponent.h"
#include "PreviewPipeline.h"
#include "PreviewNoteDiff.h"
#include "SongsmithColours.h"

#include "Core/LotroInstrument.h"

namespace lotro
{

SongsmithMainComponent::UpperRegion::UpperRegion (juce::Label& headerIn, TrackListComponent& trackListIn)
    : header (headerIn), trackList (trackListIn)
{
    addAndMakeVisible (header);
    addAndMakeVisible (trackList);
}

void SongsmithMainComponent::UpperRegion::resized()
{
    auto area = getLocalBounds();
    header.setBounds (area.removeFromTop (sourceHeaderHeight));
    trackList.setBounds (area);
}

SongsmithMainComponent::PreviewRegion::PreviewRegion (juce::Label& headerIn, PreviewAssignedPanel& panelIn,
                                                       PianoRollComponent& rollIn)
    : header (headerIn), panel (panelIn), roll (rollIn)
{
    addAndMakeVisible (header);
    addAndMakeVisible (panel);
    addAndMakeVisible (roll);
}

void SongsmithMainComponent::PreviewRegion::resized()
{
    auto area = getLocalBounds();
    header.setBounds (area.removeFromTop (sourceHeaderHeight));
    panel.setBounds (area.removeFromLeft (previewPanelWidth));
    roll.setBounds (area);
}

SongsmithMainComponent::LowerRegion::LowerRegion (PartStripComponent& partStripIn, PreviewRegion& previewRegionIn,
                                                   DiagnosticListView& diagnosticsIn)
    : partStrip (partStripIn), previewRegion (previewRegionIn), diagnostics (diagnosticsIn)
{
    addAndMakeVisible (partStrip);
    addAndMakeVisible (previewRegion);
    addAndMakeVisible (diagnostics);

    innerSplitter.setComponents (&previewRegion, &diagnostics);
    addAndMakeVisible (innerSplitter);
}

void SongsmithMainComponent::LowerRegion::resized()
{
    auto area = getLocalBounds();
    partStrip.setBounds (area.removeFromTop (partStripHeight));
    innerSplitter.setBounds (area);
}

SongsmithMainComponent::SongsmithMainComponent (SongDocument& document)
    : doc (document),
      trackList (document),
      partStrip (document),
      upperRegion (sourceHeader, trackList),
      previewRegion (previewHeader, previewAssignedPanel, previewRoll),
      lowerRegion (partStrip, previewRegion, diagnostics),
      splitter (SplitterComponent::Orientation::topBottom)
{
    sourceHeader.setText (
        juce::String::fromUTF8 ("\xe2\x96\xb2 MIDI SOURCE \xc2\xb7 drag tracks down to assign"),
        juce::dontSendNotification);
    sourceHeader.setColour (juce::Label::backgroundColourId, juce::Colour (SongsmithColours::panelHeader));
    sourceHeader.setColour (juce::Label::textColourId, juce::Colour (0xFF7FA8D0)); // mockup's blue label

    previewHeader.setText (
        juce::String::fromUTF8 ("\xe2\x96\xbc LOTRO PREVIEW \xc2\xb7 how it will sound in-game"),
        juce::dontSendNotification);
    previewHeader.setColour (juce::Label::backgroundColourId, juce::Colour (SongsmithColours::panelHeader));
    previewHeader.setColour (juce::Label::textColourId, juce::Colour (SongsmithColours::accentAmber));

    trackList.onTrackDoubleClicked = [this] (juce::int64 trackId) { trackDoubleClicked (trackId); };
    trackList.onGhostToggled = [this] (juce::int64 trackId, bool visible) { trackGhostToggled (trackId, visible); };
    trackList.isTrackGhosted = [this] (juce::int64 trackId) { return ghostedTrackIds.count (trackId) > 0; };
    partStrip.onPartSelected = [this] (juce::int64 partId) { selectPartForPreview (partId); };

    addAndMakeVisible (upperRegion);
    addAndMakeVisible (lowerRegion);

    // Splitter sits ON TOP of upperRegion/lowerRegion in z-order (same trick
    // as MainWindow::Body's splitter) — its own hit area is transparent to
    // clicks so upperRegion/lowerRegion get them, but its drag bar still
    // gets its own via the second `true`.
    splitter.setComponents (&upperRegion, &lowerRegion);
    addAndMakeVisible (splitter);
}

SongsmithMainComponent::~SongsmithMainComponent()
{
    cancelPendingUpdate();
    if (watchedPartNode.isValid())
        watchedPartNode.removeListener (this);
    for (auto& trackNode : watchedTrackNodes)
        trackNode.removeListener (this);
}

void SongsmithMainComponent::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (SongsmithColours::background));
}

void SongsmithMainComponent::resized()
{
    splitter.setBounds (getLocalBounds());
}

void SongsmithMainComponent::trackDoubleClicked (juce::int64 trackId)
{
    auto trackNode = doc.findTrackById (trackId);
    if (! trackNode.isValid())
        return;

    if (trackEditorWindow == nullptr)
    {
        trackEditorWindow = std::make_unique<TrackEditorWindow> (doc);
        trackEditorWindow->onClosed = [this] { trackEditorWindow.reset(); };
    }

    trackEditorWindow->setTrack (trackNode);
    refreshGhostTracksOnEditor();
}

void SongsmithMainComponent::trackGhostToggled (juce::int64 trackId, bool visible)
{
    if (visible)
        ghostedTrackIds.insert (trackId);
    else
        ghostedTrackIds.erase (trackId);

    refreshGhostTracksOnEditor();
}

void SongsmithMainComponent::refreshGhostTracksOnEditor()
{
    if (trackEditorWindow == nullptr)
        return;

    std::vector<juce::ValueTree> ghosts;
    for (auto ghostId : ghostedTrackIds)
    {
        if (ghostId == trackEditorWindow->getTrackId())
            continue;

        auto node = doc.findTrackById (ghostId);
        if (node.isValid())
            ghosts.push_back (node);
    }
    trackEditorWindow->setGhostTracks (std::move (ghosts));
}

void SongsmithMainComponent::setActiveEditorGridSize (GridSize size)
{
    if (trackEditorWindow == nullptr)
        return;

    const int ticksPerQuarter = (int) doc.getSourceMidiNode().getProperty (SongIDs::ticksPerQuarter, 480);
    trackEditorWindow->setGridTicks (gridSizeToTicks (size, ticksPerQuarter));
}

void SongsmithMainComponent::selectPartForPreview (juce::int64 partId)
{
    if (watchedPartNode.isValid())
        watchedPartNode.removeListener (this);
    for (auto& trackNode : watchedTrackNodes)
        trackNode.removeListener (this);
    watchedTrackNodes.clear();
    watchedPartNode = {};

    selectedPreviewPartId = partId;

    auto partNode = partId >= 0 ? doc.findPartById (partId) : juce::ValueTree();
    if (! partNode.isValid())
    {
        currentPreviewNoteSource.reset();
        previewRoll.setNoteSource (nullptr, 480, {});
        previewRoll.setPreviewRangeBand ({});
        previewAssignedPanel.clear();
        return;
    }

    watchedPartNode = partNode;
    watchedPartNode.addListener (this);

    for (int i = 0; i < SongDocument::getNumAssignments (watchedPartNode); ++i)
    {
        auto assignment = SongDocument::getAssignment (watchedPartNode, i);
        const auto trackId = (juce::int64) assignment.getProperty (SongIDs::trackId);
        auto trackNode = doc.findTrackById (trackId);
        if (! trackNode.isValid())
            continue;

        watchedTrackNodes.push_back (trackNode);
        watchedTrackNodes.back().addListener (this);
    }

    auto result = computePartPreview (doc, partId);
    auto diff = diffPreviewNotes (result);

    currentPreviewNoteSource = std::make_unique<PreviewNoteSource> (diff);
    const int ticksPerQuarter = (int) doc.getSourceMidiNode().getProperty (SongIDs::ticksPerQuarter, 480);
    previewRoll.setNoteSource (currentPreviewNoteSource.get(), ticksPerQuarter, doc.getMeterMapNode());

    LotroInstrument instrument;
    const auto instrumentName = watchedPartNode.getProperty (SongIDs::instrumentName).toString().toStdString();
    if (parseName (instrumentName, instrument).empty())
    {
        const auto range = rangeFor (instrument);
        previewRoll.setPreviewRangeBand ({ range.midiLow, range.midiHigh + 1 });
    }
    else
    {
        previewRoll.setPreviewRangeBand ({});
    }

    previewAssignedPanel.setPreview (doc, partId, result, diff);
}

void SongsmithMainComponent::handleAsyncUpdate()
{
    if (selectedPreviewPartId != -1)
        selectPartForPreview (selectedPreviewPartId);
}

} // namespace lotro
