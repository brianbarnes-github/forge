#include "AppearancePreferencesPage.h"
#include "../SongsmithColours.h"

namespace lotro
{
    AppearancePreferencesPage::AppearancePreferencesPage (PreferencesServices& servicesIn, std::function<void()> onChangedIn)
        : services (servicesIn), onChanged (std::move (onChangedIn))
    {
        for (auto* b : { &showBand, &restorePlacement })
        {
            b->setColour (juce::ToggleButton::textColourId, juce::Colour (SongsmithColours::text));
            addAndMakeVisible (*b);
        }
        showBand.setToggleState (services.settings.showRangeBand(), juce::dontSendNotification);
        restorePlacement.setToggleState (services.settings.restoreWindowPlacement(), juce::dontSendNotification);

        showBand.onClick = [this]
        {
            services.settings.setShowRangeBand (showBand.getToggleState());
            notify();
        };
        restorePlacement.onClick = [this]
        {
            services.settings.setRestoreWindowPlacement (restorePlacement.getToggleState());
            notify();
        };

        restoreNote.setText ("Takes effect the next time Songsmith starts.", juce::dontSendNotification);
        restoreNote.setFont (juce::FontOptions (12.0f));
        restoreNote.setColour (juce::Label::textColourId, juce::Colour (SongsmithColours::textMuted));
        addAndMakeVisible (restoreNote);
    }

    void AppearancePreferencesPage::notify()
    {
        if (services.applyViewSettings)
            services.applyViewSettings();
        if (onChanged)
            onChanged();
    }

    void AppearancePreferencesPage::resized()
    {
        auto area = getLocalBounds().reduced (12);
        showBand.setBounds (area.removeFromTop (24));
        area.removeFromTop (12);
        restorePlacement.setBounds (area.removeFromTop (24));
        restoreNote.setBounds (area.removeFromTop (20).withTrimmedLeft (24));
    }
}
