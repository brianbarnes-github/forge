#pragma once

#include "PreferencesServices.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

namespace lotro
{
    // Preferences > Appearance. Window placement applies at the next launch. Uses `services` only in the constructor and in click handlers.
    class AppearancePreferencesPage : public juce::Component
    {
    public:
        explicit AppearancePreferencesPage (PreferencesServices& services, std::function<void()> onChanged = {});

        void resized() override;

        juce::ToggleButton& restorePlacementToggleForTesting() { return restorePlacement; }

    private:
        void notify();

        PreferencesServices&  services;
        std::function<void()> onChanged;
        juce::ToggleButton    restorePlacement { "Restore the window position and size on launch" };
        juce::Label           restoreNote;
    };
}
