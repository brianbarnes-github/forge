#include "UI/AboutBox.h"
#include "UI/BuildInfo.h"
#include "UI/SongsmithColours.h"

#include <catch2/catch_test_macros.hpp>

using namespace lotro;

namespace
{
    juce::StringArray labelTexts (juce::Component& parent)
    {
        juce::StringArray texts;
        for (auto* child : parent.getChildren())
            if (auto* label = dynamic_cast<juce::Label*> (child))
                texts.add (label->getText());
        return texts;
    }
}

TEST_CASE ("BuildInfo: build line shows the commit count and short hash", "[about]")
{
    const BuildInfo info { "0.1.0", 201, "eb054de", false };
    CHECK (formatVersionLine (info) == "Version 0.1.0");
    CHECK (formatBuildLine (info) == "Build 201 (eb054de)");
}

TEST_CASE ("BuildInfo: a build with uncommitted changes is marked dirty", "[about]")
{
    const BuildInfo info { "0.1.0", 201, "eb054de", true };
    CHECK (formatBuildLine (info) == "Build 201 (eb054de-dirty)");
}

TEST_CASE ("BuildInfo: a build made without git says the build is unknown", "[about]")
{
    const BuildInfo info { "0.1.0", 0, "", false };
    CHECK (formatBuildLine (info) == "Build unknown");
}

TEST_CASE ("BuildInfo: the baked-in info for this build comes from git", "[about]")
{
    // The test binary is always built from this repo's git checkout, so the
    // generated header must carry a real commit, not the no-git fallback.
    const auto info = currentBuildInfo();
    CHECK (info.version.isNotEmpty());
    CHECK (info.commitCount > 0);
    CHECK (info.commitHash.length() >= 7);
    CHECK (info.commitHash.containsOnly ("0123456789abcdef"));
}

TEST_CASE ("AboutComponent: shows the app name, creator, version and build", "[about]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    const BuildInfo info { "0.1.0", 201, "eb054de", false };
    AboutComponent about (info);

    const auto texts = labelTexts (about);
    CHECK (texts.contains ("SongSmith"));
    CHECK (texts.contains ("Created by Vydor"));
    CHECK (texts.contains ("Version 0.1.0"));
    CHECK (texts.contains ("Build 201 (eb054de)"));
}

TEST_CASE ("AboutComponent: paints the copper anvil logo", "[about][theme]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    const BuildInfo info { "0.1.0", 1, "abc1234", false };
    AboutComponent about (info);
    juce::Image image (juce::Image::ARGB, about.getWidth(), about.getHeight(), true, juce::SoftwareImageType());
    { juce::Graphics g (image); about.paint (g); }
    const juce::Colour copper (SongsmithColours::accentCopper);
    bool found = false;
    for (int y = 0; y < 80 && ! found; ++y)
        for (int x = 0; x < image.getWidth() && ! found; ++x)
        {
            const auto p = image.getPixelAt (x, y);
            found = std::abs (p.getRed() - copper.getRed()) < 24 && std::abs (p.getGreen() - copper.getGreen()) < 24
                    && std::abs (p.getBlue() - copper.getBlue()) < 24;
        }
    CHECK (found);
}

TEST_CASE ("AboutComponent: the creator line sits right under the logo, with no empty band", "[about][theme]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    const BuildInfo info { "0.1.0", 1, "abc1234", false };
    AboutComponent about (info);
    const juce::Label* creator = nullptr;
    const juce::Label* build = nullptr;
    for (auto* child : about.getChildren())
        if (auto* label = dynamic_cast<juce::Label*> (child))
        {
            if (label->getText().startsWith ("Created by"))
                creator = label;
            if (label->isVisible())
                build = label;   // the last visible label is the build line
        }
    REQUIRE (creator != nullptr);
    REQUIRE (build != nullptr);
    constexpr int logoBottom = 16 + 56;
    CHECK (creator->getY() - logoBottom <= 12);
    CHECK (about.getHeight() - build->getBottom() <= 30);   // and no bigger bottom margin than before the logo
}
