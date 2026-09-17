#include "TrackListComponent.h"
#include "SongsmithColours.h"

namespace lotro
{

void TrackListComponent::ListContent::resized()
{
    int y = 0;
    for (auto* row : rows)
    {
        row->setBounds (0, y, getWidth(), TrackRowComponent::rowHeight);
        y += TrackRowComponent::rowHeight;
    }
}

TrackListComponent::TrackListComponent (SongDocument& document)
    : doc (document), sourceMidiNode (document.getSourceMidiNode())
{
    viewport.setViewedComponent (&content, false);
    viewport.setScrollBarsShown (true, false);
    addAndMakeVisible (viewport);

    sourceMidiNode.addListener (this);
    rebuild();
}

TrackListComponent::~TrackListComponent()
{
    // Cancel before removing the listener so a rebuild can't fire against a
    // component that is mid-destruction.
    cancelPendingUpdate();
    sourceMidiNode.removeListener (this);
}

void TrackListComponent::rebuild()
{
    content.rows.clear();

    for (int i = 0; i < doc.getNumTracks(); ++i)
    {
        auto* row = content.rows.add (new TrackRowComponent (doc.getTrack (i), i + 1, timelineView));
        row->setSelected (row->getTrackId() == selectedTrackId);
        row->setGhostVisible (isTrackGhosted != nullptr && isTrackGhosted (row->getTrackId()));
        row->onTrackSelected = [this] (juce::int64 trackId) { selectTrack (trackId); };
        row->onTrackDoubleClicked = [this] (juce::int64 trackId) { if (onTrackDoubleClicked) onTrackDoubleClicked (trackId); };
        row->onGhostToggled = [this] (juce::int64 trackId, bool visible) { if (onGhostToggled) onGhostToggled (trackId, visible); };
        content.addAndMakeVisible (row);
    }

    // M5: the previously-selected track may have just disappeared (e.g. all
    // tracks removed, or SongDocument::removeTrack on this one specifically)
    // — clear the stale selection so it isn't reported as still live below.
    if (selectedTrackId != -1 && ! doc.findTrackById (selectedTrackId).isValid())
        selectedTrackId = -1;

    content.setSize (contentWidth(), doc.getNumTracks() * TrackRowComponent::rowHeight);
    content.resized();
    repaint(); // M4: empty-state message visibility may have changed.

    // I1: unconditionally re-fire onTrackSelected with whatever is currently
    // selected (including "nothing"), not only when the selection just
    // disappeared. A rebuild means something under SOURCE_MIDI changed —
    // that can affect a *live* selection too (e.g. a second MIDI import
    // raising the document's ticksPerQuarter, Phase 5's I1) without the
    // selected track itself ever disappearing, so the disappearance check
    // alone isn't enough to tell callers (SongsmithMainComponent) to re-read
    // fresh document state and refit the piano roll.
    if (onTrackSelected)
        onTrackSelected (selectedTrackId);
}

void TrackListComponent::selectTrack (juce::int64 trackId)
{
    selectedTrackId = trackId;
    for (auto* row : content.rows)
        row->setSelected (row->getTrackId() == trackId);

    if (onTrackSelected)
        onTrackSelected (trackId);
}

int TrackListComponent::contentWidth() const
{
    return viewport.getWidth() - viewport.getScrollBarThickness();
}

void TrackListComponent::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    if (e.mods.isCtrlDown() || e.mods.isCommandDown())
        timelineView.zoomBy (wheel.deltaY > 0.0f ? 1.1 : 1.0 / 1.1, e.getPosition().getX());
    else
        timelineView.scrollByPixels (juce::roundToInt ((-wheel.deltaX - wheel.deltaY) * 50.0f));

    content.repaint();
}

void TrackListComponent::resized()
{
    viewport.setBounds (getLocalBounds());
    content.setSize (contentWidth(), doc.getNumTracks() * TrackRowComponent::rowHeight);
    content.resized();
}

void TrackListComponent::paint (juce::Graphics& g)
{
    // Slightly darker than the shared background, matching the mockup's
    // MIDI-track-list panel (#262626 there vs. #2b2b2b for the background).
    g.fillAll (juce::Colour (SongsmithColours::background).darker (0.15f));

    // M4: first-launch/empty-document affordance.
    if (doc.getNumTracks() == 0)
    {
        g.setColour (juce::Colour (SongsmithColours::textMuted));
        g.setFont (juce::Font (juce::FontOptions (11.0f)));
        g.drawFittedText (juce::String::fromUTF8 (
                               "No MIDI loaded \xe2\x80\x94 File \xe2\x86\x92 Open MIDI\xe2\x80\xa6 or drop a .mid here"),
                           getLocalBounds().reduced (12), juce::Justification::centred, 4);
    }
}

} // namespace lotro
