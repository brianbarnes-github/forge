#include "PlaybackPreferencesPage.h"
#include "../SongsmithColours.h"

namespace lotro
{
    PlaybackPreferencesPage::PlaybackPreferencesPage (PreferencesServices& servicesIn, std::function<void()> onChangedIn)
        : services (servicesIn), onChanged (std::move (onChangedIn))
    {
        heading.setText ("SoundFont", juce::dontSendNotification);
        heading.setColour (juce::Label::textColourId, juce::Colour (SongsmithColours::textMuted));
        heading.setFont (juce::FontOptions (13.0f));
        activeLabel.setColour (juce::Label::textColourId, juce::Colour (SongsmithColours::text));
        status.setFont (juce::FontOptions (12.0f));
        status.setColour (juce::Label::textColourId, juce::Colour (SongsmithColours::textMuted));
        status.setJustificationType (juce::Justification::topLeft);
        status.setMinimumHorizontalScale (1.0f);   // wrap, do not shrink
        for (auto* c : { static_cast<juce::Component*> (&heading), static_cast<juce::Component*> (&activeLabel),
                         static_cast<juce::Component*> (&status), static_cast<juce::Component*> (&browse),
                         static_cast<juce::Component*> (&clear) })
            addAndMakeVisible (*c);

        browse.onClick = [this] { browseForFile(); };
        clear.onClick  = [this] { clearChoice(); };
        refresh();
    }

    void PlaybackPreferencesPage::resized()
    {
        auto area = getLocalBounds().reduced (12);
        heading.setBounds (area.removeFromTop (20));
        activeLabel.setBounds (area.removeFromTop (24));
        area.removeFromTop (8);
        auto row = area.removeFromTop (28);
        browse.setBounds (row.removeFromLeft (100));
        row.removeFromLeft (8);
        clear.setBounds (row.removeFromLeft (100));
        area.removeFromTop (8);
        status.setBounds (area.removeFromTop (64));
    }

    void PlaybackPreferencesPage::refresh()
    {
        activeLabel.setText (services.activeSoundFontLabel(), juce::dontSendNotification);
        clear.setEnabled (services.settings.soundFontPath().isNotEmpty());
    }

    void PlaybackPreferencesPage::browseForFile()
    {
        chooser = std::make_unique<juce::FileChooser> ("Choose a SoundFont", juce::File(), "*.sf2");
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
            [safe = juce::Component::SafePointer<PlaybackPreferencesPage> (this)] (const juce::FileChooser& fc)
            {
                if (safe == nullptr) return;
                const auto file = fc.getResult();
                if (file != juce::File())
                    safe->loadChosen (file);
            });
    }

    void PlaybackPreferencesPage::loadChosen (const juce::File& file)
    {
        const auto loaded = services.loadSoundFont (file);
        if (loaded.result == SoundFontResult::loaded)
            status.setText ("Loaded.", juce::dontSendNotification);
        else
            status.setText ("Could not load: " + loaded.detail + ". Still using the previous SoundFont.",
                            juce::dontSendNotification);
        refresh();
        if (onChanged) onChanged();
    }

    void PlaybackPreferencesPage::clearChoice()
    {
        const auto outcome = services.useBundledSoundFont();
        switch (outcome.result)
        {
            case SoundFontResult::loaded:
                status.setText ("Using the bundled SoundFont.", juce::dontSendNotification);
                break;
            case SoundFontResult::bundledUnavailable:
                status.setText ("Your choice was cleared, but the bundled SongSmith.sf2 was not found. "
                                "The current SoundFont stays until you quit.", juce::dontSendNotification);
                break;
            case SoundFontResult::failed:
                status.setText ("Your choice was cleared, but the bundled SoundFont could not be loaded: " + outcome.detail,
                                juce::dontSendNotification);
                break;
        }
        refresh();
        if (onChanged) onChanged();
    }
}
