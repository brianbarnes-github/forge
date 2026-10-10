#include "ImportPreferencesPage.h"
#include "../SongsmithColours.h"

namespace lotro
{
    namespace
    {
        constexpr int askId      = 1;
        constexpr int expandedId = 2;
        constexpr int startId    = 1;
        constexpr int markerId   = 2;
    }

    ImportPreferencesPage::ImportPreferencesPage (AppSettings& settings, std::function<void()> onChanged)
    {
        trackOptionsLabel.setText ("Import Track Options", juce::dontSendNotification);
        trackOptionsLabel.setColour (juce::Label::textColourId, juce::Colour (SongsmithColours::text));
        addAndMakeVisible (trackOptionsLabel);

        trackOptions.addItem ("Ask", askId);
        trackOptions.addItem ("Import Expanded Always", expandedId);
        trackOptions.setSelectedId (settings.importTrackOptions() == ImportTrackOptions::ask ? askId : expandedId,
                                    juce::dontSendNotification);
        trackOptions.onChange = [this, &settings, onChanged]
        {
            settings.setImportTrackOptions (trackOptions.getSelectedId() == expandedId
                                                ? ImportTrackOptions::expandedAlways
                                                : ImportTrackOptions::ask);
            if (onChanged) onChanged();
        };
        addAndMakeVisible (trackOptions);

        placementLabel.setText ("Place imported MIDI at", juce::dontSendNotification);
        placementLabel.setColour (juce::Label::textColourId, juce::Colour (SongsmithColours::text));
        addAndMakeVisible (placementLabel);

        placement.addItem ("Start of song", startId);
        placement.addItem ("Position marker", markerId);
        placement.setSelectedId (settings.importPlacement() == ImportPlacement::atMarker ? markerId : startId,
                                 juce::dontSendNotification);
        placement.onChange = [this, &settings, onChanged]
        {
            settings.setImportPlacement (placement.getSelectedId() == markerId ? ImportPlacement::atMarker
                                                                               : ImportPlacement::atStart);
            if (onChanged) onChanged();
        };
        addAndMakeVisible (placement);

        note.setText ("With Ask, every import shows the tempo map and track choices. "
                      "Merging into one track is only available through Ask.\n"
                      "Position marker places the file where you last clicked the timing bar; "
                      "with no marker set it goes at the start.",
                      juce::dontSendNotification);
        note.setFont (juce::FontOptions (12.0f));
        note.setColour (juce::Label::textColourId, juce::Colour (SongsmithColours::textMuted));
        addAndMakeVisible (note);
    }

    void ImportPreferencesPage::resized()
    {
        auto area = getLocalBounds().reduced (12);
        trackOptionsLabel.setBounds (area.removeFromTop (22));
        trackOptions.setBounds (area.removeFromTop (26).removeFromLeft (240));
        area.removeFromTop (12);
        placementLabel.setBounds (area.removeFromTop (22));
        placement.setBounds (area.removeFromTop (26).removeFromLeft (240));
        area.removeFromTop (6);
        note.setBounds (area.removeFromTop (80));
    }
}
