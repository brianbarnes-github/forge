#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace lotro
{

class EditorPane : public juce::Component
{
public:
    EditorPane();
    ~EditorPane() override = default;

    void paint (juce::Graphics& g) override;
};

} // namespace lotro
