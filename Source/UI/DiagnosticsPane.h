#pragma once

#include "DiagnosticListView.h"
#include "AbcPreviewView.h"

#include "Core/Diagnostics.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>
#include <string>

namespace lotro
{

class DiagnosticsPane : public juce::Component
{
public:
    DiagnosticsPane();
    ~DiagnosticsPane() override;

    void show (Diagnostics diagnostics, std::string abc);

    void resized() override;

private:
    class Body;
    std::unique_ptr<Body> body;
};

} // namespace lotro
