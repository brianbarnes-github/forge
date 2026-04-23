#include "EditorPane.h"
#include "GlobalSettingsView.h"
#include "InstrumentsTable.h"

namespace lotro
{

EditorPane::EditorPane()
    : globalView (std::make_unique<GlobalSettingsView> (config,
                                                        [this] { if (onConfigChanged) onConfigChanged(); })),
      instrumentsTable (std::make_unique<InstrumentsTable> (
          config,
          /*onSelectionChanged*/ [] (int) {},
          /*onConfigMutated*/    [this] { if (onConfigChanged) onConfigChanged(); }))
{
    addAndMakeVisible (*globalView);
    addAndMakeVisible (*instrumentsTable);
}

EditorPane::~EditorPane() = default;

void EditorPane::loadFromMidi (Song newRaw, Config newCfg)
{
    raw    = std::move (newRaw);
    config = std::move (newCfg);
    globalView->refresh();
    instrumentsTable->refresh();
    repaint();
    if (onConfigChanged) onConfigChanged();
}

void EditorPane::resized()
{
    auto area = getLocalBounds().reduced (8);
    globalView->setBounds (area.removeFromTop (220));
    area.removeFromTop (8);
    instrumentsTable->setBounds (area.removeFromTop (240));
}

void EditorPane::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colours::white);
}

} // namespace lotro
