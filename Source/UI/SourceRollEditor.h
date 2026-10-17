#pragma once

#include "EditorGrid.h"
#include "NoteClipboard.h"
#include "NoteDisplay.h"
#include "NoteEdit.h"
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

    // View > Piano Roll Notes: what a click or the rubber band hits and which
    // edges resize (noteFootprint), kept in step with the roll's painting.
    void setNoteShape (NoteShape shape) noexcept { noteShape = shape; }

    // The editor's grid, pulled from `source` on demand and never cached, so a
    // meter edit or a swing change reaches the next gesture step with no hook.
    // Its spacing drives quantizeSelection()'s duration rule and create's
    // default duration fallback (a quarter note, via geometry's ticksPerQuarter,
    // when the spacing is 0 = "off"). Without a source there is no grid.
    void setGridSource (std::function<EditorGrid()> source) { gridSource = std::move (source); }
    // A fixed straight grid of `ticks` (0 = none).
    void setGridTicks (int ticks) { setGridSource ([ticks] { return EditorGrid::straight (ticks); }); }
    EditorGrid grid() const { return gridSource ? gridSource() : EditorGrid::straight (0); }
    int getGridTicks() const { return grid().spacingTicks; }

    // Options ▸ Snap to Grid: while on (and the grid is not Off), a moved note's
    // start (the one under the press; the rest of the selection keeps its
    // spacing) and a resized edge land on the nearest grid line, never closer
    // than one line to the other edge, and a double-click creates its note at
    // the start of the grid cell under it, running to the next grid line. Alt
    // held on that press or drag step places freely. Off by default.
    void setSnapToGrid (bool on) noexcept { snapToGrid = on; }
    bool getSnapToGrid() const noexcept { return snapToGrid; }

    // Options ▸ Note Length / Note Division: a double-clicked note is this long
    // (editorGridTicks at geometry's PPQ), snapped or not; the grid still places
    // its start. Off (the default) leaves the length to the grid, as above.
    void setNoteLength (GridLength length, GridDivision division) noexcept
    {
        noteLength = length;
        noteDivision = division;
    }
    GridLength getNoteLength() const noexcept { return noteLength; }
    GridDivision getNoteDivision() const noexcept { return noteDivision; }

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
    // Replaces the selection with every note whose pitch `wanted` accepts.
    void selectPitches (const std::function<bool (int pitch)>& wanted);
    int getNumSelected() const noexcept { return (int) selection.size(); }
    // The selected notes still in the track (the selection is pruned first).
    std::vector<juce::ValueTree> getSelectedNotes();

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

    // Edit > Paste / Paste at Origin: adds the clipboard's notes to the track with
    // the earliest at `startTick` (see pasteNotes, one undo transaction) and makes
    // them the selection. Reports no NoteChange (no preview). False when nothing
    // was added (an empty clipboard or no track).
    bool paste (const NoteClipboard& clipboard, int startTick);

    // Edit > Insert Note: whether insertNote would add a note -- `pitch` is a row
    // (not -1) the pitch filter allows, and no note on it covers `tick`.
    bool canInsertNote (int tick, int pitch) const;
    // Adds the note a double-click at `tick` on `pitch` would (Snap to Grid and Note
    // Length as set, never Alt), selects only it and reports it inserted. One undo
    // transaction; false (nothing changed) unless canInsertNote.
    bool insertNote (int tick, int pitch);

    // Edit > Split Notes / Join Notes / Legato / Legato to Next Note on the
    // selection (see NoteEdit.h, one undo transaction each, none when nothing
    // changes). Split selects both halves; Join drops the absorbed notes from the
    // selection; Legato keeps it.
    // None reports a NoteChange. Each returns false when nothing changed.
    bool canSplitSelection (int tick);
    bool splitSelection (int tick);
    bool canJoinSelection();
    bool joinSelection();
    bool canLegatoSelection (LegatoTarget target);
    bool legatoSelection (LegatoTarget target);

    // The whole document's undo / redo (as Ctrl+Z / Ctrl+Y), then drops any
    // selected note it took away.
    void undo();
    void redo();

    // Grid-snaps every selected note's startTick to the grid's lines (swung
    // under Swing) and its durationTicks to the grid's spacing, one undo
    // transaction regardless of selection size.
    // No-op (false) if the selection is empty or the grid's spacing <= 0
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
    // Whether `pos` falls inside any note's start-to-end span on its row, drawn
    // or not (Diamond hides the length), so a double-click there adds nothing.
    bool isWithinNoteLength (juce::Point<int> pos) const;
    int hitTestEdgeZone (const juce::ValueTree& note, juce::Point<int> pos) const;
    void toggleSelection (const juce::ValueTree& note);
    void pruneSelection();
    void updateRubberBandSelection();
    // Adds a note on `pitch` starting at `tick` (the grid cell's start when `snap`),
    // selects only it and reports it inserted.
    void createNoteAt (int tick, int pitch, bool snap);
    bool snapsWith (juce::ModifierKeys mods) const;
    void markTimingEdited (juce::ValueTree note);
    void reportChange (NoteChange change, const std::vector<juce::ValueTree>& notes) const;
    std::vector<juce::ValueTree> draggedNotes() const;

    SongDocument& doc;
    juce::ValueTree track;
    PianoRollGeometry geometry;
    std::function<EditorGrid()> gridSource;
    bool snapToGrid = false;
    GridLength noteLength = GridLength::Off;
    GridDivision noteDivision = GridDivision::Straight;
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
    NoteShape noteShape = NoteShape::rectangle;

    static constexpr int edgeThresholdPixels = 6;
    static constexpr int lassoThresholdPixels = 4;   // as the roll's marker click
};

} // namespace lotro
