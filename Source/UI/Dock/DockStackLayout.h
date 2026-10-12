#pragma once

#include <vector>

namespace lotro::dock
{

struct StackItem { double weight; int minSize; };   // all items passed in are visible

// Sizes (px) for each item. Dividers of `dividerSize` sit between neighbours.
// sum(result) + dividerSize * (n-1) == total when it fits; when the minimums
// do not fit every item gets its minimum (the caller clips). Never negative.
std::vector<int> computeStack (const std::vector<StackItem>&, int total, int dividerSize);

// New weights after dragging divider `index` (between items index and index+1)
// by `deltaPx`; the two neighbours trade pixels, neither goes below minSize;
// other items keep their size. The sum of all weights is unchanged.
std::vector<double> dragDivider (const std::vector<StackItem>&, int total, int dividerSize,
                                 int index, int deltaPx);

} // namespace lotro::dock
