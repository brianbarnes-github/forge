// ThreePaneSplitter: three sibling panes divided by two draggable bars.
// computeThreePaneGeometry is the pure layout; the component tests check
// that it positions real siblings and that a drag moves one bar only.

#include "UI/ThreePaneSplitter.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace lotro;

namespace
{
    constexpr int bar = ThreePaneSplitter::barThickness;
    const std::array<int, 3> noMins { 0, 0, 0 };

    // The three panes plus both bars tile the area exactly, left to right.
    void checkTiles (const ThreePaneGeometry& g, int width)
    {
        CHECK (g.panes[0].getX() == 0);
        CHECK (g.bars[0].getX() == g.panes[0].getRight());
        CHECK (g.panes[1].getX() == g.bars[0].getRight());
        CHECK (g.bars[1].getX() == g.panes[1].getRight());
        CHECK (g.panes[2].getX() == g.bars[1].getRight());
        CHECK (g.panes[2].getRight() == width);
    }

    struct Harness : public juce::Component
    {
        juce::Component a, b, c;
        ThreePaneSplitter splitter { { 20, 100, 20 } };

        Harness()
        {
            for (auto* p : { &a, &b, &c, (juce::Component*) &splitter })
                addAndMakeVisible (*p);
            splitter.setComponents (&a, &b, &c);
            setSize (606, 28);
            splitter.setBounds (getLocalBounds());
        }
    };
}

TEST_CASE ("ThreePaneSplitter: the default layout is three equal segments", "[ui][splitter3]")
{
    const auto g = computeThreePaneGeometry ({ 0, 0, 603, 28 }, { 1.0f / 3, 2.0f / 3 }, noMins, bar);
    checkTiles (g, 603);
    CHECK (std::abs (g.panes[0].getWidth() - g.panes[1].getWidth()) <= bar + 1);
    CHECK (std::abs (g.panes[1].getWidth() - g.panes[2].getWidth()) <= bar + 1);
    for (const auto& p : g.panes) CHECK (p.getHeight() == 28);
}

TEST_CASE ("ThreePaneSplitter: the geometry is offset by the area's origin", "[ui][splitter3]")
{
    const auto g = computeThreePaneGeometry ({ 50, 10, 600, 28 }, { 0.25f, 0.75f }, noMins, bar);
    CHECK (g.panes[0].getX() == 50);
    CHECK (g.panes[2].getRight() == 650);
    CHECK (g.panes[1].getY() == 10);
}

TEST_CASE ("ThreePaneSplitter: minimum widths clamp the dividers", "[ui][splitter3]")
{
    const std::array<int, 3> mins { 30, 200, 30 };

    const auto leftHeavy = computeThreePaneGeometry ({ 0, 0, 600, 28 }, { 0.9f, 0.95f }, mins, bar);
    checkTiles (leftHeavy, 600);
    for (size_t i = 0; i < 3; ++i) CHECK (leftHeavy.panes[i].getWidth() >= mins[i]);

    const auto rightHeavy = computeThreePaneGeometry ({ 0, 0, 600, 28 }, { 0.02f, 0.05f }, mins, bar);
    checkTiles (rightHeavy, 600);
    for (size_t i = 0; i < 3; ++i) CHECK (rightHeavy.panes[i].getWidth() >= mins[i]);
}

TEST_CASE ("ThreePaneSplitter: a strip narrower than the minimums stays well-formed", "[ui][splitter3]")
{
    const auto g = computeThreePaneGeometry ({ 0, 0, 100, 28 }, { 1.0f / 3, 2.0f / 3 }, { 30, 200, 30 }, bar);
    for (const auto& p : g.panes) CHECK (p.getWidth() >= 0);
    CHECK (g.panes[2].getRight() <= 100);
}

TEST_CASE ("ThreePaneSplitter: it places its sibling components", "[ui][splitter3]")
{
    Harness h;
    CHECK (h.a.getX() == 0);
    CHECK (h.a.getRight() < h.b.getX());
    CHECK (h.b.getRight() < h.c.getX());
    CHECK (h.c.getRight() == 606);
}

TEST_CASE ("ThreePaneSplitter: dragging a bar moves only that bar", "[ui][splitter3]")
{
    Harness h;
    const auto rightPaneBefore = h.c.getBounds();

    h.splitter.dragDivider (0, 120);
    CHECK (h.a.getRight() == 120);
    CHECK (h.c.getBounds() == rightPaneBefore);

    const auto leftPaneBefore = h.a.getBounds();
    h.splitter.dragDivider (1, 450);
    CHECK (h.b.getRight() == 450);
    CHECK (h.a.getBounds() == leftPaneBefore);
}

TEST_CASE ("ThreePaneSplitter: a drag cannot push a bar through its neighbour or squeeze a pane below its minimum", "[ui][splitter3]")
{
    Harness h;
    h.splitter.dragDivider (0, 10000);
    CHECK (h.b.getWidth() >= 100);
    CHECK (h.c.getWidth() > 0);
    h.splitter.dragDivider (1, -5);
    CHECK (h.b.getWidth() >= 100);
    CHECK (h.a.getWidth() >= 20);
}

TEST_CASE ("ThreePaneSplitter: the dragged position survives a resize proportionally", "[ui][splitter3]")
{
    Harness h;
    h.splitter.dragDivider (0, 150);
    const float before = h.splitter.getFractions()[0];
    h.setSize (1212, 28);
    h.splitter.setBounds (h.getLocalBounds());
    CHECK (h.splitter.getFractions()[0] == Catch::Approx (before));
    CHECK (std::abs (h.a.getRight() - 300) <= 2);
}
