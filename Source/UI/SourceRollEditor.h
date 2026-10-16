#pragma once

#include "NotePreview.h"
#include "PianoRollGeometry.h"
#include "SongDocument.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <vector>

// Mouse/keyboard-gesture-driven note editing for the *source* piano roll
// only (PianoRollComponent owns one of these when constructed with
// Role::Source and a SongDocument). Operates directly on a MIDI_TRACK
// ValueTree's NOTE children through SongDocument's mutation API -- one undo
// transaction per gesture, per juce-valuetree-conventions. Depends only on
// SongDocument and PianoRollGeometry (juce_data_structures/juce_core, no
// painting), so it is unit-tested directly against a hand-built ValueTree
// in Tests/SourceRollEditor_tests.cpp with zero JUCE painting involved.
//
// Hit-testing never round-trips a click through tickForX/pitchForY back to
// a tick/pitch and then back to a rect -- it compares the click point
// directly against each note's already-computed pixel rect
// (PianoRollGeometry::noteBounds), exactly like painting does. Only
// placing a *new* value (create/move/resize) converts a pixel to a tick,
// via tickForX, the geometry's single source of truth for that direction.
namespace lotro
{

class SourceRollEditor
{
public:
    explicit SourceRollEditor (SongDocument& document);

    // Repoints the editor at a different MIDI_TRACK node (or an invalid
    // ValueTree for "no track selected/editable"). Clears selection and
    // cancels any drag in progress -- selection is scoped to one track.
    void setTrack (juce::ValueTree trackNodeIn);
    juce::ValueTree getTrackNode() const noexcept { return track; }

    // Kept in sync with the owning PianoRollComponent's own geometry by the
    // caller, every time that geometry changes (setNoteSource, zoom) --
    // this class never mutates it, only reads it for hit-testing/pixel<->
    // tick conversion.
    void setGeometry (const PianoRollGeometry& geometryIn) noexcept { geometry = geometryIn; }

    // Ticks spanned by the toolbar's current grid-size selection; 0 means
    // "off". Drives both quantizeSelection()'s snap size and create's
    // default duration fallback (a quarter note, via geometry's
    // ticksPerQuarter, when this is 0). Owned/pushed by
    // SongsmithMainComponent from its grid-size combo box -- this class has
    // no notion of the combo's enum, only the resulting tick count.
    void setGridTicks (int ticks) noexcept { currentGridTicks = ticks; }
    int getGridTicks() const noexcept { return currentGridTicks; }

    // Options ▸ Snap to Grid: while on (and the grid is not Off), a moved note's
    // start (the one under the press; the rest of the selection keeps its
    // spacing) and a resized edge land on the nearest grid line, never closer
    // than one line to the other edge, and a double-click creates its note at
    // the start of the grid cell under it. Alt held on that press or drag step
    // places freely. Off by default.
    void setSnapToGrid (bool on) noexcept { snapToGrid = on; }
    bool getSnapToGrid() const noexcept { return snapToGrid; }

    // Options ▸ Snap to Key: which pitches a double-click may create a note on.
    // Empty (the default) allows every pitch. A refused pitch changes nothing --
    // no note, no selection change, no undo step. Moves, resizes and every other
    // edit ignore it.
    void setPitchFilter (std::function<bool (int pitch)> allowed) { pitchAllowed = std::move (allowed); }

    // Options ▸ Note Preview: told which notes a gesture inserted or moved, so
    // the owner can sound them. A double-click insert reports the new note; a
    // body drag reports the dragged notes each time they land on a new pitch
    // row, or once on release if only their timing changed. Resizes, deletes,
    // selection and undo report nothing.
    void setNoteChangeListener (NoteChangeListener listener) { noteChanged = std::move (listener); }

    bool isSelected (const juce::ValueTree& note) const;
    // Replaces the selection with `note` (an invalid tree clears it), as a click does.
    void selectOnly (const juce::ValueTree& note);
    // Adds `note` to the selection, keeping the rest; already selected or invalid is a no-op.
    void addToSelection (const juce::ValueTree& note);
    // Selects every note of `pitch`, replacing the selection or adding to it
    // (a right-click on the roll's keyboard key).
    void selectPitch (int pitch, bool addToSelection);
    int getNumSelected() const noexcept { return (int) selection.size(); }

    // In-progress rubber-band rect, in the same canvas-pixel space as
    // PianoRollGeometry::noteBounds; empty when no rubber-band drag is
    // active. Read by PianoRollComponent's paint to draw it.
    juce::Rectangle<int> getRubberBandRect() const noexcept { return rubberBandRect; }

    // Gesture entry points, called by PianoRollComponent's Canvas from its
    // own mouseDown/mouseDrag/mouseUp overrides with canvas-local pixel
    // coordinates. Each returns true if the caller should repaint (and
    // rebuild the canvas's content size, in case a note's extent changed).
    // A right press (ModifierKeys::isPopupMenu) becomes a lasso once it moves
    // lassoThresholdPixels: a right-drag, from a note or empty canvas,
    // rubber-bands every note it intersects, replacing the selection
    // (shift/ctrl/cmd adds to it instead). A right-click that never got
    // that far changes nothing. Neither mutates the document. A plain left
    // press on empty canvas only clears the selection (with a modifier it
    // keeps it); a left-drag there does nothing.
    bool mouseDown (juce::Point<int> pos, juce::ModifierKeys mods, bool isDoubleClick);
    bool mouseDrag (juce::Point<int> pos, juce::ModifierKeys mods = {});
    bool mouseUp (juce::Point<int> pos);

    // Delete/Backspace deletes the current selection; Ctrl+Z/Ctrl+Y (or
    // Ctrl+Shift+Z) undo/redo the whole document. Returns true (repaint
    // hint) if the key was handled, false otherwise.
    bool keyPressed (const juce::KeyPress& key);

    // Removes every currently selected note, one undo transaction
    // regardless of selection size. No-op (false) if nothing is selected.
    // Public so both keyPressed and (a future) right-click context menu
    // (see the plan's scope decisions) can trigger it directly.
    bool deleteSelection();

    // Grid-snaps every selected note's startTick and durationTicks to
    // currentGridTicks, one undo transaction regardless of selection size.
    // No-op (false) if the selection is empty or currentGridTicks <= 0
    // ("off" -- there is no implicit grid to snap to).
    bool quantizeSelection();

    // Whether a press at pos would land on a note (used by the owner to tell
    // an empty-space click from a note gesture).
    bool isOverNote (juce::Point<int> pos) const { return hitTestNote (pos).isValid(); }

    // A move, resize or rubber-band drag, or a right press that may yet
    // become a rubber band, is in progress (between mouseDown and mouseUp).
    bool isGestureActive() const noexcept { return dragMode != DragMode::None; }

    // The current pitch of the note grabbed for a move drag (the one under the
    // press, not the rest of the selection), or -1 when no move is in progress.
    int movingNotePitch() const;

private:
    enum class DragMode { None, Move, ResizeLeft, ResizeRight, RightPress, RubberBand };

    struct DragOriginal
    {
        juce::ValueTree note;
        int startTick;
        int pitch;
        int durationTicks;
    };

    juce::ValueTree hitTestNote (juce::Point<int> pos) const;
    int hitTestEdgeZone (const juce::ValueTree& note, juce::Point<int> pos) const;
    void toggleSelection (const juce::ValueTree& note);
    void pruneSelection();
    void updateRubberBandSelection();
    void createNoteAt (juce::Point<int> pos, bool snap);
    bool snapsWith (juce::ModifierKeys mods) const noexcept;
    void markTimingEdited (juce::ValueTree note);
    void reportChange (NoteChange change, const std::vector<juce::ValueTree>& notes) const;
    std::vector<juce::ValueTree> draggedNotes() const;

    SongDocument& doc;
    juce::ValueTree track;
    PianoRollGeometry geometry;
    int currentGridTicks = 0;
    bool snapToGrid = false;
    std::function<bool (int pitch)> pitchAllowed;
    NoteChangeListener noteChanged;

    std::vector<juce::ValueTree> selection;
    std::vector<juce::ValueTree> baseSelectionForRubberBand;
    bool rightPressAdds = false;   // RightPress: shift/ctrl/cmd was held, so add to the selection

    DragMode dragMode = DragMode::None;
    juce::Point<int> dragStartPos;
    juce::Rectangle<int> rubberBandRect;
    std::vector<DragOriginal> dragOriginals; // Move: every selected note. Resize: just the primary note.
    juce::ValueTree grabbedNote;              // Move: the note under the press.
    bool movePitchChanged = false;            // Move: some step of this drag changed a pitch.

    static constexpr int edgeThresholdPixels = 6;
    static constexpr int lassoThresholdPixels = 4;   // as the roll's marker click
};

} // namespace lotro
