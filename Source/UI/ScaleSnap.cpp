#include "UI/ScaleSnap.h"

#include "UI/KeyDetector.h"

#include <algorithm>
#include <string>

namespace lotro
{

namespace
{
    struct ScaleInfo
    {
        const char*      name;
        std::vector<int> intervals;
    };

    const std::array<ScaleInfo, snapScaleCount>& scaleTable()
    {
        static const std::array<ScaleInfo, snapScaleCount> table { {
            { "Major",                 { 0, 2, 4, 5, 7, 9, 11 } },
            { "Natural minor",         { 0, 2, 3, 5, 7, 8, 10 } },
            { "Melodic minor",         { 0, 2, 3, 5, 7, 9, 11 } },   // ascending form
            { "Harmonic minor",        { 0, 2, 3, 5, 7, 8, 11 } },
            { "Major pentatonic",      { 0, 2, 4, 7, 9 } },
            { "Minor pentatonic",      { 0, 3, 5, 7, 10 } },
            { "Major blues",           { 0, 2, 3, 4, 7, 9 } },
            { "Minor blues",           { 0, 3, 5, 6, 7, 10 } },
            { "Major triad",           { 0, 4, 7 } },
            { "Minor triad",           { 0, 3, 7 } },
            { "Major seventh",         { 0, 4, 7, 11 } },
            { "Minor seventh",         { 0, 3, 7, 10 } },
            { "Dominant seventh",      { 0, 4, 7, 10 } },
            { "Diminished triad",      { 0, 3, 6 } },
            { "Diminished seventh",    { 0, 3, 6, 9 } },
            { "Augmented fifth triad", { 0, 4, 8 } },
        } };
        return table;
    }
}

const std::array<SnapRoot, snapRootCount>& snapRoots()
{
    static const std::array<SnapRoot, snapRootCount> roots { {
        { "C♭", 11 }, { "B", 11 }, { "B♭", 10 }, { "A♯", 10 }, { "A", 9 }, { "A♭", 8 },
        { "G♯", 8 }, { "G", 7 }, { "G♭", 6 }, { "F♯", 6 }, { "F", 5 }, { "E", 4 },
        { "E♭", 3 }, { "D♯", 3 }, { "D", 2 }, { "D♭", 1 }, { "C♯", 1 }, { "C", 0 },
    } };
    return roots;
}

const char* snapScaleName (SnapScale scale)
{
    return scaleTable()[(std::size_t) scale].name;
}

const std::vector<int>& snapScaleIntervals (SnapScale scale)
{
    return scaleTable()[(std::size_t) scale].intervals;
}

bool isPitchAllowed (const SnapKey& key, int pitch)
{
    const int root = snapRoots()[(std::size_t) std::clamp (key.rootIndex, 0, snapRootCount - 1)].pitchClass;
    const int interval = ((pitch - root) % 12 + 12) % 12;
    const auto& intervals = snapScaleIntervals (key.scale);
    return std::find (intervals.begin(), intervals.end(), interval) != intervals.end();
}

SnapKey snapKeyForSongKey (int songKeyIndex)
{
    const auto key = keyAt (songKeyIndex);
    const auto name = key.name();
    const auto tonic = name.substr (0, name.find (' '));
    const auto& roots = snapRoots();
    const auto it = std::find_if (roots.begin(), roots.end(), [&] (const SnapRoot& r) { return tonic == r.name; });
    return { it != roots.end() ? (int) (it - roots.begin()) : snapRootCount - 1,
             key.mode == KeyMode::major ? SnapScale::major : SnapScale::naturalMinor };
}

} // namespace lotro
