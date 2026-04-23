#pragma once

#include "Core/Config.h"
#include "Core/Song.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

namespace lotro
{

class InstrumentDetailForm : public juce::Component,
                             private juce::TextEditor::Listener,
                             private juce::ComboBox::Listener,
                             private juce::Button::Listener
{
public:
    InstrumentDetailForm (Config& configRef,
                          const Song& rawRef,
                          std::function<void()> onMutated);

    // Sets which instrument index is being edited; -1 means "no selection".
    void editInstrument (int instrumentIndex);

    // Called when the underlying Config or Song changes externally
    // (e.g. on Open MIDI). Re-syncs the controls to current state.
    void refresh();

    void resized() override;
    void paint (juce::Graphics& g) override;

private:
    void textEditorTextChanged (juce::TextEditor&) override;
    void comboBoxChanged       (juce::ComboBox*) override;
    void buttonClicked         (juce::Button*) override;

    void rebuildSourceCheckboxes();
    void pushFromControlsToConfig();

    Config&                                  config;
    const Song&                              raw;
    std::function<void()>                    notifyMutation;
    int                                      currentIndex = -1;

    juce::Label    nameLabel    { {}, "name:" };
    juce::ComboBox nameCombo;
    juce::Label    labelLabel   { {}, "label:" };
    juce::TextEditor labelField;
    juce::Label    transposeLabel { {}, "transpose semitones:" };
    juce::TextEditor transposeField;
    juce::Label    volumeLabel  { {}, "volume %:" };
    juce::TextEditor volumeField;
    juce::Label    drumMapLabel { {}, "drum-map (Drums only):" };
    juce::TextEditor drumMapField;
    juce::Label    sourcesLabel { {}, "sources:" };
    juce::OwnedArray<juce::ToggleButton> sourceChecks;
};

} // namespace lotro
