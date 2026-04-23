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
    std::unique_ptr<DiagnosticListView> diagList;
    std::unique_ptr<AbcPreviewView>     abcView;
};

} // namespace lotro
