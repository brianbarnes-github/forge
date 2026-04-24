#pragma once

#include "Core/Config.h"
#include "Core/Song.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

namespace lotro
{

class SourcePropertyPage : public juce::Component,
                           private juce::TextEditor::Listener
{
public:
    SourcePropertyPage (Config& configRef, const Song& rawRef,
                        std::function<void()> onChange);

    void editSource (int instrumentIndex, int sourceIndex);
    void refresh();
    void resized() override;

private:
    void textEditorTextChanged (juce::TextEditor&) override;
    void pushToConfig();

    Config&               config;
    const Song&           raw;
    std::function<void()> notifyChange;
    int                   instrumentIndex = -1;
    int                   sourceIndex     = -1;

    juce::Label      midiInfoLabel { {}, "MIDI track:" };
    juce::Label      midiInfoValue;
    juce::Label      transposeLabel { {}, "Transpose semitones:" };
    juce::TextEditor transposeField;
    juce::Label      volumeLabel  { {}, "Volume %:" };
    juce::TextEditor volumeField;
};

} // namespace lotro
