#include "EditorPane.h"
#include "GlobalSettingsView.h"

namespace lotro
{

EditorPane::EditorPane()
    : globalView (std::make_unique<GlobalSettingsView> (config,
                                                        [this] { if (onConfigChanged) onConfigChanged(); }))
{
    addAndMakeVisible (*globalView);
}

EditorPane::~EditorPane() = default;

void EditorPane::loadFromMidi (Song newRaw, Config newCfg)
{
    raw    = std::move (newRaw);
    config = std::move (newCfg);
    globalView->refresh();
    repaint();
    if (onConfigChanged) onConfigChanged();
}

void EditorPane::resized()
{
    auto area = getLocalBounds().reduced (8);
    globalView->setBounds (area.removeFromTop (220));
    // Subsequent sub-views (instruments table, detail form, run button)
    // get added in later tasks; they fill the remainder.
}

void EditorPane::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colours::white);
}

} // namespace lotro
