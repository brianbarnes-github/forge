#include "UI/Mixer/MixerPanelComponent.h"
#include "UI/SongsmithColours.h"

namespace lotro
{

namespace
{
    constexpr int gutter = 1;                // divider between strips
    constexpr int stripPitch = MixerStripComponent::width + gutter;
    constexpr int masterPadding = 4;
    constexpr int masterLabelHeight = 30;
    constexpr int targetLabelHeight = 12;
}

MixerPanelComponent::MixerPanelComponent (ActivePlaybackTarget& active)
    : target (active.get())
{
    MixerStripComponent::configureFader (masterFader);
    masterFader.setTooltip ("Master volume");
    masterFader.onValueChange = [this] { target.setMasterPercent ((int) masterFader.getValue()); };
    addAndMakeVisible (masterFader);

    masterLabel.setText ("MASTER", juce::dontSendNotification);
    masterLabel.setJustificationType (juce::Justification::centred);
    masterLabel.setFont (juce::Font (juce::FontOptions (11.0f, juce::Font::bold)));
    masterLabel.setColour (juce::Label::textColourId, juce::Colour (SongsmithColours::text));
    addAndMakeVisible (masterLabel);

    targetLabel.setText ("MIDI", juce::dontSendNotification);
    targetLabel.setJustificationType (juce::Justification::centred);
    targetLabel.setFont (juce::Font (juce::FontOptions (9.0f)));
    targetLabel.setColour (juce::Label::textColourId, juce::Colour (SongsmithColours::textMuted));
    targetLabel.setInterceptsMouseClicks (false, false);
    addAndMakeVisible (targetLabel);

    viewport.setViewedComponent (&holder, false);
    viewport.setScrollBarsShown (false, true);
    addAndMakeVisible (viewport);

    sync();
    target.addListener (this);
}

MixerPanelComponent::~MixerPanelComponent()
{
    target.removeListener (this);
}

MixerStripComponent* MixerPanelComponent::trackStripForTesting (int index)
{
    return juce::isPositiveAndBelow (index, (int) strips.size()) ? strips[(size_t) index].get() : nullptr;
}

void MixerPanelComponent::mixerChanged()
{
    sync();
}

void MixerPanelComponent::sync()
{
    masterFader.setValue (target.masterPercent(), juce::dontSendNotification);

    const auto channels = target.channels();
    bool sameIds = channels.size() == strips.size();
    for (size_t i = 0; sameIds && i < channels.size(); ++i)
        sameIds = strips[i]->getChannelId() == channels[i].id;

    if (! sameIds)
    {
        // Destroy and recreate: never keep a strip whose channel may be gone.
        for (auto& s : strips)
            holder.removeChildComponent (s.get());
        strips.clear();
        for (const auto& c : channels)
        {
            auto strip = std::make_unique<MixerStripComponent> (target, c.id);
            holder.addAndMakeVisible (*strip);
            strips.push_back (std::move (strip));
        }
        layoutStrips();
    }

    for (size_t i = 0; i < channels.size(); ++i)
        strips[i]->refresh (channels[i]);
}

void MixerPanelComponent::resized()
{
    auto area = getLocalBounds();
    auto master = area.removeFromLeft (masterWidth).reduced (masterPadding);
    masterLabel.setBounds (master.removeFromBottom (masterLabelHeight));
    targetLabel.setBounds (master.removeFromTop (targetLabelHeight));
    masterFader.setBounds (master);
    area.removeFromLeft (gutter);
    viewport.setBounds (area);
    layoutStrips();
}

void MixerPanelComponent::layoutStrips()
{
    const int contentWidth = (int) strips.size() * stripPitch;
    const bool overflows = contentWidth > viewport.getWidth();
    const int height = juce::jmax (0, viewport.getHeight() - (overflows ? viewport.getScrollBarThickness() : 0));
    holder.setSize (contentWidth, height);
    for (size_t i = 0; i < strips.size(); ++i)
        strips[i]->setBounds ((int) i * stripPitch, 0, MixerStripComponent::width, height);
}

void MixerPanelComponent::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (SongsmithColours::panelHeader));
    g.setColour (juce::Colour (SongsmithColours::trackDivider));
    g.fillRect (masterWidth, 0, gutter, getHeight());
}

void MixerPanelComponent::StripHolder::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (SongsmithColours::border));
    g.setColour (juce::Colour (SongsmithColours::trackDivider));
    for (int x = MixerStripComponent::width; x < getWidth(); x += stripPitch)
        g.fillRect (x, 0, gutter, getHeight());
}

} // namespace lotro
