#pragma once

// MIDI editor Options ▸ Snap to Key (no JUCE): the roots and scale/chord
// types the submenu offers, and whether a pitch belongs to the chosen one.
// Only note creation is filtered (see SourceRollEditor::setPitchFilter).

#include <array>
#include <vector>

namespace lotro
{

// In Type ▸ menu order; the chords (Type ▸ Chords ▸) start at snapFirstChord.
enum class SnapScale
{
    major, naturalMinor, melodicMinor, harmonicMinor,
    majorPentatonic, minorPentatonic, majorBlues, minorBlues,
    majorTriad, minorTriad, majorSeventh, minorSeventh,
    dominantSeventh, diminishedTriad, diminishedSeventh, augmentedTriad
};
inline constexpr int snapScaleCount = 16;
inline constexpr int snapFirstChord = (int) SnapScale::majorTriad;

struct SnapRoot
{
    const char* name;      // "B♭" (UTF-8)
    int         pitchClass; // 0 = C
};

// The 18 major/minor key-signature names, high to low (Root ▸ order):
// C♭ B B♭ A♯ A A♭ G♯ G G♭ F♯ F E E♭ D♯ D D♭ C♯ C.
inline constexpr int snapRootCount = 18;
const std::array<SnapRoot, snapRootCount>& snapRoots();

const char* snapScaleName (SnapScale scale);              // the menu label
const std::vector<int>& snapScaleIntervals (SnapScale scale); // semitones above the root, ascending

struct SnapKey
{
    int       rootIndex = 0;   // into snapRoots()
    SnapScale scale     = SnapScale::major;

    bool operator== (const SnapKey& other) const noexcept { return rootIndex == other.rootIndex && scale == other.scale; }
    bool operator!= (const SnapKey& other) const noexcept { return ! (*this == other); }
};

// Whether pitch's pitch class is one of key's, in any octave.
bool isPitchAllowed (const SnapKey& key, int pitch);

// The starting choice for a song key index (keyAt): its tonic spelled as
// KeyCandidate::name() spells it, Major or Natural minor.
SnapKey snapKeyForSongKey (int songKeyIndex);

} // namespace lotro
