#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <string>

namespace lotro
{

class AbcPreviewView : public juce::Component
{
public:
    AbcPreviewView();

    void setAbc (const std::string& abc);

    void resized() override;

private:
    void updateStatusLine (const std::string& abc);

    juce::TextEditor editor;
    juce::Label      statusLine;
};

} // namespace lotro
