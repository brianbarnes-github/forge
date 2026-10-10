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
    // trackNode must be a valid MIDI_TRACK node; displayIndex is the row number
    // shown (the track's SOURCE_MIDI child index; 0 = the conductor, drawn
    // unnumbered), fixed at construction time (TrackListComponent
    // rebuilds every row from scratch on any SOURCE_MIDI change, so this
    // never goes stale in place). viewState is shared with the embedded
    // TrackNotePreview and must outlive this row.
    TrackRowComponent (juce::ValueTree trackNode, int displayIndex, const TimelineViewState& viewState);

    void setSelected (bool shouldBeSelected);
    bool isSelected() const noexcept { return selected; }

    // Restores the embedded preview's eye-icon state after TrackListComponent
    // recreates this row (see TrackListComponent::isTrackGhosted).
    void setGhostVisible (bool shouldBeVisible);

    juce::int64 getTrackId() const;

    // Hands the list's shared section state to the embedded preview (see
    // TrackNotePreview::setSectionView); must outlive this row.
    void setSectionView (const SectionViewState* state);

    void paint (juce::Graphics& g) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent& e) override;
    void mouseDrag (const juce::MouseEvent& e) override;
    void mouseDoubleClick (const juce::MouseEvent& e) override;

    // Row height bounds used by TrackListComponent (Ctrl+wheel resizes the rows
    // within [minRowHeight, maxRowHeight]; defaultRowHeight is where a new list
    // starts). The minimum is the smallest height at which the info column's two
    // text lines (11 px and 9 px fonts, each half of the height less the divider),
    // the M / S buttons (inset 8 px top and bottom) and the 10 px swatch neither
    // clip nor overlap. Layout and painting derive from the row's actual bounds.
    static constexpr int defaultRowHeight = 34;
    static constexpr int minRowHeight = 30;
    static constexpr int maxRowHeight = 120;

    // Height of the instrument-name band across the top of the canvas side of
    // every row (the Reaper-style label bar between tracks). It is extra: a row's
    // bounds are the list's row height plus this, so the notes area keeps the
    // height the user sized it to.
    static constexpr int instrumentBandHeight = 16;

    // The band's text: the track's General MIDI program name, "Drum Kit" on
    // channel 10, empty for the conductor.
    juce::String instrumentLabel() const;

    // Fixed width of the left-hand index/name/note-count text column. The
    // embedded TrackNotePreview fills everything to its right, so it grows
    // with the row instead of being pinned to a small fixed width — the
    // note data is the primary content, the text is a label for it.
    static constexpr int trackInfoWidth = 180;

    // Height of the divider line painted along the row's bottom edge, across
    // both the text column and the note preview, so adjacent tracks read as
    // separate rows.
    static constexpr int dividerThickness = 1;

    // Width of the vertical line painted at the info column's right edge
    // (inside it, so the note preview still starts at trackInfoWidth),
    // separating the label from the notes.
    static constexpr int columnDividerThickness = 2;

    // Fired with the tick under a click in the note preview (not on the ghost toggle).
    std::function<void (int tick)> onTimelineClicked;

    // Fired on a click in the info column, with this row's trackId and the click's modifiers.
    std::function<void (juce::int64, const juce::ModifierKeys&)> onTrackSelected;

    // Fired on a click in the note-preview strip, with this row's trackId and the click's modifiers.
    std::function<void (juce::int64, const juce::ModifierKeys&)> onStripSelected;

    // The preview's section gestures, the press with this row's trackId first.
    std::function<void (juce::int64, const SectionHit&, int, const juce::ModifierKeys&)> onSectionPressed;
    std::function<void (int)> onSectionDragged;
    std::function<void (int)> onSectionReleased;

    // Fired on a double-click, with this row's trackId.
    std::function<void (juce::int64)> onTrackDoubleClicked;

    // Fired when this row's ghost toggle is clicked: (trackId, newVisibility).
    std::function<void (juce::int64, bool)> onGhostToggled;

    // Fired when the M / S button is clicked: (trackId, newState).
    std::function<void (juce::int64, bool)> onMuteToggled;
    std::function<void (juce::int64, bool)> onSoloToggled;

    // Reflects the playback controller's state on the buttons without firing
    // callbacks; a muted or solo-silenced row is dimmed.
    void setMuteSolo (bool muted, bool soloed, bool silencedBySolo);

    // Width reserved at the right end of the info column for the M / S buttons.
    static constexpr int muteSoloWidth = 40;

    juce::TextButton& muteButtonForTesting() { return muteButton; }
    juce::TextButton& soloButtonForTesting() { return soloButton; }

    // Test-only access to the embedded preview -- avoids needing a separate
    // friend-struct file just for this, since TrackNotePreview's own public
    // API (toggleGhostIfHit/ghostToggleBounds) is already test-safe.
    TrackNotePreview& notePreviewForTesting() { return notePreview; }

    // False for the conductor and note-less tracks: they can't be dragged to
    // a part or opened in the editor.
    bool canDrag() const;

    // False only for the conductor: a track whose notes were all deleted can
    // still be opened (to draw new notes), though it can't be dragged to a part.
    bool canOpenEditor() const;

    juce::String buildSecondLineForTesting() const { return buildSecondLine(); }

private:
    juce::String buildSecondLine() const;

    juce::ValueTree track;
    int             index;
    bool            selected = false;
    TrackNotePreview notePreview;
    juce::TextButton muteButton { "M" }, soloButton { "S" };
    bool             silencedBySolo = false;
};

} // namespace lotro
