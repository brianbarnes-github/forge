#include "TrackRowComponent.h"
#include "SongsmithColours.h"
#include "GmProgramNames.h"
#include "InstrumentEdit.h"
#include "ProgramChanges.h"

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

TrackRowComponent::TrackRowComponent (juce::ValueTree trackNode, int displayIndex, const TimelineViewState& viewState)
    : track (trackNode), index (displayIndex), timelineView (viewState), notePreview (trackNode, viewState)
{
    jassert (track.hasType (SongIDs::MIDI_TRACK));
    setInterceptsMouseClicks (true, false);

    addAndMakeVisible (notePreview);

    const bool isConductor = (bool) track.getProperty (SongIDs::isConductor, false);
    for (auto* b : { &muteButton, &soloButton })
    {
        b->setClickingTogglesState (true);
        b->setWantsKeyboardFocus (false);
        addChildComponent (*b);
        b->setVisible (! isConductor);
    }
    muteButton.setColour (juce::TextButton::buttonOnColourId, juce::Colours::orangered);
    soloButton.setColour (juce::TextButton::buttonOnColourId, juce::Colours::gold);
    muteButton.setTooltip ("Mute");
    soloButton.setTooltip ("Solo");
    muteButton.onClick = [this] { if (onMuteToggled) onMuteToggled (getTrackId(), muteButton.getToggleState()); };
    soloButton.onClick = [this] { if (onSoloToggled) onSoloToggled (getTrackId(), soloButton.getToggleState()); };
    notePreview.onGhostToggled = [this] (bool visible)
    {
        if (onGhostToggled)
            onGhostToggled (getTrackId(), visible);
    };
    notePreview.onNonToggleClick = [this] (const juce::ModifierKeys& mods)
    {
        if (onStripSelected)
            onStripSelected (getTrackId(), mods);
    };
    notePreview.onTimelineClicked = [this] (int tick)
    {
        if (onTimelineClicked)
            onTimelineClicked (tick);
    };
    notePreview.onSectionPressed = [this] (const SectionHit& hit, int tick, const juce::ModifierKeys& mods)
    {
        if (onSectionPressed)
            onSectionPressed (getTrackId(), hit, tick, mods);
    };
    notePreview.onSectionDragged = [this] (int tick)
    {
        if (onSectionDragged)
            onSectionDragged (tick);
    };
    notePreview.onSectionReleased = [this] (int tick)
    {
        if (onSectionReleased)
            onSectionReleased (tick);
    };
    notePreview.onMergePressed = [this] (const SectionHit& hit, const juce::ModifierKeys& mods)
    {
        if (onMergePressed)
            onMergePressed (getTrackId(), hit, mods);
    };
    notePreview.onMergeDragged = [this] (juce::Point<int> p, const juce::ModifierKeys& mods)
    {
        return onMergeDragged && onMergeDragged (p, mods);
    };
    notePreview.onMergeReleased = [this] (juce::Point<int> p, const juce::ModifierKeys& mods)
    {
        if (onMergeReleased)
            onMergeReleased (p, mods);
    };
    notePreview.onNonToggleDoubleClick = [this]
    {
        if (canOpenEditor() && onTrackDoubleClicked)
            onTrackDoubleClicked (getTrackId());
    };
}

bool TrackRowComponent::canDrag() const
{
    return SongDocument::isAssignableTrack (track);
}

bool TrackRowComponent::canOpenEditor() const
{
    return track.hasType (SongIDs::MIDI_TRACK)
        && ! (bool) track.getProperty (SongIDs::isConductor, false);
}

void TrackRowComponent::resized()
{
    auto area = getLocalBounds().withTrimmedBottom (dividerThickness);
    auto info = area.removeFromLeft (juce::jmin (trackInfoWidth, area.getWidth()));
    info.removeFromRight (columnDividerThickness);
    auto buttons = info.removeFromRight (muteSoloWidth).reduced (1, 8);
    muteButton.setBounds (buttons.removeFromLeft (buttons.getWidth() / 2));
    soloButton.setBounds (buttons);
    area.removeFromTop (juce::jmin (instrumentBandHeight, area.getHeight()));
    notePreview.setBounds (area);
}

void TrackRowComponent::setMuteSolo (bool muted, bool soloed, bool silenced)
{
    muteButton.setToggleState (muted, juce::dontSendNotification);
    soloButton.setToggleState (soloed, juce::dontSendNotification);
    silencedBySolo = silenced;
    setAlpha ((muted || silenced) ? 0.5f : 1.0f);
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

void TrackRowComponent::setSectionView (const SectionViewState* state)
{
    notePreview.setSectionView (state, getTrackId());
}

void TrackRowComponent::setGhostVisible (bool shouldBeVisible)
{
    notePreview.setGhostVisible (shouldBeVisible);
}

juce::String TrackRowComponent::instrumentLabel() const
{
    if ((bool) track.getProperty (SongIDs::isConductor, false))
        return {};
    if ((int) track.getProperty (SongIDs::sourceMidiChannel) == 10)
        return "Drum Kit";
    return gmProgramName ((int) track.getProperty (SongIDs::sourceProgram));
}

juce::String TrackRowComponent::buildSecondLine() const
{
    const int numEvents = SongDocument::getEventsNode (track).getNumChildren();
    if ((bool) track.getProperty (SongIDs::isConductor, false))
        return juce::String (numEvents) + " events";

    const int numNotes = SongDocument::getNotesNode (track).getNumChildren();
    if (numNotes == 0)
        return juce::String (numNotes) + " notes" + juce::String::fromUTF8 (" \xc2\xb7 ")
             + juce::String (numEvents) + " events";

    juce::String line = juce::String (numNotes) + " notes";

    const int channel = (int) track.getProperty (SongIDs::sourceMidiChannel);
    if (channel == 10)
    {
        line += juce::String (" \xc2\xb7 ch 10"); // " · ch 10"
        return line;
    }

    int lowest = std::numeric_limits<int>::max();
    int highest = std::numeric_limits<int>::min();
    for (int i = 0; i < numNotes; ++i)
    {
        const int pitch = (int) SongDocument::getNotesNode (track).getChild (i).getProperty (SongIDs::pitch);
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
    auto bounds = getLocalBounds();

    g.setColour (juce::Colour (trackDivider));
    g.fillRect (bounds.removeFromBottom (dividerThickness));

    g.setColour (juce::Colour (selected ? selectedRow : background));
    g.fillRect (bounds);

    // Left accent bar — 3px, in the track's own swatch colour (thicker/only
    // visible for the selected row, matching the mockup's Track 1 example).
    if (selected)
    {
        g.setColour (juce::Colour (swatch));
        g.fillRect (bounds.withWidth (3));
    }

    // Vertical line between the label column and the note preview.
    g.setColour (juce::Colour (columnDivider));
    g.fillRect (bounds.withWidth (juce::jmin (trackInfoWidth, bounds.getWidth()))
                      .removeFromRight (columnDividerThickness));

    // Instrument band: canvas side only, over the top of the notes. One coloured,
    // labelled segment per instrument; drum and conductor rows keep a single label.
    {
        const auto band = bounds.withTrimmedLeft (juce::jmin (trackInfoWidth, bounds.getWidth()))
                                .removeFromTop (instrumentBandHeight);
        g.setColour (juce::Colour (instrumentBand));
        g.fillRect (band);
        g.setFont (juce::Font (juce::FontOptions (10.0f)));

        const bool plainLabel = isConductorTrack() || (int) track.getProperty (SongIDs::sourceMidiChannel) == 10;
        const auto segments = instrumentSegmentsOf (track);
        if (plainLabel || segments.size() < 2)
        {
            g.setColour (juce::Colour (textMuted));
            g.drawText (instrumentLabel(), band.withTrimmedLeft (6).withTrimmedRight (4), juce::Justification::centredLeft);
        }
        else
        {
            for (const auto& s : bandSegments (segments, timelineView))
            {
                const auto cell = juce::Rectangle<int> (band.getX() + s.x0, band.getY(), s.x1 - s.x0, band.getHeight())
                                      .getIntersection (band);
                if (cell.isEmpty())
                    continue;
                const auto family = gmFamilyFor (s.program, 1);
                g.setColour (juce::Colour (familyBaseColour[(int) family]).withAlpha (0.55f));
                g.fillRect (cell);
                g.setColour (juce::Colour (columnDivider));
                g.fillRect (cell.withWidth (1));
                g.setColour (juce::Colour (text));
                g.drawText (gmProgramName (s.program), cell.withTrimmedLeft (4).withTrimmedRight (2), juce::Justification::centredLeft);
            }
        }
    }

    const int textLeft = 8;
    const bool isConductor = (bool) track.getProperty (SongIDs::isConductor, false);
    // Keep the text clear of the M / S buttons (the conductor has none).
    auto row = bounds.withWidth (juce::jmin (trackInfoWidth, bounds.getWidth()))
                      .withTrimmedLeft (textLeft).withTrimmedRight (6)
                      .withTrimmedRight (isConductor ? 0 : muteSoloWidth);
    auto firstLine  = row.removeFromTop (row.getHeight() / 2);
    auto secondLine = row;

    // First line: index, name, swatch square.
    auto indexArea = firstLine.removeFromLeft (16);
    g.setColour (juce::Colour (textMuted));
    g.setFont (juce::Font (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), 11.0f, juce::Font::plain)));
    if (index > 0)
        g.drawText (juce::String (index), indexArea, juce::Justification::centredLeft);

    auto swatchArea = firstLine.removeFromRight (10).withSizeKeepingCentre (10, 10);
    g.setColour (juce::Colour (swatch));
    g.fillRect (swatchArea);

    g.setColour (juce::Colour (isConductor ? textMuted : text));
    g.setFont (juce::Font (juce::FontOptions (11.0f)));
    g.drawText (track.getProperty (SongIDs::name).toString(), firstLine.withTrimmedRight (4),
                juce::Justification::centredLeft);

    // Second line: note count / range.
    g.setColour (juce::Colour (textMuted));
    g.setFont (juce::Font (juce::FontOptions (9.0f)));
    g.drawText (buildSecondLine(), secondLine, juce::Justification::centredLeft);
}

void TrackRowComponent::mouseDown (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu() && inInstrumentBand (e.getPosition()) && ! isConductorTrack())
    {
        juce::Component::SafePointer<TrackRowComponent> safe (this);
        buildInstrumentMenu().showMenuAsync (juce::PopupMenu::Options(), [safe] (int id)
        {
            if (safe != nullptr)
                safe->instrumentMenuChosen (id);
        });
        return;
    }
    if (onTrackSelected)
        onTrackSelected (getTrackId(), e.mods);
}

juce::PopupMenu TrackRowComponent::buildInstrumentMenu() const
{
    const auto segments = instrumentSegmentsOf (track);
    const int current = segments.empty() ? -1 : segments.front().program;

    juce::PopupMenu menu;
    menu.addItem (1, "Auto split on instrument change", canAutoSplit (track));

    juce::PopupMenu all;
    for (int family = 0; family < 16; ++family)
    {
        juce::PopupMenu programs;
        for (int p = family * 8; p < family * 8 + 8; ++p)
            programs.addItem (1000 + p, gmProgramName (p), true, p == current);
        all.addSubMenu (gmFamilyName (family), programs);
    }
    menu.addSubMenu ("Set track instrument to", all);
    return menu;
}

void TrackRowComponent::instrumentMenuChosen (int itemId)
{
    if (itemId == 1 && onAutoSplitRequested)
        onAutoSplitRequested (getTrackId());
    else if (itemId >= 1000 && itemId < 1128 && onSetInstrumentRequested)
        onSetInstrumentRequested (getTrackId(), itemId - 1000);
}

void TrackRowComponent::mouseDoubleClick (const juce::MouseEvent&)
{
    if (canOpenEditor() && onTrackDoubleClicked)
        onTrackDoubleClicked (getTrackId());
}

void TrackRowComponent::mouseDrag (const juce::MouseEvent& e)
{
    // Only start a drag once the mouse has actually moved a few pixels, so a
    // plain click (already handled in mouseDown) doesn't also fire a
    // zero-distance drag.
    if (e.getDistanceFromDragStart() < 4)
        return;

    if (! canDrag())
        return;

    if (auto* container = juce::DragAndDropContainer::findParentDragContainerFor (this))
    {
        if (! container->isDragAndDropActive())
            container->startDragging (juce::var (getTrackId()), this);
    }
}

} // namespace lotro
