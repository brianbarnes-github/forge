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

TEST_CASE ("AppSettings: the view settings default to today's behaviour and round-trip", "[app-settings]")
{
    auto file = juce::File::createTempFile (".settings");
    const juce::ScopeGuard cleanup { [&] { file.deleteFile(); } };
    {
        juce::PropertiesFile props (file, {});
        AppSettings settings (props);
        CHECK (settings.restoreWindowPlacement());
        CHECK (settings.followPlayhead());
        CHECK (settings.defaultGrid() == GridSize::Off);
        CHECK (settings.snapToGrid());

        CHECK (settings.setRestoreWindowPlacement (false));
        CHECK (settings.setFollowPlayhead (false));
        CHECK (settings.setDefaultGrid (GridSize::Eighth));
        CHECK (settings.setSnapToGrid (false));
    }
    juce::PropertiesFile reopened (file, {});
    AppSettings settings (reopened);
    CHECK_FALSE (settings.restoreWindowPlacement());
    CHECK_FALSE (settings.followPlayhead());
    CHECK (settings.defaultGrid() == GridSize::Eighth);
    CHECK_FALSE (settings.snapToGrid());
    CHECK (reopened.getValue ("appearance.restoreWindowPlacement") == "0");
    CHECK (reopened.getValue ("editing.followPlayhead") == "0");
    CHECK (reopened.getValue ("editing.defaultGrid") == "eighth");
    CHECK (reopened.getValue ("editing.snapToGrid") == "0");
}

TEST_CASE ("AppSettings: every grid size round-trips and an unrecognised stored grid reads Off", "[app-settings]")
{
    auto file = juce::File::createTempFile (".settings");
    const juce::ScopeGuard cleanup { [&] { file.deleteFile(); } };
    juce::PropertiesFile props (file, {});
    AppSettings settings (props);

    for (const auto size : { GridSize::Off, GridSize::Quarter, GridSize::Eighth, GridSize::Sixteenth })
    {
        CHECK (settings.setDefaultGrid (size));
        CHECK (settings.defaultGrid() == size);
    }

    props.setValue ("editing.defaultGrid", "thirtysecond");   // hand-edited / future value
    CHECK (settings.defaultGrid() == GridSize::Off);
    props.setValue ("editing.defaultGrid", "");
    CHECK (settings.defaultGrid() == GridSize::Off);
}

TEST_CASE ("AppSettings: the view-setting setters report a failed save but still apply", "[app-settings]")
{
    auto blocked = juce::File::createTempFile (".settings");
    REQUIRE (blocked.createDirectory().wasOk());
    const juce::ScopedJuceInitialiser_GUI juceInit;
    const juce::ScopeGuard cleanup { [&] { blocked.deleteRecursively(); } };
    juce::PropertiesFile props (blocked, {});
    AppSettings settings (props);

    CHECK_FALSE (settings.setRestoreWindowPlacement (false));
    CHECK (settings.lastSaveFailed());
    CHECK_FALSE (settings.restoreWindowPlacement());

    CHECK_FALSE (settings.setDefaultGrid (GridSize::Quarter));
    CHECK (settings.lastSaveFailed());
    CHECK (settings.defaultGrid() == GridSize::Quarter);
}

TEST_CASE ("AppSettings: the merge scope defaults to notes only, round-trips and ignores junk", "[app-settings]")
{
    auto file = juce::File::createTempFile (".settings");
    const juce::ScopeGuard cleanup { [&] { file.deleteFile(); } };
    {
        juce::PropertiesFile props (file, {});
        AppSettings settings (props);
        CHECK (settings.mergeScope() == MergeScope::notesOnly);
        CHECK (settings.setMergeScope (MergeScope::allEvents));
    }
    juce::PropertiesFile reopened (file, {});
    AppSettings settings (reopened);
    CHECK (settings.mergeScope() == MergeScope::allEvents);
    CHECK (reopened.getValue ("editing.mergeScope") == "all");

    reopened.setValue ("editing.mergeScope", "banana");
    CHECK (settings.mergeScope() == MergeScope::notesOnly);
}

TEST_CASE ("AppSettings: import placement defaults to the start of the song and round-trips", "[app-settings]")
{
    auto file = juce::File::createTempFile (".settings");
    const juce::ScopeGuard cleanup { [&] { file.deleteFile(); } };
    juce::PropertiesFile props (file, {});
    AppSettings settings (props);
    CHECK (settings.importPlacement() == ImportPlacement::atStart);

    CHECK (settings.setImportPlacement (ImportPlacement::atMarker));
    juce::PropertiesFile reader (file, {});
    CHECK (AppSettings (reader).importPlacement() == ImportPlacement::atMarker);

    settings.setImportPlacement (ImportPlacement::atStart);
    CHECK (settings.importPlacement() == ImportPlacement::atStart);

    props.setValue ("import.placement", "banana");
    CHECK (settings.importPlacement() == ImportPlacement::atStart);
    CHECK (settings.importTrackOptions() == ImportTrackOptions::ask);   // the neighbouring setting is unaffected
}

TEST_CASE ("importStartOffsetTicks: only the marker setting with a marker set places the file later", "[app-settings]")
{
    auto file = juce::File::createTempFile (".settings");
    const juce::ScopeGuard cleanup { [&] { file.deleteFile(); } };
    juce::PropertiesFile props (file, {});
    AppSettings settings (props);

    CHECK (importStartOffsetTicks (settings, 480.0) == 0);            // Start of song ignores the marker
    settings.setImportPlacement (ImportPlacement::atMarker);
    CHECK (importStartOffsetTicks (settings, 480.0) == 480);
    CHECK (importStartOffsetTicks (settings, 480.4) == 480);          // marker ticks are fractional; rounded
    CHECK (importStartOffsetTicks (settings, 480.6) == 481);
    CHECK (importStartOffsetTicks (settings, std::nullopt) == 0);     // no marker set: the start
    CHECK (importStartOffsetTicks (settings, -5.0) == 0);
}

TEST_CASE ("AppSettings: dock.layout round-trips and is empty by default", "[app-settings][dock]")
{
    auto file = juce::File::createTempFile (".settings");
    const juce::ScopeGuard cleanup { [&] { file.deleteFile(); } };
    juce::PropertiesFile props (file, {});
    AppSettings settings (props);
    CHECK (settings.dockLayout().isEmpty());

    CHECK (settings.setDockLayout ("{\"upperWeight\":0.4}"));
    CHECK (settings.dockLayout() == "{\"upperWeight\":0.4}");

    juce::PropertiesFile reader (file, {});
    CHECK (reader.getValue ("dock.layout") == "{\"upperWeight\":0.4}");
}

TEST_CASE ("AppSettings: saved layouts default to empty and persist across a re-opened file", "[app-settings]")
{
    auto file = juce::File::createTempFile (".settings");
    const juce::ScopeGuard cleanup { [&] { file.deleteFile(); } };
    {
        juce::PropertiesFile props (file, {});
        AppSettings settings (props);
        CHECK (settings.savedLayouts().isEmpty());
        CHECK (settings.setSavedLayouts ("[1,2]"));
        CHECK (settings.savedLayouts() == "[1,2]");
        CHECK (settings.dockLayout().isEmpty());   // independent of the live layout
    }
    juce::PropertiesFile reloaded (file, {});
    CHECK (AppSettings (reloaded).savedLayouts() == "[1,2]");
}

TEST_CASE ("AppSettings: strip buttons default to empty and persist across a re-opened file", "[app-settings]")
{
    auto file = juce::File::createTempFile (".settings");
    const juce::ScopeGuard cleanup { [&] { file.deleteFile(); } };
    {
        juce::PropertiesFile props (file, {});
        AppSettings settings (props);
        CHECK (settings.stripButtons().isEmpty());
        CHECK (settings.setStripButtons ("[1]"));
        CHECK (settings.stripButtons() == "[1]");
        CHECK (settings.savedLayouts().isEmpty());   // independent of the layouts
    }
    juce::PropertiesFile reloaded (file, {});
    CHECK (AppSettings (reloaded).stripButtons() == "[1]");
}

TEST_CASE ("AppSettings: the track head width defaults to the minimum and persists across a re-opened file", "[app-settings]")
{
    auto file = juce::File::createTempFile (".settings");
    const juce::ScopeGuard cleanup { [&] { file.deleteFile(); } };
    {
        juce::PropertiesFile props (file, {});
        AppSettings settings (props);
        CHECK (settings.trackHeadWidth() == headwidth::minimum);
        CHECK (settings.setTrackHeadWidth (310));
        CHECK (settings.trackHeadWidth() == 310);
    }
    juce::PropertiesFile reloaded (file, {});
    CHECK (AppSettings (reloaded).trackHeadWidth() == 310);
}

TEST_CASE ("AppSettings: a corrupt or out-of-range track head width reads as the minimum", "[app-settings]")
{
    auto file = juce::File::createTempFile (".settings");
    const juce::ScopeGuard cleanup { [&] { file.deleteFile(); } };
    juce::PropertiesFile props (file, {});
    const AppSettings settings (props);
    for (const char* stored : { "banana", "", "150", "401", "250px" })
    {
        props.setValue ("trackList.headWidth", stored);
        CHECK (settings.trackHeadWidth() == headwidth::minimum);
    }
    props.setValue ("trackList.headWidth", "400");
    CHECK (settings.trackHeadWidth() == headwidth::maximum);
}

TEST_CASE ("AppSettings: note preview defaults all off, round-trips each flag and persists across a re-opened file", "[app-settings][note-preview]")
{
    auto file = juce::File::createTempFile (".settings");
    const juce::ScopeGuard cleanup { [&] { file.deleteFile(); } };
    {
        juce::PropertiesFile props (file, {});
        AppSettings settings (props);
        CHECK (settings.notePreview() == NotePreviewSettings {});

        for (int flag = 0; flag < 3; ++flag)
        {
            CAPTURE (flag);
            NotePreviewSettings s;
            s.onInsert = flag == 0;
            s.onEdit = flag == 1;
            s.onVelocity = flag == 2;
            CHECK (settings.setNotePreview (s));
            CHECK (settings.notePreview() == s);
        }

        NotePreviewSettings saved;
        saved.onInsert = true;
        saved.onVelocity = true;
        CHECK (settings.setNotePreview (saved));
    }
    juce::PropertiesFile reopened (file, {});
    const AppSettings settings (reopened);
    CHECK (settings.notePreview().onInsert);
    CHECK_FALSE (settings.notePreview().onEdit);
    CHECK (settings.notePreview().onVelocity);
    CHECK (reopened.getValue ("editing.previewOnInsert") == "1");
    CHECK (reopened.getValue ("editing.previewOnEdit") == "0");
    CHECK (reopened.getValue ("editing.previewOnVelocity") == "1");
}

TEST_CASE ("AppSettings: keyboard labels default to piano notes, round-trip and persist; junk reads piano notes", "[app-settings][keyboard-labels]")
{
    auto file = juce::File::createTempFile (".settings");
    const juce::ScopeGuard cleanup { [&] { file.deleteFile(); } };
    {
        juce::PropertiesFile props (file, {});
        AppSettings settings (props);
        CHECK (settings.keyboardLabels() == KeyboardLabels::pianoNotes);
        CHECK (settings.setKeyboardLabels (KeyboardLabels::midiNumbers));
        CHECK (settings.keyboardLabels() == KeyboardLabels::midiNumbers);
        CHECK (settings.setKeyboardLabels (KeyboardLabels::pianoNotes));
        CHECK (settings.keyboardLabels() == KeyboardLabels::pianoNotes);
        CHECK (settings.setKeyboardLabels (KeyboardLabels::midiNumbers));
    }
    {
        juce::PropertiesFile reopened (file, {});
        const AppSettings settings (reopened);
        CHECK (settings.keyboardLabels() == KeyboardLabels::midiNumbers);
        CHECK (reopened.getValue ("view.keyboardLabels") == "midi");
    }
    juce::PropertiesFile junk (file, {});
    junk.setValue ("view.keyboardLabels", "numbers");
    CHECK (AppSettings (junk).keyboardLabels() == KeyboardLabels::pianoNotes);
}

TEST_CASE ("AppSettings: note shape defaults to rectangle, round-trips and persists; junk reads rectangle", "[app-settings][note-display]")
{
    auto file = juce::File::createTempFile (".settings");
    const juce::ScopeGuard cleanup { [&] { file.deleteFile(); } };
    {
        juce::PropertiesFile props (file, {});
        AppSettings settings (props);
        CHECK (settings.noteShape() == NoteShape::rectangle);
        for (auto shape : { NoteShape::diamond, NoteShape::diamondLength, NoteShape::rectangle, NoteShape::diamondLength })
        {
            CHECK (settings.setNoteShape (shape));
            CHECK (settings.noteShape() == shape);
        }
    }
    {
        juce::PropertiesFile reopened (file, {});
        const AppSettings settings (reopened);
        CHECK (settings.noteShape() == NoteShape::diamondLength);
        CHECK (reopened.getValue ("view.noteShape") == "diamondLength");
    }
    juce::PropertiesFile junk (file, {});
    junk.setValue ("view.noteShape", "junk");
    CHECK (AppSettings (junk).noteShape() == NoteShape::rectangle);
    junk.setValue ("view.noteShape", "diamond");
    CHECK (AppSettings (junk).noteShape() == NoteShape::diamond);
    junk.setValue ("view.noteShape", "rect");
    CHECK (AppSettings (junk).noteShape() == NoteShape::rectangle);
}

TEST_CASE ("AppSettings: note colour mode defaults to track, round-trips and persists; junk reads track", "[app-settings][note-display]")
{
    auto file = juce::File::createTempFile (".settings");
    const juce::ScopeGuard cleanup { [&] { file.deleteFile(); } };
    {
        juce::PropertiesFile props (file, {});
        AppSettings settings (props);
        CHECK (settings.noteColourMode() == NoteColourMode::track);
        for (auto mode : { NoteColourMode::velocity, NoteColourMode::channel, NoteColourMode::pitch,
                           NoteColourMode::track, NoteColourMode::channel })
        {
            CHECK (settings.setNoteColourMode (mode));
            CHECK (settings.noteColourMode() == mode);
        }
    }
    {
        juce::PropertiesFile reopened (file, {});
        const AppSettings settings (reopened);
        CHECK (settings.noteColourMode() == NoteColourMode::channel);
        CHECK (reopened.getValue ("view.noteColour") == "channel");
    }
    juce::PropertiesFile junk (file, {});
    for (auto [value, mode] : { std::pair { "track", NoteColourMode::track }, std::pair { "velocity", NoteColourMode::velocity },
                                std::pair { "pitch", NoteColourMode::pitch }, std::pair { "junk", NoteColourMode::track } })
    {
        junk.setValue ("view.noteColour", value);
        CHECK (AppSettings (junk).noteColourMode() == mode);
    }
}

TEST_CASE ("AppSettings: note labels default off, round-trip and persist; junk reads off", "[app-settings][note-labels]")
{
    auto file = juce::File::createTempFile (".settings");
    const juce::ScopeGuard cleanup { [&] { file.deleteFile(); } };
    {
        juce::PropertiesFile props (file, {});
        AppSettings settings (props);
        CHECK (settings.noteLabels() == NoteLabels {});
        for (auto labels : { NoteLabels { true, false }, NoteLabels { false, true }, NoteLabels { true, true },
                             NoteLabels {}, NoteLabels { false, true } })
        {
            CHECK (settings.setNoteLabels (labels));
            CHECK (settings.noteLabels() == labels);
        }
    }
    {
        juce::PropertiesFile reopened (file, {});
        const AppSettings settings (reopened);
        CHECK (settings.noteLabels() == NoteLabels { false, true });
        CHECK (reopened.getValue ("view.noteNames") == "0");
        CHECK (reopened.getValue ("view.noteVelocities") == "1");
    }
    juce::PropertiesFile junk (file, {});
    junk.setValue ("view.noteNames", "yes");
    junk.setValue ("view.noteVelocities", "junk");
    CHECK (AppSettings (junk).noteLabels() == NoteLabels {});
}

TEST_CASE ("AppSettings: a junk note preview value reads off", "[app-settings][note-preview]")
{
    auto file = juce::File::createTempFile (".settings");
    const juce::ScopeGuard cleanup { [&] { file.deleteFile(); } };
    juce::PropertiesFile props (file, {});
    props.setValue ("editing.previewOnInsert", "yes");
    props.setValue ("editing.previewOnEdit", "1");
    const AppSettings settings (props);
    CHECK_FALSE (settings.notePreview().onInsert);
    CHECK (settings.notePreview().onEdit);
    CHECK_FALSE (settings.notePreview().onVelocity);
}
