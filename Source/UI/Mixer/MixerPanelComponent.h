#pragma once

#include "UI/Mixer/ActivePlaybackTarget.h"
#include "UI/Mixer/MixerStripComponent.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>
#include <vector>

namespace lotro
{

// The mixer: a Master strip pinned at the left, then one MixerStripComponent
// per channel of the active target in a horizontally scrolling viewport.
// mixerChanged() rebuilds the strips only when the channel id list changes
// (add / remove / reorder / replaced document); otherwise it just refreshes them.
class MixerPanelComponent : public juce::Component, private MixerTarget::Listener
{
public:
    static constexpr int masterWidth = 72;
    static constexpr int minimumHeight = 150;

    explicit MixerPanelComponent (ActivePlaybackTarget&);
    ~MixerPanelComponent() override;

    void resized() override;
    void paint (juce::Graphics&) override;

    // test access
    int numTrackStripsForTesting() const { return (int) strips.size(); }
    MixerStripComponent* trackStripForTesting (int index);
    juce::Slider& masterFaderForTesting() { return masterFader; }

private:
    // Holds the track strips and draws the 1 px dividers between them.
    class StripHolder : public juce::Component
    {
    public:
        void paint (juce::Graphics&) override;
    };

    void mixerChanged() override;
    void sync();
    void layoutStrips();

    MixerTarget& target;

    juce::Slider masterFader;
    juce::Label masterLabel;

    // Declared before the viewport so the viewport (which shows it) is destroyed first.
    StripHolder holder;
    std::vector<std::unique_ptr<MixerStripComponent>> strips;
    juce::Viewport viewport;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MixerPanelComponent)
};

} // namespace lotro
