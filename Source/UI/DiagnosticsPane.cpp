#include "DiagnosticsPane.h"

namespace lotro
{

DiagnosticsPane::DiagnosticsPane()
    : diagList (std::make_unique<DiagnosticListView>()),
      abcView  (std::make_unique<AbcPreviewView>())
{
    addAndMakeVisible (*diagList);
    addAndMakeVisible (*abcView);
}

DiagnosticsPane::~DiagnosticsPane() = default;

void DiagnosticsPane::show (Diagnostics diagnostics, std::string abc)
{
    diagList->setDiagnostics (std::move (diagnostics));
    abcView->setAbc (abc);
}

void DiagnosticsPane::resized()
{
    auto area = getLocalBounds().reduced (4);
    auto top = area.removeFromTop (area.getHeight() / 2);
    diagList->setBounds (top);
    area.removeFromTop (4);
    abcView->setBounds (area);
}

} // namespace lotro
