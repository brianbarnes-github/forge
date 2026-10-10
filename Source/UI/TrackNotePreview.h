#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "SectionViewState.h"
#include "SongDocument.h"
#include "TimelineViewState.h"

namespace lotro
{
    // Read-only inline note-timeline preview for one MIDI_TRACK, painted
    // directly against a shared TimelineViewState. Decision 1A from the
    // 2026-09-15 brainstorming session: no PianoRollComponent/viewport
    // involved, just raw Graphics calls, matching PartSlotComponent's
    // existing custom-painting convention.
    class TrackNotePreview : public juce::Component
    {
    public:
        TrackNotePreview (juce::ValueTree trackNodeIn, const TimelineViewState& viewStateIn);

        void paint (juce::Graphics& g) override;

        void setGhostVisible (bool shouldBeVisible) noexcept { ghostVisible = shouldBeVisible; }
        bool isGhostVisible() const noexcept { return ghostVisible; }

        // Bounds of the eye-icon ghost toggle, in this component's local
        // coordinates (top-right corner).
        juce::Rectangle<int> ghostToggleBounds() const;

        // Toggles ghost visibility if pos is inside ghostToggleBounds();
        // returns whether it hit. Kept separate from mouseDown() so tests
        // can drive it with a plain juce::Point, matching SourceRollEditor's
        // point-based gesture API convention rather than constructing a
        // full juce::MouseEvent.
        bool toggleGhostIfHit (juce::Point<int> pos);

        // Paints this track's sections from `state` (not owned; must outlive this
        // preview) and enables the section gestures. Null paints none.
        void setSectionView (const SectionViewState* state, juce::int64 trackIdIn) noexcept
        {
            sectionView = state;
            trackId = trackIdIn;
        }

        void mouseDown (const juce::MouseEvent& e) override;
        void mouseDrag (const juce::MouseEvent& e) override;
        void mouseUp (const juce::MouseEvent& e) override;
        void mouseDoubleClick (const juce::MouseEvent& e) override;

        // Fired when the ghost toggle is clicked, with the new state.
        std::function<void (bool)> onGhostToggled;

        // Fired for a click/double-click anywhere in this preview EXCEPT the
        // ghost toggle. This preview is a hit-testable child covering the
        // right 160px of its row and JUCE never forwards a child's mouse
        // events up to its parent, so without these the row's own
        // select/double-click-to-edit gestures would be dead across most of
        // its visible area. Same up-the-chain callback shape as
        // onGhostToggled.
        std::function<void (const juce::ModifierKeys&)> onNonToggleClick;

        // Fired with the tick under the pointer for the same clicks, so the
        // list can drop the start marker there.
        std::function<void (int tick)> onTimelineClicked;
        std::function<void()> onNonToggleDoubleClick;

        // A left press on the strip (after onNonToggleClick/onTimelineClicked) with
        // the section under it (zone None when there is none), then the tick under
        // the pointer while dragging and on release. Only fired with a section view.
        // Dragging starts once the pointer has moved sectionDragThresholdPixels;
        // a release before that reports the press tick.
        std::function<void (const SectionHit&, int tick, const juce::ModifierKeys&)> onSectionPressed;
        std::function<void (int tick)> onSectionDragged;
        std::function<void (int tick)> onSectionReleased;

        // An Alt + left press on the strip starts a merge instead of a section gesture:
        // press (with the section under it), then drags once the pointer is
        // mergeDragThresholdPixels from the press (returns whether the row under the
        // pointer can take the notes), then release. The cursor follows the return value.
        std::function<void (const SectionHit&, const juce::ModifierKeys&)> onMergePressed;
        std::function<bool (juce::Point<int> screenPos, const juce::ModifierKeys&)> onMergeDragged;
        std::function<void (juce::Point<int> screenPos, const juce::ModifierKeys&)> onMergeReleased;

    private:
        void paintGrid (juce::Graphics& g) const;
        void paintSections (juce::Graphics& g) const;
        void paintMergePreview (juce::Graphics& g) const;
        int sectionDragTick (const juce::MouseEvent& e);

        static constexpr int sectionDragThresholdPixels = 3;
        static constexpr int mergeDragThresholdPixels = 3;

        juce::ValueTree track;
        const TimelineViewState& viewState;
        bool ghostVisible = false;
        const SectionViewState* sectionView = nullptr;
        juce::int64 trackId = 0;
        bool sectionPressActive = false;
        bool sectionDragStarted = false;
        int sectionPressX = 0;
        int sectionPressTick = 0;
        bool mergePressActive = false;
        bool mergeDragStarted = false;
        juce::Point<int> mergePressPos;
    };
}
