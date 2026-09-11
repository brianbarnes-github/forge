#include "TrackRowComponent.h"
#include "SongsmithColours.h"

#include <algorithm>
#include <limits>

namespace lotro
{

namespace
{
    // Standard MIDI naming (60 = C4, "middle C") — matches the Songsmith UI
    // Guide mockup's own numbers (LuteOfAges' 36..72 MIDI range is captioned
    // "Range: C2 - C5" there).
    juce::String pitchName (int midiPitch)
    {
        static const char* const names[12] =
            { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
        const int octave = midiPitch / 12 - 1;
        const int pitchClass = ((midiPitch % 12) + 12) % 12;
        return juce::String (names[pitchClass]) + juce::String (octave);
    }
}

TrackRowComponent::TrackRowComponent (juce::ValueTree trackNode, int displayIndex)
    : track (std::move (trackNode)), index (displayIndex)
{
    jassert (track.hasType (SongIDs::MIDI_TRACK));
    setInterceptsMouseClicks (true, false);
}

juce::int64 TrackRowComponent::getTrackId() const
{
    return (juce::int64) track.getProperty (SongIDs::trackId);
}

void TrackRowComponent::setSelected (bool shouldBeSelected)
{
    if (selected == shouldBeSelected) return;
    selected = shouldBeSelected;
    repaint();
}

juce::String TrackRowComponent::buildSecondLine() const
{
    const int numNotes = track.getNumChildren();
    juce::String line = juce::String (numNotes) + " notes";

    const int channel = (int) track.getProperty (SongIDs::sourceMidiChannel);
    if (channel == 10)
    {
        line += juce::String (" \xc2\xb7 ch 10"); // " · ch 10"
        return line;
    }

    if (numNotes == 0)
        return line;

    int lowest = std::numeric_limits<int>::max();
    int highest = std::numeric_limits<int>::min();
    for (int i = 0; i < numNotes; ++i)
    {
        const int pitch = (int) track.getChild (i).getProperty (SongIDs::pitch);
        lowest  = std::min (lowest, pitch);
        highest = std::max (highest, pitch);
    }

    line += juce::String (" \xc2\xb7 ") + pitchName (lowest) + "\xe2\x80\x93" + pitchName (highest); // " · lo–hi"
    return line;
}

void TrackRowComponent::paint (juce::Graphics& g)
{
    using namespace SongsmithColours;

    const auto swatch = (juce::uint32) (int) track.getProperty (SongIDs::colorArgb);
    const auto bounds = getLocalBounds();

    g.setColour (juce::Colour (selected ? selectedRow : background));
    g.fillRect (bounds);

    // Left accent bar — 3px, in the track's own swatch colour (thicker/only
    // visible for the selected row, matching the mockup's Track 1 example).
    if (selected)
    {
        g.setColour (juce::Colour (swatch));
        g.fillRect (bounds.withWidth (3));
    }

    const int textLeft = 8;
    auto row = bounds.reduced (0, 0).withTrimmedLeft (textLeft).withTrimmedRight (6);
    auto firstLine  = row.removeFromTop (row.getHeight() / 2);
    auto secondLine = row;

    // First line: index, name, swatch square.
    auto indexArea = firstLine.removeFromLeft (16);
    g.setColour (juce::Colour (textMuted));
    g.setFont (juce::Font (juce::Font::getDefaultMonospacedFontName(), 11.0f, juce::Font::plain));
    g.drawText (juce::String (index), indexArea, juce::Justification::centredLeft);

    auto swatchArea = firstLine.removeFromRight (10).withSizeKeepingCentre (10, 10);
    g.setColour (juce::Colour (swatch));
    g.fillRect (swatchArea);

    g.setColour (juce::Colour (text));
    g.setFont (juce::Font (11.0f));
    g.drawText (track.getProperty (SongIDs::name).toString(), firstLine.withTrimmedRight (4),
                juce::Justification::centredLeft);

    // Second line: note count / range.
    g.setColour (juce::Colour (textMuted));
    g.setFont (juce::Font (9.0f));
    g.drawText (buildSecondLine(), secondLine, juce::Justification::centredLeft);
}

void TrackRowComponent::mouseDown (const juce::MouseEvent&)
{
    if (onTrackSelected)
        onTrackSelected (getTrackId());
}

void TrackRowComponent::mouseDrag (const juce::MouseEvent& e)
{
    // Only start a drag once the mouse has actually moved a few pixels, so a
    // plain click (already handled in mouseDown) doesn't also fire a
    // zero-distance drag.
    if (e.getDistanceFromDragStart() < 4)
        return;

    if (auto* container = juce::DragAndDropContainer::findParentDragContainerFor (this))
    {
        if (! container->isDragAndDropActive())
            container->startDragging (juce::var (getTrackId()), this);
    }
}

} // namespace lotro
