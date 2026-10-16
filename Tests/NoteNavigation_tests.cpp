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
