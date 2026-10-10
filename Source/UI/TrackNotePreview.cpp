#include "TrackNotePreview.h"
#include "GridLinePaint.h"
#include "SongsmithColours.h"

#include <algorithm>
#include <cstdlib>

namespace lotro
{
    TrackNotePreview::TrackNotePreview (juce::ValueTree trackNodeIn, const TimelineViewState& viewStateIn)
        : track (trackNodeIn), viewState (viewStateIn)
    {
    }

    void TrackNotePreview::paintGrid (juce::Graphics& g) const
    {
        // The meter and PPQ live beside the tracks under the document root.
        const auto root = track.getRoot();
        const int ticksPerQuarter = (int) root.getChildWithName (SongIDs::SOURCE_MIDI)
                                         .getProperty (SongIDs::ticksPerQuarter, 480);

        RulerMeter meter;
        const auto meterMap = root.getChildWithName (SongIDs::METER_MAP);
        if (meterMap.getNumChildren() > 0)
        {
            meter.numerator = (int) meterMap.getChild (0).getProperty (SongIDs::numerator, 4);
            meter.denominator = (int) meterMap.getChild (0).getProperty (SongIDs::denominator, 4);
        }

        paintGridLines (g, g.getClipBounds().withY (0).withHeight (getHeight()), viewState.getPixelsPerTick(),
                        ticksPerQuarter, meter,
                        [this] (int tick) { return viewState.xForTick (tick); },
                        [this] (int x) { return viewState.tickForX (x); });
    }

    void TrackNotePreview::paintSections (juce::Graphics& g) const
    {
        if (sectionView == nullptr)
            return;

        const auto bounds = getLocalBounds();
        for (const auto& s : sectionsOf (track))
        {
            int start = s.startTick, end = s.endTick;
            const bool selected = sectionView->selected.count ({ trackId, s.id }) > 0;
            if (selected && sectionView->drag)
            {
                // Clamped as moveSections/resizeSectionsBy will, so the preview is the result.
                const auto& d = *sectionView->drag;
                if (d.kind == SectionDragPreview::Kind::Move)
                {
                    start += d.deltaTicks;   // the list already clamped the shared delta
                    end += d.deltaTicks;
                }
                else if (d.kind == SectionDragPreview::Kind::ResizeLeft)
                    start = std::clamp (start + d.deltaTicks, 0, end - 1);
                else
                    end = std::max (end + d.deltaTicks, start + 1);
            }

            const int x0 = viewState.xForTick (start);
            const int x1 = std::max (x0 + 1, viewState.xForTick (end));
            g.setColour (juce::Colour (selected ? SongsmithColours::sectionSelectedFill : SongsmithColours::sectionFill));
            g.fillRect (x0, bounds.getY(), x1 - x0, bounds.getHeight());
            g.setColour (juce::Colour (selected ? SongsmithColours::sectionSelectedEdge : SongsmithColours::sectionEdge));
            g.drawVerticalLine (x0, (float) bounds.getY(), (float) bounds.getBottom());
            g.drawVerticalLine (x1 - 1, (float) bounds.getY(), (float) bounds.getBottom());
        }
    }

    void TrackNotePreview::paint (juce::Graphics& g)
    {
        using namespace SongsmithColours;

        auto bounds = getLocalBounds();
        g.setColour (juce::Colour (background));
        g.fillRect (bounds);

        paintGrid (g);
        paintSections (g);

        int minPitch = 127;
        int maxPitch = 0;
        bool anyNotes = false;

        auto notes = SongDocument::getNotesNode (track);
        for (int i = 0; i < notes.getNumChildren(); ++i)
        {
            auto note = notes.getChild (i);
            anyNotes = true;
            const int pitch = (int) note.getProperty (SongIDs::pitch);
            minPitch = juce::jmin (minPitch, pitch);
            maxPitch = juce::jmax (maxPitch, pitch);
        }

        if (anyNotes)
        {
            const int pitchSpan = juce::jmax (1, maxPitch - minPitch);

            // While a selected section is being moved its notes travel with it (the
            // list already clamped the shared delta, as for the section itself).
            const auto sections = sectionsOf (track);
            const bool moving = sectionView != nullptr && sectionView->drag
                                && sectionView->drag->kind == SectionDragPreview::Kind::Move;

            g.setColour (juce::Colour ((juce::uint32) (int) track.getProperty (SongIDs::colorArgb)));
            for (int i = 0; i < notes.getNumChildren(); ++i)
            {
                auto note = notes.getChild (i);
                const int pitch = (int) note.getProperty (SongIDs::pitch);
                int startTick = (int) note.getProperty (SongIDs::startTick);
                if (moving && sectionView->selected.count ({ trackId, sectionIdOfNote (note, sections) }) > 0)
                    startTick += sectionView->drag->deltaTicks;
                const int durationTicks = (int) note.getProperty (SongIDs::durationTicks);

                const int x = viewState.xForTick (startTick);
                const int width = juce::jmax (1, viewState.xForTick (startTick + durationTicks) - x);
                const float normalisedPitch = (float) (pitch - minPitch) / (float) pitchSpan;
                const int y = juce::roundToInt ((1.0f - normalisedPitch) * (float) juce::jmax (0, bounds.getHeight() - 2));

                g.fillRect (x, y, width, 2);
            }
        }

        auto toggleBounds = ghostToggleBounds().toFloat();
        if (ghostVisible)
        {
            g.setColour (juce::Colour (accentAmber));
            g.fillEllipse (toggleBounds);
        }
        else
        {
            g.setColour (juce::Colour (textMuted));
            g.drawEllipse (toggleBounds.reduced (1.0f), 1.5f);
        }
    }

    juce::Rectangle<int> TrackNotePreview::ghostToggleBounds() const
    {
        return getLocalBounds().removeFromRight (16).removeFromTop (16).reduced (3);
    }

    void TrackNotePreview::mouseDown (const juce::MouseEvent& e)
    {
        if (toggleGhostIfHit (e.getPosition()))
            return;
        if (onNonToggleClick)
            onNonToggleClick (e.mods);
        const int tick = viewState.tickForX (e.getPosition().x);
        if (onTimelineClicked)
            onTimelineClicked (tick);

        // Only the left button picks up a section, so another button can never drag one.
        sectionPressActive = false;
        mergePressActive = false;
        if (sectionView != nullptr && e.mods.isLeftButtonDown())
        {
            const auto hit = hitTestSection (sectionsOf (track), tick, viewState.getPixelsPerTick(), 5);
            if (e.mods.isAltDown() && onMergePressed)
            {
                mergePressActive = true;
                mergeDragStarted = false;
                mergePressPos = e.getPosition();
                onMergePressed (hit, e.mods);
            }
            else if (onSectionPressed)
            {
                sectionPressActive = true;
                sectionDragStarted = false;
                sectionPressX = e.getPosition().x;
                sectionPressTick = tick;
                onSectionPressed (hit, tick, e.mods);
            }
        }
    }

    int TrackNotePreview::sectionDragTick (const juce::MouseEvent& e)
    {
        // Until the pointer has moved a few pixels the press is a click: report the
        // press tick, so a little jitter neither moves the preview nor commits.
        if (std::abs (e.getPosition().x - sectionPressX) >= sectionDragThresholdPixels)
            sectionDragStarted = true;
        return sectionDragStarted ? viewState.tickForX (e.getPosition().x) : sectionPressTick;
    }

    void TrackNotePreview::mouseDrag (const juce::MouseEvent& e)
    {
        if (mergePressActive)
        {
            if (! mergeDragStarted && e.getPosition().getDistanceFrom (mergePressPos) < mergeDragThresholdPixels)
                return;
            mergeDragStarted = true;
            const bool valid = onMergeDragged && onMergeDragged (e.getScreenPosition(), e.mods);
            const bool copy = e.mods.isCtrlDown() || e.mods.isCommandDown();
            setMouseCursor (! valid ? juce::MouseCursor::NormalCursor
                                    : copy ? juce::MouseCursor::CopyingCursor
                                           : juce::MouseCursor::DraggingHandCursor);
            return;
        }
        if (! sectionPressActive)
            return;
        const int tick = sectionDragTick (e);
        if (sectionDragStarted && onSectionDragged)
            onSectionDragged (tick);
    }

    void TrackNotePreview::mouseUp (const juce::MouseEvent& e)
    {
        if (mergePressActive)
        {
            mergePressActive = false;
            mergeDragStarted = false;
            setMouseCursor (juce::MouseCursor::NormalCursor);
            if (onMergeReleased)
                onMergeReleased (e.getScreenPosition(), e.mods);
            return;
        }
        if (! sectionPressActive)
            return;
        sectionPressActive = false;
        const int tick = sectionDragTick (e);
        if (onSectionReleased)
            onSectionReleased (tick);
    }

    void TrackNotePreview::mouseDoubleClick (const juce::MouseEvent& e)
    {
        // Tested against the toggle bounds rather than toggleGhostIfHit() so
        // the second click of a double-click on the toggle doesn't flip the
        // ghost back off again.
        if (! ghostToggleBounds().contains (e.getPosition()) && onNonToggleDoubleClick)
            onNonToggleDoubleClick();
    }

    bool TrackNotePreview::toggleGhostIfHit (juce::Point<int> pos)
    {
        if (! ghostToggleBounds().contains (pos))
            return false;

        ghostVisible = ! ghostVisible;
        repaint();
        if (onGhostToggled)
            onGhostToggled (ghostVisible);
        return true;
    }
}
