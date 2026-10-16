#pragma once

#include <juce_data_structures/juce_data_structures.h>

#include "GridSize.h"
#include "MergeScope.h"
#include "NotePreview.h"
#include "TrackHeadWidth.h"

#include <algorithm>
#include <cmath>
#include <optional>

// Typed view over the per-user settings file (MainWindow's PropertiesFile).
// Key names and defaults live here and nowhere else. Setters write and save at
// once (so a toggle survives a crash) and return whether the save succeeded.
// Reads are live: callers ask at the moment of use, so a change takes effect
// without a restart.
namespace lotro
{

// Preferences > Import > "Import Track Options".
enum class ImportTrackOptions
{
    ask,            // Import > MIDI shows the tempo map / track choices dialog
    expandedAlways  // no dialog: keep the tempo map, expand the tracks (today's behaviour)
};

// Preferences > Import > "Place imported MIDI at".
enum class ImportPlacement
{
    atStart,   // the file's tick 0 is the Song's tick 0 (today's behaviour)
    atMarker   // the file's tick 0 is the position (start) marker, when one is set
};

class AppSettings
{
public:
    explicit AppSettings (juce::PropertiesFile& fileIn) : file (fileIn) {}

    bool askToSaveUnsavedChanges() const { return read (keyUnsavedChanges); }
    bool setAskToSaveUnsavedChanges (bool on) { return write (keyUnsavedChanges, on); }

    bool askBeforeReplacingFile() const { return read (keyReplaceFile); }
    bool setAskBeforeReplacingFile (bool on) { return write (keyReplaceFile, on); }

    ImportTrackOptions importTrackOptions() const
    {
        return file.getValue (keyImportTrackOptions) == "expanded" ? ImportTrackOptions::expandedAlways
                                                                   : ImportTrackOptions::ask;
    }
    bool setImportTrackOptions (ImportTrackOptions value)
    {
        file.setValue (keyImportTrackOptions, value == ImportTrackOptions::expandedAlways ? "expanded" : "ask");
        return save();
    }

    ImportPlacement importPlacement() const
    {
        return file.getValue (keyImportPlacement) == "marker" ? ImportPlacement::atMarker : ImportPlacement::atStart;
    }
    bool setImportPlacement (ImportPlacement value)
    {
        file.setValue (keyImportPlacement, value == ImportPlacement::atMarker ? "marker" : "start");
        return save();
    }

    // The SoundFont the user chose (full path); empty when never chosen or cleared,
    // in which case the bundled default is used. Key unchanged since before Preferences.
    juce::String soundFontPath() const { return file.getValue (keySoundFontPath); }
    bool setSoundFontPath (const juce::File& soundFont)
    {
        file.setValue (keySoundFontPath, soundFont.getFullPathName());
        return save();
    }
    bool clearSoundFontPath()
    {
        file.removeValue (keySoundFontPath);
        return save();
    }

    // Preferences > Appearance / Editing. Defaults equal the behaviour before the pages existed.
    bool restoreWindowPlacement() const { return read (keyRestorePlacement); }
    bool setRestoreWindowPlacement (bool on) { return write (keyRestorePlacement, on); }

    bool followPlayhead() const { return read (keyFollowPlayhead); }
    bool setFollowPlayhead (bool on) { return write (keyFollowPlayhead, on); }

    // The MIDI editor's Options > Snap to Grid, the same value as the Editing page's checkbox.
    bool snapToGrid() const { return read (keySnapToGrid); }
    bool setSnapToGrid (bool on) { return write (keySnapToGrid, on); }

    // The MIDI editor's Options > Note Preview checkboxes, also on the Editing page.
    // Unlike the other toggles these default off: only an explicit "1" reads on.
    NotePreviewSettings notePreview() const
    {
        NotePreviewSettings s;
        s.onInsert = file.getValue (keyPreviewOnInsert) == "1";
        s.onEdit = file.getValue (keyPreviewOnEdit) == "1";
        s.onVelocity = file.getValue (keyPreviewOnVelocity) == "1";
        return s;
    }
    bool setNotePreview (const NotePreviewSettings& s)
    {
        file.setValue (keyPreviewOnInsert, s.onInsert ? "1" : "0");
        file.setValue (keyPreviewOnEdit, s.onEdit ? "1" : "0");
        file.setValue (keyPreviewOnVelocity, s.onVelocity ? "1" : "0");
        return save();
    }

    // What a newly opened track editor starts on. Absent or unrecognised reads Off.
    GridSize defaultGrid() const
    {
        const auto stored = file.getValue (keyDefaultGrid);
        if (stored == "quarter")   return GridSize::Quarter;
        if (stored == "eighth")    return GridSize::Eighth;
        if (stored == "sixteenth") return GridSize::Sixteenth;
        return GridSize::Off;
    }
    bool setDefaultGrid (GridSize size)
    {
        const char* name = "off";
        switch (size)
        {
            case GridSize::Quarter:   name = "quarter";   break;
            case GridSize::Eighth:    name = "eighth";    break;
            case GridSize::Sixteenth: name = "sixteenth"; break;
            case GridSize::Off:       break;
        }
        file.setValue (keyDefaultGrid, name);
        return save();
    }

    // What moving or copying sections to another track carries. Absent or unrecognised reads notesOnly.
    MergeScope mergeScope() const
    {
        return file.getValue (keyMergeScope) == "all" ? MergeScope::allEvents : MergeScope::notesOnly;
    }
    bool setMergeScope (MergeScope scope)
    {
        file.setValue (keyMergeScope, scope == MergeScope::allEvents ? "all" : "notes");
        return save();
    }

    // The dockable-panel layout as JSON (see Dock/DockLayoutState.h). Empty when never saved;
    // dock::fromJson turns empty or corrupt text into the defaults.
    juce::String dockLayout() const { return file.getValue (keyDockLayout); }
    bool setDockLayout (const juce::String& json)
    {
        file.setValue (keyDockLayout, json);
        return save();
    }

    // The user's named layouts as JSON (see Dock/SavedLayouts.h). Empty when none were saved.
    juce::String savedLayouts() const { return file.getValue (keySavedLayouts); }
    bool setSavedLayouts (const juce::String& json)
    {
        file.setValue (keySavedLayouts, json);
        return save();
    }

    // The transport strip's chosen buttons as JSON (see Dock/StripButtons.h). Empty when none were saved.
    juce::String stripButtons() const { return file.getValue (keyStripButtons); }
    bool setStripButtons (const juce::String& json)
    {
        file.setValue (keyStripButtons, json);
        return save();
    }

    // The main screen's track-head column width in px. Absent, non-numeric or outside
    // [headwidth::minimum, headwidth::maximum] reads as the minimum (the original width).
    int trackHeadWidth() const
    {
        const auto stored = file.getValue (keyTrackHeadWidth).trim();
        if (stored.isEmpty() || ! stored.containsOnly ("0123456789"))
            return headwidth::minimum;
        const int width = stored.getIntValue();
        return (width >= headwidth::minimum && width <= headwidth::maximum) ? width : headwidth::minimum;
    }
    bool setTrackHeadWidth (int width)
    {
        file.setValue (keyTrackHeadWidth, headwidth::clamp (width));
        return save();
    }

    // True when the most recent setter could not write the settings file. The new
    // value still applies for this session (PropertiesFile holds it in memory).
    bool lastSaveFailed() const noexcept { return saveFailed; }

private:
    static constexpr const char* keyUnsavedChanges    = "confirm.unsavedChanges";
    static constexpr const char* keyReplaceFile       = "confirm.replaceFile";
    static constexpr const char* keyImportTrackOptions = "import.trackOptions";
    static constexpr const char* keyImportPlacement    = "import.placement";
    static constexpr const char* keySoundFontPath      = "soundFontPath";
    static constexpr const char* keyRestorePlacement   = "appearance.restoreWindowPlacement";
    static constexpr const char* keyFollowPlayhead     = "editing.followPlayhead";
    static constexpr const char* keyDefaultGrid        = "editing.defaultGrid";
    static constexpr const char* keySnapToGrid         = "editing.snapToGrid";
    static constexpr const char* keyMergeScope         = "editing.mergeScope";
    static constexpr const char* keyPreviewOnInsert    = "editing.previewOnInsert";
    static constexpr const char* keyPreviewOnEdit      = "editing.previewOnEdit";
    static constexpr const char* keyPreviewOnVelocity  = "editing.previewOnVelocity";
    static constexpr const char* keyDockLayout         = "dock.layout";
    static constexpr const char* keySavedLayouts       = "dock.savedLayouts";
    static constexpr const char* keyStripButtons       = "strip.buttons";
    static constexpr const char* keyTrackHeadWidth     = "trackList.headWidth";

    // Anything that is not an explicit "0" (corrupt or hand-edited file) reads
    // as the default, which is on.
    bool read (const char* key) const
    {
        return file.getValue (key) != "0";
    }
    bool write (const char* key, bool on)
    {
        file.setValue (key, on ? "1" : "0");
        return save();
    }
    bool save()
    {
        saveFailed = ! file.saveIfNeeded();
        return ! saveFailed;
    }

    juce::PropertiesFile& file;
    bool saveFailed = false;
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

// Import > MIDI: where the file's tick 0 lands, in the Song's ticks. Only the marker
// setting with a marker actually set moves it; otherwise (or with no marker) it is 0.
inline int importStartOffsetTicks (const AppSettings& settings, std::optional<double> markerTick)
{
    if (settings.importPlacement() != ImportPlacement::atMarker || ! markerTick.has_value())
        return 0;
    return std::max (0, (int) std::lround (*markerTick));
}

} // namespace lotro
