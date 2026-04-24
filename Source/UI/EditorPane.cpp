#include "EditorPane.h"
#include "InstrumentsTree.h"
#include "PropertyPageHost.h"

namespace lotro
{

EditorPane::EditorPane()
{
    host = std::make_unique<PropertyPageHost> (
        config, raw,
        [this] { if (onConfigChanged) onConfigChanged(); });

    tree = std::make_unique<InstrumentsTree> (
        config, raw,
        /*onSelection*/ [this] (PropertyPageHost::Kind k, int iIdx, int sIdx)
                         { host->showFor (k, iIdx, sIdx); },
        /*onMutation*/  [this] { if (onConfigChanged) onConfigChanged();
                                  host->refresh(); });

    addAndMakeVisible (*tree);
    addAndMakeVisible (*host);
    addAndMakeVisible (runButton);

    runButton.onClick = [this] { if (onRunRequested) onRunRequested(); };
    runButton.setEnabled (false);
}

EditorPane::~EditorPane() = default;

void EditorPane::loadFromMidi (Song newRaw, Config newCfg)
{
    raw    = std::move (newRaw);
    config = std::move (newCfg);
    tree->rebuild();
    host->showFor (PropertyPageHost::Kind::Song);
    host->refresh();
    runButton.setEnabled (! raw.tracks.empty());
    repaint();
    if (onConfigChanged) onConfigChanged();
}

void EditorPane::resized()
{
    auto area = getLocalBounds().reduced (8);
    runButton.setBounds (area.removeFromBottom (32));
    area.removeFromBottom (8);
    const int treeHeight = area.getHeight() * 2 / 5;   // tree gets top 40%
    tree->setBounds (area.removeFromTop (treeHeight));
    area.removeFromTop (8);
    host->setBounds (area);
}

void EditorPane::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colours::white);
    if (raw.tracks.empty())
    {
        g.setColour (juce::Colours::grey);
        g.setFont (16.0f);
        g.drawText ("Drop a MIDI file here, or use File -> Open MIDI...",
                    getLocalBounds(), juce::Justification::centred);
    }
}

} // namespace lotro
