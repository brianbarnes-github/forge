#pragma once

#include "../AppSettings.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace lotro
{
    // Preferences > Import. The choice is applied (and saved) as soon as it changes.
    class ImportPreferencesPage : public juce::Component
    {
    public:
        explicit ImportPreferencesPage (AppSettings& settings);

        void resized() override;

        juce::ComboBox& trackOptionsForTesting() { return trackOptions; }

    private:
        juce::Label    trackOptionsLabel;
        juce::ComboBox trackOptions;
        juce::Label    note;
    };
}
