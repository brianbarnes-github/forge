#include "SongsmithMainComponent.h"
#include "PreviewPipeline.h"
#include "PreviewNoteDiff.h"
#include "HistoryKeys.h"
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

SongsmithMainComponent::PreviewRegion::PreviewRegion (PreviewAssignedPanel& panelIn, PianoRollComponent& rollIn)
    : panel (panelIn), roll (rollIn)
{
    addAndMakeVisible (panel);
    addAndMakeVisible (roll);
}

void SongsmithMainComponent::PreviewRegion::resized()
{
    auto area = getLocalBounds();
    panel.setBounds (area.removeFromLeft (previewPanelWidth));
    roll.setBounds (area);
}

SongsmithMainComponent::LowerRegion::LowerRegion (juce::Component* transportIn, dock::DockStack& bottomStackIn,
                                                   DiagnosticListView& diagnosticsIn, dock::DockStack& topStackIn)
    : transport (transportIn), transportDivider (topStackIn), bottomStack (bottomStackIn), diagnostics (diagnosticsIn)
{
    if (transport != nullptr)
    {
        addAndMakeVisible (*transport);
        addAndMakeVisible (transportDivider);
    }
    addAndMakeVisible (bottomStack);

    // Hidden by default (View -> Diagnostics list shows it) -- addChildComponent,
    // not addAndMakeVisible, so it starts invisible without an extra call.
    addChildComponent (diagnostics);

    innerSplitter.setComponents (&bottomStack, &diagnostics);
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
    {
        transport->setBounds (area.removeFromTop (TransportStrip::height));
        transportDivider.setBounds (area.removeFromTop (dock::DockStack::dividerSize));
    }

    if (diagnostics.isVisible() && bottomStack.items().empty())
    {
        // Nothing docked at the bottom: diagnostics take it all, no empty strip and no divider.
        innerSplitter.setVisible (false);
        diagnostics.setBounds (area);
    }
    else if (diagnostics.isVisible())
    {
        innerSplitter.setVisible (true);
        innerSplitter.setBounds (area);
    }
    else
    {
        // No dead gap where diagnostics used to share space via the inner
        // splitter -- the bottom stack takes all of it directly, bypassing
        // the splitter entirely (it only positions siblings when visible).
        innerSplitter.setVisible (false);
        bottomStack.setBounds (area);
    }
}

SongsmithMainComponent::SongsmithMainComponent (SongDocument& document, PlaybackController* playbackIn)
    : doc (document),
      playback (playbackIn),
      trackList (document),
      partStrip (document),
      trackChipBar (document),
      transportStrip (playbackIn != nullptr ? std::make_unique<TransportStrip> (*playbackIn, document) : nullptr),
      upperRegion (sourceHeader, trackList),
      previewRegion (previewAssignedPanel, previewRoll),
      lowerRegion (transportStrip.get(), bottomStack, diagnostics, topStack),
      dockHost (topStack, bottomStack, upperRegion, lowerRegion, nullptr)
{
    sourceHeader.setText (
        juce::String::fromUTF8 ("\xe2\x96\xb2 MIDI SOURCE \xc2\xb7 drag tracks down to assign"),
        juce::dontSendNotification);
    sourceHeader.setColour (juce::Label::backgroundColourId, juce::Colour (SongsmithColours::panelHeader));
    sourceHeader.setColour (juce::Label::textColourId, juce::Colour (SongsmithColours::sourceHeaderLabel));

    trackList.setPlayback (playback);
    trackList.onTrackDoubleClicked = [this] (juce::int64 trackId) { trackDoubleClicked (trackId); };
    trackList.onHeadWidthChanged = [this] (int width) { if (onTrackHeadWidthChanged) onTrackHeadWidthChanged (width); };
    trackList.onConductorMarkerClicked = [this] (TempoMeterKind kind, int tick) { return selectTempoMeterEvent (kind, tick); };
    trackList.onConductorMarkerOpened = [this] (TempoMeterKind kind, int tick) { openTempoMeterEditorOnEvent (kind, tick); };
    trackList.onTempoMeterRequested = [this] { openTempoMeterEditor(); };
    trackList.onViewChanged = [this] { if (tempoMeterWindow != nullptr) tempoMeterWindow->timelineViewChanged(); };
    trackList.onGhostToggled = [this] (juce::int64 trackId, bool visible) { trackGhostToggled (trackId, visible); };
    trackList.isTrackGhosted = [this] (juce::int64 trackId) { return ghostedTrackIds.count (trackId) > 0; };
    trackList.loopSnapGrid = [this] { return currentLoopSnapGrid(); };
    trackList.onTrackAppearanceChanged = [this] (juce::int64 trackId)
    {
        partStrip.trackAppearanceChanged();
        trackEditor->trackAppearanceChanged (trackId);
    };
    trackChipBar.onTrackClicked = [this] (juce::int64 trackId, const juce::ModifierKeys& mods)
    {
        trackList.selectTrackExternally (trackId, mods);
    };
    partStrip.onPartSelected = [this] (juce::int64 partId) { selectPartForPreview (partId); };

    addAndMakeVisible (topStack);

    // The LOTRO preview is the first dockable panel; the dock host puts it in the
    // bottom zone (today's layout) and parents upperRegion/lowerRegion in topStack.
    dockHost.setLowerChromeHeight (transportStrip != nullptr ? TransportStrip::height + dock::DockStack::dividerSize : 0);
    dockHost.addPanel (dock::panelDefs()[0], juce::Colour (SongsmithColours::accentCopper), previewRegion);
    dockHost.panel ("lotroPreview")->setTitle (
        juce::String::fromUTF8 ("\xe2\x96\xbc LOTRO PREVIEW \xc2\xb7 how it will sound in-game"));

    // The Parts strip is the third dockable panel; it sits first in the bottom zone, above the preview.
    dockHost.addPanel (dock::panelDefs()[2], juce::Colour (SongsmithColours::text), partStrip);

    // The Tracks bar (hidden by default) sits above Parts: chips to click or drag onto a part card.
    dockHost.addPanel (dock::panelDefs()[4], juce::Colour (SongsmithColours::text), trackChipBar);
    dockHost.panel ("tracks")->setTitle (juce::String::fromUTF8 ("TRACKS \xc2\xb7 drag onto a part"));

    // The Mixer (hidden by default, last in the bottom zone) drives the source MIDI's playback, so
    // there is none without a controller; the dock host then leaves its layout entry unshown.
    if (playback != nullptr)
    {
        mixerTarget  = std::make_unique<SourceMidiTarget> (doc, *playback);
        activeTarget = std::make_unique<ActivePlaybackTarget> (*mixerTarget);
        mixerPanel   = std::make_unique<MixerPanelComponent> (*activeTarget);
        dockHost.addPanel (dock::panelDefs()[3], juce::Colour (SongsmithColours::text), *mixerPanel);
    }

    trackEditor = std::make_unique<TrackEditorPanel> (doc);
    if (playback != nullptr)
        trackEditor->setPlayback (playback);
    trackEditor->setFollowPlayhead (followPlayheadSetting);
    trackEditor->onTitleChanged = [this] (const juce::String& title) { dockHost.setTitle ("midiEditor", title); };
    trackEditor->onCloseRequested = [this] { setMidiEditorVisible (false); };
    trackEditor->onExportTrackMidi = [this] (juce::int64 trackId)
    {
        if (onExportTrackMidi)
            onExportTrackMidi (trackId);
    };
    trackEditor->onSnapToGridChanged = [this] (bool on)
    {
        if (onSnapToGridChanged)
            onSnapToGridChanged (on);
    };
    trackEditor->onNotePreviewChanged = [this] (const NotePreviewSettings& settings)
    {
        if (onNotePreviewChanged)
            onNotePreviewChanged (settings);
    };
    dockHost.addPanel (dock::panelDefs()[1], juce::Colour (SongsmithColours::text), *trackEditor);
    dockHost.setTitle ("midiEditor", trackEditor->title());

    dockHost.onLayoutChanged = [this] (const dock::LayoutState& layout)
    {
        if (onDockLayoutChanged)
            onDockLayoutChanged (layout);
    };
    dockHost.onFloatingKey = [this] (const juce::KeyPress& key)
    {
        if (key == juce::KeyPress (juce::KeyPress::escapeKey))
            return escapePressed();
        const bool save = key == juce::KeyPress ('s', juce::ModifierKeys::commandModifier, 0);
        const bool del = key == juce::KeyPress (juce::KeyPress::deleteKey);
        return (save || del || historyActionFor (key).has_value()) && onFloatingShortcut && onFloatingShortcut (key);
    };
    dockHost.apply (dock::defaultLayout());
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
    topStack.setBounds (getLocalBounds());
}

void SongsmithMainComponent::trackDoubleClicked (juce::int64 trackId)
{
    auto trackNode = doc.findTrackById (trackId);
    if (! trackNode.isValid())
        return;

    // A hidden editor, or one emptied by New / Open, starts afresh on the default grid; one that
    // is showing a track keeps its grid.
    const bool keepsGrid = isTrackEditorOpen() && trackEditor->getTrackId() != -1;
    dockHost.setCentreFloatsAround (getTopLevelComponent());
    trackEditor->setTrack (trackNode);
    dockHost.setVisible ("midiEditor", true);
    if (! keepsGrid)
    {
        trackEditor->setGridDivision (GridDivision::Straight);
        setActiveEditorGridSize (defaultGridSize);
    }
    if (trackEditor->isShowing())
        trackEditor->grabKeyboardFocus();   // now that it is shown, so Space reaches it
    refreshGhostTracksOnEditor();
}

void SongsmithMainComponent::restoreDockLayout (dock::LayoutState layout)
{
    if (trackEditor->getTrackId() == -1)
        dock::hide (layout, "midiEditor");
    dockHost.apply (layout);
}

void SongsmithMainComponent::setMergeScope (MergeScope scope)
{
    trackList.setMergeScope (scope);
}

void SongsmithMainComponent::setStripButtonsHost (StripButtonsHost* host)
{
    if (transportStrip != nullptr)
        transportStrip->setButtonsHost (host);
    trackEditor->setStripButtonsHost (host);
}

void SongsmithMainComponent::setFollowPlayhead (bool follow)
{
    followPlayheadSetting = follow;
    previewRoll.setFollowPlayhead (follow); // no effect today (no playback controller); kept for symmetry
    trackList.setFollowPlayhead (follow);
    trackEditor->setFollowPlayhead (follow);
}

void SongsmithMainComponent::setSnapToGrid (bool on)
{
    trackEditor->setSnapToGrid (on);
}

void SongsmithMainComponent::setNotePreview (const NotePreviewSettings& settings)
{
    trackEditor->setNotePreview (settings);
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
    if (trackEditor->getTrackId() == -1)
        return;

    std::vector<juce::ValueTree> ghosts;
    for (auto ghostId : ghostedTrackIds)
    {
        if (ghostId == trackEditor->getTrackId())
            continue;

        auto node = doc.findTrackById (ghostId);
        if (node.isValid())
            ghosts.push_back (node);
    }
    trackEditor->setGhostTracks (std::move (ghosts));
}

void SongsmithMainComponent::setActiveEditorGridSize (GridSize size)
{
    if (trackEditor->getTrackId() == -1)
        return;

    trackEditor->setGridLength (gridLengthForSize (size));
}

EditorGrid SongsmithMainComponent::currentLoopSnapGrid() const
{
    if (isTrackEditorLoaded())
        return trackEditor->getEditorGrid();
    const int ticksPerQuarter = (int) doc.getSourceMidiNode().getProperty (SongIDs::ticksPerQuarter, 480);
    return EditorGrid::straight (gridSizeToTicks (defaultGridSize, ticksPerQuarter));
}

bool SongsmithMainComponent::escapePressed()
{
    // One action per press, in this order.
    if (trackList.cancelSectionDrag())
        return true;
    if (trackEditor->isNoteGestureActive())
        return false;   // the editor has no Escape of its own; the press must not reach past its drag
    if (playback == nullptr || ! playback->getLoopRange().has_value())
        return false;
    playback->clearLoopRange();
    return true;
}

void SongsmithMainComponent::openTempoMeterEditor()
{
    if (tempoMeterWindow == nullptr)
    {
        tempoMeterWindow = std::make_unique<TempoMeterEditorWindow> (doc, getTopLevelComponent());
        tempoMeterWindow->setTimelineView (&trackList.getTimelineView());
        tempoMeterWindow->onSelectedTickChanged = [this] (std::optional<int> tick) { trackList.setEventMarkerTick (tick); };
        tempoMeterWindow->onClosed = [this]
        {
            trackList.setEventMarkerTick (std::nullopt);
            tempoMeterWindow.reset();
        };
    }
    tempoMeterWindow->toFront (true);
}

bool SongsmithMainComponent::selectTempoMeterEvent (TempoMeterKind kind, int tick)
{
    if (tempoMeterWindow == nullptr)
        return false;
    tempoMeterWindow->selectEvent (kind, tick);
    tempoMeterWindow->toFront (true);
    return true;
}

void SongsmithMainComponent::openTempoMeterEditorOnEvent (TempoMeterKind kind, int tick)
{
    openTempoMeterEditor();
    selectTempoMeterEvent (kind, tick);
}

void SongsmithMainComponent::documentReplaced()
{
    tempoMeterWindow.reset();
    trackList.setEventMarkerTick (std::nullopt);
    trackEditor->clearTrack();
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
