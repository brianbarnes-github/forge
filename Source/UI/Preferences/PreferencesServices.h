#pragma once

#include "../AppSettings.h"

#include <juce_core/juce_core.h>

#include <functional>

namespace lotro
{
    enum class SoundFontResult { loaded, failed, bundledUnavailable };

    struct SoundFontLoad
    {
        SoundFontResult result;
        juce::String detail;   // error text when failed
    };

    // What a Preferences page may ask of the app. MainWindow fills it in; pages call it
    // only from their constructor and click/chooser handlers, never from a destructor.
    // A failed load keeps the previous SoundFont active and writes nothing.
    struct PreferencesServices
    {
        AppSettings& settings;
        std::function<SoundFontLoad (const juce::File&)> loadSoundFont;        // load, then remember the choice
        std::function<SoundFontLoad ()>                  useBundledSoundFont;  // clear the choice, load the bundled file
        std::function<juce::String ()>                   activeSoundFontLabel;
        std::function<void ()>                           applyViewSettings = {};   // re-read the view settings and push them to the views
    };
}
