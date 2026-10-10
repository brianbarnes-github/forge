#pragma once

#include "../AppSettings.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

namespace lotro
{
    // Preferences > Import. The choice is applied (and saved) as soon as it changes.
    class ImportPreferencesPage : public juce::Component
    {
    public:
        explicit ImportPreferencesPage (AppSettings& settings, std::function<void()> onChanged = {});

        void resized() override;

        juce::ComboBox& trackOptionsForTesting() { return trackOptions; }

        juce::ComboBox& placementForTesting() { return placement; }

    private:
        juce::Label    trackOptionsLabel;
        juce::ComboBox trackOptions;
        juce::Label    placementLabel;
        juce::ComboBox placement;
        juce::Label    note;
    };
}
