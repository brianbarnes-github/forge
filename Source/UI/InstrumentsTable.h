#pragma once

#include "Core/Config.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

namespace lotro
{

class InstrumentsTable : public juce::Component,
                         private juce::TableListBoxModel
{
public:
    InstrumentsTable (Config& configRef,
                      std::function<void(int)> onSelectionChanged,
                      std::function<void()>    onConfigMutated);

    void refresh();
    int  getSelectedInstrumentIndex() const;

    void resized() override;

private:
    // TableListBoxModel
    int  getNumRows() override;
    void paintRowBackground (juce::Graphics&, int rowNumber, int, int, bool rowIsSelected) override;
    void paintCell (juce::Graphics&, int rowNumber, int columnId, int width, int height,
                    bool rowIsSelected) override;
    void selectedRowsChanged (int lastRowSelected) override;

    void addInstrument();
    void removeSelectedInstrument();

    Config&                  config;
    std::function<void(int)> notifySelection;
    std::function<void()>    notifyMutation;
    juce::TableListBox       table;
    juce::TextButton         addButton    { "+ Add" };
    juce::TextButton         removeButton { "- Remove" };
};

} // namespace lotro
