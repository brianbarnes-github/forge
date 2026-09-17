#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

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

        void mouseDown (const juce::MouseEvent& e) override;
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
        std::function<void()> onNonToggleClick;
        std::function<void()> onNonToggleDoubleClick;

    private:
        juce::ValueTree track;
        const TimelineViewState& viewState;
        bool ghostVisible = false;
    };
}
