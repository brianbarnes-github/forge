#pragma once

#include "../AppSettings.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace lotro
{
    // Preferences > General. Each toggle is applied (and saved) on click.
    class GeneralPreferencesPage : public juce::Component
    {
    public:
        explicit GeneralPreferencesPage (AppSettings& settings);

        void resized() override;

        juce::ToggleButton& unsavedChangesToggleForTesting() { return unsavedChanges; }
        juce::ToggleButton& replaceFileToggleForTesting() { return replaceFile; }

    private:
        juce::ToggleButton unsavedChanges { "Ask about unsaved changes" };
        juce::Label        unsavedChangesNote;
        juce::ToggleButton replaceFile { "Ask before replacing an existing file" };
    };
}
