#pragma once

#include "PreferencesServices.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>

namespace lotro
{
    // Preferences > Playback: the active SoundFont, Browse... to load another, Clear to
    // forget the choice and return to the bundled default. Everything applies live.
    // Uses `services` only in the constructor and in click / chooser handlers.
    class PlaybackPreferencesPage : public juce::Component
    {
    public:
        explicit PlaybackPreferencesPage (PreferencesServices& services, std::function<void()> onChanged = {});

        void resized() override;

        juce::String labelTextForTesting() const { return activeLabel.getText(); }
        juce::String statusTextForTesting() const { return status.getText(); }
        juce::TextButton& browseButtonForTesting() { return browse; }
        juce::TextButton& clearButtonForTesting() { return clear; }
        // What the Browse chooser's callback runs once a file is picked.
        void loadChosenFileForTesting (const juce::File& file) { loadChosen (file); }

    private:
        void browseForFile();
        void loadChosen (const juce::File& file);
        void clearChoice();
        void refresh();

        PreferencesServices&              services;
        std::function<void()>             onChanged;
        juce::Label                       heading, activeLabel, status;
        juce::TextButton                  browse { "Browse..." };
        juce::TextButton                  clear  { "Clear" };
        std::unique_ptr<juce::FileChooser> chooser;
    };
}
