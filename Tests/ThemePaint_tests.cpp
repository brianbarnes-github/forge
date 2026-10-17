#include "UI/Theme/ThemePaint.h"
#include "UI/SongsmithColours.h"

#include <catch2/catch_test_macros.hpp>

using namespace lotro;

namespace
{
    juce::Image blank (int w, int h)
    {
        juce::Image image (juce::Image::ARGB, w, h, true, juce::SoftwareImageType());
        juce::Graphics (image).fillAll (juce::Colours::black);
        return image;
    }

    bool isNear (juce::Colour a, juce::Colour b, int tolerance)
    {
        return std::abs (a.getRed() - b.getRed()) <= tolerance
            && std::abs (a.getGreen() - b.getGreen()) <= tolerance
            && std::abs (a.getBlue() - b.getBlue()) <= tolerance;
    }

    const juce::Colour fill (0xFF8060A0);
}

TEST_CASE ("ThemePaint: a note body's centre is close to its fill", "[theme]")
{
    auto image = blank (60, 30);
    { juce::Graphics g (image); ThemePaint::paintNoteBody (g, { 10, 10, 40, 12 }, fill); }
    CHECK (isNear (image.getPixelAt (30, 16), fill, 16));
}

TEST_CASE ("ThemePaint: a tall note has rounded corners", "[theme]")
{
    auto image = blank (60, 30);
    { juce::Graphics g (image); ThemePaint::paintNoteBody (g, { 10, 10, 40, 12 }, fill); }
    // The corner pixel carries at most glow, so it is darker than the body.
    CHECK (image.getPixelAt (10, 10).getBrightness() < image.getPixelAt (30, 16).getBrightness());
}

TEST_CASE ("ThemePaint: a tall note glows, fading with distance", "[theme]")
{
    auto image = blank (60, 30);
    { juce::Graphics g (image); ThemePaint::paintNoteBody (g, { 10, 10, 40, 12 }, fill); }
    const auto near = image.getPixelAt (30, 9);   // 1 px above
    const auto far  = image.getPixelAt (30, 8);   // 2 px above
    const auto out  = image.getPixelAt (30, 6);   // beyond the overhang
    CHECK (near.getBrightness() > far.getBrightness());
    CHECK (far.getBrightness() > 0.0f);
    CHECK (out == juce::Colours::black);
}

TEST_CASE ("ThemePaint: a short note has no glow", "[theme]")
{
    auto image = blank (60, 30);
    { juce::Graphics g (image); ThemePaint::paintNoteBody (g, { 10, 10, 40, ThemePaint::glowMinHeight - 1 }, fill); }
    CHECK (image.getPixelAt (30, 9) == juce::Colours::black);
    CHECK (image.getPixelAt (30, 8) == juce::Colours::black);
}

TEST_CASE ("ThemePaint: a tiny note (1 px wide, 2 px tall) still draws a body", "[theme]")
{
    auto image = blank (20, 20);
    { juce::Graphics g (image); ThemePaint::paintNoteBody (g, { 5, 5, 1, 2 }, fill); }
    CHECK (image.getPixelAt (5, 5) != juce::Colours::black);
    CHECK (image.getPixelAt (5, 3) == juce::Colours::black);
}

TEST_CASE ("ThemePaint: a small note is a flat fill in its colour", "[theme]")
{
    auto image = blank (60, 30);
    { juce::Graphics g (image); ThemePaint::paintNoteBody (g, { 10, 10, 40, ThemePaint::smallNoteSize - 1 }, fill); }
    CHECK (image.getPixelAt (10, 10) == fill);   // square corner, exact colour: no rounding or gradient
    CHECK (image.getPixelAt (49, 12) == fill);
}

TEST_CASE ("ThemePaint: a selected note has a white edge", "[theme]")
{
    auto image = blank (60, 30);
    { juce::Graphics g (image); ThemePaint::paintNoteBody (g, { 10, 10, 40, 12 }, fill, { true, std::nullopt }); }
    CHECK (image.getPixelAt (30, 11).getBrightness() > 0.9f);
}

TEST_CASE ("ThemePaint: a selected tiny note keeps a visible edge", "[theme]")
{
    auto image = blank (60, 30);
    { juce::Graphics g (image); ThemePaint::paintNoteBody (g, { 10, 10, 40, 4 }, fill, { true, std::nullopt }); }
    CHECK (image.getPixelAt (30, 10).getBrightness() > 0.9f);
}

TEST_CASE ("ThemePaint: a border override colours the edge", "[theme]")
{
    auto image = blank (60, 30);
    { juce::Graphics g (image); ThemePaint::paintNoteBody (g, { 10, 10, 40, 12 }, fill, { false, juce::Colours::red }); }
    const auto edge = image.getPixelAt (30, 10);
    CHECK (edge.getRed() > 200);
    CHECK (edge.getGreen() < 80);
}

TEST_CASE ("ThemePaint: the panel gradient is lighter at the top, with a highlight line", "[theme]")
{
    auto image = blank (40, 30);
    { juce::Graphics g (image); ThemePaint::paintPanelGradient (g, { 0, 0, 40, 30 }); }
    const auto highlight = image.getPixelAt (20, 0);
    const auto top = image.getPixelAt (20, 2);
    const auto bottom = image.getPixelAt (20, 27);
    CHECK (top.getBrightness() > bottom.getBrightness());
    CHECK (highlight.getBrightness() > top.getBrightness());
    CHECK (image.getPixelAt (20, 29) == juce::Colour (SongsmithColours::border));
}

TEST_CASE ("ThemePaint: the anvil path fits the unit square and is wider than tall", "[theme]")
{
    const auto bounds = ThemePaint::anvilPath().getBounds();
    CHECK (! bounds.isEmpty());
    CHECK (bounds.getX() >= 0.0f);
    CHECK (bounds.getY() >= 0.0f);
    CHECK (bounds.getRight() <= 1.0f);
    CHECK (bounds.getBottom() <= 1.0f);
    CHECK (bounds.getWidth() > bounds.getHeight());
}

TEST_CASE ("ThemePaint: the app icon has copper pixels on a transparent ground", "[theme]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    const auto icon = ThemePaint::renderAppIcon (64);
    CHECK (icon.getWidth() == 64);
    CHECK (icon.getPixelAt (0, 0).getAlpha() == 0);
    CHECK (isNear (icon.getPixelAt (32, 30), juce::Colour (SongsmithColours::accentCopper), 40));
}

namespace
{
    int countNearWhite (const juce::Image& image)
    {
        int count = 0;
        for (int y = 0; y < image.getHeight(); ++y)
            for (int x = 0; x < image.getWidth(); ++x)
                if (isNear (image.getPixelAt (x, y), juce::Colours::white, 40))
                    ++count;
        return count;
    }
}

TEST_CASE ("ThemePaint: a diamond note fills its centre and leaves its square's corners clear", "[theme][note-display]")
{
    auto image = blank (60, 40);
    { juce::Graphics g (image); ThemePaint::paintDiamondNote (g, { 20, 10, 14, 14 }, {}, fill); }
    CHECK (isNear (image.getPixelAt (27, 17), fill, 16));
    for (auto corner : { juce::Point<int> (20, 10), juce::Point<int> (33, 10), juce::Point<int> (20, 23), juce::Point<int> (33, 23) })
    {
        CAPTURE (corner.x, corner.y);
        CHECK (image.getPixelAt (corner.x, corner.y) == juce::Colours::black);
    }
}

TEST_CASE ("ThemePaint: a diamond with a length line paints the line to its end", "[theme][note-display]")
{
    auto image = blank (80, 40);
    { juce::Graphics g (image); ThemePaint::paintDiamondNote (g, { 20, 10, 14, 14 }, { 27, 16, 40, 2 }, fill); }
    CHECK (image.getPixelAt (50, 16).getBrightness() > 0.2f);
    CHECK (image.getPixelAt (66, 17).getBrightness() > 0.2f);
    CHECK (image.getPixelAt (50, 13) == juce::Colours::black);
    CHECK (image.getPixelAt (70, 16) == juce::Colours::black);
}

TEST_CASE ("ThemePaint: a selected diamond is outlined in white", "[theme][note-display]")
{
    auto plain = blank (80, 40);
    { juce::Graphics g (plain); ThemePaint::paintDiamondNote (g, { 20, 10, 14, 14 }, { 27, 16, 40, 2 }, fill); }
    CHECK (countNearWhite (plain) == 0);

    ThemePaint::NoteStyle selected;
    selected.selected = true;
    auto image = blank (80, 40);
    { juce::Graphics g (image); ThemePaint::paintDiamondNote (g, { 20, 10, 14, 14 }, { 27, 16, 40, 2 }, fill, selected); }
    CHECK (countNearWhite (image) > 20);
    // The line is outlined too, out past the diamond.
    CHECK (isNear (image.getPixelAt (50, 15), juce::Colours::white, 40));
}

TEST_CASE ("ThemePaint: a diamond too small for the gradient still fills", "[theme][note-display]")
{
    auto image = blank (20, 20);
    { juce::Graphics g (image); ThemePaint::paintDiamondNote (g, { 5, 5, 3, 3 }, {}, fill); }
    CHECK (image.getPixelAt (6, 6).getBrightness() > 0.1f);
}
