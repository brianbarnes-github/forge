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
    // Cancel before removing the listener so a rebuild can't fire against a
    // component that is mid-destruction.
    cancelPendingUpdate();
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

    // M5: the previously-selected track may have just disappeared (e.g. all
    // tracks removed, or SongDocument::removeTrack on this one specifically)
    // — clear the stale selection and notify via the same callback used for
    // a real click, so listeners (SongsmithMainComponent) can drop it too
    // instead of keeping the now-orphaned track's notes on screen.
    if (selectedTrackId != -1 && ! doc.findTrackById (selectedTrackId).isValid())
    {
        selectedTrackId = -1;
        if (onTrackSelected)
            onTrackSelected (selectedTrackId);
    }

    content.setSize (contentWidth(), doc.getNumTracks() * TrackRowComponent::rowHeight);
    content.resized();
    repaint(); // M4: empty-state message visibility may have changed.
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
