#include "TrackRowComponent.h"
#include "SongsmithColours.h"
#include "GmProgramNames.h"
#include "InstrumentEdit.h"
#include "ProgramChanges.h"

namespace lotro
{

TrackRowComponent::TrackRowComponent (juce::ValueTree trackNode, int displayIndex, const TimelineViewState& viewState)
    : track (trackNode), index (displayIndex), timelineView (viewState), head (trackNode, displayIndex), notePreview (trackNode, viewState)
{
    jassert (track.hasType (SongIDs::MIDI_TRACK));
    setInterceptsMouseClicks (true, false);

    addAndMakeVisible (notePreview);

    addAndMakeVisible (head);
    head.onMuteToggled = [this] (bool on) { if (onMuteToggled) onMuteToggled (getTrackId(), on); };
    head.onSoloToggled = [this] (bool on) { if (onSoloToggled) onSoloToggled (getTrackId(), on); };
    head.onVolumeChanged = [this] (int percent, bool startsGesture)
    {
        if (onVolumeChanged)
            onVolumeChanged (getTrackId(), percent, startsGesture);
    };
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
    head.setBounds (info);
    area.removeFromTop (juce::jmin (instrumentBandHeight, area.getHeight()));
    notePreview.setBounds (area);
}

void TrackRowComponent::setMuteSolo (bool muted, bool soloed, bool silenced)
{
    head.setMuteSolo (muted, soloed);
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
    // The program the track plays at its start (a tick-0 change can differ from sourceProgram).
    const auto segments = instrumentSegmentsOf (track);
    return gmProgramName (segments.empty() ? (int) track.getProperty (SongIDs::sourceProgram)
                                           : segments.front().program);
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
