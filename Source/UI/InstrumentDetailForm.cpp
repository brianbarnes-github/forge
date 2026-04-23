#include "InstrumentDetailForm.h"
#include "Core/LotroInstrument.h"

namespace lotro
{

InstrumentDetailForm::InstrumentDetailForm (Config& cfgRef, const Song& rawRef,
                                            std::function<void()> onMutated)
    : config (cfgRef), raw (rawRef), notifyMutation (std::move (onMutated))
{
    auto setUpLabel = [this] (juce::Label& l) { addAndMakeVisible (l); };
    auto setUpField = [this] (juce::TextEditor& f, bool numeric)
    {
        addAndMakeVisible (f);
        f.setMultiLine (false);
        f.setBorder (juce::BorderSize<int> (1));
        if (numeric) f.setInputRestrictions (8, "-0123456789");
        f.addListener (this);
    };

    setUpLabel (nameLabel);
    addAndMakeVisible (nameCombo);
    int id = 1;
    for (const auto name : allInstrumentNames())
        nameCombo.addItem (juce::String (std::string (name)), id++);
    nameCombo.addListener (this);

    setUpLabel (labelLabel);
    setUpField (labelField, false);
    setUpLabel (transposeLabel);
    setUpField (transposeField, true);
    setUpLabel (volumeLabel);
    setUpField (volumeField, true);
    setUpLabel (drumMapLabel);
    setUpField (drumMapField, false);
    setUpLabel (sourcesLabel);
}

void InstrumentDetailForm::editInstrument (int instrumentIndex)
{
    currentIndex = instrumentIndex;
    refresh();
}

void InstrumentDetailForm::refresh()
{
    const bool valid = currentIndex >= 0 && currentIndex < (int) config.instruments.size();
    const bool enabled = valid;

    nameCombo.setEnabled (enabled);
    labelField.setEnabled (enabled);
    transposeField.setEnabled (enabled);
    volumeField.setEnabled (enabled);

    if (! valid)
    {
        nameCombo.setSelectedId (0, juce::dontSendNotification);
        labelField.setText      ({}, juce::dontSendNotification);
        transposeField.setText  ({}, juce::dontSendNotification);
        volumeField.setText     ({}, juce::dontSendNotification);
        drumMapField.setText    ({}, juce::dontSendNotification);
        drumMapField.setEnabled (false);
        sourceChecks.clear();
        repaint();
        return;
    }

    const auto& inst = config.instruments[(size_t) currentIndex];

    int comboId = 1;
    for (const auto name : allInstrumentNames())
    {
        if (std::string (name) == inst.name)
        {
            nameCombo.setSelectedId (comboId, juce::dontSendNotification);
            break;
        }
        ++comboId;
    }

    labelField.setText      (juce::String (inst.label.value_or (std::string{})), juce::dontSendNotification);
    transposeField.setText  (juce::String (inst.transposeSemitones), juce::dontSendNotification);
    volumeField.setText     (juce::String (inst.volumePercent), juce::dontSendNotification);
    drumMapField.setText    (juce::String (inst.drumMap.value_or (std::string{})), juce::dontSendNotification);

    LotroInstrument parsed = LotroInstrument::LuteOfAges;
    parseName (inst.name, parsed);
    drumMapField.setEnabled (parsed == LotroInstrument::Drums);

    rebuildSourceCheckboxes();
    resized();
    repaint();
}

void InstrumentDetailForm::rebuildSourceCheckboxes()
{
    sourceChecks.clear();
    if (currentIndex < 0) return;
    const auto& inst = config.instruments[(size_t) currentIndex];

    for (size_t i = 0; i < raw.tracks.size(); ++i)
    {
        const auto& track = raw.tracks[i];
        juce::String label = juce::String ((int) i) + ": " + juce::String (track.name)
            + " (chan " + juce::String (track.sourceMidiChannel)
            + ", " + juce::String ((int) track.notes.size()) + " notes)";
        auto* cb = sourceChecks.add (new juce::ToggleButton (label));
        addAndMakeVisible (cb);
        cb->setToggleState (
            std::find (inst.sources.begin(), inst.sources.end(), (int) i) != inst.sources.end(),
            juce::dontSendNotification);
        cb->addListener (this);
    }
}

void InstrumentDetailForm::pushFromControlsToConfig()
{
    if (currentIndex < 0 || currentIndex >= (int) config.instruments.size()) return;
    auto& inst = config.instruments[(size_t) currentIndex];

    if (auto idx = nameCombo.getSelectedId(); idx > 0)
    {
        const auto names = allInstrumentNames();
        if ((size_t) idx <= names.size())
            inst.name = std::string (names[(size_t) idx - 1]);
    }

    {
        const auto s = labelField.getText().toStdString();
        inst.label = s.empty() ? std::optional<std::string>{} : std::optional<std::string>{ s };
    }
    {
        const auto t = transposeField.getText();
        inst.transposeSemitones = t.isEmpty() ? 0 : t.getIntValue();
    }
    {
        const auto v = volumeField.getText();
        inst.volumePercent = v.isEmpty() ? 100 : v.getIntValue();
    }
    {
        const auto s = drumMapField.getText().toStdString();
        inst.drumMap = s.empty() ? std::optional<std::string>{} : std::optional<std::string>{ s };
    }
    {
        std::vector<int> picked;
        for (int i = 0; i < sourceChecks.size(); ++i)
            if (sourceChecks[i]->getToggleState())
                picked.push_back (i);
        inst.sources = std::move (picked);
    }

    LotroInstrument parsed = LotroInstrument::LuteOfAges;
    parseName (inst.name, parsed);
    drumMapField.setEnabled (parsed == LotroInstrument::Drums);

    if (notifyMutation) notifyMutation();
}

void InstrumentDetailForm::textEditorTextChanged (juce::TextEditor&) { pushFromControlsToConfig(); }
void InstrumentDetailForm::comboBoxChanged       (juce::ComboBox*)    { pushFromControlsToConfig(); }
void InstrumentDetailForm::buttonClicked         (juce::Button*)      { pushFromControlsToConfig(); }

void InstrumentDetailForm::resized()
{
    auto area = getLocalBounds().reduced (4);
    const int rowH = 26;
    const int labelW = 180;

    auto layoutRow = [&] (juce::Label& l, juce::Component& f)
    {
        auto row = area.removeFromTop (rowH);
        l.setBounds (row.removeFromLeft (labelW));
        f.setBounds (row);
        area.removeFromTop (4);
    };

    layoutRow (nameLabel,      nameCombo);
    layoutRow (labelLabel,     labelField);
    layoutRow (transposeLabel, transposeField);
    layoutRow (volumeLabel,    volumeField);
    layoutRow (drumMapLabel,   drumMapField);

    sourcesLabel.setBounds (area.removeFromTop (rowH));
    area.removeFromTop (4);
    for (auto* cb : sourceChecks)
        cb->setBounds (area.removeFromTop (rowH));
}

void InstrumentDetailForm::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colours::whitesmoke);
}

} // namespace lotro
