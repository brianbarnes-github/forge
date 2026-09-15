#pragma once

#include "PianoRollGeometry.h"
#include "SongDocument.h"

#include <juce_gui_basics/juce_gui_basics.h>

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

    bool isSelected (const juce::ValueTree& note) const;
    int getNumSelected() const noexcept { return (int) selection.size(); }

    // In-progress rubber-band rect, in the same canvas-pixel space as
    // PianoRollGeometry::noteBounds; empty when no rubber-band drag is
    // active. Read by PianoRollComponent's paint to draw it.
    juce::Rectangle<int> getRubberBandRect() const noexcept { return rubberBandRect; }

    // Gesture entry points, called by PianoRollComponent's Canvas from its
    // own mouseDown/mouseDrag/mouseUp overrides with canvas-local pixel
    // coordinates. Each returns true if the caller should repaint (and
    // rebuild the canvas's content size, in case a note's extent changed).
    bool mouseDown (juce::Point<int> pos, juce::ModifierKeys mods, bool isDoubleClick);
    bool mouseDrag (juce::Point<int> pos);
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

private:
    enum class DragMode { None, Move, ResizeLeft, ResizeRight, RubberBand };

    struct DragOriginal
    {
        juce::ValueTree note;
        int startTick;
        int pitch;
        int durationTicks;
    };

    juce::ValueTree hitTestNote (juce::Point<int> pos) const;
    int hitTestEdgeZone (const juce::ValueTree& note, juce::Point<int> pos) const;
    void selectOnly (const juce::ValueTree& note);
    void toggleSelection (const juce::ValueTree& note);
    void pruneSelection();
    void updateRubberBandSelection();
    void createNoteAt (juce::Point<int> pos);

    SongDocument& doc;
    juce::ValueTree track;
    PianoRollGeometry geometry;
    int currentGridTicks = 0;

    std::vector<juce::ValueTree> selection;
    std::vector<juce::ValueTree> baseSelectionForRubberBand;

    DragMode dragMode = DragMode::None;
    juce::Point<int> dragStartPos;
    juce::Rectangle<int> rubberBandRect;
    std::vector<DragOriginal> dragOriginals; // Move: every selected note. Resize: just the primary note.

    static constexpr int edgeThresholdPixels = 6;
};

} // namespace lotro
