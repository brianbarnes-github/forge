#include "AboutBox.h"
#include "SongsmithColours.h"

namespace lotro
{
    namespace
    {
        constexpr int dialogWidth  = 320;
        constexpr int dialogHeight = 170;

        void setUpLabel (juce::Component& parent, juce::Label& label, const juce::String& text,
                         float fontHeight, juce::uint32 colour)
        {
            label.setText (text, juce::dontSendNotification);
            label.setJustificationType (juce::Justification::centred);
            label.setFont (juce::FontOptions (fontHeight));
            label.setColour (juce::Label::textColourId, juce::Colour (colour));
            parent.addAndMakeVisible (label);
        }
    }

    AboutComponent::AboutComponent (const BuildInfo& info)
    {
        setUpLabel (*this, appName, "SongSmith",               24.0f, SongsmithColours::text);
        setUpLabel (*this, creator, "Created by Vydor",        15.0f, SongsmithColours::text);
        setUpLabel (*this, version, formatVersionLine (info), 14.0f, SongsmithColours::textMuted);
        setUpLabel (*this, build,   formatBuildLine (info),   14.0f, SongsmithColours::textMuted);
        setSize (dialogWidth, dialogHeight);
    }

    void AboutComponent::paint (juce::Graphics& g)
    {
        g.fillAll (juce::Colour (SongsmithColours::background));
    }

    void AboutComponent::resized()
    {
        auto area = getLocalBounds().reduced (16);
        appName.setBounds (area.removeFromTop (40));
        creator.setBounds (area.removeFromTop (28));
        area.removeFromTop (12);
        version.setBounds (area.removeFromTop (22));
        build.setBounds (area.removeFromTop (22));
    }

    void showAboutDialog (juce::Component* centreAround)
    {
        juce::DialogWindow::LaunchOptions options;
        options.content.setOwned (new AboutComponent (currentBuildInfo()));
        options.dialogTitle = "About SongSmith";
        options.dialogBackgroundColour = juce::Colour (SongsmithColours::background);
        options.componentToCentreAround = centreAround;
        options.useNativeTitleBar = true;
        options.resizable = false;
        options.launchAsync();
    }
}
