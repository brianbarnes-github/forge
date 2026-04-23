#include "DiagnosticListView.h"

namespace lotro
{

DiagnosticListView::DiagnosticListView()
{
    table.setModel (this);
    table.getHeader().addColumn ("Severity", 1, 80);
    table.getHeader().addColumn ("Source",   2, 130);
    table.getHeader().addColumn ("Tick",     3, 70);
    table.getHeader().addColumn ("Pitch",    4, 60);
    table.getHeader().addColumn ("Track",    5, 60);
    table.getHeader().addColumn ("Message",  6, 400);
    addAndMakeVisible (table);
}

void DiagnosticListView::setDiagnostics (Diagnostics newDiags)
{
    diagnostics = std::move (newDiags);
    table.updateContent();
    table.repaint();
}

int DiagnosticListView::getNumRows() { return (int) diagnostics.size(); }

void DiagnosticListView::paintRowBackground (juce::Graphics& g, int, int, int, bool selected)
{
    g.fillAll (selected ? juce::Colours::lightblue : juce::Colours::white);
}

void DiagnosticListView::paintCell (juce::Graphics& g, int rowNumber, int columnId,
                                    int width, int height, bool)
{
    if (rowNumber < 0 || rowNumber >= (int) diagnostics.size()) return;
    const auto& d = diagnostics[(size_t) rowNumber];
    g.setColour (juce::Colours::black);
    g.setFont (13.0f);

    auto severityToString = [] (Severity s) -> juce::String
    {
        switch (s) { case Severity::Info: return "Info";
                     case Severity::Warning: return "Warning";
                     case Severity::Error: return "Error"; }
        return "?";
    };

    auto severityColour = [] (Severity s) -> juce::Colour
    {
        switch (s) { case Severity::Info: return juce::Colours::cornflowerblue;
                     case Severity::Warning: return juce::Colours::orange;
                     case Severity::Error: return juce::Colours::red; }
        return juce::Colours::grey;
    };

    juce::String text;
    switch (columnId)
    {
        case 1:
            g.setColour (severityColour (d.severity));
            g.fillEllipse ((float) (4), (float) (height / 2 - 4), 8.0f, 8.0f);
            g.setColour (juce::Colours::black);
            text = severityToString (d.severity);
            g.drawText (text, 18, 0, width - 22, height, juce::Justification::centredLeft);
            return;
        case 2: text = juce::String (d.source); break;
        case 3: text = d.tick  >= 0 ? juce::String (d.tick)  : juce::String ("--"); break;
        case 4: text = d.pitch >= 0 ? juce::String (d.pitch) : juce::String ("--"); break;
        case 5: text = d.trackIndex >= 0 ? juce::String (d.trackIndex) : juce::String ("--"); break;
        case 6: text = juce::String (d.message); break;
    }
    g.drawText (text, 4, 0, width - 8, height, juce::Justification::centredLeft);
}

void DiagnosticListView::resized() { table.setBounds (getLocalBounds()); }

void DiagnosticListView::paint (juce::Graphics& g)
{
    if (diagnostics.empty())
    {
        g.fillAll (juce::Colours::white);
        g.setColour (juce::Colours::darkgrey);
        g.setFont (14.0f);
        g.drawText ("No diagnostics from the last run.",
                    getLocalBounds(), juce::Justification::centred);
    }
}

} // namespace lotro
