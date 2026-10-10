#pragma once

#include "PreferencesServices.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

namespace lotro
{
    // Preferences > Editing: default grid size and playhead follow. Uses `services` only in the constructor and in click handlers.
    class EditingPreferencesPage : public juce::Component
    {
    public:
        explicit EditingPreferencesPage (PreferencesServices& services, std::function<void()> onChanged = {});

        void resized() override;

        juce::ComboBox& gridComboForTesting() { return grid; }
        juce::ToggleButton& followToggleForTesting() { return follow; }

    private:
        void notify();

        PreferencesServices&  services;
        std::function<void()> onChanged;
        juce::Label           gridLabel;
        juce::ComboBox        grid;
        juce::Label           gridNote;
        juce::ToggleButton    follow { "Follow the playhead during playback" };
    };
}
