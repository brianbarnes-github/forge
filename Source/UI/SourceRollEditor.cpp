#include "SourceRollEditor.h"
#include "NoteFootprint.h"
#include "SectionEdit.h"

#include <algorithm>
#include <cmath>

namespace lotro
{

namespace
{
    PianoRollNote toPianoRollNote (const juce::ValueTree& noteNode)
    {
        PianoRollNote note;
        note.pitch         = (int) noteNode.getProperty (SongIDs::pitch);
        note.startTick     = (int) noteNode.getProperty (SongIDs::startTick);
        note.durationTicks = (int) noteNode.getProperty (SongIDs::durationTicks);
        return note;
    }

    juce::Rectangle<int> toRect (const PianoRollNoteBounds& b)
    {
        return { b.x, b.y, b.width, b.height };
    }
}

SourceRollEditor::SourceRollEditor (SongDocument& document) : doc (document) {}

void SourceRollEditor::setTrack (juce::ValueTree trackNodeIn)
{
    track = trackNodeIn;
    selection.clear();
    baseSelectionForRubberBand.clear();
    dragOriginals.clear();
    dragMode = DragMode::None;
    rubberBandRect = {};
}

bool SourceRollEditor::isSelected (const juce::ValueTree& note) const
{
    return std::find (selection.begin(), selection.end(), note) != selection.end();
}

void SourceRollEditor::pruneSelection()
{
    selection.erase (std::remove_if (selection.begin(), selection.end(),
                                      [this] (const juce::ValueTree& n) { return n.getParent() != SongDocument::getNotesNode (track); }),
                      selection.end());
}

void SourceRollEditor::selectOnly (const juce::ValueTree& note)
{
    selection.clear();
    if (note.isValid())
        selection.push_back (note);
}

void SourceRollEditor::addToSelection (const juce::ValueTree& note)
{
    if (note.isValid() && ! isSelected (note))
        selection.push_back (note);
}

void SourceRollEditor::selectPitch (int pitch, bool addToSelection)
{
    if (! addToSelection)
        selection.clear();
    if (! track.isValid())
        return;

    auto notes = SongDocument::getNotesNode (track);
    for (int i = 0; i < notes.getNumChildren(); ++i)
    {
        auto noteNode = notes.getChild (i);
        if ((int) noteNode.getProperty (SongIDs::pitch) == pitch && ! isSelected (noteNode))
            selection.push_back (noteNode);
    }
}

void SourceRollEditor::selectPitches (const std::function<bool (int pitch)>& wanted)
{
    selection.clear();
    if (! track.isValid())
        return;

    auto notes = SongDocument::getNotesNode (track);
    for (int i = 0; i < notes.getNumChildren(); ++i)
    {
        auto noteNode = notes.getChild (i);
        if (wanted ((int) noteNode.getProperty (SongIDs::pitch)))
            selection.push_back (noteNode);
    }
}

void SourceRollEditor::toggleSelection (const juce::ValueTree& note)
{
    auto it = std::find (selection.begin(), selection.end(), note);
    if (it != selection.end())
        selection.erase (it);
    else
        selection.push_back (note);
}

juce::ValueTree SourceRollEditor::hitTestNote (juce::Point<int> pos) const
{
    if (! track.isValid())
        return {};

    // Back-to-front: a later child paints on top, so it should win the hit
    // test for overlapping notes, matching what's visually on top.
    auto notes = SongDocument::getNotesNode (track);
    for (int i = notes.getNumChildren(); --i >= 0; )
    {
        auto noteNode = notes.getChild (i);
        if (noteFootprint (geometry.noteBounds (toPianoRollNote (noteNode)), noteShape).hit.contains (pos))
            return noteNode;
    }
    return {};
}

bool SourceRollEditor::isWithinNoteLength (juce::Point<int> pos) const
{
    if (! track.isValid())
        return false;

    auto notes = SongDocument::getNotesNode (track);
    for (int i = 0; i < notes.getNumChildren(); ++i)
        if (toRect (geometry.noteBounds (toPianoRollNote (notes.getChild (i)))).contains (pos))
            return true;
    return false;
}

int SourceRollEditor::hitTestEdgeZone (const juce::ValueTree& note, juce::Point<int> pos) const
{
    const auto noteBounds = geometry.noteBounds (toPianoRollNote (note));
    const auto footprint = noteFootprint (noteBounds, noteShape);
    const auto bounds = toRect (noteBounds);
    if (footprint.edges == NoteEdges::none)
        return 0;
    if (footprint.edges == NoteEdges::rightOnly)
    {
        // Only the line's end resizes, and only where it reaches past the diamond --
        // otherwise the diamond wins and the press moves the note.
        const int zoneStart = bounds.getRight() - edgeThresholdPixels;
        return pos.x >= zoneStart && zoneStart > footprint.body.getRight() ? 1 : 0;
    }
    if (bounds.getWidth() < 3 * edgeThresholdPixels)
        return 0; // too narrow for distinct edge zones -- body drag (Move) wins
    if (pos.x <= bounds.getX() + edgeThresholdPixels)
        return -1;
    if (pos.x >= bounds.getRight() - edgeThresholdPixels)
        return 1;
    return 0;
}

void SourceRollEditor::updateRubberBandSelection()
{
    selection = baseSelectionForRubberBand;
    if (! track.isValid())
        return;

    auto notes = SongDocument::getNotesNode (track);
    for (int i = 0; i < notes.getNumChildren(); ++i)
    {
        auto noteNode = notes.getChild (i);
        if (noteFootprint (geometry.noteBounds (toPianoRollNote (noteNode)), noteShape).hit.intersects (rubberBandRect)
            && ! isSelected (noteNode))
            selection.push_back (noteNode);
    }
}

bool SourceRollEditor::mouseDown (juce::Point<int> pos, juce::ModifierKeys mods, bool isDoubleClick)
{
    pruneSelection();

    if (mods.isPopupMenu())
    {
        dragMode = DragMode::RightPress;
        dragStartPos = pos;
        rightPressAdds = mods.isShiftDown() || mods.isCtrlDown() || mods.isCommandDown();
        return false;
    }

    if (isDoubleClick)
    {
        if (hitTestNote (pos).isValid() || isWithinNoteLength (pos))
            return false;
        if (pitchAllowed && ! pitchAllowed (geometry.pitchForY (pos.y)))
            return false;

        createNoteAt (pos, snapsWith (mods));
        return true;
    }

    auto note = hitTestNote (pos);

    if (! note.isValid())
    {
        dragMode = DragMode::None;
        if (mods.isShiftDown() || mods.isCtrlDown() || mods.isCommandDown() || selection.empty())
            return false;
        selection.clear();
        return true;
    }

    if (mods.isShiftDown() || mods.isCtrlDown() || mods.isCommandDown())
    {
        toggleSelection (note);
        dragMode = DragMode::None;
        return true;
    }

    if (! isSelected (note))
        selectOnly (note);

    const int edge = hitTestEdgeZone (note, pos);
    dragMode = edge < 0 ? DragMode::ResizeLeft : edge > 0 ? DragMode::ResizeRight : DragMode::Move;
    dragStartPos = pos;

    dragOriginals.clear();
    movePitchChanged = false;
    grabbedNote = dragMode == DragMode::Move ? note : juce::ValueTree();
    if (dragMode == DragMode::Move)
    {
        for (auto& n : selection)
            dragOriginals.push_back ({ n, (int) n.getProperty (SongIDs::startTick),
                                        (int) n.getProperty (SongIDs::pitch),
                                        (int) n.getProperty (SongIDs::durationTicks) });
    }
    else
    {
        dragOriginals.push_back ({ note, (int) note.getProperty (SongIDs::startTick),
                                    (int) note.getProperty (SongIDs::pitch),
                                    (int) note.getProperty (SongIDs::durationTicks) });
    }

    doc.getUndoManager().beginNewTransaction();
    return true;
}

bool SourceRollEditor::snapsWith (juce::ModifierKeys mods) const
{
    return snapToGrid && grid().spacingTicks > 0 && ! mods.isAltDown();
}

bool SourceRollEditor::mouseDrag (juce::Point<int> pos, juce::ModifierKeys mods)
{
    if (dragMode == DragMode::RightPress)
    {
        if (pos.getDistanceFrom (dragStartPos) < lassoThresholdPixels)
            return false;
        baseSelectionForRubberBand = rightPressAdds ? selection : std::vector<juce::ValueTree>();
        dragMode = DragMode::RubberBand;
    }

    if (dragMode == DragMode::RubberBand)
    {
        rubberBandRect = juce::Rectangle<int> (dragStartPos, pos);
        updateRubberBandSelection();
        return true;
    }

    if (dragMode == DragMode::None || dragOriginals.empty())
        return false;

    int deltaTick = geometry.tickForX (pos.x) - geometry.tickForX (dragStartPos.x);
    // Snapping, a pointer within a few pixels of the press keeps the original
    // timing, so a pitch drag's wobble never pulls an off-grid note onto the grid.
    const bool nearPress = std::abs (pos.x - dragStartPos.x) < lassoThresholdPixels;
    const bool snapping = snapsWith (mods);
    const bool snap = snapping && ! nearPress;
    const EditorGrid g = snap ? grid() : EditorGrid::straight (0);
    if (snapping && nearPress)
        deltaTick = 0;

    if (dragMode == DragMode::Move)
    {
        const int deltaPitch = geometry.pitchForY (pos.y) - geometry.pitchForY (dragStartPos.y);
        if (snap)
        {
            // The grabbed note lands on the grid; the rest follow by the same delta.
            const auto grabbed = std::find_if (dragOriginals.begin(), dragOriginals.end(),
                                               [this] (const DragOriginal& o) { return o.note == grabbedNote; });
            const int anchor = grabbed != dragOriginals.end() ? grabbed->startTick : dragOriginals.front().startTick;
            deltaTick = (int) snapToEditorGrid (g, anchor + deltaTick) - anchor;
        }
        bool pitchChanged = false;
        for (auto& orig : dragOriginals)
        {
            const int newStart = std::max (0, orig.startTick + deltaTick);
            const int newPitch = juce::jlimit (0, 127, orig.pitch + deltaPitch);
            if ((int) orig.note.getProperty (SongIDs::startTick) != newStart)
            {
                doc.setProperty (orig.note, SongIDs::startTick, newStart, false);
                markTimingEdited (orig.note);
            }
            if ((int) orig.note.getProperty (SongIDs::pitch) != newPitch)
            {
                doc.setProperty (orig.note, SongIDs::pitch, newPitch, false);
                pitchChanged = true;
            }
        }
        if (pitchChanged)
        {
            movePitchChanged = true;
            reportChange (NoteChange::edited, draggedNotes());
        }
        return true;
    }

    // Resize: dragOriginals holds exactly one entry (the primary note --
    // resize is not group-scoped, see the plan's scope decisions).
    auto& orig = dragOriginals.front();
    if (dragMode == DragMode::ResizeRight)
    {
        int newDuration = std::max (1, orig.durationTicks + deltaTick);
        if (snap)
        {
            const int end = std::max ((int) snapToEditorGrid (g, orig.startTick + newDuration),
                                      (int) stepEditorGrid (g, orig.startTick, 1));
            newDuration = end - orig.startTick;
        }
        if ((int) orig.note.getProperty (SongIDs::durationTicks) != newDuration)
        {
            doc.setProperty (orig.note, SongIDs::durationTicks, newDuration, false);
            markTimingEdited (orig.note);
        }
    }
    else // ResizeLeft: end tick (startTick + durationTicks) stays fixed.
    {
        const int endTick = orig.startTick + orig.durationTicks;
        int newStart = juce::jlimit (0, endTick - 1, orig.startTick + deltaTick);
        if (snap)
            newStart = std::min ((int) snapToEditorGrid (g, newStart),
                                 (int) stepEditorGrid (g, endTick, -1));
        const int newDuration = endTick - newStart;
        if ((int) orig.note.getProperty (SongIDs::startTick) != newStart)
        {
            doc.setProperty (orig.note, SongIDs::startTick, newStart, false);
            markTimingEdited (orig.note);
        }
        if ((int) orig.note.getProperty (SongIDs::durationTicks) != newDuration)
        {
            doc.setProperty (orig.note, SongIDs::durationTicks, newDuration, false);
            markTimingEdited (orig.note);
        }
    }
    return true;
}

bool SourceRollEditor::mouseUp (juce::Point<int>)
{
    const bool wasActive = dragMode != DragMode::None;
    // A drag that only moved in time sounds once, on release; one that changed
    // a pitch already sounded on each new row.
    if (dragMode == DragMode::Move && ! movePitchChanged)
    {
        const bool moved = std::any_of (dragOriginals.begin(), dragOriginals.end(), [] (const DragOriginal& o)
                                        { return (int) o.note.getProperty (SongIDs::startTick) != o.startTick; });
        if (moved)
            reportChange (NoteChange::edited, draggedNotes());
    }
    dragMode = DragMode::None;
    dragOriginals.clear();
    grabbedNote = {};
    rubberBandRect = {};
    return wasActive;
}

int SourceRollEditor::movingNotePitch() const
{
    if (dragMode != DragMode::Move || ! grabbedNote.isValid())
        return -1;
    return (int) grabbedNote.getProperty (SongIDs::pitch);
}

bool SourceRollEditor::keyPressed (const juce::KeyPress& key)
{
    if (key == juce::KeyPress (juce::KeyPress::deleteKey) || key == juce::KeyPress (juce::KeyPress::backspaceKey))
        return deleteSelection();

    // KeyPress equality is used instead of the more idiomatic getModifiers()/getTextCharacter()
    // pattern because KeyPress::operator== treats textCharacter as a wildcard when either side
    // is 0, and case-folds keyCode when both are <256. This allows a lowercase-character KeyPress
    // with textCharacter 0 to match both a real keyboard event (where keyCode='Z'/90) and a
    // manually-constructed test KeyPress. In contrast, getTextCharacter() returns 0 on test
    // KeyPress objects and never matches 'z'.
    // (See JUCE/modules/juce_gui_basics/keyboard/juce_KeyPress.cpp:52-63)

    if (key == juce::KeyPress ('z', juce::ModifierKeys (juce::ModifierKeys::ctrlModifier), 0))
    {
        doc.undo();
        pruneSelection();
        return true;
    }

    if (key == juce::KeyPress ('z', juce::ModifierKeys (juce::ModifierKeys::ctrlModifier | juce::ModifierKeys::shiftModifier), 0)
        || key == juce::KeyPress ('y', juce::ModifierKeys (juce::ModifierKeys::ctrlModifier), 0))
    {
        doc.redo();
        pruneSelection();
        return true;
    }

    return false;
}
bool SourceRollEditor::deleteSelection()
{
    pruneSelection();
    if (selection.empty())
        return false;

    doc.getUndoManager().beginNewTransaction();
    for (auto& note : selection)
        doc.removeChild (SongDocument::getNotesNode (track), note, false);
    selection.clear();
    return true;
}
bool SourceRollEditor::quantizeSelection()
{
    pruneSelection();
    const EditorGrid g = grid();
    if (selection.empty() || g.spacingTicks <= 0)
        return false;

    doc.getUndoManager().beginNewTransaction();
    for (auto& note : selection)
    {
        const int origStart = (int) note.getProperty (SongIDs::startTick);
        const int origDuration = (int) note.getProperty (SongIDs::durationTicks);

        const int snappedStart = (int) snapToEditorGrid (g, origStart);
        // At least one grid unit -- the spec sets no floor, but a
        // zero-length note after quantize is nonsensical.
        const int snappedDuration = juce::jmax (g.spacingTicks,
                                                 (int) std::lround ((double) origDuration / g.spacingTicks) * g.spacingTicks);

        if (origStart != snappedStart)
        {
            doc.setProperty (note, SongIDs::startTick, snappedStart, false);
            markTimingEdited (note);
        }
        if (origDuration != snappedDuration)
        {
            doc.setProperty (note, SongIDs::durationTicks, snappedDuration, false);
            markTimingEdited (note);
        }
    }
    return true;
}
// A note whose timing changed is no longer at its imported position in the
// file, so it exports as new material with a real note-off (2026-10-03
// MIDI-fidelity spec, "Editing interactions"). Joins the caller's open
// transaction.
void SourceRollEditor::markTimingEdited (juce::ValueTree note)
{
    markNoteTimingEdited (doc, note);
}

void SourceRollEditor::createNoteAt (juce::Point<int> pos, bool snap)
{
    if (! track.isValid())
        return;

    // Snapped, the note starts at the grid line at or before the click: the cell it is in.
    const int tick = std::max (0, geometry.tickForX (pos.x));
    const EditorGrid g = grid();
    const int startTick = snap ? (int) editorGridLineAtOrBefore (g, tick) : tick;
    // A Note Length sets it outright. Otherwise, snapped, it runs to the next line; unsnapped it
    // is one grid spacing long (a quarter with no grid).
    const int noteTicks = editorGridTicks (noteLength, noteDivision, geometry.getTicksPerQuarter());
    const int duration = noteTicks > 0 ? noteTicks
                       : snap ? (int) editorGridLineAfter (g, startTick) - startTick
                              : g.spacingTicks > 0 ? g.spacingTicks : geometry.getTicksPerQuarter();

    juce::ValueTree note (SongIDs::NOTE);
    note.setProperty (SongIDs::pitch, geometry.pitchForY (pos.y), nullptr);
    note.setProperty (SongIDs::startTick, startTick, nullptr);
    note.setProperty (SongIDs::durationTicks, duration, nullptr);
    note.setProperty (SongIDs::velocity, 100, nullptr);
    note.setProperty (SongIDs::isDrum, false, nullptr);
    note.setProperty (SongIDs::sourceTrackIndex, -1, nullptr);
    note.setProperty (SongIDs::sourceEventIndex, -1, nullptr);
    note.setProperty (SongIDs::channel, (int) track.getProperty (SongIDs::defaultChannel, 1), nullptr);
    note.setProperty (SongIDs::offVelocity, 64, nullptr);
    note.setProperty (SongIDs::offIsNoteOnZero, false, nullptr);
    note.setProperty (SongIDs::offSynthesized, false, nullptr);

    doc.addChild (SongDocument::getNotesNode (track), note);
    selectOnly (note);
    reportChange (NoteChange::inserted, { note });
}

void SourceRollEditor::reportChange (NoteChange change, const std::vector<juce::ValueTree>& notes) const
{
    if (noteChanged)
        noteChanged (change, notes);
}

std::vector<juce::ValueTree> SourceRollEditor::draggedNotes() const
{
    std::vector<juce::ValueTree> notes;
    notes.reserve (dragOriginals.size());
    for (const auto& o : dragOriginals)
        notes.push_back (o.note);
    return notes;
}

} // namespace lotro
