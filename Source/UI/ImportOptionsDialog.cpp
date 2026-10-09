#include "ImportOptionsDialog.h"
#include "SongsmithColours.h"

namespace lotro
{
    namespace
    {
        constexpr int dialogWidth  = 380;
        constexpr int dialogHeight = 270;
        constexpr int tempoGroup   = 7101;
        constexpr int tracksGroup  = 7102;

        void styleLabel (juce::Label& label, const juce::String& text, float height, juce::uint32 colour)
        {
            label.setText (text, juce::dontSendNotification);
            label.setFont (juce::FontOptions (height));
            label.setColour (juce::Label::textColourId, juce::Colour (colour));
        }
    }

    ImportOptionsComponent::ImportOptionsComponent (const juce::String& fileName, bool songHasTempoMap)
    {
        styleLabel (heading,     "Import " + fileName, 16.0f, SongsmithColours::text);
        styleLabel (tempoLabel,  songHasTempoMap ? "Tempo map" : "Tempo map (first import: the file's is used)",
                    13.0f, SongsmithColours::textMuted);
        styleLabel (tracksLabel, "Tracks", 13.0f, SongsmithColours::textMuted);
        for (auto* l : { &heading, &tempoLabel, &tracksLabel })
            addAndMakeVisible (*l);

        for (auto* b : { &keepTempo, &replaceTempo })
        {
            b->setRadioGroupId (tempoGroup);
            b->setEnabled (songHasTempoMap);
        }
        for (auto* b : { &expandTracks, &mergeTracks })
            b->setRadioGroupId (tracksGroup);
        for (auto* b : { &keepTempo, &replaceTempo, &expandTracks, &mergeTracks })
        {
            b->setColour (juce::ToggleButton::textColourId, juce::Colour (SongsmithColours::text));
            addAndMakeVisible (*b);
        }
        keepTempo.setToggleState (true, juce::dontSendNotification);
        expandTracks.setToggleState (true, juce::dontSendNotification);

        okButton.onClick     = [this] { if (onAccepted) onAccepted (chosenOptions()); };
        cancelButton.onClick = [this] { if (onCancelled) onCancelled(); };
        addAndMakeVisible (okButton);
        addAndMakeVisible (cancelButton);

        setSize (dialogWidth, dialogHeight);
    }

    ImportOptions ImportOptionsComponent::chosenOptions() const
    {
        ImportOptions options;
        options.tempo  = replaceTempo.getToggleState() ? TempoMode::replace : TempoMode::keep;
        options.tracks = mergeTracks.getToggleState()  ? TrackMode::merged  : TrackMode::expanded;
        return options;
    }

    void ImportOptionsComponent::paint (juce::Graphics& g)
    {
        g.fillAll (juce::Colour (SongsmithColours::background));
    }

    void ImportOptionsComponent::resized()
    {
        auto area = getLocalBounds().reduced (16);
        heading.setBounds (area.removeFromTop (26));
        area.removeFromTop (8);
        tempoLabel.setBounds (area.removeFromTop (20));
        keepTempo.setBounds (area.removeFromTop (24));
        replaceTempo.setBounds (area.removeFromTop (24));
        area.removeFromTop (8);
        tracksLabel.setBounds (area.removeFromTop (20));
        expandTracks.setBounds (area.removeFromTop (24));
        mergeTracks.setBounds (area.removeFromTop (24));

        auto buttons = area.removeFromBottom (32);
        cancelButton.setBounds (buttons.removeFromRight (80));
        buttons.removeFromRight (8);
        okButton.setBounds (buttons.removeFromRight (80));
    }

    void showImportOptionsDialog (juce::Component* centreAround, const juce::String& fileName,
                                  bool songHasTempoMap, std::function<void (const ImportOptions&)> onAccepted)
    {
        auto content = std::make_unique<ImportOptionsComponent> (fileName, songHasTempoMap);
        auto* component = content.get();

        juce::DialogWindow::LaunchOptions options;
        options.content.setOwned (content.release());
        options.dialogTitle = "MIDI File Import";
        options.dialogBackgroundColour = juce::Colour (SongsmithColours::background);
        options.componentToCentreAround = centreAround;
        options.useNativeTitleBar = true;
        options.resizable = false;
        auto* window = options.launchAsync();   // enters modal state

        // The window owns the content that owns these callbacks, so it cannot outlive `window`.
        component->onAccepted = [window, accepted = std::move (onAccepted)] (const ImportOptions& chosen)
        {
            window->exitModalState (1);
            if (accepted) accepted (chosen);
        };
        component->onCancelled = [window] { window->exitModalState (0); };
    }
}
