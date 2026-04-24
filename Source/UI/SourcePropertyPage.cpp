#include "SourcePropertyPage.h"

namespace lotro
{

SourcePropertyPage::SourcePropertyPage (Config& cfgRef, const Song& rawRef,
                                        std::function<void()> onChange)
    : config (cfgRef), raw (rawRef), notifyChange (std::move (onChange))
{
    auto setUpLabel = [this] (juce::Label& l)
    {
        addAndMakeVisible (l);
        l.setColour (juce::Label::textColourId, juce::Colours::darkgrey);
    };
    auto setUpField = [this] (juce::TextEditor& f)
    {
        addAndMakeVisible (f);
        f.setMultiLine (false);
        f.setBorder (juce::BorderSize<int> (1));
        f.setInputRestrictions (8, "-0123456789");
        f.addListener (this);
    };

    setUpLabel (midiInfoLabel);
    setUpLabel (midiInfoValue);
    setUpLabel (transposeLabel);
    setUpField (transposeField);
    setUpLabel (volumeLabel);
    setUpField (volumeField);

    refresh();
}

void SourcePropertyPage::editSource (int iIdx, int sIdx)
{
    instrumentIndex = iIdx;
    sourceIndex     = sIdx;
    refresh();
}

void SourcePropertyPage::refresh()
{
    const bool valid =
        instrumentIndex >= 0 && instrumentIndex < (int) config.instruments.size()
        && sourceIndex >= 0
        && sourceIndex < (int) config.instruments[(size_t) instrumentIndex].sources.size();

    transposeField.setEnabled (valid);
    volumeField.setEnabled    (valid);

    if (! valid)
    {
        midiInfoValue.setText  ("(no selection)", juce::dontSendNotification);
        transposeField.setText ({}, juce::dontSendNotification);
        volumeField.setText    ({}, juce::dontSendNotification);
        return;
    }

    const auto& src = config.instruments[(size_t) instrumentIndex]
                            .sources[(size_t) sourceIndex];

    juce::String info = "MIDI " + juce::String (src.midiTrackIndex);
    if (src.midiTrackIndex >= 0 && src.midiTrackIndex < (int) raw.tracks.size())
    {
        const auto& t = raw.tracks[(size_t) src.midiTrackIndex];
        info += ": " + juce::String (t.name)
              + " (chan " + juce::String (t.sourceMidiChannel)
              + ", " + juce::String ((int) t.notes.size()) + " notes)";
    }
    midiInfoValue.setText (info, juce::dontSendNotification);

    transposeField.setText (juce::String (src.transposeSemitones),
                            juce::dontSendNotification);
    volumeField.setText (juce::String (src.volumePercent),
                         juce::dontSendNotification);
}

void SourcePropertyPage::pushToConfig()
{
    if (instrumentIndex < 0 || instrumentIndex >= (int) config.instruments.size()) return;
    auto& inst = config.instruments[(size_t) instrumentIndex];
    if (sourceIndex < 0 || sourceIndex >= (int) inst.sources.size()) return;
    auto& src = inst.sources[(size_t) sourceIndex];

    {
        const auto t = transposeField.getText();
        src.transposeSemitones = t.isEmpty() ? 0 : t.getIntValue();
    }
    {
        const auto v = volumeField.getText();
        src.volumePercent = v.isEmpty() ? 0 : v.getIntValue();
    }

    if (notifyChange) notifyChange();
}

void SourcePropertyPage::textEditorTextChanged (juce::TextEditor&) { pushToConfig(); }

void SourcePropertyPage::resized()
{
    auto area = getLocalBounds().reduced (4);
    const int rowH = 26;
    const int labelW = 180;

    auto row = [&] (juce::Label& l, juce::Component& f)
    {
        auto r = area.removeFromTop (rowH);
        l.setBounds (r.removeFromLeft (labelW));
        f.setBounds (r);
        area.removeFromTop (4);
    };

    row (midiInfoLabel,  midiInfoValue);
    row (transposeLabel, transposeField);
    row (volumeLabel,    volumeField);
}

} // namespace lotro
