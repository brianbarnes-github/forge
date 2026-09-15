#include "SourceRollEditor.h"

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
                                      [this] (const juce::ValueTree& n) { return n.getParent() != track; }),
                      selection.end());
}

void SourceRollEditor::selectOnly (const juce::ValueTree& note)
{
    selection.clear();
    if (note.isValid())
        selection.push_back (note);
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
    for (int i = track.getNumChildren(); --i >= 0; )
    {
        auto noteNode = track.getChild (i);
        if (toRect (geometry.noteBounds (toPianoRollNote (noteNode))).contains (pos))
            return noteNode;
    }
    return {};
}

int SourceRollEditor::hitTestEdgeZone (const juce::ValueTree& note, juce::Point<int> pos) const
{
    auto bounds = toRect (geometry.noteBounds (toPianoRollNote (note)));
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

    for (int i = 0; i < track.getNumChildren(); ++i)
    {
        auto noteNode = track.getChild (i);
        if (toRect (geometry.noteBounds (toPianoRollNote (noteNode))).intersects (rubberBandRect)
            && ! isSelected (noteNode))
            selection.push_back (noteNode);
    }
}

bool SourceRollEditor::mouseDown (juce::Point<int> pos, juce::ModifierKeys mods, bool isDoubleClick)
{
    pruneSelection();

    if (isDoubleClick)
    {
        if (hitTestNote (pos).isValid())
            return false;

        createNoteAt (pos);
        return true;
    }

    auto note = hitTestNote (pos);

    if (! note.isValid())
    {
        baseSelectionForRubberBand = (mods.isShiftDown() || mods.isCtrlDown() || mods.isCommandDown())
                                          ? selection
                                          : std::vector<juce::ValueTree>();
        selection = baseSelectionForRubberBand;
        dragMode = DragMode::RubberBand;
        dragStartPos = pos;
        rubberBandRect = { pos.x, pos.y, 0, 0 };
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

bool SourceRollEditor::mouseDrag (juce::Point<int> pos)
{
    if (dragMode == DragMode::RubberBand)
    {
        rubberBandRect = juce::Rectangle<int> (dragStartPos, pos);
        updateRubberBandSelection();
        return true;
    }

    if (dragMode == DragMode::None || dragOriginals.empty())
        return false;

    const int deltaTick = geometry.tickForX (pos.x) - geometry.tickForX (dragStartPos.x);

    if (dragMode == DragMode::Move)
    {
        const int deltaPitch = geometry.pitchForY (pos.y) - geometry.pitchForY (dragStartPos.y);
        for (auto& orig : dragOriginals)
        {
            const int newStart = std::max (0, orig.startTick + deltaTick);
            const int newPitch = juce::jlimit (0, 127, orig.pitch + deltaPitch);
            if ((int) orig.note.getProperty (SongIDs::startTick) != newStart)
                doc.setProperty (orig.note, SongIDs::startTick, newStart, false);
            if ((int) orig.note.getProperty (SongIDs::pitch) != newPitch)
                doc.setProperty (orig.note, SongIDs::pitch, newPitch, false);
        }
        return true;
    }

    // Resize: dragOriginals holds exactly one entry (the primary note --
    // resize is not group-scoped, see the plan's scope decisions).
    auto& orig = dragOriginals.front();
    if (dragMode == DragMode::ResizeRight)
    {
        const int newDuration = std::max (1, orig.durationTicks + deltaTick);
        if ((int) orig.note.getProperty (SongIDs::durationTicks) != newDuration)
            doc.setProperty (orig.note, SongIDs::durationTicks, newDuration, false);
    }
    else // ResizeLeft: end tick (startTick + durationTicks) stays fixed.
    {
        const int endTick = orig.startTick + orig.durationTicks;
        const int newStart = juce::jlimit (0, endTick - 1, orig.startTick + deltaTick);
        const int newDuration = endTick - newStart;
        if ((int) orig.note.getProperty (SongIDs::startTick) != newStart)
            doc.setProperty (orig.note, SongIDs::startTick, newStart, false);
        if ((int) orig.note.getProperty (SongIDs::durationTicks) != newDuration)
            doc.setProperty (orig.note, SongIDs::durationTicks, newDuration, false);
    }
    return true;
}

bool SourceRollEditor::mouseUp (juce::Point<int>)
{
    const bool wasActive = dragMode != DragMode::None;
    dragMode = DragMode::None;
    dragOriginals.clear();
    rubberBandRect = {};
    return wasActive;
}

bool SourceRollEditor::keyPressed (const juce::KeyPress&) { return false; }     // Task 6
bool SourceRollEditor::deleteSelection()
{
    pruneSelection();
    if (selection.empty())
        return false;

    doc.getUndoManager().beginNewTransaction();
    for (auto& note : selection)
        doc.removeChild (track, note, false);
    selection.clear();
    return true;
}
bool SourceRollEditor::quantizeSelection()
{
    pruneSelection();
    if (selection.empty() || currentGridTicks <= 0)
        return false;

    doc.getUndoManager().beginNewTransaction();
    for (auto& note : selection)
    {
        const int origStart = (int) note.getProperty (SongIDs::startTick);
        const int origDuration = (int) note.getProperty (SongIDs::durationTicks);

        const int snappedStart = juce::jmax (0, (int) std::lround ((double) origStart / currentGridTicks) * currentGridTicks);
        // At least one grid unit -- the spec sets no floor, but a
        // zero-length note after quantize is nonsensical.
        const int snappedDuration = juce::jmax (currentGridTicks,
                                                 (int) std::lround ((double) origDuration / currentGridTicks) * currentGridTicks);

        if (origStart != snappedStart)
            doc.setProperty (note, SongIDs::startTick, snappedStart, false);
        if (origDuration != snappedDuration)
            doc.setProperty (note, SongIDs::durationTicks, snappedDuration, false);
    }
    return true;
}
void SourceRollEditor::createNoteAt (juce::Point<int> pos)
{
    if (! track.isValid())
        return;

    juce::ValueTree note (SongIDs::NOTE);
    note.setProperty (SongIDs::pitch, geometry.pitchForY (pos.y), nullptr);
    note.setProperty (SongIDs::startTick, std::max (0, geometry.tickForX (pos.x)), nullptr);
    note.setProperty (SongIDs::durationTicks,
                       currentGridTicks > 0 ? currentGridTicks : geometry.getTicksPerQuarter(), nullptr);
    note.setProperty (SongIDs::velocity, 100, nullptr);
    note.setProperty (SongIDs::isDrum, false, nullptr);
    note.setProperty (SongIDs::sourceTrackIndex, -1, nullptr);
    note.setProperty (SongIDs::sourceEventIndex, -1, nullptr);

    doc.addChild (track, note);
    selectOnly (note);
}

} // namespace lotro
