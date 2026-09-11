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

TrackListComponent::TrackListComponent (SongDocument& document) : doc (document)
{
    viewport.setViewedComponent (&content, false);
    viewport.setScrollBarsShown (true, false);
    addAndMakeVisible (viewport);

    doc.getSourceMidiNode().addListener (this);
    rebuild();
}

TrackListComponent::~TrackListComponent()
{
    doc.getSourceMidiNode().removeListener (this);
}

void TrackListComponent::rebuild()
{
    content.rows.clear();

    for (int i = 0; i < doc.getNumTracks(); ++i)
    {
        auto* row = content.rows.add (new TrackRowComponent (doc.getTrack (i), i + 1));
        row->setSelected (row->getTrackId() == selectedTrackId);
        row->onTrackSelected = [this] (juce::int64 trackId) { selectTrack (trackId); };
        content.addAndMakeVisible (row);
    }

    content.setSize (viewport.getWidth(), doc.getNumTracks() * TrackRowComponent::rowHeight);
    content.resized();
}

void TrackListComponent::selectTrack (juce::int64 trackId)
{
    selectedTrackId = trackId;
    for (auto* row : content.rows)
        row->setSelected (row->getTrackId() == trackId);

    if (onTrackSelected)
        onTrackSelected (trackId);
}

void TrackListComponent::resized()
{
    viewport.setBounds (getLocalBounds());
    content.setSize (viewport.getWidth() - viewport.getScrollBarThickness(),
                      doc.getNumTracks() * TrackRowComponent::rowHeight);
    content.resized();
}

void TrackListComponent::paint (juce::Graphics& g)
{
    // Slightly darker than the shared background, matching the mockup's
    // MIDI-track-list panel (#262626 there vs. #2b2b2b for the background).
    g.fillAll (juce::Colour (SongsmithColours::background).darker (0.15f));
}

} // namespace lotro
