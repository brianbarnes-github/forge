#include "GeneralPreferencesPage.h"
#include "../SongsmithColours.h"

namespace lotro
{
    GeneralPreferencesPage::GeneralPreferencesPage (AppSettings& settings)
    {
        for (auto* b : { &unsavedChanges, &replaceFile })
        {
            b->setColour (juce::ToggleButton::textColourId, juce::Colour (SongsmithColours::text));
            addAndMakeVisible (*b);
        }
        unsavedChanges.setToggleState (settings.askToSaveUnsavedChanges(), juce::dontSendNotification);
        replaceFile.setToggleState (settings.askBeforeReplacingFile(), juce::dontSendNotification);
        unsavedChanges.onClick = [this, &settings] { settings.setAskToSaveUnsavedChanges (unsavedChanges.getToggleState()); };
        replaceFile.onClick    = [this, &settings] { settings.setAskBeforeReplacingFile (replaceFile.getToggleState()); };

        unsavedChangesNote.setText ("When off, New, Open and Quit discard unsaved edits without asking.",
                                    juce::dontSendNotification);
        unsavedChangesNote.setFont (juce::FontOptions (12.0f));
        unsavedChangesNote.setColour (juce::Label::textColourId, juce::Colour (SongsmithColours::textMuted));
        addAndMakeVisible (unsavedChangesNote);
    }

    void GeneralPreferencesPage::resized()
    {
        auto area = getLocalBounds().reduced (12);
        unsavedChanges.setBounds (area.removeFromTop (24));
        unsavedChangesNote.setBounds (area.removeFromTop (20).withTrimmedLeft (24));
        area.removeFromTop (12);
        replaceFile.setBounds (area.removeFromTop (24));
    }
}
