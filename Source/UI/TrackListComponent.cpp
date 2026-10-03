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

    horizontalBar.setAutoHide (true);
    horizontalBar.addListener (this);
    addAndMakeVisible (horizontalBar); // auto-hide still decides whether it shows

    sourceMidiNode.addListener (this);
    rebuild();
}

TrackListComponent::~TrackListComponent()
{
    // Cancel before removing the listener so a rebuild can't fire against a
    // component that is mid-destruction.
    cancelPendingUpdate();
    sourceMidiNode.removeListener (this);
    horizontalBar.removeListener (this);
}

void TrackListComponent::rebuild()
{
    content.rows.clear();

    for (int i = 0; i < doc.getNumTracks(); ++i)
    {
        auto* row = content.rows.add (new TrackRowComponent (doc.getTrack (i), i, timelineView));
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
    syncHorizontalBar(); // the song may have got longer or shorter.
    repaint(); // M4: empty-state message visibility may have changed.
}

int TrackListComponent::documentEndTick() const
{
    int lastTick = 0;
    for (int t = 0; t < doc.getNumTracks(); ++t)
    {
        auto track = doc.getTrack (t);
        auto notes = SongDocument::getNotesNode (track);
        for (int n = 0; n < notes.getNumChildren(); ++n)
        {
            auto note = notes.getChild (n);
            const int endTick = (int) note.getProperty (SongIDs::startTick)
                               + (int) note.getProperty (SongIDs::durationTicks);
            lastTick = juce::jmax (lastTick, endTick);
        }
    }
    return lastTick;
}

int TrackListComponent::previewWidth() const
{
    return juce::jmax (0, contentWidth() - notePreviewOriginX());
}

void TrackListComponent::fitTimelineToDocument()
{
    timelineView.fitToWidth ((double) documentEndTick(), previewWidth());
    timelineFitted = true;
    syncHorizontalBar();
    content.repaint();
}

void TrackListComponent::syncHorizontalBar()
{
    const double endTick = (double) documentEndTick();
    const double visibleTicks = (double) previewWidth() / timelineView.getPixelsPerTick();
    const double maxOffset = juce::jmax (0.0, endTick - visibleTicks);

    if (timelineView.getScrollOffsetTicks() > maxOffset)
        timelineView.setScrollOffsetTicks (maxOffset);

    // Never let the total range be shorter than the visible span, so a song
    // that fits (or an empty document) reads as "fully visible" and the bar
    // auto-hides instead of showing a thumb that can't move.
    horizontalBar.setRangeLimits (0.0, juce::jmax (endTick, visibleTicks), juce::dontSendNotification);
    horizontalBar.setCurrentRange (timelineView.getScrollOffsetTicks(), visibleTicks, juce::dontSendNotification);
    horizontalBar.setSingleStepSize (visibleTicks / 20.0);
}

void TrackListComponent::scrollBarMoved (juce::ScrollBar*, double newRangeStart)
{
    timelineView.setScrollOffsetTicks (newRangeStart);
    content.repaint();
}

void TrackListComponent::clearSelection()
{
    selectTrack (-1);
}

void TrackListComponent::selectTrack (juce::int64 trackId)
{
    selectedTrackId = trackId;
    for (auto* row : content.rows)
        row->setSelected (row->getTrackId() == trackId);
}

int TrackListComponent::contentWidth() const
{
    return viewport.getWidth() - viewport.getScrollBarThickness();
}

int TrackListComponent::notePreviewOriginX() const
{
    // Mirrors TrackRowComponent::resized()'s own jmin, so the two agree when
    // the list is narrower than the fixed info column.
    return juce::jmin (TrackRowComponent::trackInfoWidth, contentWidth());
}

void TrackListComponent::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    if (e.mods.isCtrlDown() || e.mods.isCommandDown())
    {
        timelineView.zoomBy (wheel.deltaY > 0.0f ? 1.1 : 1.0 / 1.1,
                              e.getPosition().getX() - notePreviewOriginX());
        timelineFitted = false;
    }
    else
    {
        timelineView.scrollByPixels (juce::roundToInt ((-wheel.deltaX - wheel.deltaY) * 50.0f));
    }

    syncHorizontalBar();
    content.repaint();
}

void TrackListComponent::resized()
{
    // The horizontal bar's strip is always reserved (even while it
    // auto-hides) so rows don't jump when it appears.
    auto area = getLocalBounds();
    auto barStrip = area.removeFromBottom (viewport.getScrollBarThickness());
    viewport.setBounds (area);

    horizontalBar.setBounds (barStrip.withLeft (notePreviewOriginX())
                                     .withWidth (previewWidth()));

    content.setSize (contentWidth(), doc.getNumTracks() * TrackRowComponent::rowHeight);
    content.resized();

    if (timelineFitted)
        timelineView.fitToWidth ((double) documentEndTick(), previewWidth());

    syncHorizontalBar();
}

void TrackListComponent::paint (juce::Graphics& g)
{
    // Slightly darker than the shared background, matching the mockup's
    // MIDI-track-list panel (#262626 there vs. #2b2b2b for the background).
    g.fillAll (juce::Colour (SongsmithColours::background).darker (0.15f));

    // M4: first-launch/empty-document affordance.
    if (doc.getNumTracks() <= 1) // only the conductor: nothing imported yet
    {
        g.setColour (juce::Colour (SongsmithColours::textMuted));
        g.setFont (juce::Font (juce::FontOptions (11.0f)));
        g.drawFittedText (juce::String::fromUTF8 (
                               "No MIDI loaded \xe2\x80\x94 File \xe2\x86\x92 Open MIDI\xe2\x80\xa6 or drop a .mid here"),
                           getLocalBounds().reduced (12), juce::Justification::centred, 4);
    }
}

} // namespace lotro
