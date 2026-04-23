#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace lotro
{

class DiagnosticsPane : public juce::Component
{
public:
    DiagnosticsPane();
    ~DiagnosticsPane() override = default;

    void paint (juce::Graphics& g) override;
};

} // namespace lotro
