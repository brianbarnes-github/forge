#pragma once

#include "SongDocument.h"
#include "TimelineViewState.h"
#include "TrackNotePreview.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

// One row in TrackListComponent's list, representing a single MIDI_TRACK.
// Phase 4, component B1 of the Songsmith plan's "Part strip UI" phase — see
// docs/ARCHITECTURE.md §9.x and the Songsmith UI Guide mockup's "MIDI track
// list" block for the visual spec.
namespace lotro
{

class TrackRowComponent : public juce::Component
{
public:
    // trackNode must be a valid MIDI_TRACK node; displayIndex is its 1-based
    // position in SOURCE_MIDI at construction time (TrackListComponent
    // rebuilds every row from scratch on any SOURCE_MIDI change, so this
    // never goes stale in place). viewState is shared with the embedded
    // TrackNotePreview and must outlive this row.
    TrackRowComponent (juce::ValueTree trackNode, int displayIndex, const TimelineViewState& viewState);

    void setSelected (bool shouldBeSelected);

    // Restores the embedded preview's eye-icon state after TrackListComponent
    // recreates this row (see TrackListComponent::isTrackGhosted).
    void setGhostVisible (bool shouldBeVisible);

    juce::int64 getTrackId() const;

    void paint (juce::Graphics& g) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent& e) override;
    void mouseDrag (const juce::MouseEvent& e) override;
    void mouseDoubleClick (const juce::MouseEvent& e) override;

    // Fixed row height used by TrackListComponent to lay out its content.
    static constexpr int rowHeight = 34;

    // Fixed width of the embedded TrackNotePreview, at the right edge of the row.
    static constexpr int notePreviewWidth = 160;

    // Fired on a plain (non-drag) click, with this row's trackId.
    std::function<void (juce::int64)> onTrackSelected;

    // Fired on a double-click, with this row's trackId.
    std::function<void (juce::int64)> onTrackDoubleClicked;

    // Fired when this row's ghost toggle is clicked: (trackId, newVisibility).
    std::function<void (juce::int64, bool)> onGhostToggled;

    // Test-only access to the embedded preview -- avoids needing a separate
    // friend-struct file just for this, since TrackNotePreview's own public
    // API (toggleGhostIfHit/ghostToggleBounds) is already test-safe.
    TrackNotePreview& notePreviewForTesting() { return notePreview; }

private:
    juce::String buildSecondLine() const;

    juce::ValueTree track;
    int             index;
    bool            selected = false;
    TrackNotePreview notePreview;
};

} // namespace lotro
