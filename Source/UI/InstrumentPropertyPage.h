#pragma once

#include "Core/Config.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>

namespace lotro
{

class InstrumentPropertyPage : public juce::Component,
                               private juce::TextEditor::Listener,
                               private juce::ComboBox::Listener
{
public:
    InstrumentPropertyPage (Config& configRef, std::function<void()> onChange);

    void editInstrument (int instrumentIndex);
    void refresh();
    void resized() override;

private:
    void textEditorTextChanged (juce::TextEditor&) override;
    void comboBoxChanged       (juce::ComboBox*) override;
    void pushToConfig();

    Config&               config;
    std::function<void()> notifyChange;
    int                   currentIndex = -1;

    juce::Label      xLabel          { {}, "X: index:" };
    juce::TextEditor xField;
    juce::Label      nameLabel       { {}, "Name:" };
    juce::ComboBox   nameCombo;
    juce::Label      labelLabel      { {}, "Label:" };
    juce::TextEditor labelField;
    juce::Label      drumMapLabel    { {}, "Drum map:" };
    juce::TextEditor drumMapField;
    juce::TextButton drumMapBrowse   { "Browse..." };
    std::unique_ptr<juce::FileChooser> fileChooser;
};

} // namespace lotro
