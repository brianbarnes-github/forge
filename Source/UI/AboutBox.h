#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "BuildInfo.h"

namespace lotro
{
    // Help -> About... content: app name, creator, version and build, so a
    // user can report exactly which build they are running.
    class AboutComponent : public juce::Component
    {
    public:
        explicit AboutComponent (const BuildInfo& info);

        void paint (juce::Graphics& g) override;
        void resized() override;

    private:
        juce::Label appName, creator, version, build;
    };

    // Opens the About box as a modal dialog centred over `centreAround`
    // (the application window); nullptr centres on the active window.
    void showAboutDialog (juce::Component* centreAround);
}
