#include "TrackChipBar.h"
#include "SongsmithColours.h"

namespace lotro
{

namespace
{
    constexpr int chipHeight = 24, chipGap = 4, padding = 4, swatchWidth = 6;
}

TrackChipBar::Chip::Chip (juce::int64 trackIdIn, juce::String labelIn, juce::Colour colourIn)
    : trackId (trackIdIn), label (std::move (labelIn)), colour (colourIn)
{
    setMouseCursor (juce::MouseCursor::DraggingHandCursor);
}

void TrackChipBar::Chip::paint (juce::Graphics& g)
{
    auto area = getLocalBounds().toFloat();
    g.setColour (juce::Colour (SongsmithColours::panelHeader));
    g.fillRoundedRectangle (area, 3.0f);
    g.setColour (colour);
    g.fillRoundedRectangle (area.removeFromLeft ((float) swatchWidth), 3.0f);

    g.setColour (juce::Colour (SongsmithColours::text));
    g.setFont (juce::Font (juce::FontOptions (12.0f)));
    g.drawText (label, area.withTrimmedLeft (6.0f).withTrimmedRight (4.0f), juce::Justification::centredLeft, true);
}

void TrackChipBar::Chip::mouseDown (const juce::MouseEvent& e)
{
    if (onClicked)
        onClicked (trackId, e.mods);
}

void TrackChipBar::Chip::mouseDrag (const juce::MouseEvent& e)
{
    // Only once the mouse has really moved, so a plain click does not also start a drag.
    if (e.getDistanceFromDragStart() < 4)
        return;

    if (auto* container = juce::DragAndDropContainer::findParentDragContainerFor (this))
        if (! container->isDragAndDropActive())
            // Other JUCE windows too: a floating Parts panel is its own window.
            container->startDragging (dragDescription(), this, juce::ScaledImage(), true);
}

TrackChipBar::TrackChipBar (SongDocument& document)
    : doc (document), sourceMidiNode (document.getSourceMidiNode())
{
    viewport.setViewedComponent (&row, false);
    viewport.setScrollBarsShown (false, true);   // horizontal only
    addAndMakeVisible (viewport);

    sourceMidiNode.addListener (this);
    refresh();
}

TrackChipBar::~TrackChipBar()
{
    // Cancel before removing the listener so an update cannot fire mid-destruction.
    cancelPendingUpdate();
    sourceMidiNode.removeListener (this);
}

std::vector<TrackChipBar::ChipInfo> TrackChipBar::currentInfos() const
{
    std::vector<ChipInfo> out;
    for (int i = 0; i < doc.getNumTracks(); ++i)
    {
        const auto track = doc.getTrack (i);
        if (! SongDocument::isAssignableTrack (track))
            continue;
        out.push_back ({ (juce::int64) track.getProperty (SongIDs::trackId),
                         juce::String (i) + "  " + track.getProperty (SongIDs::name).toString(),
                         juce::Colour ((juce::uint32) (int) track.getProperty (SongIDs::colorArgb)) });
    }
    return out;
}

void TrackChipBar::refresh()
{
    auto now = currentInfos();
    if (now == infos)
        return;

    infos = std::move (now);
    row.chips.clear();
    for (const auto& info : infos)
    {
        auto* chip = row.chips.add (new Chip (info.trackId, info.label, info.colour));
        chip->onClicked = [this] (juce::int64 id, const juce::ModifierKeys& m) { if (onTrackClicked) onTrackClicked (id, m); };
        row.addAndMakeVisible (chip);
    }
    resized();
    repaint();
}

void TrackChipBar::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (SongsmithColours::background));
    if (! infos.empty())
        return;

    g.setColour (juce::Colour (SongsmithColours::textMuted));
    g.setFont (juce::Font (juce::FontOptions (11.0f)));
    g.drawText ("No tracks with notes", getLocalBounds().reduced (padding * 2), juce::Justification::centredLeft, true);
}

void TrackChipBar::resized()
{
    viewport.setBounds (getLocalBounds());

    const int n = row.chips.size();
    const int contentWidth = n == 0 ? viewport.getWidth() : padding * 2 + n * chipWidth + (n - 1) * chipGap;
    row.setSize (std::max (contentWidth, viewport.getWidth()), viewport.getMaximumVisibleHeight());

    int x = padding;
    for (auto* chip : row.chips)
    {
        chip->setBounds (x, (row.getHeight() - chipHeight) / 2, chipWidth, chipHeight);
        x += chipWidth + chipGap;
    }
}

} // namespace lotro
