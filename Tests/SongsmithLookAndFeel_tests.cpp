#include "UI/Theme/SongsmithLookAndFeel.h"
#include "UI/SongsmithColours.h"

#include <catch2/catch_test_macros.hpp>

using namespace lotro;

namespace
{
    juce::Image paintComponent (juce::Component& c)
    {
        juce::Image image (juce::Image::ARGB, c.getWidth(), c.getHeight(), true, juce::SoftwareImageType());
        juce::Graphics g (image);
        c.paintEntireComponent (g, true);
        return image;
    }

    bool hasCopperPixel (const juce::Image& image)
    {
        const juce::Colour copper (SongsmithColours::accentCopper);
        for (int y = 0; y < image.getHeight(); ++y)
            for (int x = 0; x < image.getWidth(); ++x)
            {
                const auto p = image.getPixelAt (x, y);
                if (std::abs (p.getRed() - copper.getRed()) < 24 && std::abs (p.getGreen() - copper.getGreen()) < 24
                    && std::abs (p.getBlue() - copper.getBlue()) < 24)
                    return true;
            }
        return false;
    }
}

TEST_CASE ("SongsmithLookAndFeel: colour IDs come from the palette", "[theme]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SongsmithLookAndFeel lf;
    CHECK (lf.findColour (juce::ResizableWindow::backgroundColourId) == juce::Colour (SongsmithColours::background));
    CHECK (lf.findColour (juce::Label::textColourId) == juce::Colour (SongsmithColours::text));
    CHECK (std::abs (lf.findColour (juce::PopupMenu::highlightedBackgroundColourId).getHue()
                     - juce::Colour (SongsmithColours::accentCopper).getHue()) < 0.001f);
    CHECK (lf.findColour (juce::Slider::trackColourId) == juce::Colour (SongsmithColours::accentCopper));
    CHECK (lf.findColour (juce::TextEditor::focusedOutlineColourId) == juce::Colour (SongsmithColours::accentCopper));
}

TEST_CASE ("SongsmithLookAndFeel: stock widgets paint at several sizes", "[theme]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    ScopedSongsmithLookAndFeel installed;
    for (const auto size : { juce::Point<int> (8, 8), juce::Point<int> (24, 24), juce::Point<int> (160, 32) })
    {
        juce::TextButton button ("OK");
        juce::Slider linear (juce::Slider::LinearHorizontal, juce::Slider::NoTextBox);
        juce::Slider rotary (juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::NoTextBox);
        juce::ToggleButton toggle ("On");
        for (juce::Component* c : std::initializer_list<juce::Component*> { &button, &linear, &rotary, &toggle })
        {
            c->setSize (size.x, size.y);
            CHECK (paintComponent (*c).getWidth() == size.x);
        }
    }
}

TEST_CASE ("SongsmithLookAndFeel: a ticked toggle shows copper", "[theme]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    ScopedSongsmithLookAndFeel installed;
    juce::ToggleButton toggle ("On");
    toggle.setSize (120, 24);
    CHECK (! hasCopperPixel (paintComponent (toggle)));
    toggle.setToggleState (true, juce::dontSendNotification);
    CHECK (hasCopperPixel (paintComponent (toggle)));
}

TEST_CASE ("SongsmithLookAndFeel: a linear slider fills copper up to its thumb", "[theme]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    ScopedSongsmithLookAndFeel installed;
    juce::Slider slider (juce::Slider::LinearHorizontal, juce::Slider::NoTextBox);
    slider.setRange (0.0, 1.0);
    slider.setValue (0.5);
    slider.setSize (200, 24);
    const auto image = paintComponent (slider);
    CHECK (hasCopperPixel (image.getClippedImage ({ 0, 0, 90, 24 })));
    CHECK (! hasCopperPixel (image.getClippedImage ({ 120, 0, 80, 24 })));
}

namespace
{
    // Rec. 709 luma, 0..255: enough to tell readable text from its background.
    float luma (juce::Colour c) { return 0.2126f * c.getRed() + 0.7152f * c.getGreen() + 0.0722f * c.getBlue(); }
}

TEST_CASE ("SongsmithLookAndFeel: a disabled linear slider is drawn dimmed, not copper, but stays visible", "[theme]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    ScopedSongsmithLookAndFeel installed;
    juce::Slider slider (juce::Slider::LinearHorizontal, juce::Slider::NoTextBox);
    slider.setRange (-100.0, 100.0, 1.0);
    slider.setValue (60.0);
    slider.setSize (200, 24);
    const auto live = paintComponent (slider);
    REQUIRE (hasCopperPixel (live));

    slider.setEnabled (false);
    const auto dimmed = paintComponent (slider);
    CHECK (! hasCopperPixel (dimmed));
    bool anyVisible = false;
    for (int x = 0; x < dimmed.getWidth(); ++x)
        anyVisible = anyVisible || dimmed.getPixelAt (x, 12).getAlpha() > 0;
    CHECK (anyVisible);
}

TEST_CASE ("SongsmithLookAndFeel: a toggled-on button's text stands out from its background", "[theme]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    ScopedSongsmithLookAndFeel installed;
    juce::TextButton button ("MMMMMMMM");
    button.setClickingTogglesState (true);
    button.setToggleState (true, juce::dontSendNotification);
    button.setSize (160, 28);
    const auto image = paintComponent (button);

    const auto background = image.getPixelAt (4, 14);
    float brightestText = 0.0f;
    for (int y = 6; y < 22; ++y)
        for (int x = 30; x < 130; ++x)
            brightestText = std::max (brightestText, std::abs (luma (image.getPixelAt (x, y)) - luma (background)));
    CHECK (brightestText > 100.0f);
}

TEST_CASE ("SongsmithLookAndFeel: selected text in an editor stands out from the highlight", "[theme]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SongsmithLookAndFeel lf;
    const auto highlight = juce::Colour (SongsmithColours::surfaceBottom)
                               .overlaidWith (lf.findColour (juce::TextEditor::highlightColourId));
    const auto text = lf.findColour (juce::TextEditor::highlightedTextColourId);
    CHECK (std::abs (luma (text) - luma (highlight)) > 100.0f);
}
