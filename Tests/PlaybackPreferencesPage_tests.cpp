#include "UI/Preferences/PlaybackPreferencesPage.h"

#include <catch2/catch_test_macros.hpp>

using namespace lotro;

namespace
{
    // Fake app: records calls, keeps the settings and label consistent the way MainWindow does.
    struct FakeApp
    {
        juce::File settingsFile = juce::File::createTempFile (".settings");
        std::unique_ptr<juce::PropertiesFile> props = std::make_unique<juce::PropertiesFile> (settingsFile, juce::PropertiesFile::Options());
        AppSettings settings { *props };
        SoundFontLoad nextLoad    { SoundFontResult::loaded, {} };
        SoundFontLoad nextBundled { SoundFontResult::loaded, {} };
        juce::String  label { "Using bundled SongSmith.sf2" };
        juce::File    lastLoaded;
        int           bundledCalls = 0;

        PreferencesServices services
        {
            settings,
            [this] (const juce::File& file)
            {
                lastLoaded = file;
                if (nextLoad.result == SoundFontResult::loaded)
                {
                    label = "Using " + file.getFullPathName();
                    settings.setSoundFontPath (file);
                }
                return nextLoad;
            },
            [this]
            {
                ++bundledCalls;
                settings.clearSoundFontPath();
                if (nextBundled.result == SoundFontResult::loaded)
                    label = "Using bundled SongSmith.sf2";
                return nextBundled;
            },
            [this] { return label; }
        };

        ~FakeApp() { props.reset(); settingsFile.deleteFile(); }
    };

    const juce::File chosen = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("Chosen.sf2");
}

TEST_CASE ("Playback page: shows the active SoundFont label; Clear is off while no choice is saved", "[preferences][playback-page]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    FakeApp app;
    PlaybackPreferencesPage page (app.services);
    CHECK (page.labelTextForTesting() == "Using bundled SongSmith.sf2");
    CHECK_FALSE (page.clearButtonForTesting().isEnabled());
}

TEST_CASE ("Playback page: a chosen file that loads updates the label, enables Clear and notifies", "[preferences][playback-page]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    FakeApp app;
    int changed = 0;
    PlaybackPreferencesPage page (app.services, [&] { ++changed; });

    page.loadChosenFileForTesting (chosen);
    CHECK (app.lastLoaded == chosen);
    CHECK (page.labelTextForTesting() == "Using " + chosen.getFullPathName());
    CHECK (page.statusTextForTesting() == "Loaded.");
    CHECK (page.clearButtonForTesting().isEnabled());
    CHECK (changed == 1);
}

TEST_CASE ("Playback page: a file that fails to load shows the error and changes nothing", "[preferences][playback-page]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    FakeApp app;
    app.nextLoad = { SoundFontResult::failed, "not a SoundFont" };
    PlaybackPreferencesPage page (app.services);

    page.loadChosenFileForTesting (chosen);
    CHECK (page.statusTextForTesting() == "Could not load: not a SoundFont. Still using the previous SoundFont.");
    CHECK (page.labelTextForTesting() == "Using bundled SongSmith.sf2");   // unchanged
    CHECK (app.settings.soundFontPath().isEmpty());                         // nothing saved
    CHECK_FALSE (page.clearButtonForTesting().isEnabled());
}

TEST_CASE ("Playback page: Clear goes back to the bundled SoundFont", "[preferences][playback-page]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    FakeApp app;
    PlaybackPreferencesPage page (app.services);
    page.loadChosenFileForTesting (chosen);
    REQUIRE (page.clearButtonForTesting().isEnabled());

    int changed = 0;
    PlaybackPreferencesPage again (app.services, [&] { ++changed; });   // a reopened page sees the saved choice
    REQUIRE (again.clearButtonForTesting().isEnabled());
    again.clearButtonForTesting().onClick();

    CHECK (app.bundledCalls == 1);
    CHECK (again.statusTextForTesting() == "Using the bundled SoundFont.");
    CHECK (again.labelTextForTesting() == "Using bundled SongSmith.sf2");
    CHECK_FALSE (again.clearButtonForTesting().isEnabled());
    CHECK (app.settings.soundFontPath().isEmpty());
    CHECK (changed == 1);
}

TEST_CASE ("Playback page: Clear reports a missing or unloadable bundled SoundFont but still forgets the choice", "[preferences][playback-page]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    FakeApp app;
    app.settings.setSoundFontPath (chosen);
    PlaybackPreferencesPage page (app.services);
    REQUIRE (page.clearButtonForTesting().isEnabled());

    app.nextBundled = { SoundFontResult::bundledUnavailable, {} };
    page.clearButtonForTesting().onClick();
    CHECK (page.statusTextForTesting() == "Your choice was cleared, but the bundled SongSmith.sf2 was not found. The current SoundFont stays until you quit.");
    CHECK (app.settings.soundFontPath().isEmpty());
    CHECK (page.labelTextForTesting() == "Using bundled SongSmith.sf2");   // the fake label did not change: the current font stays

    app.settings.setSoundFontPath (chosen);
    PlaybackPreferencesPage second (app.services);
    app.nextBundled = { SoundFontResult::failed, "corrupt" };
    second.clearButtonForTesting().onClick();
    CHECK (second.statusTextForTesting() == "Your choice was cleared, but the bundled SoundFont could not be loaded: corrupt");
}

TEST_CASE ("Playback page: a SoundFont chosen while the settings file is unwritable still loads and the failed save is visible", "[preferences][playback-page]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    auto blocked = juce::File::createTempFile (".settings");
    REQUIRE (blocked.createDirectory().wasOk());
    const juce::ScopeGuard cleanup { [&] { blocked.deleteRecursively(); } };
    juce::PropertiesFile props (blocked, {});
    AppSettings settings (props);
    juce::String label = "Using bundled SongSmith.sf2";
    PreferencesServices services
    {
        settings,
        [&] (const juce::File& file) { label = "Using " + file.getFullPathName(); settings.setSoundFontPath (file); return SoundFontLoad { SoundFontResult::loaded, {} }; },
        [&] { return SoundFontLoad { SoundFontResult::loaded, {} }; },
        [&] { return label; }
    };
    int changed = 0;
    PlaybackPreferencesPage page (services, [&] { ++changed; });

    page.loadChosenFileForTesting (chosen);
    CHECK (page.labelTextForTesting() == "Using " + chosen.getFullPathName());   // the load worked
    CHECK (settings.lastSaveFailed());                                            // the dialog's notice reads this after onChanged
    CHECK (changed == 1);
}
