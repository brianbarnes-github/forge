#include "EditingPreferencesPage.h"
#include "../SongsmithColours.h"

namespace lotro
{
    EditingPreferencesPage::EditingPreferencesPage (PreferencesServices& servicesIn, std::function<void()> onChangedIn)
        : services (servicesIn), onChanged (std::move (onChangedIn))
    {
        gridLabel.setText ("Default grid size", juce::dontSendNotification);
        gridLabel.setColour (juce::Label::textColourId, juce::Colour (SongsmithColours::text));
        addAndMakeVisible (gridLabel);

        // Item ids are (int) GridSize + 1 (ComboBox ids start at 1): Off, 1/4, 1/8, 1/16.
        grid.addItemList ({ "Off", "1/4", "1/8", "1/16" }, 1);
        grid.setSelectedId ((int) services.settings.defaultGrid() + 1, juce::dontSendNotification);
        grid.onChange = [this]
        {
            services.settings.setDefaultGrid ((GridSize) (grid.getSelectedId() - 1));
            notify();
        };
        addAndMakeVisible (grid);

        gridNote.setText ("Applies to track editors opened afterwards. Edit > Grid size still changes an open editor.",
                          juce::dontSendNotification);
        gridNote.setFont (juce::FontOptions (12.0f));
        gridNote.setColour (juce::Label::textColourId, juce::Colour (SongsmithColours::textMuted));
        addAndMakeVisible (gridNote);

        follow.setColour (juce::ToggleButton::textColourId, juce::Colour (SongsmithColours::text));
        follow.setToggleState (services.settings.followPlayhead(), juce::dontSendNotification);
        follow.onClick = [this]
        {
            services.settings.setFollowPlayhead (follow.getToggleState());
            notify();
        };
        addAndMakeVisible (follow);

        scopeLabel.setText ("When moving or copying sections to another track (Alt-drag), carry", juce::dontSendNotification);
        scopeLabel.setColour (juce::Label::textColourId, juce::Colour (SongsmithColours::text));
        addAndMakeVisible (scopeLabel);

        constexpr int radioGroup = 4201;
        for (auto* b : { &notesOnly, &allEvents })
        {
            b->setRadioGroupId (radioGroup);
            b->setColour (juce::ToggleButton::textColourId, juce::Colour (SongsmithColours::text));
            addAndMakeVisible (*b);
        }
        const bool all = services.settings.mergeScope() == MergeScope::allEvents;
        notesOnly.setToggleState (! all, juce::dontSendNotification);
        allEvents.setToggleState (all, juce::dontSendNotification);
        // Turning one radio on also notifies the one it turns off; only the button that is now on acts.
        const auto chooseScope = [this] (juce::ToggleButton& button, MergeScope scope)
        {
            if (! button.getToggleState())
                return;
            services.settings.setMergeScope (scope);
            notify();
        };
        notesOnly.onClick = [this, chooseScope] { chooseScope (notesOnly, MergeScope::notesOnly); };
        allEvents.onClick = [this, chooseScope] { chooseScope (allEvents, MergeScope::allEvents); };
    }

    void EditingPreferencesPage::notify()
    {
        if (services.applyViewSettings)
            services.applyViewSettings();
        if (onChanged)
            onChanged();
    }

    void EditingPreferencesPage::resized()
    {
        auto area = getLocalBounds().reduced (12);
        gridLabel.setBounds (area.removeFromTop (22));
        grid.setBounds (area.removeFromTop (26).removeFromLeft (160));
        area.removeFromTop (6);
        gridNote.setBounds (area.removeFromTop (36));
        area.removeFromTop (12);
        follow.setBounds (area.removeFromTop (24));
        area.removeFromTop (16);
        scopeLabel.setBounds (area.removeFromTop (22));
        notesOnly.setBounds (area.removeFromTop (24));
        allEvents.setBounds (area.removeFromTop (24));
    }
}
