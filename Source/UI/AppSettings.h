#pragma once

#include <juce_data_structures/juce_data_structures.h>

// Typed view over the per-user settings file (MainWindow's PropertiesFile).
// Key names and defaults live here and nowhere else. Setters write and save at
// once, so a toggle survives a crash. Reads are live: callers ask at the moment
// of use, so a change takes effect without a restart.
namespace lotro
{

// Preferences > Import > "Import Track Options".
enum class ImportTrackOptions
{
    ask,            // Import > MIDI shows the tempo map / track choices dialog
    expandedAlways  // no dialog: keep the tempo map, expand the tracks (today's behaviour)
};

class AppSettings
{
public:
    explicit AppSettings (juce::PropertiesFile& fileIn) : file (fileIn) {}

    bool askToSaveUnsavedChanges() const { return read (keyUnsavedChanges); }
    void setAskToSaveUnsavedChanges (bool on) { write (keyUnsavedChanges, on); }

    bool askBeforeReplacingFile() const { return read (keyReplaceFile); }
    void setAskBeforeReplacingFile (bool on) { write (keyReplaceFile, on); }

    ImportTrackOptions importTrackOptions() const
    {
        return file.getValue (keyImportTrackOptions) == "expanded" ? ImportTrackOptions::expandedAlways
                                                                   : ImportTrackOptions::ask;
    }
    void setImportTrackOptions (ImportTrackOptions value)
    {
        file.setValue (keyImportTrackOptions, value == ImportTrackOptions::expandedAlways ? "expanded" : "ask");
        file.saveIfNeeded();
    }

private:
    static constexpr const char* keyUnsavedChanges    = "confirm.unsavedChanges";
    static constexpr const char* keyReplaceFile       = "confirm.replaceFile";
    static constexpr const char* keyImportTrackOptions = "import.trackOptions";

    // Anything that is not an explicit "0" (corrupt or hand-edited file) reads
    // as the default, which is on.
    bool read (const char* key) const
    {
        return file.getValue (key) != "0";
    }
    void write (const char* key, bool on)
    {
        file.setValue (key, on ? "1" : "0");
        file.saveIfNeeded();
    }

    juce::PropertiesFile& file;
};

// New / Open / drops / Quit: ask only when the Song is dirty and the user wants asking.
// When this is false for a dirty Song, the caller proceeds as "Don't Save".
inline bool shouldPromptForUnsavedChanges (const AppSettings& settings, bool isDirty)
{
    return isDirty && settings.askToSaveUnsavedChanges();
}

inline bool shouldConfirmReplace (const AppSettings& settings)
{
    return settings.askBeforeReplacingFile();
}

// Import > MIDI: show the options dialog only when the user wants asking.
inline bool shouldAskImportOptions (const AppSettings& settings)
{
    return settings.importTrackOptions() == ImportTrackOptions::ask;
}

} // namespace lotro
