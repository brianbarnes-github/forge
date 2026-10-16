#pragma once

#include "Playback/TransportStrip.h"
#include "Theme/ThemePaint.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

namespace lotro
{

// The MIDI editor's fixed bar along its bottom edge: the transport strip's height and
// background, no dividers, never docked or floated. It holds the editor-only controls;
// today that is the Swing strength slider (label, slider, "+67%" readout) at the left.
class EditorBottomBar : public juce::Component
{
public:
    static constexpr int height = TransportStrip::height;

    EditorBottomBar();

    // Fired when the user moves the slider (not by setSwing).
    std::function<void (int percent)> onSwingChanged;

    // Moves the slider and readout to `percent` (-100..100); reports nothing.
    void setSwing (int percent);
    // Greys the label, slider and readout (they stay visible) when off.
    void setSwingEnabled (bool enabled);

    juce::Slider& swingSliderForTesting() noexcept { return swingSlider; }
    juce::String swingReadoutText() const { return swingReadout.getText(); }

    void paint (juce::Graphics& g) override { ThemePaint::paintPanelGradient (g, getLocalBounds()); }
    void resized() override;

private:
    juce::Label swingLabel { {}, "Swing" };
    juce::Slider swingSlider { juce::Slider::LinearHorizontal, juce::Slider::NoTextBox };
    juce::Label swingReadout;
};

} // namespace lotro
