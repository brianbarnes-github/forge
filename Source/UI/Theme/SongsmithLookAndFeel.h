#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace lotro
{

// Songsmith's app-wide look: the palette mapped onto JUCE's colour IDs, plus
// drawing overrides for the stock widgets the app uses. Installed as the
// default LookAndFeel in UiMain; custom-painted components use ThemePaint.
class SongsmithLookAndFeel : public juce::LookAndFeel_V4
{
public:
    SongsmithLookAndFeel();

    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour& backgroundColour,
                               bool highlighted, bool down) override;
    void drawTickBox (juce::Graphics&, juce::Component&, float x, float y, float w, float h,
                      bool ticked, bool isEnabled, bool highlighted, bool down) override;
    void drawLinearSlider (juce::Graphics&, int x, int y, int width, int height, float sliderPos,
                           float minSliderPos, float maxSliderPos, juce::Slider::SliderStyle, juce::Slider&) override;
    void drawRotarySlider (juce::Graphics&, int x, int y, int width, int height, float sliderPos,
                           float startAngle, float endAngle, juce::Slider&) override;
    void drawScrollbar (juce::Graphics&, juce::ScrollBar&, int x, int y, int width, int height, bool vertical,
                        int thumbStart, int thumbSize, bool mouseOver, bool mouseDown) override;
    void drawPopupMenuBackground (juce::Graphics&, int width, int height) override;
    void drawMenuBarBackground (juce::Graphics&, int width, int height, bool isMouseOverBar,
                                juce::MenuBarComponent&) override;
};

// Installs a SongsmithLookAndFeel as the default for its lifetime (tests).
struct ScopedSongsmithLookAndFeel
{
    ScopedSongsmithLookAndFeel()  { juce::LookAndFeel::setDefaultLookAndFeel (&lookAndFeel); }
    ~ScopedSongsmithLookAndFeel() { juce::LookAndFeel::setDefaultLookAndFeel (nullptr); }
    SongsmithLookAndFeel lookAndFeel;
};

} // namespace lotro
