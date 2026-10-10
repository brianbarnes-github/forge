#include "UI/AppSettings.h"

#include <catch2/catch_test_macros.hpp>

using namespace lotro;

TEST_CASE ("AppSettings: both confirmations default to on", "[app-settings]")
{
    auto file = juce::File::createTempFile (".settings");
    const juce::ScopeGuard cleanup { [&] { file.deleteFile(); } };
    juce::PropertiesFile props (file, {});
    const AppSettings settings (props);
    CHECK (settings.askToSaveUnsavedChanges());
    CHECK (settings.askBeforeReplacingFile());
}

TEST_CASE ("AppSettings: setters round-trip and persist across a re-opened file", "[app-settings]")
{
    auto file = juce::File::createTempFile (".settings");
    const juce::ScopeGuard cleanup { [&] { file.deleteFile(); } };
    {
        juce::PropertiesFile props (file, {});
        AppSettings settings (props);
        settings.setAskToSaveUnsavedChanges (false);
        CHECK_FALSE (settings.askToSaveUnsavedChanges());
        CHECK (settings.askBeforeReplacingFile());   // independent
        // No explicit saveIfNeeded() here. (The PropertiesFile destructor also saves, so
        // this block alone does not prove the setter writes; the later block
        // below reads back while the first file is still alive.)
    }
    juce::PropertiesFile reloaded (file, {});
    const AppSettings settings (reloaded);
    CHECK_FALSE (settings.askToSaveUnsavedChanges());
    CHECK (settings.askBeforeReplacingFile());

    juce::PropertiesFile again (file, {});
    AppSettings writer (again);
    writer.setAskBeforeReplacingFile (false);
    juce::PropertiesFile third (file, {});
    CHECK_FALSE (AppSettings (third).askBeforeReplacingFile());
}

TEST_CASE ("AppSettings: a non-boolean stored value falls back to the default", "[app-settings]")
{
    auto file = juce::File::createTempFile (".settings");
    const juce::ScopeGuard cleanup { [&] { file.deleteFile(); } };
    juce::PropertiesFile props (file, {});
    props.setValue ("confirm.unsavedChanges", "banana");
    props.setValue ("confirm.replaceFile", "");
    const AppSettings settings (props);
    CHECK (settings.askToSaveUnsavedChanges());
    CHECK (settings.askBeforeReplacingFile());
}

TEST_CASE ("shouldPromptForUnsavedChanges / shouldConfirmReplace follow the settings", "[app-settings]")
{
    auto file = juce::File::createTempFile (".settings");
    const juce::ScopeGuard cleanup { [&] { file.deleteFile(); } };
    juce::PropertiesFile props (file, {});
    AppSettings settings (props);

    CHECK (shouldPromptForUnsavedChanges (settings, true));
    CHECK_FALSE (shouldPromptForUnsavedChanges (settings, false));   // clean: nothing to ask
    CHECK (shouldConfirmReplace (settings));

    settings.setAskToSaveUnsavedChanges (false);
    settings.setAskBeforeReplacingFile (false);
    CHECK_FALSE (shouldPromptForUnsavedChanges (settings, true));    // off: discard silently
    CHECK_FALSE (shouldPromptForUnsavedChanges (settings, false));
    CHECK_FALSE (shouldConfirmReplace (settings));
}

TEST_CASE ("AppSettings: import track options default to Ask and round-trip", "[app-settings]")
{
    auto file = juce::File::createTempFile (".settings");
    const juce::ScopeGuard cleanup { [&] { file.deleteFile(); } };
    juce::PropertiesFile props (file, {});
    AppSettings settings (props);
    CHECK (settings.importTrackOptions() == ImportTrackOptions::ask);
    CHECK (shouldAskImportOptions (settings));

    settings.setImportTrackOptions (ImportTrackOptions::expandedAlways);
    juce::PropertiesFile reader (file, {});   // written through while `props` is still alive
    CHECK (AppSettings (reader).importTrackOptions() == ImportTrackOptions::expandedAlways);
    CHECK_FALSE (shouldAskImportOptions (settings));

    settings.setImportTrackOptions (ImportTrackOptions::ask);
    CHECK (shouldAskImportOptions (settings));
}

TEST_CASE ("AppSettings: an unrecognised import setting reads as Ask and leaves the confirmations alone", "[app-settings]")
{
    auto file = juce::File::createTempFile (".settings");
    const juce::ScopeGuard cleanup { [&] { file.deleteFile(); } };
    juce::PropertiesFile props (file, {});
    props.setValue ("import.trackOptions", "banana");
    const AppSettings settings (props);
    CHECK (settings.importTrackOptions() == ImportTrackOptions::ask);
    CHECK (settings.askToSaveUnsavedChanges());
    CHECK (settings.askBeforeReplacingFile());
}

TEST_CASE ("AppSettings: a failed save is reported, keeps the value for the session and clears on the next good save", "[app-settings]")
{
    // A directory where the settings file should be: the PropertiesFile cannot replace it.
    auto blocked = juce::File::createTempFile (".settings");
    REQUIRE (blocked.createDirectory().wasOk());
    const juce::ScopedJuceInitialiser_GUI juceInit;   // PropertiesFile may touch the message manager
    const juce::ScopeGuard cleanup { [&] { blocked.deleteRecursively(); } };

    juce::PropertiesFile props (blocked, {});
    AppSettings settings (props);
    CHECK_FALSE (settings.lastSaveFailed());

    CHECK_FALSE (settings.setAskToSaveUnsavedChanges (false));
    CHECK (settings.lastSaveFailed());
    CHECK_FALSE (settings.askToSaveUnsavedChanges());   // still applied for this session

    blocked.deleteRecursively();                         // the path is writable again
    CHECK (settings.setAskBeforeReplacingFile (false));
    CHECK_FALSE (settings.lastSaveFailed());
}

TEST_CASE ("AppSettings: a normal save reports success", "[app-settings]")
{
    auto file = juce::File::createTempFile (".settings");
    const juce::ScopeGuard cleanup { [&] { file.deleteFile(); } };
    juce::PropertiesFile props (file, {});
    AppSettings settings (props);
    CHECK (settings.setAskToSaveUnsavedChanges (false));
    CHECK (settings.setImportTrackOptions (ImportTrackOptions::expandedAlways));
    CHECK_FALSE (settings.lastSaveFailed());
}

TEST_CASE ("AppSettings: the SoundFont path defaults to empty, round-trips, persists and clears", "[app-settings]")
{
    auto file = juce::File::createTempFile (".settings");
    const juce::ScopeGuard cleanup { [&] { file.deleteFile(); } };
    const auto chosen = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("Chosen.sf2");
    {
        juce::PropertiesFile props (file, {});
        AppSettings settings (props);
        CHECK (settings.soundFontPath().isEmpty());
        CHECK (settings.setSoundFontPath (chosen));
        CHECK (settings.soundFontPath() == chosen.getFullPathName());
    }
    juce::PropertiesFile reopened (file, {});
    AppSettings settings (reopened);
    CHECK (settings.soundFontPath() == chosen.getFullPathName());   // persisted under the existing key
    CHECK (reopened.getValue ("soundFontPath") == chosen.getFullPathName());

    CHECK (settings.clearSoundFontPath());
    CHECK (settings.soundFontPath().isEmpty());
    juce::PropertiesFile third (file, {});
    CHECK (AppSettings (third).soundFontPath().isEmpty());
}

TEST_CASE ("AppSettings: SoundFont setters report a failed save", "[app-settings]")
{
    auto blocked = juce::File::createTempFile (".settings");
    REQUIRE (blocked.createDirectory().wasOk());
    const juce::ScopedJuceInitialiser_GUI juceInit;
    const juce::ScopeGuard cleanup { [&] { blocked.deleteRecursively(); } };
    juce::PropertiesFile props (blocked, {});
    AppSettings settings (props);

    CHECK_FALSE (settings.setSoundFontPath (juce::File ("/tmp/x.sf2")));
    CHECK (settings.lastSaveFailed());
    CHECK (settings.soundFontPath().isNotEmpty());   // still applied for the session

    CHECK_FALSE (settings.clearSoundFontPath());
    CHECK (settings.lastSaveFailed());
    CHECK (settings.soundFontPath().isEmpty());
}
