#include "UI/KeyDetector.h"
#include "UI/ScaleSnap.h"

#include <catch2/catch_test_macros.hpp>

#include <string>

using namespace lotro;

namespace
{
    int rootOf (const std::string& name)
    {
        for (int i = 0; i < snapRootCount; ++i)
            if (name == snapRoots()[(std::size_t) i].name)
                return i;
        FAIL ("no root named " << name);
        return -1;
    }

    std::vector<int> allowedPitchClasses (const SnapKey& key)
    {
        std::vector<int> classes;
        for (int pc = 0; pc < 12; ++pc)
            if (isPitchAllowed (key, 60 + pc))
                classes.push_back ((60 + pc) % 12);
        return classes;
    }
}

TEST_CASE ("ScaleSnap: the 18 roots run high to low with sharp and flat spellings", "[scale-snap]")
{
    const std::vector<std::string> names { "C♭", "B", "B♭", "A♯", "A", "A♭", "G♯", "G", "G♭",
                                           "F♯", "F", "E", "E♭", "D♯", "D", "D♭", "C♯", "C" };
    const std::vector<int> classes { 11, 11, 10, 10, 9, 8, 8, 7, 6, 6, 5, 4, 3, 3, 2, 1, 1, 0 };

    REQUIRE (snapRootCount == 18);
    for (int i = 0; i < snapRootCount; ++i)
    {
        CHECK (snapRoots()[(std::size_t) i].name == names[(std::size_t) i]);
        CHECK (snapRoots()[(std::size_t) i].pitchClass == classes[(std::size_t) i]);
    }
}

TEST_CASE ("ScaleSnap: every scale and chord type has its label and intervals", "[scale-snap]")
{
    struct Expected { SnapScale scale; const char* label; std::vector<int> intervals; };
    const std::vector<Expected> table {
        { SnapScale::major,             "Major",                 { 0, 2, 4, 5, 7, 9, 11 } },
        { SnapScale::naturalMinor,      "Natural minor",         { 0, 2, 3, 5, 7, 8, 10 } },
        { SnapScale::melodicMinor,      "Melodic minor",         { 0, 2, 3, 5, 7, 9, 11 } },
        { SnapScale::harmonicMinor,     "Harmonic minor",        { 0, 2, 3, 5, 7, 8, 11 } },
        { SnapScale::majorPentatonic,   "Major pentatonic",      { 0, 2, 4, 7, 9 } },
        { SnapScale::minorPentatonic,   "Minor pentatonic",      { 0, 3, 5, 7, 10 } },
        { SnapScale::majorBlues,        "Major blues",           { 0, 2, 3, 4, 7, 9 } },
        { SnapScale::minorBlues,        "Minor blues",           { 0, 3, 5, 6, 7, 10 } },
        { SnapScale::majorTriad,        "Major triad",           { 0, 4, 7 } },
        { SnapScale::minorTriad,        "Minor triad",           { 0, 3, 7 } },
        { SnapScale::majorSeventh,      "Major seventh",         { 0, 4, 7, 11 } },
        { SnapScale::minorSeventh,      "Minor seventh",         { 0, 3, 7, 10 } },
        { SnapScale::dominantSeventh,   "Dominant seventh",      { 0, 4, 7, 10 } },
        { SnapScale::diminishedTriad,   "Diminished triad",      { 0, 3, 6 } },
        { SnapScale::diminishedSeventh, "Diminished seventh",    { 0, 3, 6, 9 } },
        { SnapScale::augmentedTriad,    "Augmented fifth triad", { 0, 4, 8 } },
    };

    REQUIRE ((int) table.size() == snapScaleCount);
    CHECK (snapFirstChord == (int) SnapScale::majorTriad);
    for (int i = 0; i < snapScaleCount; ++i)
    {
        const auto& e = table[(std::size_t) i];
        CHECK ((int) e.scale == i);
        CHECK (std::string (snapScaleName (e.scale)) == e.label);
        CHECK (snapScaleIntervals (e.scale) == e.intervals);
    }
}

TEST_CASE ("ScaleSnap: C major allows the white keys in any octave and refuses the black ones", "[scale-snap]")
{
    const SnapKey cMajor { rootOf ("C"), SnapScale::major };
    for (int pitch : { 60, 62, 64, 65, 67, 69, 71, 72, 0, 127 })
        CHECK (isPitchAllowed (cMajor, pitch));
    for (int pitch : { 61, 66, 70, 1, 121 })
        CHECK_FALSE (isPitchAllowed (cMajor, pitch));
}

TEST_CASE ("ScaleSnap: enharmonic roots allow the same pitches", "[scale-snap]")
{
    const SnapKey aSharp { rootOf ("A♯"), SnapScale::minorPentatonic };
    const SnapKey bFlat { rootOf ("B♭"), SnapScale::minorPentatonic };
    for (int pitch = 0; pitch <= 127; ++pitch)
        CHECK (isPitchAllowed (aSharp, pitch) == isPitchAllowed (bFlat, pitch));
}

TEST_CASE ("ScaleSnap: a pitch below the root's pitch class wraps to a positive interval", "[scale-snap]")
{
    const SnapKey bMajor { rootOf ("B"), SnapScale::major };
    CHECK_FALSE (isPitchAllowed (bMajor, 0));   // C
    CHECK (isPitchAllowed (bMajor, 1));         // C♯
    CHECK (isPitchAllowed (bMajor, 11));        // B
    CHECK_FALSE (isPitchAllowed (bMajor, 127)); // G
}

TEST_CASE ("ScaleSnap: a chord type allows only its chord tones", "[scale-snap]")
{
    const SnapKey g7 { rootOf ("G"), SnapScale::dominantSeventh };
    CHECK (allowedPitchClasses (g7) == std::vector<int> { 2, 5, 7, 11 });
}

TEST_CASE ("ScaleSnap: the song key gives the starting root, spelled as the key spells its tonic", "[scale-snap]")
{
    CHECK (snapKeyForSongKey (0) == SnapKey { rootOf ("C"), SnapScale::major });
    CHECK (snapKeyForSongKey (1) == SnapKey { rootOf ("D♭"), SnapScale::major });
    CHECK (snapKeyForSongKey (13) == SnapKey { rootOf ("C♯"), SnapScale::naturalMinor });
    CHECK (snapKeyForSongKey (22) == SnapKey { rootOf ("B♭"), SnapScale::naturalMinor });

    for (int i = 0; i < keyCount; ++i)
    {
        const auto name = keyAt (i).name();
        const auto tonic = name.substr (0, name.find (' '));
        const auto key = snapKeyForSongKey (i);
        CHECK (snapRoots()[(std::size_t) key.rootIndex].name == tonic);
        CHECK (key.scale == (i < 12 ? SnapScale::major : SnapScale::naturalMinor));
    }
}
