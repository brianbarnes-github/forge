#include "UI/Theme/SongsmithLookAndFeel.h"

#include "UI/SongsmithColours.h"
#include "UI/Theme/ThemePaint.h"

namespace lotro
{

namespace
{
    juce::Colour c (juce::uint32 argb) { return juce::Colour (argb); }

    juce::LookAndFeel_V4::ColourScheme songsmithScheme()
    {
        using namespace SongsmithColours;
        // windowBackground, widgetBackground, menuBackground, outline, defaultText,
        // defaultFill, highlightedText, highlightedFill, menuText
        return { c (background), c (panelHeader), c (panelHeader), c (border), c (text),
                 c (surfaceTop), c (background), c (accentCopper), c (text) };
    }
}

SongsmithLookAndFeel::SongsmithLookAndFeel() : juce::LookAndFeel_V4 (songsmithScheme())
{
    using namespace SongsmithColours;
    const auto copper = c (accentCopper);

    setColour (juce::ResizableWindow::backgroundColourId, c (background));
    setColour (juce::DocumentWindow::textColourId, c (textMuted));
    setColour (juce::Label::textColourId, c (text));
    setColour (juce::TextEditor::backgroundColourId, c (surfaceBottom));
    setColour (juce::TextEditor::textColourId, c (text));
    setColour (juce::TextEditor::outlineColourId, c (border));
    setColour (juce::TextEditor::focusedOutlineColourId, copper);
    setColour (juce::TextEditor::highlightColourId, copper.withAlpha (0.35f));
    setColour (juce::TextEditor::highlightedTextColourId, c (text));
    setColour (juce::CaretComponent::caretColourId, copper);
    setColour (juce::ComboBox::backgroundColourId, c (surfaceBottom));
    setColour (juce::ComboBox::textColourId, c (text));
    setColour (juce::ComboBox::outlineColourId, c (border));
    setColour (juce::ComboBox::arrowColourId, c (textMuted));
    setColour (juce::ComboBox::focusedOutlineColourId, copper);
    setColour (juce::PopupMenu::backgroundColourId, c (panelHeader));
    setColour (juce::PopupMenu::textColourId, c (text));
    setColour (juce::PopupMenu::headerTextColourId, c (textMuted));
    setColour (juce::PopupMenu::highlightedBackgroundColourId, copper.withAlpha (0.85f));
    setColour (juce::PopupMenu::highlightedTextColourId, c (background));
    setColour (juce::ListBox::backgroundColourId, c (background));
    setColour (juce::ListBox::textColourId, c (text));
    setColour (juce::TreeView::backgroundColourId, c (background));
    setColour (juce::TreeView::selectedItemBackgroundColourId, c (selectedRow));
    setColour (juce::ScrollBar::thumbColourId, c (textMuted));
    setColour (juce::Slider::trackColourId, copper);
    setColour (juce::Slider::backgroundColourId, c (surfaceBottom));
    setColour (juce::Slider::thumbColourId, c (text));
    setColour (juce::Slider::rotarySliderFillColourId, copper);
    setColour (juce::Slider::rotarySliderOutlineColourId, c (columnDivider));
    setColour (juce::Slider::textBoxTextColourId, c (text));
    setColour (juce::Slider::textBoxOutlineColourId, c (border));
    setColour (juce::ToggleButton::textColourId, c (text));
    setColour (juce::ToggleButton::tickColourId, c (background));
    setColour (juce::ToggleButton::tickDisabledColourId, c (textMuted));
    setColour (juce::TextButton::buttonColourId, c (surfaceTop));
    setColour (juce::TextButton::buttonOnColourId, copper);
    setColour (juce::TextButton::textColourOffId, c (text));
    // A toggled-on button is the dark gradient with a copper tint (drawButtonBackground), so light text.
    setColour (juce::TextButton::textColourOnId, c (text));
    setColour (juce::AlertWindow::backgroundColourId, c (panelHeader));
    setColour (juce::AlertWindow::textColourId, c (text));
    setColour (juce::AlertWindow::outlineColourId, c (border));
}

void SongsmithLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& button, const juce::Colour&,
                                                 bool highlighted, bool down)
{
    using namespace SongsmithColours;
    const auto r = button.getLocalBounds().toFloat().reduced (0.5f);
    const float radius = juce::jmin (4.0f, r.getHeight() / 2.0f);
    auto top = c (surfaceTop), bottom = c (surfaceBottom);
    if (highlighted) { top = top.brighter (0.08f); bottom = bottom.brighter (0.08f); }
    if (down)        { top = top.darker (0.15f);   bottom = bottom.darker (0.15f); }
    g.setGradientFill (juce::ColourGradient (top, 0.0f, r.getY(), bottom, 0.0f, r.getBottom(), false));
    g.fillRoundedRectangle (r, radius);
    if (button.getToggleState())
    {
        g.setColour (c (accentCopper).withAlpha (0.18f));
        g.fillRoundedRectangle (r, radius);
    }
    g.setColour (c (edgeHighlight));
    g.drawHorizontalLine ((int) r.getY() + 1, r.getX() + radius, r.getRight() - radius);
    g.setColour (button.getToggleState() ? c (accentCopper) : c (border));
    g.drawRoundedRectangle (r, radius, 1.0f);
}

void SongsmithLookAndFeel::drawTickBox (juce::Graphics& g, juce::Component& component, float x, float y, float w, float h,
                                        bool ticked, bool isEnabled, bool highlighted, bool)
{
    using namespace SongsmithColours;
    const juce::Rectangle<float> box (x + 0.5f, y + 0.5f, w - 1.0f, h - 1.0f);
    g.setColour (ticked ? c (accentCopper) : c (surfaceBottom).brighter (highlighted ? 0.1f : 0.0f));
    g.fillRoundedRectangle (box, 3.0f);
    g.setColour (ticked ? c (accentCopper).brighter (0.2f) : c (columnDivider));
    g.drawRoundedRectangle (box, 3.0f, 1.0f);
    if (ticked)
    {
        auto tick = getTickShape (0.75f);
        g.setColour (component.findColour (isEnabled ? juce::ToggleButton::tickColourId
                                                     : juce::ToggleButton::tickDisabledColourId));
        g.fillPath (tick, tick.getTransformToScaleToFit (box.reduced (box.getWidth() * 0.22f), false));
    }
}

void SongsmithLookAndFeel::drawLinearSlider (juce::Graphics& g, int x, int y, int width, int height, float sliderPos,
                                             float minSliderPos, float maxSliderPos, juce::Slider::SliderStyle style,
                                             juce::Slider& slider)
{
    if (! slider.isBar() && (style == juce::Slider::LinearHorizontal || style == juce::Slider::LinearVertical))
    {
        using namespace SongsmithColours;
        const bool horizontal = style == juce::Slider::LinearHorizontal;
        const auto bounds = juce::Rectangle<int> (x, y, width, height).toFloat();
        const float trackThickness = 4.0f;
        const auto track = horizontal
            ? juce::Rectangle<float> (bounds.getX(), bounds.getCentreY() - trackThickness / 2, bounds.getWidth(), trackThickness)
            : juce::Rectangle<float> (bounds.getCentreX() - trackThickness / 2, bounds.getY(), trackThickness, bounds.getHeight());
        g.setColour (c (border));
        g.fillRoundedRectangle (track, trackThickness / 2);

        const auto filled = horizontal ? track.withRight (sliderPos) : track.withTop (sliderPos);
        g.setColour (slider.findColour (juce::Slider::trackColourId));
        g.fillRoundedRectangle (filled, trackThickness / 2);

        const float thumb = juce::jmin (14.0f, (horizontal ? bounds.getHeight() : bounds.getWidth()) - 2.0f);
        const auto centre = horizontal ? juce::Point<float> (sliderPos, bounds.getCentreY())
                                       : juce::Point<float> (bounds.getCentreX(), sliderPos);
        const auto knob = juce::Rectangle<float> (thumb, thumb).withCentre (centre);
        g.setGradientFill (juce::ColourGradient (c (text), knob.getX(), knob.getY(),
                                                 c (textMuted).darker (0.3f), knob.getRight(), knob.getBottom(), false));
        g.fillEllipse (knob);
        g.setColour (c (accentCopper));
        g.drawEllipse (knob.reduced (0.75f), 1.5f);
        juce::ignoreUnused (minSliderPos, maxSliderPos);
        return;
    }
    juce::LookAndFeel_V4::drawLinearSlider (g, x, y, width, height, sliderPos, minSliderPos, maxSliderPos, style, slider);
}

// No 10 px inset (LookAndFeel_V4's would leave almost nothing of a 24 px knob).
void SongsmithLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height, float sliderPos,
                                             float startAngle, float endAngle, juce::Slider& slider)
{
    const auto bounds = juce::Rectangle<int> (x, y, width, height).toFloat().reduced (3.0f);
    const auto radius = std::min (bounds.getWidth(), bounds.getHeight()) / 2.0f;
    const auto centre = bounds.getCentre();
    const auto angle = startAngle + sliderPos * (endAngle - startAngle);
    const auto arcRadius = radius - 1.5f;
    if (arcRadius <= 0.0f)
        return;

    juce::Path track;
    track.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f, startAngle, endAngle, true);
    g.setColour (slider.findColour (juce::Slider::rotarySliderOutlineColourId));
    g.strokePath (track, juce::PathStrokeType (3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    juce::Path fill;
    fill.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f, startAngle, angle, true);
    g.setColour (slider.findColour (juce::Slider::rotarySliderFillColourId));
    g.strokePath (fill, juce::PathStrokeType (3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    juce::Path pointer;
    pointer.addLineSegment (juce::Line<float> (0.0f, -2.0f, 0.0f, -(arcRadius - 3.0f)), 0.0f);
    g.setColour (slider.findColour (juce::Slider::thumbColourId));
    g.strokePath (pointer, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded),
                  juce::AffineTransform::rotation (angle).translated (centre));
}

void SongsmithLookAndFeel::drawScrollbar (juce::Graphics& g, juce::ScrollBar& bar, int x, int y, int width, int height,
                                          bool vertical, int thumbStart, int thumbSize, bool mouseOver, bool mouseDown)
{
    const auto thumb = vertical ? juce::Rectangle<int> (x, thumbStart, width, thumbSize)
                                : juce::Rectangle<int> (thumbStart, y, thumbSize, height);
    const auto alpha = mouseDown ? 0.8f : mouseOver ? 0.6f : 0.35f;
    g.setColour (bar.findColour (juce::ScrollBar::thumbColourId).withAlpha (alpha));
    const auto r = thumb.toFloat().reduced (vertical ? 3.0f : 1.0f, vertical ? 1.0f : 3.0f);
    g.fillRoundedRectangle (r, juce::jmin (r.getWidth(), r.getHeight()) / 2.0f);
}

void SongsmithLookAndFeel::drawPopupMenuBackground (juce::Graphics& g, int width, int height)
{
    g.fillAll (findColour (juce::PopupMenu::backgroundColourId));
    g.setColour (juce::Colour (SongsmithColours::border));
    g.drawRect (0, 0, width, height, 1);
}

void SongsmithLookAndFeel::drawMenuBarBackground (juce::Graphics& g, int width, int height, bool, juce::MenuBarComponent&)
{
    ThemePaint::paintPanelGradient (g, { 0, 0, width, height });
}

} // namespace lotro
