#pragma once

#include "Core/Config.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

namespace lotro
{

class GlobalSettingsView : public juce::Component,
                           private juce::TextEditor::Listener
{
public:
    GlobalSettingsView (Config& configRef, std::function<void()> onChange);

    void refresh();

    void resized() override;

private:
    void textEditorTextChanged (juce::TextEditor&) override;

    Config&                       config;
    std::function<void()>         notifyChange;

    juce::Label       inputLabel       { {}, "Input MIDI:" };
    juce::Label       inputValue;
    juce::Label       outputLabel      { {}, "Output ABC:" };
    juce::Label       outputValue;
    juce::Label       titleLabel       { {}, "Title:" };
    juce::TextEditor  titleField;
    juce::Label       transcriberLabel { {}, "Transcriber:" };
    juce::TextEditor  transcriberField;
    juce::Label       tempoLabel       { {}, "Tempo (BPM):" };
    juce::TextEditor  tempoField;
    juce::Label       transposeLabel   { {}, "Global transpose:" };
    juce::TextEditor  transposeField;
};

} // namespace lotro
