#pragma once

#include "Core/Diagnostics.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace lotro
{

class DiagnosticListView : public juce::Component,
                           private juce::TableListBoxModel
{
public:
    DiagnosticListView();

    void setDiagnostics (Diagnostics newDiags);

    void resized() override;
    void paint (juce::Graphics& g) override;

private:
    int  getNumRows() override;
    void paintRowBackground (juce::Graphics&, int, int, int, bool) override;
    void paintCell (juce::Graphics&, int rowNumber, int columnId,
                    int width, int height, bool) override;

    Diagnostics        diagnostics;
    juce::TableListBox table;
};

} // namespace lotro
