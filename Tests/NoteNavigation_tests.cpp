#include "UI/NoteNavigation.h"

#include <catch2/catch_test_macros.hpp>

using namespace lotro;

TEST_CASE ("NoteNavigation: the nearest note is the one whose start is closest in time, on any pitch", "[note-nav]")
{
    const std::vector<NoteStart> notes { { 0, 60 }, { 960, 40 }, { 1920, 90 } };
    CHECK (nearestNoteIndex (notes, 1000.0, -1) == 1);
    CHECK (nearestNoteIndex (notes, 1500.0, 60) == 2);   // after the cursor counts too; pitch does not
    CHECK (nearestNoteIndex (notes, 0.0, 90) == 0);
    CHECK (nearestNoteIndex (notes, 99999.0, 60) == 2);
}

TEST_CASE ("NoteNavigation: a note still sounding under the cursor loses to a nearer start", "[note-nav]")
{
    // The note at 0 lasts past the cursor, but only starts count.
    const std::vector<NoteStart> notes { { 0, 60 }, { 1100, 62 } };
    CHECK (nearestNoteIndex (notes, 1000.0, 60) == 1);
}

TEST_CASE ("NoteNavigation: equal starts go to the pitch nearest the active row, then the higher pitch", "[note-nav]")
{
    const std::vector<NoteStart> chord { { 480, 60 }, { 480, 64 }, { 480, 67 } };
    CHECK (nearestNoteIndex (chord, 480.0, 63) == 1);
    CHECK (nearestNoteIndex (chord, 480.0, 66) == 2);
    CHECK (nearestNoteIndex (chord, 480.0, 62) == 1);   // 60 and 64 both 2 away: higher wins
    CHECK (nearestNoteIndex (chord, 480.0, -1) == 2);   // no active row: highest

    // Equally far before and after the cursor: the same tie rules.
    const std::vector<NoteStart> around { { 0, 72 }, { 960, 48 } };
    CHECK (nearestNoteIndex (around, 480.0, 50) == 1);
    CHECK (nearestNoteIndex (around, 480.0, -1) == 0);
}

TEST_CASE ("NoteNavigation: no notes, no nearest note", "[note-nav]")
{
    CHECK (nearestNoteIndex ({}, 480.0, 60) == -1);
}

TEST_CASE ("NoteNavigation: excluded notes are skipped, so the next nearest is found; all excluded gives none", "[note-nav]")
{
    const std::vector<NoteStart> notes { { 0, 60 }, { 960, 40 }, { 1920, 90 } };
    CHECK (nearestNoteIndex (notes, 900.0, -1, { false, true, false }) == 0);
    CHECK (nearestNoteIndex (notes, 1000.0, -1, { true, true, false }) == 2);
    CHECK (nearestNoteIndex (notes, 1000.0, -1, { true, true, true }) == -1);
    CHECK (nearestNoteIndex (notes, 1000.0, -1, {}) == 1);   // empty: nothing excluded
}

TEST_CASE ("NoteNavigation: with nothing selected at the cursor, Next is the first note starting at or after it, lowest first", "[note-nav]")
{
    const std::vector<NoteStart> notes { { 0, 60 }, { 960, 67 }, { 960, 40 }, { 1920, 90 } };
    CHECK (steppedNoteIndex (notes, 0.0, {}, +1) == 0);      // a start at the cursor counts
    CHECK (steppedNoteIndex (notes, 1.0, {}, +1) == 2);      // the chord's lowest, whatever the row
    CHECK (steppedNoteIndex (notes, 961.0, {}, +1) == 3);
    CHECK (steppedNoteIndex (notes, 1921.0, {}, +1) == -1);  // past the last note
}

TEST_CASE ("NoteNavigation: with nothing selected at the cursor, Previous is the last note starting before it, highest first", "[note-nav]")
{
    const std::vector<NoteStart> notes { { 0, 60 }, { 960, 67 }, { 960, 40 }, { 1920, 90 } };
    CHECK (steppedNoteIndex (notes, 1920.0, {}, -1) == 1);   // the chord's highest; not the note at the cursor
    CHECK (steppedNoteIndex (notes, 99999.0, {}, -1) == 3);
    CHECK (steppedNoteIndex (notes, 960.0, {}, -1) == 0);
    CHECK (steppedNoteIndex (notes, 0.0, {}, -1) == -1);     // nothing before the first note
}

TEST_CASE ("NoteNavigation: a selected note starting at the cursor steps through its chord one pitch at a time", "[note-nav]")
{
    const std::vector<NoteStart> notes { { 0, 60 }, { 480, 67 }, { 480, 60 }, { 480, 64 }, { 960, 50 } };
    auto selectedOnly = [&] (int index)
    {
        std::vector<bool> selected (notes.size(), false);
        selected[(std::size_t) index] = true;
        return selected;
    };
    CHECK (steppedNoteIndex (notes, 480.0, selectedOnly (2), +1) == 3);   // 60 -> 64
    CHECK (steppedNoteIndex (notes, 480.0, selectedOnly (3), +1) == 1);   // 64 -> 67
    CHECK (steppedNoteIndex (notes, 480.0, selectedOnly (1), +1) == 4);   // 67 -> the next start
    CHECK (steppedNoteIndex (notes, 480.0, selectedOnly (1), -1) == 3);   // 67 -> 64
    CHECK (steppedNoteIndex (notes, 480.0, selectedOnly (2), -1) == 0);   // 60 -> the previous start
    CHECK (steppedNoteIndex (notes, 960.0, selectedOnly (4), +1) == -1);  // the last note
    CHECK (steppedNoteIndex (notes, 0.0, selectedOnly (0), -1) == -1);    // the first note
}

TEST_CASE ("NoteNavigation: a selected note away from the cursor does not anchor the step", "[note-nav]")
{
    const std::vector<NoteStart> notes { { 0, 60 }, { 480, 62 }, { 960, 64 } };
    const std::vector<bool> firstSelected { true, false, false };
    CHECK (steppedNoteIndex (notes, 700.0, firstSelected, +1) == 2);
    CHECK (steppedNoteIndex (notes, 700.0, firstSelected, -1) == 1);
}

TEST_CASE ("NoteNavigation: several selected at the cursor step from the highest for Next, the lowest for Previous", "[note-nav]")
{
    const std::vector<NoteStart> notes { { 0, 50 }, { 480, 60 }, { 480, 64 }, { 480, 67 }, { 960, 70 } };
    const std::vector<bool> lowerTwo { false, true, true, false, false };
    CHECK (steppedNoteIndex (notes, 480.0, lowerTwo, +1) == 3);
    CHECK (steppedNoteIndex (notes, 480.0, lowerTwo, -1) == 0);
}

TEST_CASE ("NoteNavigation: notes with the same start and pitch step in their stored order", "[note-nav]")
{
    const std::vector<NoteStart> notes { { 480, 60 }, { 480, 60 } };
    CHECK (steppedNoteIndex (notes, 480.0, {}, +1) == 0);
    CHECK (steppedNoteIndex (notes, 480.0, { true, false }, +1) == 1);
    CHECK (steppedNoteIndex (notes, 480.0, { false, true }, -1) == 0);
    CHECK (steppedNoteIndex (notes, 481.0, {}, -1) == 1);
}

TEST_CASE ("NoteNavigation: no notes, no step", "[note-nav]")
{
    CHECK (steppedNoteIndex ({}, 480.0, {}, +1) == -1);
    CHECK (steppedNoteIndex ({}, 480.0, {}, -1) == -1);
}

TEST_CASE ("NoteNavigation: from the one selected note, Next / Previous of the Same Pitch go to the nearest note of its pitch later / earlier", "[note-nav][same-pitch]")
{
    const std::vector<NoteStart> notes { { 0, 60 }, { 480, 62 }, { 960, 60 }, { 960, 64 }, { 1920, 60 }, { 2400, 62 } };
    auto selectedOnly = [&] (int index)
    {
        std::vector<bool> selected (notes.size(), false);
        selected[(std::size_t) index] = true;
        return selected;
    };
    CHECK (samePitchNoteIndex (notes, selectedOnly (0), +1) == 2);
    CHECK (samePitchNoteIndex (notes, selectedOnly (2), +1) == 4);
    CHECK (samePitchNoteIndex (notes, selectedOnly (4), +1) == -1);   // the last 60
    CHECK (samePitchNoteIndex (notes, selectedOnly (4), -1) == 2);
    CHECK (samePitchNoteIndex (notes, selectedOnly (0), -1) == -1);   // the first 60
    CHECK (samePitchNoteIndex (notes, selectedOnly (1), +1) == 5);    // 62 skips the 60s and 64
    CHECK (samePitchNoteIndex (notes, selectedOnly (3), +1) == -1);   // the only 64
    CHECK (samePitchNoteIndex (notes, selectedOnly (3), -1) == -1);
}

TEST_CASE ("NoteNavigation: Same Pitch steps need exactly one selected note", "[note-nav][same-pitch]")
{
    const std::vector<NoteStart> notes { { 0, 60 }, { 480, 60 }, { 960, 60 } };
    CHECK (samePitchNoteIndex (notes, {}, +1) == -1);
    CHECK (samePitchNoteIndex (notes, { false, false, false }, +1) == -1);
    CHECK (samePitchNoteIndex (notes, { true, true, false }, +1) == -1);
    CHECK (samePitchNoteIndex (notes, { false, true, true }, -1) == -1);
    CHECK (soleSelectedIndex ({ false, true, false }) == 1);
    CHECK (soleSelectedIndex ({ true, true, false }) == -1);
    CHECK (soleSelectedIndex ({}) == -1);
}

TEST_CASE ("NoteNavigation: same-pitch notes starting together step in their stored order, whatever the order in the track", "[note-nav][same-pitch]")
{
    const std::vector<NoteStart> notes { { 960, 60 }, { 480, 60 }, { 480, 60 } };
    CHECK (samePitchNoteIndex (notes, { false, true, false }, +1) == 2);
    CHECK (samePitchNoteIndex (notes, { false, false, true }, +1) == 0);
    CHECK (samePitchNoteIndex (notes, { true, false, false }, -1) == 2);
    CHECK (samePitchNoteIndex (notes, { false, false, true }, -1) == 1);
}
