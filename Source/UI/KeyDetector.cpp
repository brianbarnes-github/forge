#include "UI/KeyDetector.h"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace lotro
{

namespace
{
    // Krumhansl & Kessler (1982) probe-tone ratings.
    const KeyProfiles krumhanslKesslerProfiles {
        { 6.35, 2.23, 3.48, 2.33, 4.38, 4.09, 2.52, 5.19, 2.39, 3.66, 2.29, 2.88 },
        { 6.33, 2.68, 3.52, 5.38, 2.60, 3.53, 2.54, 4.75, 3.98, 2.69, 3.34, 3.17 },
    };

    // Temperley (1999), "What's Key for Key?".
    const KeyProfiles temperleyProfiles {
        { 5.0, 2.0, 3.5, 2.0, 4.5, 4.0, 2.0, 4.5, 2.0, 3.5, 1.5, 4.0 },
        { 5.0, 2.0, 3.5, 4.5, 2.0, 4.0, 2.0, 4.5, 3.5, 2.0, 1.5, 4.0 },
    };

    // Albrecht & Shanahan (2013), corpus-derived pitch-class distributions.
    const KeyProfiles albrechtShanahanProfiles {
        { 0.238, 0.006, 0.111, 0.006, 0.137, 0.094, 0.016, 0.214, 0.009, 0.080, 0.008, 0.081 },
        { 0.220, 0.006, 0.104, 0.123, 0.019, 0.103, 0.012, 0.214, 0.062, 0.022, 0.061, 0.052 },
    };

    // Pearson correlation of the histogram against `profile` rotated to `tonic`;
    // 0 when either side has no variance.
    double correlation (const std::array<double, 12>& histogram, const std::array<double, 12>& profile, int tonic)
    {
        const double meanH = std::accumulate (histogram.begin(), histogram.end(), 0.0) / 12.0;
        const double meanP = std::accumulate (profile.begin(), profile.end(), 0.0) / 12.0;
        double covariance = 0.0, varianceH = 0.0, varianceP = 0.0;
        for (int pc = 0; pc < 12; ++pc)
        {
            const double h = histogram[(std::size_t) pc] - meanH;
            const double p = profile[(std::size_t) ((pc - tonic + 12) % 12)] - meanP;
            covariance += h * p;
            varianceH += h * h;
            varianceP += p * p;
        }
        constexpr double epsilon = 1e-12;
        if (varianceH < epsilon || varianceP < epsilon)
            return 0.0;
        return covariance / std::sqrt (varianceH * varianceP);
    }
}

std::string KeyCandidate::name() const
{
    static const char* const majorNames[12] = { "C", "D\xe2\x99\xad", "D", "E\xe2\x99\xad", "E", "F",
                                                "F\xe2\x99\xaf", "G", "A\xe2\x99\xad", "A", "B\xe2\x99\xad", "B" };
    static const char* const minorNames[12] = { "C", "C\xe2\x99\xaf", "D", "E\xe2\x99\xad", "E", "F",
                                                "F\xe2\x99\xaf", "G", "G\xe2\x99\xaf", "A", "B\xe2\x99\xad", "B" };
    const auto pc = (std::size_t) (((tonic % 12) + 12) % 12);
    return mode == KeyMode::major ? std::string (majorNames[pc]) + " major"
                                  : std::string (minorNames[pc]) + " minor";
}

const KeyProfiles& keyProfiles (KeyProfileSet set)
{
    switch (set)
    {
        case KeyProfileSet::temperley:        return temperleyProfiles;
        case KeyProfileSet::albrechtShanahan: return albrechtShanahanProfiles;
        case KeyProfileSet::krumhanslKessler: break;
    }
    return krumhanslKesslerProfiles;
}

std::array<double, 12> pitchClassHistogram (const std::vector<KeyNote>& notes)
{
    std::array<double, 12> histogram {};
    double total = 0.0;
    for (const auto& n : notes)
    {
        if (n.isDrum)
            continue;
        const double weight = n.durationTicks > 0 ? (double) n.durationTicks : 1.0;
        histogram[(std::size_t) (((n.pitch % 12) + 12) % 12)] += weight;
        total += weight;
    }
    if (total > 0.0)
        for (auto& bin : histogram)
            bin /= total;
    return histogram;
}

KeyEstimate detectKey (const std::array<double, 12>& histogram, KeyProfileSet set)
{
    const auto& profiles = keyProfiles (set);
    KeyEstimate estimate;
    estimate.ranked.reserve (24);
    for (const auto mode : { KeyMode::major, KeyMode::minor })
        for (int tonic = 0; tonic < 12; ++tonic)
            estimate.ranked.push_back ({ tonic, mode,
                                         correlation (histogram, mode == KeyMode::major ? profiles.major : profiles.minor, tonic) });

    std::stable_sort (estimate.ranked.begin(), estimate.ranked.end(),
                      [] (const KeyCandidate& a, const KeyCandidate& b) { return a.correlation > b.correlation; });
    estimate.best = estimate.ranked[0];
    estimate.confidence = estimate.ranked[0].correlation - estimate.ranked[1].correlation;
    return estimate;
}

KeyEstimate detectKey (const std::vector<KeyNote>& notes, KeyProfileSet set)
{
    return detectKey (pitchClassHistogram (notes), set);
}

std::string formatKeyText (const KeyEstimate& estimate)
{
    if (estimate.confidence <= 0.0)
        return {};
    return "key: " + estimate.best.name();
}

} // namespace lotro
