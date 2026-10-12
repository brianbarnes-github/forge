#include "UI/Dock/DockStackLayout.h"

#include <catch2/catch_test_macros.hpp>

using namespace lotro::dock;

TEST_CASE ("stack: weights split the space after dividers", "[dock][stack]")
{
    CHECK (computeStack ({ { 1, 10 }, { 1, 10 } }, 106, 6) == std::vector<int> { 50, 50 });
    const auto s = computeStack ({ { 0.55, 10 }, { 0.45, 10 } }, 700, 6);
    CHECK (s[0] + s[1] + 6 == 700);
    CHECK (s[0] > s[1]);
}

TEST_CASE ("stack: a single item takes everything; empty is empty", "[dock][stack]")
{
    CHECK (computeStack ({ { 1, 10 } }, 300, 6) == std::vector<int> { 300 });
    CHECK (computeStack ({}, 300, 6).empty());
}

TEST_CASE ("stack: minimums win over weights and the rest is redistributed", "[dock][stack]")
{
    const auto s = computeStack ({ { 0.9, 10 }, { 0.1, 100 } }, 206, 6);   // 0.1 share would be 20
    CHECK (s[1] == 100);
    CHECK (s[0] == 100);
}

TEST_CASE ("stack: when minimums cannot fit, everyone gets their minimum, never negative", "[dock][stack]")
{
    const auto s = computeStack ({ { 1, 120 }, { 1, 160 } }, 100, 6);
    CHECK (s == std::vector<int> { 120, 160 });
    for (const int v : computeStack ({ { 1, 0 }, { 1, 0 } }, 3, 6))
        CHECK (v >= 0);
}

TEST_CASE ("stack: non-positive weights are treated as tiny, not as errors", "[dock][stack]")
{
    const auto s = computeStack ({ { 0, 10 }, { -2, 10 } }, 106, 6);
    CHECK (s[0] + s[1] == 100);
    CHECK (s[0] >= 10);
    CHECK (s[1] >= 10);
}

TEST_CASE ("stack: dragging a divider trades pixels and respects minimums", "[dock][stack]")
{
    const std::vector<StackItem> items { { 1, 40 }, { 1, 40 } };
    auto w = dragDivider (items, 206, 6, 0, 30);       // 100/100 -> 130/70
    auto s = computeStack ({ { w[0], 40 }, { w[1], 40 } }, 206, 6);
    CHECK (s == std::vector<int> { 130, 70 });
    w = dragDivider (items, 206, 6, 0, 500);           // clamps at the neighbour's minimum
    s = computeStack ({ { w[0], 40 }, { w[1], 40 } }, 206, 6);
    CHECK (s[1] == 40);
    CHECK (dragDivider (items, 206, 6, 5, 10) == std::vector<double> { 1, 1 });   // bad index: unchanged
}

TEST_CASE ("stack: dragging leaves other items' sizes and the weight total alone", "[dock][stack]")
{
    const std::vector<StackItem> items { { 1, 40 }, { 1, 40 }, { 2, 40 } };
    const auto before = computeStack (items, 312, 6);   // 150 / 150 / ... avail 300
    auto w = dragDivider (items, 312, 6, 0, 20);
    CHECK (w[0] + w[1] + w[2] == 4.0);
    const auto after = computeStack ({ { w[0], 40 }, { w[1], 40 }, { w[2], 40 } }, 312, 6);
    CHECK (after[2] == before[2]);
    CHECK (after[0] == before[0] + 20);
}
