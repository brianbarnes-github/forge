#include "AbcPreviewView.h"

namespace lotro
{

AbcPreviewView::AbcPreviewView()
{
    editor.setMultiLine (true);
    editor.setReadOnly (true);
    editor.setReturnKeyStartsNewLine (true);
    editor.setScrollbarsShown (true);
    editor.setCaretVisible (false);
    editor.setFont (juce::Font (juce::Font::getDefaultMonospacedFontName(), 13.0f, juce::Font::plain));
    addAndMakeVisible (editor);

    addAndMakeVisible (statusLine);
    statusLine.setColour (juce::Label::textColourId, juce::Colours::darkgrey);
    statusLine.setFont (12.0f);
    statusLine.setText ("(no output)", juce::dontSendNotification);
}

void AbcPreviewView::setAbc (const std::string& abc)
{
    editor.setText (juce::String (abc), false);
    updateStatusLine (abc);
}

void AbcPreviewView::updateStatusLine (const std::string& abc)
{
    auto count = [&] (const std::string& needle)
    {
        int n = 0;
        size_t p = 0;
        while ((p = abc.find (needle, p)) != std::string::npos) { ++n; p += needle.size(); }
        return n;
    };
    const int parts = count ("X:");
    const int bars  = count ("% bar");
    juce::String s = juce::String ((int) abc.size()) + " bytes  ·  "
                   + juce::String (bars)  + " bars  ·  "
                   + juce::String (parts) + " parts";
    statusLine.setText (s, juce::dontSendNotification);
}

void AbcPreviewView::resized()
{
    auto area = getLocalBounds();
    statusLine.setBounds (area.removeFromBottom (20));
    editor.setBounds (area);
}

} // namespace lotro
