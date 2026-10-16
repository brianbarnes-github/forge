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
