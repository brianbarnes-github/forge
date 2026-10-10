#pragma once

#include "PreferencesServices.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

namespace lotro
{
    // Preferences > Appearance. The band switch applies live; window placement applies at the
    // next launch. Uses `services` only in the constructor and in click handlers.
    class AppearancePreferencesPage : public juce::Component
    {
    public:
        explicit AppearancePreferencesPage (PreferencesServices& services, std::function<void()> onChanged = {});

        void resized() override;

        juce::ToggleButton& showBandToggleForTesting() { return showBand; }
        juce::ToggleButton& restorePlacementToggleForTesting() { return restorePlacement; }

    private:
        void notify();

        PreferencesServices&  services;
        std::function<void()> onChanged;
        juce::ToggleButton    showBand { "Show the instrument range band in the LOTRO preview" };
        juce::ToggleButton    restorePlacement { "Restore the window position and size on launch" };
        juce::Label           restoreNote;
    };
}
