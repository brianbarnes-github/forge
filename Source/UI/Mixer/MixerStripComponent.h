#pragma once

#include "UI/Mixer/MixerTarget.h"
#include "UI/MuteSoloIconButton.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

namespace lotro
{

// One track's channel strip in the mixer panel: pan knob, a vertical volume
// fader filling the height beneath it, the mute/solo row and the number + name
// label at the bottom. Every user change goes straight to the MixerTarget;
// refresh() mirrors the target's state back without notifications.
class MixerStripComponent : public juce::Component
{
public:
    static constexpr int width = 64;

    MixerStripComponent (MixerTarget&, juce::int64 channelId);

    // Sets every control from the channel WITHOUT notification (no echo back to the target).
    void refresh (const MixerChannel&);

    juce::int64 getChannelId() const noexcept { return id; }
    const juce::String& getChannelName() const noexcept { return name; }

    // The fader setup shared with the panel's master strip.
    static void configureFader (juce::Slider&);

    void resized() override;
    void paint (juce::Graphics&) override;

    // test access
    juce::Slider& faderForTesting() { return fader; }
    juce::Slider& panForTesting() { return panKnob; }
    juce::Button& muteForTesting() { return muteButton; }
    juce::Button& soloForTesting() { return soloButton; }
    bool isDimmedForTesting() const { return fader.getAlpha() < 1.0f; }

private:
    // A rotary pan knob whose double-click returns the pan to following the file.
    class PanKnob : public juce::Slider
    {
    public:
        std::function<void()> onClear;
        void mouseDoubleClick (const juce::MouseEvent&) override { if (onClear) onClear(); }
    };

    MixerTarget& target;
    const juce::int64 id;

    juce::String name;

    PanKnob panKnob;
    MuteSoloIconButton muteButton { MuteSoloIconButton::Kind::Mute, juce::Colours::orangered };
    MuteSoloIconButton soloButton { MuteSoloIconButton::Kind::Solo, juce::Colours::gold };
    juce::Slider fader;
    juce::Label nameLabel;

    bool faderDragging = false;        // between the fader's onDragStart and onDragEnd
    bool faderDragHasChanged = false;  // the current fader drag has already reported a change
    bool panDragging = false;
    bool panDragHasChanged = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MixerStripComponent)
};

} // namespace lotro
