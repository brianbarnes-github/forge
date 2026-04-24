#include "InstrumentPropertyPage.h"
#include "Core/LotroInstrument.h"

namespace lotro
{

InstrumentPropertyPage::InstrumentPropertyPage (Config& cfgRef, std::function<void()> onChange)
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
        f.setBorder (juce::BorderSize<int> (1));
        if (numeric) f.setInputRestrictions (8, "0123456789");
        f.addListener (this);
    };

    setUpLabel (xLabel);
    setUpField (xField, true);
    setUpLabel (nameLabel);
    addAndMakeVisible (nameCombo);
    int id = 1;
    for (const auto name : allInstrumentNames())
        nameCombo.addItem (juce::String (std::string (name)), id++);
    nameCombo.addListener (this);

    setUpLabel (labelLabel);
    setUpField (labelField, false);
    setUpLabel (drumMapLabel);
    setUpField (drumMapField, false);
    addAndMakeVisible (drumMapBrowse);
    drumMapBrowse.onClick = [this]
    {
        fileChooser = std::make_unique<juce::FileChooser> (
            "Choose drum map", juce::File(), "*.json");
        fileChooser->launchAsync (juce::FileBrowserComponent::openMode
                                | juce::FileBrowserComponent::canSelectFiles,
            [this] (const juce::FileChooser& fc)
            {
                const auto file = fc.getResult();
                if (file == juce::File()) return;
                drumMapField.setText (file.getFullPathName(), juce::sendNotification);
            });
    };

    refresh();
}

void InstrumentPropertyPage::editInstrument (int idx)
{
    currentIndex = idx;
    refresh();
}

void InstrumentPropertyPage::refresh()
{
    const bool valid = currentIndex >= 0
                    && currentIndex < (int) config.instruments.size();

    xField.setEnabled         (valid);
    nameCombo.setEnabled      (valid);
    labelField.setEnabled     (valid);
    drumMapField.setEnabled   (false);
    drumMapBrowse.setEnabled  (false);

    if (! valid)
    {
        xField.setText       ({}, juce::dontSendNotification);
        labelField.setText   ({}, juce::dontSendNotification);
        drumMapField.setText ({}, juce::dontSendNotification);
        nameCombo.setSelectedId (0, juce::dontSendNotification);
        repaint();
        return;
    }

    const auto& inst = config.instruments[(size_t) currentIndex];
    xField.setText (juce::String (inst.x), juce::dontSendNotification);
    labelField.setText (juce::String (inst.label.value_or (std::string{})),
                        juce::dontSendNotification);

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

    drumMapField.setText (juce::String (inst.drumMap.value_or (std::string{})),
                          juce::dontSendNotification);

    LotroInstrument parsed = LotroInstrument::LuteOfAges;
    parseName (inst.name, parsed);
    const bool isDrums = (parsed == LotroInstrument::Drums);
    drumMapField.setEnabled  (isDrums);
    drumMapBrowse.setEnabled (isDrums);

    repaint();
}

void InstrumentPropertyPage::pushToConfig()
{
    if (currentIndex < 0 || currentIndex >= (int) config.instruments.size()) return;
    auto& inst = config.instruments[(size_t) currentIndex];

    {
        const auto x = xField.getText().getIntValue();
        if (x > 0) inst.x = x;
    }
    {
        const auto sel = nameCombo.getSelectedId();
        if (sel > 0)
        {
            const auto names = allInstrumentNames();
            if ((size_t) sel <= names.size())
                inst.name = std::string (names[(size_t) sel - 1]);
        }
    }
    {
        const auto s = labelField.getText().toStdString();
        inst.label = s.empty() ? std::optional<std::string>{} : std::optional<std::string>{ s };
    }
    {
        const auto s = drumMapField.getText().toStdString();
        inst.drumMap = s.empty() ? std::optional<std::string>{} : std::optional<std::string>{ s };
    }

    LotroInstrument parsed = LotroInstrument::LuteOfAges;
    parseName (inst.name, parsed);
    const bool isDrums = (parsed == LotroInstrument::Drums);
    drumMapField.setEnabled  (isDrums);
    drumMapBrowse.setEnabled (isDrums);

    if (notifyChange) notifyChange();
}

void InstrumentPropertyPage::textEditorTextChanged (juce::TextEditor&) { pushToConfig(); }
void InstrumentPropertyPage::comboBoxChanged       (juce::ComboBox*)    { pushToConfig(); }

void InstrumentPropertyPage::resized()
{
    auto area = getLocalBounds().reduced (4);
    const int rowH = 26;
    const int labelW = 120;

    auto row = [&] (juce::Label& l, juce::Component& f)
    {
        auto r = area.removeFromTop (rowH);
        l.setBounds (r.removeFromLeft (labelW));
        f.setBounds (r);
        area.removeFromTop (4);
    };

    row (xLabel,     xField);
    row (nameLabel,  nameCombo);
    row (labelLabel, labelField);

    {
        auto r = area.removeFromTop (rowH);
        drumMapLabel.setBounds (r.removeFromLeft (labelW));
        drumMapBrowse.setBounds (r.removeFromRight (80));
        drumMapField.setBounds (r.withTrimmedRight (4));
        area.removeFromTop (4);
    }
}

} // namespace lotro
