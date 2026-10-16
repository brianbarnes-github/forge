// Verifies SourceRollEditor's mouse-gesture-driven note editing against a
// hand-built ValueTree: hit-testing, click/shift-click/rubber-band
// selection, and move/resize drags, plus their undo-transaction
// boundaries. No JUCE painting involved.

#include "UI/SourceRollEditor.h"

#include <catch2/catch_test_macros.hpp>

using namespace lotro;

namespace
{
    constexpr int ticksPerQuarter = 480;
    constexpr int viewportWidth = 800;
    constexpr int viewportHeight = 300;

    juce::ValueTree makeNote (int pitch, int startTick, int durationTicks)
    {
        juce::ValueTree note (SongIDs::NOTE);
        note.setProperty (SongIDs::pitch, pitch, nullptr);
        note.setProperty (SongIDs::startTick, startTick, nullptr);
        note.setProperty (SongIDs::durationTicks, durationTicks, nullptr);
        note.setProperty (SongIDs::sourceTrackIndex, -1, nullptr);
        note.setProperty (SongIDs::sourceEventIndex, -1, nullptr);
        return note;
    }

    // A track with two one-quarter-note notes: pitch 60 at tick 0, pitch 64
    // immediately following at tick 480.
    struct Fixture
    {
        SongDocument doc;
        juce::ValueTree track = doc.addTrack ("Track A", 0xFF0000, 0, 1);
        juce::ValueTree noteA = makeNote (60, 0, ticksPerQuarter);
        juce::ValueTree noteB = makeNote (64, ticksPerQuarter, ticksPerQuarter);
        PianoRollGeometry geometry = PianoRollGeometry::fitToContent ({ 0, ticksPerQuarter * 2 }, { 60, 65 },
                                                                        ticksPerQuarter, viewportWidth, viewportHeight);
        SourceRollEditor editor { doc };

        Fixture()
        {
            SongDocument::getNotesNode (track).addChild (noteA, -1, nullptr);
            SongDocument::getNotesNode (track).addChild (noteB, -1, nullptr);
            editor.setTrack (track);
            editor.setGeometry (geometry);
        }

        static PianoRollNote toNote (const juce::ValueTree& n)
        {
            PianoRollNote pn;
            pn.pitch = (int) n.getProperty (SongIDs::pitch);
            pn.startTick = (int) n.getProperty (SongIDs::startTick);
            pn.durationTicks = (int) n.getProperty (SongIDs::durationTicks);
            return pn;
        }

        juce::Point<int> centreOf (const juce::ValueTree& note) const
        {
            auto b = geometry.noteBounds (toNote (note));
            return { b.x + b.width / 2, b.y + b.height / 2 };
        }
    };
}

TEST_CASE ("SourceRollEditor: a plain click on a note selects only that note", "[source-roll-editor]")
{
    Fixture f;
    CHECK (f.editor.mouseDown (f.centreOf (f.noteA), {}, false));
    CHECK (f.editor.isSelected (f.noteA));
    CHECK_FALSE (f.editor.isSelected (f.noteB));
    CHECK (f.editor.getNumSelected() == 1);
}

TEST_CASE ("SourceRollEditor: clicking empty space clears the selection", "[source-roll-editor]")
{
    Fixture f;
    f.editor.mouseDown (f.centreOf (f.noteA), {}, false);
    REQUIRE (f.editor.isSelected (f.noteA));

    f.editor.mouseDown ({ f.geometry.getKeyboardGutterWidth() + 2, f.geometry.yForPitch (70) }, {}, false);
    CHECK_FALSE (f.editor.isSelected (f.noteA));
    CHECK (f.editor.getNumSelected() == 0);
}

TEST_CASE ("SourceRollEditor: shift-click toggles a note into and out of the selection without starting a drag", "[source-roll-editor]")
{
    Fixture f;
    juce::ModifierKeys shift (juce::ModifierKeys::shiftModifier);

    f.editor.mouseDown (f.centreOf (f.noteA), {}, false);
    f.editor.mouseDown (f.centreOf (f.noteB), shift, false);
    CHECK (f.editor.isSelected (f.noteA));
    CHECK (f.editor.isSelected (f.noteB));
    CHECK (f.editor.getNumSelected() == 2);

    f.editor.mouseDown (f.centreOf (f.noteA), shift, false);
    CHECK_FALSE (f.editor.isSelected (f.noteA));
    CHECK (f.editor.isSelected (f.noteB));

    const auto before = (int) f.noteB.getProperty (SongIDs::startTick);
    f.editor.mouseDrag (f.centreOf (f.noteB).translated (100, 0));
    CHECK ((int) f.noteB.getProperty (SongIDs::startTick) == before); // shift-click never starts a drag
}

TEST_CASE ("SourceRollEditor: a right-drag rubber-bands every note it intersects", "[source-roll-editor][lasso]")
{
    Fixture f;
    const juce::ModifierKeys right (juce::ModifierKeys::rightButtonModifier);
    const juce::Point<int> start (f.geometry.getKeyboardGutterWidth() + 2, f.geometry.yForPitch (70));
    const juce::Point<int> end (f.geometry.xForTick (ticksPerQuarter * 2), f.geometry.yForPitch (55));

    f.editor.mouseDown (start, right, false);
    CHECK (f.editor.mouseDrag (end));
    CHECK (f.editor.getRubberBandRect() == juce::Rectangle<int> (start, end));

    CHECK (f.editor.isSelected (f.noteA));
    CHECK (f.editor.isSelected (f.noteB));

    f.editor.mouseUp (end);
    CHECK (f.editor.getRubberBandRect().isEmpty());
    CHECK (f.editor.getNumSelected() == 2);   // the release does not row-select
}

TEST_CASE ("SourceRollEditor: a right-drag starting on a note lassos and leaves the note where it is", "[source-roll-editor][lasso]")
{
    Fixture f;
    const juce::ModifierKeys right (juce::ModifierKeys::rightButtonModifier);
    const auto press = f.centreOf (f.noteA);
    const juce::Point<int> end (f.geometry.xForTick (ticksPerQuarter * 2), f.geometry.yForPitch (66));

    f.editor.mouseDown (press, right, false);
    f.editor.mouseDrag (end);
    f.editor.mouseUp (end);

    CHECK ((int) f.noteA.getProperty (SongIDs::startTick) == 0);
    CHECK ((int) f.noteA.getProperty (SongIDs::pitch) == 60);
    CHECK (f.editor.isSelected (f.noteA));
    CHECK (f.editor.isSelected (f.noteB));
}

TEST_CASE ("SourceRollEditor: a right-drag replaces the selection; with shift or ctrl it adds to it", "[source-roll-editor][lasso]")
{
    Fixture f;
    const juce::Point<int> overB (f.geometry.xForTick (ticksPerQuarter + 10), f.geometry.yForPitch (66));
    const juce::Point<int> overBEnd (f.geometry.xForTick (ticksPerQuarter * 2), f.geometry.yForPitch (63));

    f.editor.mouseDown (f.centreOf (f.noteA), {}, false);
    f.editor.mouseUp (f.centreOf (f.noteA));
    f.editor.mouseDown (overB, juce::ModifierKeys (juce::ModifierKeys::rightButtonModifier), false);
    f.editor.mouseDrag (overBEnd);
    f.editor.mouseUp (overBEnd);
    CHECK_FALSE (f.editor.isSelected (f.noteA));
    CHECK (f.editor.isSelected (f.noteB));

    f.editor.mouseDown (f.centreOf (f.noteA), {}, false);
    f.editor.mouseUp (f.centreOf (f.noteA));
    for (const auto modifier : { juce::ModifierKeys::shiftModifier, juce::ModifierKeys::ctrlModifier })
    {
        f.editor.mouseDown (overB, juce::ModifierKeys (juce::ModifierKeys::rightButtonModifier | modifier), false);
        f.editor.mouseDrag (overBEnd);
        f.editor.mouseUp (overBEnd);
        CHECK (f.editor.isSelected (f.noteA));
        CHECK (f.editor.isSelected (f.noteB));
    }
}

TEST_CASE ("SourceRollEditor: a left-drag on empty canvas selects nothing and draws no band", "[source-roll-editor][lasso]")
{
    Fixture f;
    const juce::Point<int> start (f.geometry.getKeyboardGutterWidth() + 2, f.geometry.yForPitch (70));
    const juce::Point<int> end (f.geometry.xForTick (ticksPerQuarter * 2), f.geometry.yForPitch (55));

    f.editor.mouseDown (start, {}, false);
    CHECK_FALSE (f.editor.mouseDrag (end));
    CHECK (f.editor.getRubberBandRect().isEmpty());
    f.editor.mouseUp (end);
    CHECK (f.editor.getNumSelected() == 0);
}

TEST_CASE ("SourceRollEditor: a shift- or ctrl-click on empty canvas keeps the selection", "[source-roll-editor][lasso]")
{
    Fixture f;
    const juce::Point<int> empty (f.geometry.getKeyboardGutterWidth() + 2, f.geometry.yForPitch (70));
    f.editor.mouseDown (f.centreOf (f.noteA), {}, false);
    f.editor.mouseUp (f.centreOf (f.noteA));

    for (const auto modifier : { juce::ModifierKeys::shiftModifier, juce::ModifierKeys::ctrlModifier })
    {
        f.editor.mouseDown (empty, juce::ModifierKeys (modifier), false);
        f.editor.mouseUp (empty);
        CHECK (f.editor.isSelected (f.noteA));
        CHECK (f.editor.getNumSelected() == 1);
    }
}

TEST_CASE ("SourceRollEditor: a move or a right press (a possible rubber band) is a gesture in progress until the release; a shift-click or empty left-click is not", "[source-roll-editor]")
{
    Fixture f;
    CHECK_FALSE (f.editor.isGestureActive());

    f.editor.mouseDown (f.centreOf (f.noteA), {}, false);   // a move
    CHECK (f.editor.isGestureActive());
    f.editor.mouseUp (f.centreOf (f.noteA));
    CHECK_FALSE (f.editor.isGestureActive());

    const juce::Point<int> empty (f.geometry.getKeyboardGutterWidth() + 2, f.geometry.yForPitch (70));
    f.editor.mouseDown (empty, juce::ModifierKeys (juce::ModifierKeys::rightButtonModifier), false);   // a rubber band or row pick
    CHECK (f.editor.isGestureActive());
    f.editor.mouseUp (empty);
    CHECK_FALSE (f.editor.isGestureActive());

    f.editor.mouseDown (empty, {}, false);   // a plain empty click
    CHECK_FALSE (f.editor.isGestureActive());

    f.editor.mouseDown (f.centreOf (f.noteB), juce::ModifierKeys (juce::ModifierKeys::shiftModifier), false);
    CHECK_FALSE (f.editor.isGestureActive());
}

TEST_CASE ("SourceRollEditor: dragging a selected note's body moves startTick and pitch together, one undo transaction", "[source-roll-editor]")
{
    Fixture f;
    const auto start = f.centreOf (f.noteA);
    const auto end = start.translated (f.geometry.xForTick (ticksPerQuarter) - f.geometry.xForTick (0),
                                        f.geometry.yForPitch (58) - f.geometry.yForPitch (60));

    REQUIRE (f.editor.mouseDown (start, {}, false));
    f.editor.mouseDrag (end);
    f.editor.mouseUp (end);

    CHECK ((int) f.noteA.getProperty (SongIDs::startTick) == ticksPerQuarter);
    CHECK ((int) f.noteA.getProperty (SongIDs::pitch) == 58);
    CHECK ((int) f.noteA.getProperty (SongIDs::durationTicks) == ticksPerQuarter); // unchanged

    REQUIRE (f.doc.canUndo());
    f.doc.undo();
    CHECK ((int) f.noteA.getProperty (SongIDs::startTick) == 0);
    CHECK ((int) f.noteA.getProperty (SongIDs::pitch) == 60);
}

TEST_CASE ("SourceRollEditor: dragging one note in a multi-selection moves every selected note by the same delta, one undo transaction", "[source-roll-editor]")
{
    Fixture f;
    juce::ModifierKeys shift (juce::ModifierKeys::shiftModifier);
    f.editor.mouseDown (f.centreOf (f.noteA), {}, false);
    f.editor.mouseDown (f.centreOf (f.noteB), shift, false);
    REQUIRE (f.editor.getNumSelected() == 2);

    const auto start = f.centreOf (f.noteA);
    const int deltaTickPixels = f.geometry.xForTick (ticksPerQuarter) - f.geometry.xForTick (0);
    const auto end = start.translated (deltaTickPixels, 0);

    REQUIRE (f.editor.mouseDown (start, {}, false)); // re-click an already-selected note keeps the multi-selection
    f.editor.mouseDrag (end);
    f.editor.mouseUp (end);

    CHECK ((int) f.noteA.getProperty (SongIDs::startTick) == ticksPerQuarter);
    CHECK ((int) f.noteB.getProperty (SongIDs::startTick) == ticksPerQuarter * 2);

    f.doc.undo();
    CHECK ((int) f.noteA.getProperty (SongIDs::startTick) == 0);
    CHECK ((int) f.noteB.getProperty (SongIDs::startTick) == ticksPerQuarter); // one undo restores BOTH
}

TEST_CASE ("SourceRollEditor: dragging a note's right edge resizes durationTicks only", "[source-roll-editor]")
{
    Fixture f;
    auto bounds = f.geometry.noteBounds (Fixture::toNote (f.noteA));
    const juce::Point<int> edgeStart (bounds.x + bounds.width - 1, bounds.y + bounds.height / 2);
    const int extraTicks = ticksPerQuarter / 2;
    const auto end = edgeStart.translated (f.geometry.xForTick (extraTicks) - f.geometry.xForTick (0), 0);

    REQUIRE (f.editor.mouseDown (edgeStart, {}, false));
    f.editor.mouseDrag (end);
    f.editor.mouseUp (end);

    CHECK ((int) f.noteA.getProperty (SongIDs::startTick) == 0); // unchanged
    CHECK ((int) f.noteA.getProperty (SongIDs::durationTicks) == ticksPerQuarter + extraTicks);
}

TEST_CASE ("SourceRollEditor: dragging a note's left edge changes startTick and durationTicks together, keeping the end tick fixed", "[source-roll-editor]")
{
    Fixture f; // noteB starts at tick 480, duration 480 (end tick 960)
    auto bounds = f.geometry.noteBounds (Fixture::toNote (f.noteB));
    const juce::Point<int> edgeStart (bounds.x + 1, bounds.y + bounds.height / 2);
    // /2, not /4: at this fixture's geometry (~0.77 px/tick, computed by
    // fitToContent for ticksPerQuarter=480/viewportWidth=800), /4 (120
    // ticks) lands on an exact half-pixel that std::lround rounds up, and
    // that half-pixel surplus survives the tickForX round-trip as a full
    // extra tick. /2 (240 ticks) lands on an exact pixel with no rounding
    // ambiguity.
    const int shrinkTicks = ticksPerQuarter / 2;
    const auto end = edgeStart.translated (f.geometry.xForTick (shrinkTicks) - f.geometry.xForTick (0), 0);

    REQUIRE (f.editor.mouseDown (edgeStart, {}, false));
    f.editor.mouseDrag (end);
    f.editor.mouseUp (end);

    const int expectedStart = ticksPerQuarter + shrinkTicks;
    const int expectedDuration = ticksPerQuarter - shrinkTicks;
    CHECK ((int) f.noteB.getProperty (SongIDs::startTick) == expectedStart);
    CHECK ((int) f.noteB.getProperty (SongIDs::durationTicks) == expectedDuration);
    CHECK (expectedStart + expectedDuration == ticksPerQuarter * 2); // end tick unchanged
}

TEST_CASE ("SourceRollEditor: a note too narrow for distinct edge zones (heavy zoom-out) starts a Move, not a resize, even when clicked at its very edge", "[source-roll-editor]")
{
    Fixture f;
    PianoRollGeometry narrowGeometry = f.geometry;
    narrowGeometry.setPixelsPerQuarterNote (2.0); // PianoRollComponent::zoom's minimum clamp
    f.editor.setGeometry (narrowGeometry);

    auto bounds = narrowGeometry.noteBounds (Fixture::toNote (f.noteA));
    REQUIRE (bounds.width < 3 * 6); // narrower than 3 * edgeThresholdPixels -- the guard's threshold

    const juce::Point<int> edgeStart (bounds.x, bounds.y + bounds.height / 2); // click at the note's very left pixel
    const auto end = edgeStart.translated (narrowGeometry.xForTick (ticksPerQuarter) - narrowGeometry.xForTick (0), 0);
    const int deltaTick = narrowGeometry.tickForX (end.x) - narrowGeometry.tickForX (edgeStart.x);
    REQUIRE (deltaTick != 0);

    REQUIRE (f.editor.mouseDown (edgeStart, {}, false));
    f.editor.mouseDrag (end);
    f.editor.mouseUp (end);

    // A Move (startTick shifts by the drag delta, durationTicks untouched) --
    // before the fix, this click at the very left pixel would have been
    // classified ResizeLeft and durationTicks would have shrunk instead.
    CHECK ((int) f.noteA.getProperty (SongIDs::startTick) == deltaTick);
    CHECK ((int) f.noteA.getProperty (SongIDs::durationTicks) == ticksPerQuarter);
}

TEST_CASE ("SourceRollEditor: double-clicking an empty cell creates a note there, one undo transaction", "[source-roll-editor]")
{
    Fixture f;
    const juce::Point<int> emptyCell (f.geometry.xForTick (ticksPerQuarter * 3), f.geometry.yForPitch (72));

    REQUIRE (SongDocument::getNotesNode (f.track).getNumChildren() == 2);
    CHECK (f.editor.mouseDown (emptyCell, {}, true));
    REQUIRE (SongDocument::getNotesNode (f.track).getNumChildren() == 3);

    auto created = SongDocument::getNotesNode (f.track).getChild (2);
    CHECK ((int) created.getProperty (SongIDs::pitch) == 72);
    CHECK ((int) created.getProperty (SongIDs::startTick) == f.geometry.tickForX (emptyCell.x));
    CHECK ((int) created.getProperty (SongIDs::durationTicks) == ticksPerQuarter); // grid off -> quarter note fallback

    REQUIRE (f.doc.canUndo());
    f.doc.undo();
    CHECK (SongDocument::getNotesNode (f.track).getNumChildren() == 2);
}

TEST_CASE ("SourceRollEditor: create uses the current grid size for the new note's duration when grid is on", "[source-roll-editor]")
{
    Fixture f;
    f.editor.setGridTicks (ticksPerQuarter / 4);

    const juce::Point<int> emptyCell (f.geometry.xForTick (ticksPerQuarter * 3), f.geometry.yForPitch (72));
    f.editor.mouseDown (emptyCell, {}, true);

    auto created = SongDocument::getNotesNode (f.track).getChild (2);
    CHECK ((int) created.getProperty (SongIDs::durationTicks) == ticksPerQuarter / 4);
}

TEST_CASE ("SourceRollEditor: double-clicking an existing note is a no-op", "[source-roll-editor]")
{
    Fixture f;
    CHECK_FALSE (f.editor.mouseDown (f.centreOf (f.noteA), {}, true));
    CHECK (SongDocument::getNotesNode (f.track).getNumChildren() == 2);
}

TEST_CASE ("SourceRollEditor: deleteSelection removes every selected note in one undo transaction", "[source-roll-editor]")
{
    Fixture f;
    juce::ModifierKeys shift (juce::ModifierKeys::shiftModifier);
    f.editor.mouseDown (f.centreOf (f.noteA), {}, false);
    f.editor.mouseDown (f.centreOf (f.noteB), shift, false);
    REQUIRE (f.editor.getNumSelected() == 2);

    CHECK (f.editor.deleteSelection());
    CHECK (SongDocument::getNotesNode (f.track).getNumChildren() == 0);
    CHECK (f.editor.getNumSelected() == 0);

    f.doc.undo();
    CHECK (SongDocument::getNotesNode (f.track).getNumChildren() == 2); // one undo restores both
}

TEST_CASE ("SourceRollEditor: deleteSelection with nothing selected is a no-op", "[source-roll-editor]")
{
    Fixture f;
    CHECK_FALSE (f.editor.deleteSelection());
    CHECK (SongDocument::getNotesNode (f.track).getNumChildren() == 2);
}

TEST_CASE ("SourceRollEditor: quantizeSelection snaps startTick and durationTicks of every selected note to the grid, one undo transaction", "[source-roll-editor]")
{
    Fixture f;
    // Nudge noteA off-grid so quantize has something to actually snap.
    f.noteA.setProperty (SongIDs::startTick, 10, nullptr);
    f.noteA.setProperty (SongIDs::durationTicks, 470, nullptr);

    f.editor.setGridTicks (ticksPerQuarter / 4); // 120 ticks
    f.editor.mouseDown (f.centreOf (f.noteA), {}, false);
    juce::ModifierKeys shift (juce::ModifierKeys::shiftModifier);
    f.editor.mouseDown (f.centreOf (f.noteB), shift, false);

    CHECK (f.editor.quantizeSelection());

    CHECK ((int) f.noteA.getProperty (SongIDs::startTick) == 0);       // round(10/120)*120 = 0
    CHECK ((int) f.noteA.getProperty (SongIDs::durationTicks) == 480); // round(470/120)*120 = 480
    CHECK ((int) f.noteB.getProperty (SongIDs::startTick) == 480);     // already on-grid, value preserved
    CHECK ((int) f.noteB.getProperty (SongIDs::durationTicks) == 480);

    REQUIRE (f.doc.canUndo());
    f.doc.undo();
    CHECK ((int) f.noteA.getProperty (SongIDs::startTick) == 10); // one undo restores BOTH notes
}

TEST_CASE ("SourceRollEditor: quantizeSelection is a no-op with an empty selection or the grid off", "[source-roll-editor]")
{
    Fixture f;
    CHECK_FALSE (f.editor.quantizeSelection()); // nothing selected

    f.editor.mouseDown (f.centreOf (f.noteA), {}, false);
    CHECK_FALSE (f.editor.quantizeSelection()); // grid still off (currentGridTicks == 0)
}

TEST_CASE ("SourceRollEditor: the delete key removes the selection; backspace does too", "[source-roll-editor]")
{
    Fixture f;
    f.editor.mouseDown (f.centreOf (f.noteA), {}, false);
    CHECK (f.editor.keyPressed (juce::KeyPress (juce::KeyPress::deleteKey)));
    CHECK (SongDocument::getNotesNode (f.track).getNumChildren() == 1);

    f.editor.mouseDown (f.centreOf (f.noteB), {}, false);
    CHECK (f.editor.keyPressed (juce::KeyPress (juce::KeyPress::backspaceKey)));
    CHECK (SongDocument::getNotesNode (f.track).getNumChildren() == 0);
}

TEST_CASE ("SourceRollEditor: the delete key with nothing selected is not handled", "[source-roll-editor]")
{
    Fixture f;
    CHECK_FALSE (f.editor.keyPressed (juce::KeyPress (juce::KeyPress::deleteKey)));
}

TEST_CASE ("SourceRollEditor: Ctrl+Z undoes and Ctrl+Y redoes the last mutation", "[source-roll-editor]")
{
    Fixture f;
    f.editor.mouseDown (f.centreOf (f.noteA), {}, false);
    REQUIRE (f.editor.keyPressed (juce::KeyPress (juce::KeyPress::deleteKey)));
    REQUIRE (SongDocument::getNotesNode (f.track).getNumChildren() == 1);

    const auto ctrlZ = juce::KeyPress ('z', juce::ModifierKeys (juce::ModifierKeys::ctrlModifier), 0);
    CHECK (f.editor.keyPressed (ctrlZ));
    CHECK (SongDocument::getNotesNode (f.track).getNumChildren() == 2);

    const auto ctrlY = juce::KeyPress ('y', juce::ModifierKeys (juce::ModifierKeys::ctrlModifier), 0);
    CHECK (f.editor.keyPressed (ctrlY));
    CHECK (SongDocument::getNotesNode (f.track).getNumChildren() == 1);
}

TEST_CASE ("SourceRollEditor: Ctrl+Shift+Z also redoes", "[source-roll-editor]")
{
    Fixture f;
    f.editor.mouseDown (f.centreOf (f.noteA), {}, false);
    f.editor.keyPressed (juce::KeyPress (juce::KeyPress::deleteKey));
    f.editor.keyPressed (juce::KeyPress ('z', juce::ModifierKeys (juce::ModifierKeys::ctrlModifier), 0));
    REQUIRE (SongDocument::getNotesNode (f.track).getNumChildren() == 2);

    const auto ctrlShiftZ = juce::KeyPress ('z', juce::ModifierKeys (juce::ModifierKeys::ctrlModifier
                                                                       | juce::ModifierKeys::shiftModifier), 0);
    CHECK (f.editor.keyPressed (ctrlShiftZ));
    CHECK (SongDocument::getNotesNode (f.track).getNumChildren() == 1);
}

namespace
{
    const juce::ModifierKeys rightClick { juce::ModifierKeys::rightButtonModifier };

    // Any x inside `pitch`'s row.
    juce::Point<int> rowPoint (const PianoRollGeometry& geometry, int x, int pitch)
    {
        return { x, geometry.yForPitch (pitch) + geometry.getRowHeight() / 2 };
    }
}

TEST_CASE ("SourceRollEditor: selectPitch selects every note of that pitch and nothing else", "[source-roll-editor][pitch-select]")
{
    Fixture f;
    auto laterC = makeNote (60, ticksPerQuarter * 4, ticksPerQuarter);
    SongDocument::getNotesNode (f.track).addChild (laterC, -1, nullptr);
    f.editor.mouseDown (f.centreOf (f.noteB), {}, false); // prior selection is replaced
    f.editor.mouseUp (f.centreOf (f.noteB));

    f.editor.selectPitch (60, false);

    CHECK (f.editor.getNumSelected() == 2);
    CHECK (f.editor.isSelected (f.noteA));
    CHECK (f.editor.isSelected (laterC));
    CHECK_FALSE (f.editor.isSelected (f.noteB));
}

TEST_CASE ("SourceRollEditor: selectPitch adding keeps the existing selection", "[source-roll-editor][pitch-select]")
{
    Fixture f;
    f.editor.selectPitch (60, false);
    f.editor.selectPitch (64, true);

    CHECK (f.editor.getNumSelected() == 2);
    CHECK (f.editor.isSelected (f.noteA));
    CHECK (f.editor.isSelected (f.noteB));
}

TEST_CASE ("SourceRollEditor: selectPitch on a pitch with no notes clears the selection", "[source-roll-editor][pitch-select]")
{
    Fixture f;
    f.editor.selectOnly (f.noteA);
    f.editor.selectPitch (62, false);
    CHECK (f.editor.getNumSelected() == 0);
}

TEST_CASE ("SourceRollEditor: a right-click on the roll, even with a wobble under 4 px, leaves the selection alone", "[source-roll-editor][pitch-select]")
{
    Fixture f;
    f.editor.selectOnly (f.noteA);

    // In noteB's row, on empty canvas and on noteB itself.
    for (const auto press : { rowPoint (f.geometry, f.geometry.xForTick (ticksPerQuarter * 3), 64), f.centreOf (f.noteB) })
    {
        f.editor.mouseDown (press, rightClick, false);
        CHECK_FALSE (f.editor.mouseDrag (press.translated (3, 0)));
        CHECK (f.editor.getRubberBandRect().isEmpty());
        f.editor.mouseUp (press.translated (3, 0));

        CHECK (f.editor.getNumSelected() == 1);
        CHECK (f.editor.isSelected (f.noteA));
    }
    CHECK ((int) f.noteB.getProperty (SongIDs::startTick) == ticksPerQuarter);
}

namespace
{
    void markImported (juce::ValueTree note)
    {
        note.setProperty (SongIDs::onOrder, 4, nullptr);
        note.setProperty (SongIDs::offOrder, 9, nullptr);
        note.setProperty (SongIDs::offSynthesized, true, nullptr);
    }
}

TEST_CASE ("SourceRollEditor: a created note takes the track's defaultChannel and standard note-off fields", "[source-roll-editor][fidelity]")
{
    Fixture f;
    f.track.setProperty (SongIDs::defaultChannel, 7, nullptr);
    const juce::Point<int> emptyCell (f.geometry.xForTick (ticksPerQuarter * 3), f.geometry.yForPitch (72));

    f.editor.mouseDown (emptyCell, {}, true);

    auto notes = SongDocument::getNotesNode (f.track);
    REQUIRE (notes.getNumChildren() == 3);
    auto created = notes.getChild (2);
    CHECK ((int) created.getProperty (SongIDs::channel) == 7);
    CHECK ((int) created.getProperty (SongIDs::offVelocity) == 64);
    CHECK (created.hasProperty (SongIDs::offIsNoteOnZero));
    CHECK_FALSE ((bool) created.getProperty (SongIDs::offIsNoteOnZero));
    CHECK (created.hasProperty (SongIDs::offSynthesized));
    CHECK_FALSE ((bool) created.getProperty (SongIDs::offSynthesized));
    CHECK_FALSE (created.hasProperty (SongIDs::onOrder));
    CHECK_FALSE (created.hasProperty (SongIDs::offOrder));
}

TEST_CASE ("SourceRollEditor: a created note on a track with no defaultChannel uses channel 1", "[source-roll-editor][fidelity]")
{
    Fixture f;
    f.track.removeProperty (SongIDs::defaultChannel, nullptr);
    const juce::Point<int> emptyCell (f.geometry.xForTick (ticksPerQuarter * 3), f.geometry.yForPitch (72));
    f.editor.mouseDown (emptyCell, {}, true);
    auto notes = SongDocument::getNotesNode (f.track);
    CHECK ((int) notes.getChild (notes.getNumChildren() - 1).getProperty (SongIDs::channel, -1) == 1);
}

TEST_CASE ("SourceRollEditor: moving a note in time clears its import orders in the same undo step", "[source-roll-editor][fidelity]")
{
    Fixture f;
    markImported (f.noteA);
    const auto start = f.centreOf (f.noteA);
    const auto end = start.translated (f.geometry.xForTick (ticksPerQuarter) - f.geometry.xForTick (0), 0);

    f.editor.mouseDown (start, {}, false);
    f.editor.mouseDrag (end);
    f.editor.mouseUp (end);

    REQUIRE ((int) f.noteA.getProperty (SongIDs::startTick) == ticksPerQuarter);
    CHECK_FALSE (f.noteA.hasProperty (SongIDs::onOrder));
    CHECK_FALSE (f.noteA.hasProperty (SongIDs::offOrder));
    CHECK_FALSE ((bool) f.noteA.getProperty (SongIDs::offSynthesized));

    f.doc.undo();
    CHECK ((int) f.noteA.getProperty (SongIDs::startTick) == 0);
    CHECK ((int) f.noteA.getProperty (SongIDs::onOrder) == 4);
    CHECK ((int) f.noteA.getProperty (SongIDs::offOrder) == 9);
    CHECK ((bool) f.noteA.getProperty (SongIDs::offSynthesized));
}

TEST_CASE ("SourceRollEditor: a pitch-only drag keeps a note's import orders", "[source-roll-editor][fidelity]")
{
    Fixture f;
    markImported (f.noteA);
    const auto start = f.centreOf (f.noteA);
    const auto end = start.translated (0, f.geometry.yForPitch (58) - f.geometry.yForPitch (60));

    f.editor.mouseDown (start, {}, false);
    f.editor.mouseDrag (end);
    f.editor.mouseUp (end);

    REQUIRE ((int) f.noteA.getProperty (SongIDs::pitch) == 58);
    CHECK ((int) f.noteA.getProperty (SongIDs::startTick) == 0);
    CHECK ((int) f.noteA.getProperty (SongIDs::onOrder) == 4);
    CHECK ((int) f.noteA.getProperty (SongIDs::offOrder) == 9);
    CHECK ((bool) f.noteA.getProperty (SongIDs::offSynthesized));
}

TEST_CASE ("SourceRollEditor: right and left resize clear import orders", "[source-roll-editor][fidelity]")
{
    Fixture f;
    markImported (f.noteA);
    markImported (f.noteB);

    auto a = f.geometry.noteBounds (Fixture::toNote (f.noteA));
    const juce::Point<int> rightEdge (a.x + a.width - 1, a.y + a.height / 2);
    const auto rightEnd = rightEdge.translated (f.geometry.xForTick (ticksPerQuarter / 2) - f.geometry.xForTick (0), 0);
    f.editor.mouseDown (rightEdge, {}, false);
    f.editor.mouseDrag (rightEnd);
    f.editor.mouseUp (rightEnd);
    REQUIRE ((int) f.noteA.getProperty (SongIDs::durationTicks) != ticksPerQuarter);
    CHECK_FALSE (f.noteA.hasProperty (SongIDs::onOrder));
    CHECK_FALSE (f.noteA.hasProperty (SongIDs::offOrder));
    CHECK_FALSE ((bool) f.noteA.getProperty (SongIDs::offSynthesized));

    auto b = f.geometry.noteBounds (Fixture::toNote (f.noteB));
    const juce::Point<int> leftEdge (b.x + 1, b.y + b.height / 2);
    const auto leftEnd = leftEdge.translated (f.geometry.xForTick (ticksPerQuarter / 2) - f.geometry.xForTick (0), 0);
    f.editor.mouseDown (leftEdge, {}, false);
    f.editor.mouseDrag (leftEnd);
    f.editor.mouseUp (leftEnd);
    REQUIRE ((int) f.noteB.getProperty (SongIDs::startTick) != ticksPerQuarter);
    CHECK_FALSE (f.noteB.hasProperty (SongIDs::onOrder));
    CHECK_FALSE (f.noteB.hasProperty (SongIDs::offOrder));
    CHECK_FALSE ((bool) f.noteB.getProperty (SongIDs::offSynthesized));
}

TEST_CASE ("SourceRollEditor: quantize clears import orders only on notes whose timing changed", "[source-roll-editor][fidelity]")
{
    Fixture f;
    f.noteA.setProperty (SongIDs::startTick, 100, nullptr); // off the 1/4 grid; noteB is on it
    markImported (f.noteA);
    markImported (f.noteB);
    f.editor.setGridTicks (ticksPerQuarter);

    f.editor.mouseDown (f.centreOf (f.noteA), {}, false);
    f.editor.mouseUp (f.centreOf (f.noteA));
    f.editor.mouseDown (f.centreOf (f.noteB), juce::ModifierKeys (juce::ModifierKeys::shiftModifier), false);
    f.editor.mouseUp (f.centreOf (f.noteB));
    REQUIRE (f.editor.getNumSelected() == 2);
    REQUIRE (f.editor.quantizeSelection());

    REQUIRE ((int) f.noteA.getProperty (SongIDs::startTick) == 0);
    CHECK_FALSE (f.noteA.hasProperty (SongIDs::onOrder));
    CHECK_FALSE ((bool) f.noteA.getProperty (SongIDs::offSynthesized));
    CHECK ((int) f.noteB.getProperty (SongIDs::onOrder) == 4);
    CHECK ((int) f.noteB.getProperty (SongIDs::offOrder) == 9);
    CHECK ((bool) f.noteB.getProperty (SongIDs::offSynthesized));

    f.doc.undo();
    CHECK ((int) f.noteA.getProperty (SongIDs::startTick) == 100);
    CHECK ((int) f.noteA.getProperty (SongIDs::onOrder) == 4);
    CHECK ((int) f.noteA.getProperty (SongIDs::offOrder) == 9);
    CHECK ((bool) f.noteA.getProperty (SongIDs::offSynthesized));
}

TEST_CASE ("SourceRollEditor: a quantize that changes nothing keeps import orders", "[source-roll-editor][fidelity]")
{
    Fixture f;
    markImported (f.noteB);
    f.editor.setGridTicks (ticksPerQuarter);
    f.editor.mouseDown (f.centreOf (f.noteB), {}, false);
    f.editor.mouseUp (f.centreOf (f.noteB));
    REQUIRE (f.editor.quantizeSelection());
    CHECK ((int) f.noteB.getProperty (SongIDs::onOrder) == 4);
    CHECK ((bool) f.noteB.getProperty (SongIDs::offSynthesized));
}

TEST_CASE ("SourceRollEditor: deleting a note leaves the track's events alone", "[source-roll-editor][fidelity]")
{
    Fixture f;
    juce::ValueTree event (SongIDs::EVENT);
    event.setProperty (SongIDs::tick, 0, nullptr);
    SongDocument::getEventsNode (f.track).addChild (event, -1, nullptr);

    f.editor.mouseDown (f.centreOf (f.noteA), {}, false);
    f.editor.mouseUp (f.centreOf (f.noteA));
    REQUIRE (f.editor.deleteSelection());
    CHECK (SongDocument::getEventsNode (f.track).getNumChildren() == 1);
}

TEST_CASE ("SourceRollEditor: addToSelection keeps the selection and adds a note once", "[source-roll-editor][note-nav]")
{
    SongDocument doc;
    auto track = doc.addTrack ("A", 0xFF0000, 0, 1);
    for (int i = 0; i < 3; ++i)
    {
        juce::ValueTree note (SongIDs::NOTE);
        note.setProperty (SongIDs::pitch, 60 + i, nullptr);
        note.setProperty (SongIDs::startTick, i * 480, nullptr);
        note.setProperty (SongIDs::durationTicks, 480, nullptr);
        SongDocument::getNotesNode (track).addChild (note, -1, nullptr);
    }
    SourceRollEditor editor (doc);
    editor.setTrack (track);
    const auto notes = SongDocument::getNotesNode (track);

    editor.selectOnly (notes.getChild (0));
    editor.addToSelection (notes.getChild (2));
    CHECK (editor.getNumSelected() == 2);
    CHECK (editor.isSelected (notes.getChild (0)));
    CHECK (editor.isSelected (notes.getChild (2)));
    editor.addToSelection (notes.getChild (2));   // already selected: unchanged
    CHECK (editor.getNumSelected() == 2);
    editor.addToSelection ({});   // invalid: ignored
    CHECK (editor.getNumSelected() == 2);
}

TEST_CASE ("SourceRollEditor: a pitch filter refuses a double-click on a disallowed row", "[source-roll-editor][snap-to-key]")
{
    Fixture f;
    f.editor.setPitchFilter ([] (int pitch) { return pitch != 72; });
    const juce::Point<int> emptyCell (f.geometry.xForTick (ticksPerQuarter * 3), f.geometry.yForPitch (72));
    const bool couldUndo = f.doc.canUndo();

    CHECK_FALSE (f.editor.mouseDown (emptyCell, {}, true));
    CHECK (SongDocument::getNotesNode (f.track).getNumChildren() == 2);
    CHECK (f.doc.canUndo() == couldUndo);
}

TEST_CASE ("SourceRollEditor: a refused double-click leaves the selection alone", "[source-roll-editor][snap-to-key]")
{
    Fixture f;
    f.editor.selectOnly (f.noteA);
    f.editor.setPitchFilter ([] (int pitch) { return pitch != 72; });
    const juce::Point<int> emptyCell (f.geometry.xForTick (ticksPerQuarter * 3), f.geometry.yForPitch (72));

    f.editor.mouseDown (emptyCell, {}, true);
    CHECK (f.editor.isSelected (f.noteA));
    CHECK (f.editor.getNumSelected() == 1);
}

TEST_CASE ("SourceRollEditor: an allowed pitch still creates a note under a pitch filter", "[source-roll-editor][snap-to-key]")
{
    Fixture f;
    f.editor.setPitchFilter ([] (int pitch) { return pitch == 72; });
    const juce::Point<int> emptyCell (f.geometry.xForTick (ticksPerQuarter * 3), f.geometry.yForPitch (72));

    CHECK (f.editor.mouseDown (emptyCell, {}, true));
    REQUIRE (SongDocument::getNotesNode (f.track).getNumChildren() == 3);
    CHECK ((int) SongDocument::getNotesNode (f.track).getChild (2).getProperty (SongIDs::pitch) == 72);
}

TEST_CASE ("SourceRollEditor: clearing the pitch filter allows every pitch again", "[source-roll-editor][snap-to-key]")
{
    Fixture f;
    f.editor.setPitchFilter ([] (int) { return false; });
    f.editor.setPitchFilter (nullptr);
    const juce::Point<int> emptyCell (f.geometry.xForTick (ticksPerQuarter * 3), f.geometry.yForPitch (72));

    CHECK (f.editor.mouseDown (emptyCell, {}, true));
    CHECK (SongDocument::getNotesNode (f.track).getNumChildren() == 3);
}

TEST_CASE ("SourceRollEditor: a pitch filter never stops a note being dragged onto a refused pitch", "[source-roll-editor][snap-to-key]")
{
    Fixture f;
    f.editor.setPitchFilter ([] (int pitch) { return pitch != 58; });
    const auto start = f.centreOf (f.noteA);
    const auto end = start.translated (0, f.geometry.yForPitch (58) - f.geometry.yForPitch (60));

    REQUIRE (f.editor.mouseDown (start, {}, false));
    f.editor.mouseDrag (end);
    f.editor.mouseUp (end);
    CHECK ((int) f.noteA.getProperty (SongIDs::pitch) == 58);
}

namespace
{
    // Records every note-change report as (change, number of notes, first note's pitch).
    struct ChangeRecorder
    {
        struct Call { NoteChange change; size_t count; int firstPitch; };
        std::vector<Call> calls;

        NoteChangeListener listener()
        {
            return [this] (NoteChange change, const std::vector<juce::ValueTree>& notes)
            {
                calls.push_back ({ change, notes.size(), notes.empty() ? -1 : (int) notes.front().getProperty (SongIDs::pitch) });
            };
        }
    };

    juce::Point<int> rowsUp (const Fixture& f, juce::Point<int> p, int rows)
    {
        return p.translated (0, f.geometry.yForPitch (60 + rows) - f.geometry.yForPitch (60));
    }
}

TEST_CASE ("SourceRollEditor: a double-click insert reports the new note", "[source-roll-editor][note-preview]")
{
    Fixture f;
    ChangeRecorder rec;
    f.editor.setNoteChangeListener (rec.listener());
    const juce::Point<int> emptyCell (f.geometry.xForTick (ticksPerQuarter * 3), f.geometry.yForPitch (62));

    REQUIRE (f.editor.mouseDown (emptyCell, {}, true));
    REQUIRE (rec.calls.size() == 1);
    CHECK (rec.calls[0].change == NoteChange::inserted);
    CHECK (rec.calls[0].count == 1);
    CHECK (rec.calls[0].firstPitch == 62);
}

TEST_CASE ("SourceRollEditor: a pitch-filter refusal reports nothing", "[source-roll-editor][note-preview]")
{
    Fixture f;
    ChangeRecorder rec;
    f.editor.setNoteChangeListener (rec.listener());
    f.editor.setPitchFilter ([] (int) { return false; });
    const juce::Point<int> emptyCell (f.geometry.xForTick (ticksPerQuarter * 3), f.geometry.yForPitch (62));

    f.editor.mouseDown (emptyCell, {}, true);
    f.editor.mouseDown (f.centreOf (f.noteA), {}, true);   // a double-click on a note creates nothing either
    CHECK (rec.calls.empty());
}

TEST_CASE ("SourceRollEditor: a drag reports each new row at once, and nothing more on release", "[source-roll-editor][note-preview]")
{
    Fixture f;
    ChangeRecorder rec;
    f.editor.setNoteChangeListener (rec.listener());
    const auto start = f.centreOf (f.noteA);

    REQUIRE (f.editor.mouseDown (start, {}, false));
    f.editor.mouseDrag (rowsUp (f, start, 1));
    f.editor.mouseDrag (rowsUp (f, start, 1));   // same row again: nothing new
    f.editor.mouseDrag (rowsUp (f, start, 2));
    REQUIRE (rec.calls.size() == 2);
    CHECK (rec.calls[0].change == NoteChange::edited);
    CHECK (rec.calls[0].firstPitch == 61);
    CHECK (rec.calls[1].firstPitch == 62);

    f.editor.mouseUp (rowsUp (f, start, 2));
    CHECK (rec.calls.size() == 2);
}

TEST_CASE ("SourceRollEditor: a drag that only moves in time reports once, on release", "[source-roll-editor][note-preview]")
{
    Fixture f;
    ChangeRecorder rec;
    f.editor.setNoteChangeListener (rec.listener());
    const auto start = f.centreOf (f.noteA);
    const auto end = start.translated (f.geometry.xForTick (ticksPerQuarter) - f.geometry.xForTick (0), 0);

    REQUIRE (f.editor.mouseDown (start, {}, false));
    f.editor.mouseDrag (end);
    CHECK (rec.calls.empty());
    f.editor.mouseUp (end);
    REQUIRE (rec.calls.size() == 1);
    CHECK (rec.calls[0].change == NoteChange::edited);
    CHECK (rec.calls[0].firstPitch == 60);
}

TEST_CASE ("SourceRollEditor: a drag that leaves and returns to its row while moving in time reports no release preview", "[source-roll-editor][note-preview]")
{
    Fixture f;
    ChangeRecorder rec;
    f.editor.setNoteChangeListener (rec.listener());
    const auto start = f.centreOf (f.noteA);
    const auto sideways = start.translated (f.geometry.xForTick (ticksPerQuarter) - f.geometry.xForTick (0), 0);

    REQUIRE (f.editor.mouseDown (start, {}, false));
    f.editor.mouseDrag (rowsUp (f, start, 1));
    f.editor.mouseDrag (start);
    f.editor.mouseDrag (sideways);
    f.editor.mouseUp (sideways);
    REQUIRE (rec.calls.size() == 2);
    CHECK (rec.calls[0].firstPitch == 61);
    CHECK (rec.calls[1].firstPitch == 60);
}

TEST_CASE ("SourceRollEditor: a group drag reports every selected note", "[source-roll-editor][note-preview]")
{
    Fixture f;
    ChangeRecorder rec;
    f.editor.setNoteChangeListener (rec.listener());
    f.editor.selectOnly (f.noteA);
    f.editor.addToSelection (f.noteB);
    const auto start = f.centreOf (f.noteA);

    REQUIRE (f.editor.mouseDown (start, {}, false));
    f.editor.mouseDrag (rowsUp (f, start, 1));
    f.editor.mouseUp (rowsUp (f, start, 1));
    REQUIRE (rec.calls.size() == 1);
    CHECK (rec.calls[0].count == 2);
}

TEST_CASE ("SourceRollEditor: resizing reports nothing", "[source-roll-editor][note-preview]")
{
    Fixture f;
    ChangeRecorder rec;
    f.editor.setNoteChangeListener (rec.listener());
    const int shift = f.geometry.xForTick (ticksPerQuarter / 2) - f.geometry.xForTick (0);

    auto a = f.geometry.noteBounds (Fixture::toNote (f.noteA));
    const juce::Point<int> rightEdge (a.x + a.width - 1, a.y + a.height / 2);
    REQUIRE (f.editor.mouseDown (rightEdge, {}, false));
    f.editor.mouseDrag (rightEdge.translated (shift, 0));
    f.editor.mouseUp (rightEdge.translated (shift, 0));

    auto b = f.geometry.noteBounds (Fixture::toNote (f.noteB));
    const juce::Point<int> leftEdge (b.x + 1, b.y + b.height / 2);
    REQUIRE (f.editor.mouseDown (leftEdge, {}, false));
    f.editor.mouseDrag (leftEdge.translated (shift, 0));
    f.editor.mouseUp (leftEdge.translated (shift, 0));

    CHECK ((int) f.noteB.getProperty (SongIDs::startTick) != ticksPerQuarter);   // the left edge really moved
    CHECK (rec.calls.empty());
}

TEST_CASE ("SourceRollEditor: a click on a note, without moving, reports nothing", "[source-roll-editor][note-preview]")
{
    Fixture f;
    ChangeRecorder rec;
    f.editor.setNoteChangeListener (rec.listener());
    const auto at = f.centreOf (f.noteA);

    REQUIRE (f.editor.mouseDown (at, {}, false));
    f.editor.mouseUp (at);
    REQUIRE (f.editor.isSelected (f.noteA));
    CHECK (rec.calls.empty());
}

TEST_CASE ("SourceRollEditor: a drag that moves in time and comes back to its start reports nothing", "[source-roll-editor][note-preview]")
{
    Fixture f;
    ChangeRecorder rec;
    f.editor.setNoteChangeListener (rec.listener());
    const auto start = f.centreOf (f.noteA);
    const auto sideways = start.translated (f.geometry.xForTick (ticksPerQuarter) - f.geometry.xForTick (0), 0);

    REQUIRE (f.editor.mouseDown (start, {}, false));
    f.editor.mouseDrag (sideways);
    f.editor.mouseDrag (start);
    f.editor.mouseUp (start);
    REQUIRE ((int) f.noteA.getProperty (SongIDs::startTick) == 0);
    CHECK (rec.calls.empty());
}

namespace
{
    constexpr int eighth = ticksPerQuarter / 2;   // the grid for the snap tests

    int pixelsFor (const PianoRollGeometry& g, int ticks) { return g.xForTick (ticks) - g.xForTick (0); }

    const juce::ModifierKeys altKey (juce::ModifierKeys::altModifier);
}

TEST_CASE ("SourceRollEditor: snap is off by default and has no effect with the grid off", "[source-roll-editor][grid-snap]")
{
    Fixture f;
    CHECK_FALSE (f.editor.getSnapToGrid());

    f.editor.setSnapToGrid (true);   // grid ticks still 0
    const auto start = f.centreOf (f.noteA);
    const auto end = start.translated (pixelsFor (f.geometry, 200), 0);
    f.editor.mouseDown (start, {}, false);
    f.editor.mouseDrag (end);
    f.editor.mouseUp (end);

    CHECK ((int) f.noteA.getProperty (SongIDs::startTick) == f.geometry.tickForX (end.x) - f.geometry.tickForX (start.x));
}

TEST_CASE ("SourceRollEditor: with snap on a moved note's start lands on the nearest grid line", "[source-roll-editor][grid-snap]")
{
    Fixture f;
    f.editor.setGridTicks (eighth);
    f.editor.setSnapToGrid (true);

    const auto start = f.centreOf (f.noteA);
    f.editor.mouseDown (start, {}, false);
    f.editor.mouseDrag (start.translated (pixelsFor (f.geometry, 200), 0));   // ~200 -> 240
    CHECK ((int) f.noteA.getProperty (SongIDs::startTick) == eighth);
    f.editor.mouseDrag (start.translated (pixelsFor (f.geometry, 70), 0));    // ~70 -> 0
    CHECK ((int) f.noteA.getProperty (SongIDs::startTick) == 0);
    f.editor.mouseUp (start);
}

TEST_CASE ("SourceRollEditor: a snapped group move puts the grabbed note on the grid and keeps the spacing", "[source-roll-editor][grid-snap]")
{
    Fixture f;
    f.noteA.setProperty (SongIDs::startTick, 100, nullptr);   // off the grid
    f.editor.setGridTicks (eighth);
    f.editor.setSnapToGrid (true);
    f.editor.mouseDown (f.centreOf (f.noteA), {}, false);
    f.editor.mouseDown (f.centreOf (f.noteB), juce::ModifierKeys (juce::ModifierKeys::shiftModifier), false);
    REQUIRE (f.editor.getNumSelected() == 2);

    const auto start = f.centreOf (f.noteA);
    const auto end = start.translated (pixelsFor (f.geometry, 150), 0);   // ~250 -> 240
    f.editor.mouseDown (start, {}, false);
    f.editor.mouseDrag (end);
    f.editor.mouseUp (end);

    CHECK ((int) f.noteA.getProperty (SongIDs::startTick) == eighth);
    CHECK ((int) f.noteB.getProperty (SongIDs::startTick) == ticksPerQuarter + (eighth - 100));
}

TEST_CASE ("SourceRollEditor: holding Alt moves a note freely while snap is on", "[source-roll-editor][grid-snap]")
{
    Fixture f;
    f.editor.setGridTicks (eighth);
    f.editor.setSnapToGrid (true);

    const auto start = f.centreOf (f.noteA);
    const auto end = start.translated (pixelsFor (f.geometry, 200), 0);
    f.editor.mouseDown (start, {}, false);
    f.editor.mouseDrag (end, altKey);
    const int free = f.geometry.tickForX (end.x) - f.geometry.tickForX (start.x);
    REQUIRE (free % eighth != 0);
    CHECK ((int) f.noteA.getProperty (SongIDs::startTick) == free);

    f.editor.mouseDrag (end);   // Alt released mid-drag: snaps again
    CHECK ((int) f.noteA.getProperty (SongIDs::startTick) == eighth);
    f.editor.mouseUp (end);
}

TEST_CASE ("SourceRollEditor: with snap on a right-edge resize puts the end on the nearest grid line, at least one line after the start", "[source-roll-editor][grid-snap]")
{
    Fixture f;   // noteA: 0..480
    f.editor.setGridTicks (eighth);
    f.editor.setSnapToGrid (true);
    auto bounds = f.geometry.noteBounds (Fixture::toNote (f.noteA));
    const juce::Point<int> edge (bounds.x + bounds.width - 1, bounds.y + bounds.height / 2);

    f.editor.mouseDown (edge, {}, false);
    f.editor.mouseDrag (edge.translated (pixelsFor (f.geometry, 200), 0));    // end ~680 -> 720
    CHECK ((int) f.noteA.getProperty (SongIDs::durationTicks) == 720);
    f.editor.mouseDrag (edge.translated (pixelsFor (f.geometry, 70), 0));     // end ~550 -> 480
    CHECK ((int) f.noteA.getProperty (SongIDs::durationTicks) == ticksPerQuarter);
    f.editor.mouseDrag (edge.translated (-pixelsFor (f.geometry, 470), 0));  // end ~10 -> first line after 0
    CHECK ((int) f.noteA.getProperty (SongIDs::durationTicks) == eighth);
    f.editor.mouseUp (edge);
    CHECK ((int) f.noteA.getProperty (SongIDs::startTick) == 0);
}

TEST_CASE ("SourceRollEditor: with snap on a left-edge resize puts the start on the nearest grid line, at least one line before the end", "[source-roll-editor][grid-snap]")
{
    Fixture f;   // noteB: 480..960
    f.editor.setGridTicks (eighth);
    f.editor.setSnapToGrid (true);
    auto bounds = f.geometry.noteBounds (Fixture::toNote (f.noteB));
    const juce::Point<int> edge (bounds.x + 1, bounds.y + bounds.height / 2);

    f.editor.mouseDown (edge, {}, false);
    f.editor.mouseDrag (edge.translated (pixelsFor (f.geometry, 200), 0));    // start ~680 -> 720
    CHECK ((int) f.noteB.getProperty (SongIDs::startTick) == 720);
    CHECK ((int) f.noteB.getProperty (SongIDs::durationTicks) == eighth);
    f.editor.mouseDrag (edge.translated (pixelsFor (f.geometry, 470), 0));    // start ~950 -> last line before 960
    CHECK ((int) f.noteB.getProperty (SongIDs::startTick) == 720);
    f.editor.mouseDrag (edge.translated (-pixelsFor (f.geometry, 200), 0));   // start ~280 -> 240
    CHECK ((int) f.noteB.getProperty (SongIDs::startTick) == eighth);
    CHECK ((int) f.noteB.getProperty (SongIDs::durationTicks) == 2 * ticksPerQuarter - eighth);
    f.editor.mouseUp (edge);
}

TEST_CASE ("SourceRollEditor: holding Alt resizes freely while snap is on", "[source-roll-editor][grid-snap]")
{
    Fixture f;
    f.editor.setGridTicks (eighth);
    f.editor.setSnapToGrid (true);
    auto bounds = f.geometry.noteBounds (Fixture::toNote (f.noteA));
    const juce::Point<int> edge (bounds.x + bounds.width - 1, bounds.y + bounds.height / 2);
    const auto end = edge.translated (pixelsFor (f.geometry, 200), 0);

    f.editor.mouseDown (edge, {}, false);
    f.editor.mouseDrag (end, altKey);
    f.editor.mouseUp (end);

    const int free = ticksPerQuarter + f.geometry.tickForX (end.x) - f.geometry.tickForX (edge.x);
    REQUIRE (free % eighth != 0);
    CHECK ((int) f.noteA.getProperty (SongIDs::durationTicks) == free);
}

TEST_CASE ("SourceRollEditor: with snap on a double-click creates the note at the start of the grid cell; Alt places it exactly", "[source-roll-editor][grid-snap]")
{
    Fixture f;
    f.editor.setGridTicks (eighth);
    f.editor.setSnapToGrid (true);
    const juce::Point<int> cell (f.geometry.xForTick (3 * ticksPerQuarter + 200), f.geometry.yForPitch (72));   // inside 1440..1680

    f.editor.mouseDown (cell, {}, true);
    auto snapped = SongDocument::getNotesNode (f.track).getChild (2);
    CHECK ((int) snapped.getProperty (SongIDs::startTick) == 3 * ticksPerQuarter);

    const juce::Point<int> other (f.geometry.xForTick (5 * ticksPerQuarter + 200), f.geometry.yForPitch (72));
    f.editor.mouseDown (other, altKey, true);
    auto exact = SongDocument::getNotesNode (f.track).getChild (3);
    CHECK ((int) exact.getProperty (SongIDs::startTick) == f.geometry.tickForX (other.x));
}

TEST_CASE ("SourceRollEditor: with snap on a pitch drag with a little sideways wobble leaves an off-grid note's timing and import orders alone", "[source-roll-editor][grid-snap]")
{
    Fixture f;
    f.noteA.setProperty (SongIDs::startTick, 100, nullptr);   // off the grid
    markImported (f.noteA);
    f.editor.setGridTicks (eighth);
    f.editor.setSnapToGrid (true);
    const auto start = f.centreOf (f.noteA);
    const auto end = start.translated (2, f.geometry.yForPitch (58) - f.geometry.yForPitch (60));

    f.editor.mouseDown (start, {}, false);
    f.editor.mouseDrag (end);
    f.editor.mouseUp (end);

    REQUIRE ((int) f.noteA.getProperty (SongIDs::pitch) == 58);
    CHECK ((int) f.noteA.getProperty (SongIDs::startTick) == 100);
    CHECK ((int) f.noteA.getProperty (SongIDs::onOrder) == 4);
    CHECK ((int) f.noteA.getProperty (SongIDs::offOrder) == 9);
}

TEST_CASE ("SourceRollEditor: with snap on a drag out and back puts an off-grid note back where it was and previews nothing", "[source-roll-editor][grid-snap]")
{
    Fixture f;
    f.noteA.setProperty (SongIDs::startTick, 100, nullptr);
    f.editor.setGridTicks (eighth);
    f.editor.setSnapToGrid (true);
    ChangeRecorder rec;
    f.editor.setNoteChangeListener (rec.listener());
    const auto start = f.centreOf (f.noteA);

    f.editor.mouseDown (start, {}, false);
    f.editor.mouseDrag (start.translated (pixelsFor (f.geometry, 300), 0));
    REQUIRE ((int) f.noteA.getProperty (SongIDs::startTick) != 100);
    f.editor.mouseDrag (start);
    f.editor.mouseUp (start);

    CHECK ((int) f.noteA.getProperty (SongIDs::startTick) == 100);
    CHECK (rec.calls.empty());
}

TEST_CASE ("SourceRollEditor: with snap on pressing an off-grid note's edge without moving leaves it alone", "[source-roll-editor][grid-snap]")
{
    Fixture f;
    f.noteA.setProperty (SongIDs::durationTicks, 430, nullptr);   // ends off the grid
    f.editor.setGridTicks (eighth);
    f.editor.setSnapToGrid (true);
    auto bounds = f.geometry.noteBounds (Fixture::toNote (f.noteA));
    const juce::Point<int> edge (bounds.x + bounds.width - 1, bounds.y + bounds.height / 2);

    f.editor.mouseDown (edge, {}, false);
    f.editor.mouseDrag (edge.translated (1, 0));
    f.editor.mouseUp (edge.translated (1, 0));

    CHECK ((int) f.noteA.getProperty (SongIDs::durationTicks) == 430);
}
