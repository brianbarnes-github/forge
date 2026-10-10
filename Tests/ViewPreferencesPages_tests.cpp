#include "UI/Preferences/AppearancePreferencesPage.h"
#include "UI/Preferences/EditingPreferencesPage.h"

#include <catch2/catch_test_macros.hpp>

using namespace lotro;

namespace
{
    // Fake app: counts applyViewSettings calls; the settings file is real so persistence is checked.
    struct FakeApp
    {
        juce::File settingsFile = juce::File::createTempFile (".settings");
        std::unique_ptr<juce::PropertiesFile> props = std::make_unique<juce::PropertiesFile> (settingsFile, juce::PropertiesFile::Options());
        AppSettings settings { *props };
        int applyCalls = 0;
        int changedCalls = 0;

        PreferencesServices services
        {
            settings,
            [] (const juce::File&) { return SoundFontLoad { SoundFontResult::loaded, {} }; },
            [] { return SoundFontLoad { SoundFontResult::loaded, {} }; },
            [] { return juce::String(); },
            [this] { ++applyCalls; }
        };

        ~FakeApp() { props.reset(); settingsFile.deleteFile(); }
    };
}

TEST_CASE ("Appearance page: the toggle reflects the setting", "[preferences][appearance-page]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    FakeApp app;
    AppearancePreferencesPage page (app.services);
    CHECK (page.restorePlacementToggleForTesting().getToggleState());   // default on
    app.settings.setRestoreWindowPlacement (false);
    AppearancePreferencesPage reopened (app.services);
    CHECK_FALSE (reopened.restorePlacementToggleForTesting().getToggleState());
}

TEST_CASE ("Appearance page: a click writes the setting, applies the view settings and notifies once", "[preferences][appearance-page]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    FakeApp app;
    AppearancePreferencesPage page (app.services, [&] { ++app.changedCalls; });

    page.restorePlacementToggleForTesting().setToggleState (false, juce::sendNotification);
    CHECK_FALSE (app.settings.restoreWindowPlacement());
    CHECK (app.applyCalls == 1);
    CHECK (app.changedCalls == 1);
}

TEST_CASE ("Editing page: the grid dropdown and follow toggle reflect the settings", "[preferences][editing-page]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    FakeApp app;
    app.settings.setDefaultGrid (GridSize::Sixteenth);
    app.settings.setFollowPlayhead (false);
    EditingPreferencesPage page (app.services);
    CHECK (page.gridComboForTesting().getText() == "1/16");
    CHECK_FALSE (page.followToggleForTesting().getToggleState());
}

TEST_CASE ("Editing page: choosing a grid and toggling follow write the settings, apply and notify", "[preferences][editing-page]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    FakeApp app;
    EditingPreferencesPage page (app.services, [&] { ++app.changedCalls; });

    page.gridComboForTesting().setSelectedId (3, juce::sendNotificationSync);   // 1/8
    CHECK (app.settings.defaultGrid() == GridSize::Eighth);
    CHECK (app.applyCalls == 1);
    CHECK (app.changedCalls == 1);

    page.followToggleForTesting().setToggleState (false, juce::sendNotification);
    CHECK_FALSE (app.settings.followPlayhead());
    CHECK (app.applyCalls == 2);
    CHECK (app.changedCalls == 2);
}

TEST_CASE ("View pages: with an unwritable settings file the change still applies and the notice hook fires", "[preferences][appearance-page][editing-page]")
{
    auto blocked = juce::File::createTempFile (".settings");
    REQUIRE (blocked.createDirectory().wasOk());
    const juce::ScopedJuceInitialiser_GUI juceInit;
    const juce::ScopeGuard cleanup { [&] { blocked.deleteRecursively(); } };
    juce::PropertiesFile props (blocked, {});
    AppSettings settings (props);
    int applyCalls = 0;
    int changedCalls = 0;
    PreferencesServices services { settings,
                                   [] (const juce::File&) { return SoundFontLoad { SoundFontResult::loaded, {} }; },
                                   [] { return SoundFontLoad { SoundFontResult::loaded, {} }; },
                                   [] { return juce::String(); },
                                   [&] { ++applyCalls; } };

    {
        AppearancePreferencesPage page (services, [&] { ++changedCalls; });
        page.restorePlacementToggleForTesting().setToggleState (false, juce::sendNotification);
        CHECK (settings.lastSaveFailed());
        CHECK_FALSE (settings.restoreWindowPlacement());   // still applied for the session
        CHECK (applyCalls == 1);
        CHECK (changedCalls == 1);
    }
    {
        EditingPreferencesPage page (services, [&] { ++changedCalls; });
        page.gridComboForTesting().setSelectedId (2, juce::sendNotificationSync);   // 1/4
        CHECK (settings.lastSaveFailed());
        CHECK (settings.defaultGrid() == GridSize::Quarter);
        CHECK (applyCalls == 2);
        CHECK (changedCalls == 2);
    }
}
