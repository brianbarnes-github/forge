#include "UI/Dock/DockStackLayout.h"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace lotro::dock
{
namespace
{
constexpr double tinyWeight = 1e-6;

double effectiveWeight (double w) { return std::isfinite (w) && w > 0.0 ? w : tinyWeight; }
} // namespace

std::vector<int> computeStack (const std::vector<StackItem>& items, int total, int dividerSize)
{
    const auto n = items.size();
    if (n == 0)
        return {};

    const int avail = std::max (0, total - dividerSize * (static_cast<int> (n) - 1));

    std::vector<int> result (n);
    int minSum = 0;
    for (size_t i = 0; i < n; ++i)
    {
        result[i] = std::max (0, items[i].minSize);
        minSum += result[i];
    }
    if (minSum >= avail)
        return result;   // everyone at their minimum; the caller clips

    std::vector<bool> fixed (n, false);
    double remaining = avail;
    for (bool changed = true; changed;)
    {
        changed = false;
        double weightSum = 0.0;
        for (size_t i = 0; i < n; ++i)
            if (! fixed[i])
                weightSum += effectiveWeight (items[i].weight);
        if (weightSum <= 0.0)
            break;
        for (size_t i = 0; i < n; ++i)
        {
            if (fixed[i])
                continue;
            const double share = remaining * effectiveWeight (items[i].weight) / weightSum;
            if (share < std::max (0, items[i].minSize))
            {
                fixed[i] = true;
                remaining -= std::max (0, items[i].minSize);
                changed = true;
                break;   // recompute shares with the new remainder
            }
        }
    }

    double weightSum = 0.0;
    int lastUnfixed = -1;
    for (size_t i = 0; i < n; ++i)
        if (! fixed[i])
        {
            weightSum += effectiveWeight (items[i].weight);
            lastUnfixed = static_cast<int> (i);
        }

    int assigned = 0;
    for (size_t i = 0; i < n; ++i)
    {
        if (fixed[i])
            result[i] = std::max (0, items[i].minSize);
        else if (static_cast<int> (i) != lastUnfixed)
            result[i] = static_cast<int> (std::lround (remaining * effectiveWeight (items[i].weight) / weightSum));
        if (static_cast<int> (i) != lastUnfixed)
            assigned += result[i];
    }
    if (lastUnfixed >= 0)
        result[static_cast<size_t> (lastUnfixed)] = std::max (0, avail - assigned);
    return result;
}

std::vector<double> dragDivider (const std::vector<StackItem>& items, int total, int dividerSize,
                                 int index, int deltaPx)
{
    std::vector<double> weights;
    weights.reserve (items.size());
    for (const auto& it : items)
        weights.push_back (it.weight);

    if (index < 0 || static_cast<size_t> (index) + 1 >= items.size())
        return weights;

    const auto sizes = computeStack (items, total, dividerSize);
    const auto a = static_cast<size_t> (index);
    const auto b = a + 1;

    const int lo = std::min (0, std::max (0, items[a].minSize) - sizes[a]);   // most we can shrink a (<= 0)
    const int hi = std::max (0, sizes[b] - std::max (0, items[b].minSize));   // most we can grow a
    const int delta = std::clamp (deltaPx, lo, hi);

    std::vector<double> newSizes (sizes.begin(), sizes.end());
    newSizes[a] += delta;
    newSizes[b] -= delta;

    const double sizeSum = std::accumulate (newSizes.begin(), newSizes.end(), 0.0);
    double oldWeightSum = 0.0;
    for (const double w : weights)
        oldWeightSum += effectiveWeight (w);
    if (sizeSum <= 0.0)
        return weights;

    for (size_t i = 0; i < weights.size(); ++i)
        weights[i] = newSizes[i] / sizeSum * oldWeightSum;
    return weights;
}

} // namespace lotro::dock
