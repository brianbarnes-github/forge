#pragma once

#include "PreferencesServices.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

namespace lotro
{
    // Preferences > Editing: default grid size, snap to grid, playhead follow and what a cross-track Move/Copy carries. Uses `services` only in the constructor and in click handlers.
    class EditingPreferencesPage : public juce::Component
    {
    public:
        explicit EditingPreferencesPage (PreferencesServices& services, std::function<void()> onChanged = {});

        void resized() override;

        juce::ComboBox& gridComboForTesting() { return grid; }
        juce::ToggleButton& snapToggleForTesting() { return snap; }
        juce::ToggleButton& followToggleForTesting() { return follow; }
        juce::ToggleButton& notesOnlyRadioForTesting() { return notesOnly; }
        juce::ToggleButton& allEventsRadioForTesting() { return allEvents; }

    private:
        void notify();

        PreferencesServices&  services;
        std::function<void()> onChanged;
        juce::Label           gridLabel;
        juce::ComboBox        grid;
        juce::Label           gridNote;
        juce::ToggleButton    snap { "Snap to grid" };
        juce::ToggleButton    follow { "Follow the playhead during playback" };
        juce::Label           scopeLabel;
        juce::ToggleButton    notesOnly { "Notes only" };
        juce::ToggleButton    allEvents { "All events (controllers, program changes, pitch bend)" };
    };
}
