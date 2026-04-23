#include "EditorPane.h"
#include "GlobalSettingsView.h"
#include "InstrumentsTable.h"
#include "InstrumentDetailForm.h"

namespace lotro
{

EditorPane::EditorPane()
{
    globalView = std::make_unique<GlobalSettingsView> (
        config, [this] { if (onConfigChanged) onConfigChanged(); });

    detailForm = std::make_unique<InstrumentDetailForm> (
        config, raw, [this] { if (onConfigChanged) onConfigChanged();
                              if (instrumentsTable) instrumentsTable->refresh(); });

    instrumentsTable = std::make_unique<InstrumentsTable> (
        config,
        /*onSelectionChanged*/ [this] (int row) { if (detailForm) detailForm->editInstrument (row); },
        /*onConfigMutated*/    [this]            { if (onConfigChanged) onConfigChanged();
                                                   if (detailForm) detailForm->refresh(); });

    addAndMakeVisible (*globalView);
    addAndMakeVisible (*instrumentsTable);
    addAndMakeVisible (*detailForm);
    addAndMakeVisible (runButton);
    runButton.onClick = [this] { if (onRunRequested) onRunRequested(); };
    runButton.setEnabled (false);
}

EditorPane::~EditorPane() = default;

void EditorPane::loadFromMidi (Song newRaw, Config newCfg)
{
    raw    = std::move (newRaw);
    config = std::move (newCfg);
    globalView->refresh();
    instrumentsTable->refresh();
    detailForm->editInstrument (-1);
    repaint();
    runButton.setEnabled (! raw.tracks.empty());
    if (onConfigChanged) onConfigChanged();
}

void EditorPane::resized()
{
    auto area = getLocalBounds().reduced (8);
    globalView->setBounds (area.removeFromTop (220));
    area.removeFromTop (8);
    instrumentsTable->setBounds (area.removeFromTop (200));
    area.removeFromTop (8);
    runButton.setBounds (area.removeFromBottom (32));
    area.removeFromBottom (8);
    detailForm->setBounds (area);
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
