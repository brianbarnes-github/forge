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

SongsmithMainComponent::LowerRegion::LowerRegion (juce::Component* transportIn, PartStripComponent& partStripIn,
                                                   PreviewRegion& previewRegionIn, DiagnosticListView& diagnosticsIn)
    : transport (transportIn), partStrip (partStripIn), previewRegion (previewRegionIn), diagnostics (diagnosticsIn)
{
    if (transport != nullptr)
        addAndMakeVisible (*transport);
    addAndMakeVisible (partStrip);
    addAndMakeVisible (previewRegion);

    // Hidden by default (View -> Diagnostics list shows it) -- addChildComponent,
    // not addAndMakeVisible, so it starts invisible without an extra call.
    addChildComponent (diagnostics);

    innerSplitter.setComponents (&previewRegion, &diagnostics);
    addChildComponent (innerSplitter);
}

void SongsmithMainComponent::LowerRegion::setDiagnosticsVisible (bool shouldShow)
{
    diagnostics.setVisible (shouldShow);
    resized();
}

void SongsmithMainComponent::LowerRegion::resized()
{
    auto area = getLocalBounds();
    if (transport != nullptr)
        transport->setBounds (area.removeFromTop (TransportStrip::height));
    partStrip.setBounds (area.removeFromTop (partStripHeight));

    if (diagnostics.isVisible())
    {
        innerSplitter.setVisible (true);
        innerSplitter.setBounds (area);
    }
    else
    {
        // No dead gap where diagnostics used to share space via the inner
        // splitter -- the preview region takes all of it directly, bypassing
        // the splitter entirely (it only positions siblings when visible).
        innerSplitter.setVisible (false);
        previewRegion.setBounds (area);
    }
}

SongsmithMainComponent::SongsmithMainComponent (SongDocument& document, PlaybackController* playbackIn)
    : doc (document),
      playback (playbackIn),
      trackList (document),
      partStrip (document),
      transportStrip (playbackIn != nullptr ? std::make_unique<TransportStrip> (*playbackIn) : nullptr),
      upperRegion (sourceHeader, trackList),
      previewRegion (previewHeader, previewAssignedPanel, previewRoll),
      lowerRegion (transportStrip.get(), partStrip, previewRegion, diagnostics),
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

    trackList.setPlayback (playback);
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
        trackEditorWindow = std::make_unique<TrackEditorWindow> (doc, getTopLevelComponent());
        if (playback != nullptr)
            trackEditorWindow->setPlayback (playback);
        trackEditorWindow->onClosed = [this] { trackEditorWindow.reset(); };
        trackEditorWindow->setFollowPlayhead (followPlayheadSetting);
        setActiveEditorGridSize (defaultGridSize);
    }

    trackEditorWindow->setTrack (trackNode);
    refreshGhostTracksOnEditor();
}

void SongsmithMainComponent::setShowRangeBand (bool show)
{
    previewRoll.setShowRangeBand (show);
}

void SongsmithMainComponent::setFollowPlayhead (bool follow)
{
    followPlayheadSetting = follow;
    previewRoll.setFollowPlayhead (follow); // no effect today (no playback controller); kept for symmetry
    trackList.setFollowPlayhead (follow);
    if (trackEditorWindow != nullptr)
        trackEditorWindow->setFollowPlayhead (follow);
}

// Remembered for editors opened later; an editor that is already open keeps its grid.
void SongsmithMainComponent::setDefaultGridSize (GridSize size)
{
    defaultGridSize = size;
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

void SongsmithMainComponent::documentReplaced()
{
    trackEditorWindow.reset();
    ghostedTrackIds.clear();
    trackList.clearSelection();
    partStrip.clearSelection();
    selectPartForPreview (-1);
    trackList.fitTimelineToDocument();
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
