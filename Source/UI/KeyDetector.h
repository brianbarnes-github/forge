#pragma once

// Musical key detection (Krumhansl-Schmuckler, no JUCE): a duration-weighted
// pitch-class histogram of the pitched notes, Pearson-correlated against the
// 24 major / minor key profiles of a chosen profile set.

#include <array>
#include <string>
#include <vector>

namespace lotro
{

enum class KeyProfileSet { krumhanslKessler, temperley, albrechtShanahan };
enum class KeyMode { major, minor };

// One note as key detection sees it; drums are skipped.
struct KeyNote
{
    int  pitch         = 0;
    int  durationTicks = 0;
    bool isDrum        = false;
};

// Each profile indexed from the tonic: [0] = tonic weight, [7] = the fifth's.
struct KeyProfiles
{
    std::array<double, 12> major {};
    std::array<double, 12> minor {};
};

struct KeyCandidate
{
    int     tonic       = 0;   // pitch class, 0 = C
    KeyMode mode        = KeyMode::major;
    double  correlation = 0.0;

    // "G major", "E♭ minor" (UTF-8).
    std::string name() const;
};

struct KeyEstimate
{
    KeyCandidate              best;
    double                    confidence = 0.0;   // best minus second correlation; 0 when the histogram is flat
    std::vector<KeyCandidate> ranked;             // all 24, best first
};

// The 24 keys as one index: C..B major are 0..11, C..B minor 12..23 (the
// Song ▸ Set Key list order).
inline constexpr int keyCount = 24;
int          keyIndex (const KeyCandidate& key);
KeyCandidate keyAt (int index);   // correlation 0; index clamped to 0..23

const KeyProfiles& keyProfiles (KeyProfileSet set);

// Indexed by pitch % 12, each note weighted by its duration (1 when the duration
// is not positive), normalised to sum to 1. All zero when there are no pitched notes.
std::array<double, 12> pitchClassHistogram (const std::vector<KeyNote>& notes);

// A flat histogram (no notes, or every pitch class equal) correlates with
// nothing: every candidate gets 0, best is C major, confidence is 0.
KeyEstimate detectKey (const std::array<double, 12>& histogram, KeyProfileSet set = KeyProfileSet::krumhanslKessler);
KeyEstimate detectKey (const std::vector<KeyNote>& notes, KeyProfileSet set = KeyProfileSet::krumhanslKessler);

// The Conductor head's "key: G major"; for an estimate, empty when it has no confidence.
std::string formatKeyText (const KeyCandidate& key);
std::string formatKeyText (const KeyEstimate& estimate);

} // namespace lotro
