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
