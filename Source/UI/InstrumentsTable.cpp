#include "InstrumentsTable.h"

namespace lotro
{

InstrumentsTable::InstrumentsTable (Config&                   cfgRef,
                                    std::function<void(int)>  onSelChanged,
                                    std::function<void()>     onMutated)
    : config (cfgRef),
      notifySelection (std::move (onSelChanged)),
      notifyMutation  (std::move (onMutated))
{
    table.setModel (this);
    table.getHeader().addColumn ("X",       1, 40);
    table.getHeader().addColumn ("Name",    2, 140);
    table.getHeader().addColumn ("Label",   3, 140);
    table.getHeader().addColumn ("Sources", 4, 100);
    addAndMakeVisible (table);

    addAndMakeVisible (addButton);
    addAndMakeVisible (removeButton);
    addButton.onClick    = [this] { addInstrument(); };
    removeButton.onClick = [this] { removeSelectedInstrument(); };
}

void InstrumentsTable::refresh()
{
    table.updateContent();
    table.repaint();
}

int InstrumentsTable::getSelectedInstrumentIndex() const
{
    return table.getSelectedRow();
}

int InstrumentsTable::getNumRows()
{
    return (int) config.instruments.size();
}

void InstrumentsTable::paintRowBackground (juce::Graphics& g, int, int, int, bool rowIsSelected)
{
    g.fillAll (rowIsSelected ? juce::Colours::lightblue : juce::Colours::white);
}

void InstrumentsTable::paintCell (juce::Graphics& g, int rowNumber, int columnId,
                                  int width, int height, bool)
{
    if (rowNumber < 0 || rowNumber >= (int) config.instruments.size()) return;
    const auto& inst = config.instruments[(size_t) rowNumber];
    g.setColour (juce::Colours::black);
    g.setFont (14.0f);
    juce::String text;
    switch (columnId)
    {
        case 1: text = juce::String (inst.x); break;
        case 2: text = juce::String (inst.name); break;
        case 3: text = juce::String (inst.label.value_or (std::string{})); break;
        case 4:
        {
            juce::StringArray parts;
            for (const auto& s : inst.sources) parts.add (juce::String (s.midiTrackIndex));
            text = parts.joinIntoString (", ");
            break;
        }
    }
    g.drawText (text, 4, 0, width - 8, height, juce::Justification::centredLeft);
}

void InstrumentsTable::selectedRowsChanged (int lastRowSelected)
{
    if (notifySelection) notifySelection (lastRowSelected);
}

void InstrumentsTable::addInstrument()
{
    ConfigInstrument fresh;
    int nextX = 1;
    for (const auto& inst : config.instruments)
        nextX = std::max (nextX, inst.x + 1);
    fresh.x       = nextX;
    fresh.name    = "LuteOfAges";
    fresh.sources = {};
    config.instruments.push_back (fresh);
    if (notifyMutation) notifyMutation();
    refresh();
    table.selectRow ((int) config.instruments.size() - 1);
}

void InstrumentsTable::removeSelectedInstrument()
{
    const int row = table.getSelectedRow();
    if (row < 0 || row >= (int) config.instruments.size()) return;
    config.instruments.erase (config.instruments.begin() + row);
    if (notifyMutation) notifyMutation();
    refresh();
    if (notifySelection) notifySelection (-1);
}

void InstrumentsTable::resized()
{
    auto area = getLocalBounds();
    auto buttons = area.removeFromBottom (28);
    addButton.setBounds    (buttons.removeFromLeft (80).reduced (2));
    removeButton.setBounds (buttons.removeFromLeft (100).reduced (2));
    table.setBounds (area);
}

} // namespace lotro
