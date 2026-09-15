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
            track.addChild (noteA, -1, nullptr);
            track.addChild (noteB, -1, nullptr);
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

TEST_CASE ("SourceRollEditor: dragging on empty canvas rubber-bands every note it intersects", "[source-roll-editor]")
{
    Fixture f;
    const juce::Point<int> start (f.geometry.getKeyboardGutterWidth() + 2, f.geometry.yForPitch (70));
    const juce::Point<int> end (f.geometry.xForTick (ticksPerQuarter * 2), f.geometry.yForPitch (55));

    REQUIRE (f.editor.mouseDown (start, {}, false));
    f.editor.mouseDrag (end);

    CHECK (f.editor.isSelected (f.noteA));
    CHECK (f.editor.isSelected (f.noteB));

    f.editor.mouseUp (end);
    CHECK (f.editor.getRubberBandRect().isEmpty());
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

    REQUIRE (f.track.getNumChildren() == 2);
    CHECK (f.editor.mouseDown (emptyCell, {}, true));
    REQUIRE (f.track.getNumChildren() == 3);

    auto created = f.track.getChild (2);
    CHECK ((int) created.getProperty (SongIDs::pitch) == 72);
    CHECK ((int) created.getProperty (SongIDs::startTick) == f.geometry.tickForX (emptyCell.x));
    CHECK ((int) created.getProperty (SongIDs::durationTicks) == ticksPerQuarter); // grid off -> quarter note fallback

    REQUIRE (f.doc.canUndo());
    f.doc.undo();
    CHECK (f.track.getNumChildren() == 2);
}

TEST_CASE ("SourceRollEditor: create uses the current grid size for the new note's duration when grid is on", "[source-roll-editor]")
{
    Fixture f;
    f.editor.setGridTicks (ticksPerQuarter / 4);

    const juce::Point<int> emptyCell (f.geometry.xForTick (ticksPerQuarter * 3), f.geometry.yForPitch (72));
    f.editor.mouseDown (emptyCell, {}, true);

    auto created = f.track.getChild (2);
    CHECK ((int) created.getProperty (SongIDs::durationTicks) == ticksPerQuarter / 4);
}

TEST_CASE ("SourceRollEditor: double-clicking an existing note is a no-op", "[source-roll-editor]")
{
    Fixture f;
    CHECK_FALSE (f.editor.mouseDown (f.centreOf (f.noteA), {}, true));
    CHECK (f.track.getNumChildren() == 2);
}

TEST_CASE ("SourceRollEditor: deleteSelection removes every selected note in one undo transaction", "[source-roll-editor]")
{
    Fixture f;
    juce::ModifierKeys shift (juce::ModifierKeys::shiftModifier);
    f.editor.mouseDown (f.centreOf (f.noteA), {}, false);
    f.editor.mouseDown (f.centreOf (f.noteB), shift, false);
    REQUIRE (f.editor.getNumSelected() == 2);

    CHECK (f.editor.deleteSelection());
    CHECK (f.track.getNumChildren() == 0);
    CHECK (f.editor.getNumSelected() == 0);

    f.doc.undo();
    CHECK (f.track.getNumChildren() == 2); // one undo restores both
}

TEST_CASE ("SourceRollEditor: deleteSelection with nothing selected is a no-op", "[source-roll-editor]")
{
    Fixture f;
    CHECK_FALSE (f.editor.deleteSelection());
    CHECK (f.track.getNumChildren() == 2);
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
    CHECK (f.track.getNumChildren() == 1);

    f.editor.mouseDown (f.centreOf (f.noteB), {}, false);
    CHECK (f.editor.keyPressed (juce::KeyPress (juce::KeyPress::backspaceKey)));
    CHECK (f.track.getNumChildren() == 0);
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
    REQUIRE (f.track.getNumChildren() == 1);

    const auto ctrlZ = juce::KeyPress ('z', juce::ModifierKeys (juce::ModifierKeys::ctrlModifier), 0);
    CHECK (f.editor.keyPressed (ctrlZ));
    CHECK (f.track.getNumChildren() == 2);

    const auto ctrlY = juce::KeyPress ('y', juce::ModifierKeys (juce::ModifierKeys::ctrlModifier), 0);
    CHECK (f.editor.keyPressed (ctrlY));
    CHECK (f.track.getNumChildren() == 1);
}

TEST_CASE ("SourceRollEditor: Ctrl+Shift+Z also redoes", "[source-roll-editor]")
{
    Fixture f;
    f.editor.mouseDown (f.centreOf (f.noteA), {}, false);
    f.editor.keyPressed (juce::KeyPress (juce::KeyPress::deleteKey));
    f.editor.keyPressed (juce::KeyPress ('z', juce::ModifierKeys (juce::ModifierKeys::ctrlModifier), 0));
    REQUIRE (f.track.getNumChildren() == 2);

    const auto ctrlShiftZ = juce::KeyPress ('z', juce::ModifierKeys (juce::ModifierKeys::ctrlModifier
                                                                       | juce::ModifierKeys::shiftModifier), 0);
    CHECK (f.editor.keyPressed (ctrlShiftZ));
    CHECK (f.track.getNumChildren() == 1);
}
