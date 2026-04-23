#include "GlobalSettingsView.h"

namespace lotro
{

GlobalSettingsView::GlobalSettingsView (Config& cfgRef, std::function<void()> onChange)
    : config (cfgRef), notifyChange (std::move (onChange))
{
    auto setUpLabel = [this] (juce::Label& l)
    {
        addAndMakeVisible (l);
        l.setColour (juce::Label::textColourId, juce::Colours::darkgrey);
    };

    auto setUpField = [this] (juce::TextEditor& f, bool numeric)
    {
        addAndMakeVisible (f);
        f.setMultiLine (false);
        f.setReturnKeyStartsNewLine (false);
        f.setBorder (juce::BorderSize<int> (1));
        if (numeric)
            f.setInputRestrictions (16, "-0123456789.");
        f.addListener (this);
    };

    setUpLabel (inputLabel);
    setUpLabel (inputValue);
    setUpLabel (outputLabel);
    setUpLabel (outputValue);
    setUpLabel (titleLabel);
    setUpField (titleField, false);
    setUpLabel (transcriberLabel);
    setUpField (transcriberField, false);
    setUpLabel (tempoLabel);
    setUpField (tempoField, true);
    setUpLabel (transposeLabel);
    setUpField (transposeField, true);

    refresh();
}

void GlobalSettingsView::refresh()
{
    inputValue.setText  (juce::String (config.input),                       juce::dontSendNotification);
    outputValue.setText (juce::String (config.output.value_or (std::string{})), juce::dontSendNotification);
    titleField.setText  (juce::String (config.title.value_or (std::string{})),  juce::dontSendNotification);
    transcriberField.setText (juce::String (config.transcriber.value_or (std::string{})),
                              juce::dontSendNotification);
    tempoField.setText  (config.tempo.has_value() ? juce::String (*config.tempo) : juce::String(),
                          juce::dontSendNotification);
    transposeField.setText (juce::String (config.transpose), juce::dontSendNotification);
}

void GlobalSettingsView::resized()
{
    auto area = getLocalBounds().reduced (4);
    const int rowH = 26;
    const int labelW = 140;

    auto layoutRow = [&] (juce::Label& label, juce::Component& field)
    {
        auto row = area.removeFromTop (rowH);
        label.setBounds (row.removeFromLeft (labelW));
        field.setBounds (row);
        area.removeFromTop (4);
    };

    layoutRow (inputLabel,       inputValue);
    layoutRow (outputLabel,      outputValue);
    layoutRow (titleLabel,       titleField);
    layoutRow (transcriberLabel, transcriberField);
    layoutRow (tempoLabel,       tempoField);
    layoutRow (transposeLabel,   transposeField);
}

void GlobalSettingsView::textEditorTextChanged (juce::TextEditor& ed)
{
    if (&ed == &titleField)
    {
        const auto s = titleField.getText().toStdString();
        config.title = s.empty() ? std::optional<std::string>{} : std::optional<std::string>{ s };
    }
    else if (&ed == &transcriberField)
    {
        const auto s = transcriberField.getText().toStdString();
        config.transcriber = s.empty() ? std::optional<std::string>{} : std::optional<std::string>{ s };
    }
    else if (&ed == &tempoField)
    {
        const auto s = tempoField.getText();
        if (s.isEmpty()) config.tempo.reset();
        else             config.tempo = s.getDoubleValue();
    }
    else if (&ed == &transposeField)
    {
        config.transpose = transposeField.getText().getIntValue();
    }

    if (notifyChange) notifyChange();
}

} // namespace lotro
