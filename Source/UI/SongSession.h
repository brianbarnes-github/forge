#pragma once

#include "SongDocument.h"

#include <juce_core/juce_core.h>
#include <juce_data_structures/juce_data_structures.h>

#include <functional>

namespace lotro
{

// Where the open Song lives on disk and whether it has unsaved changes.
// Dirty is any change to the document tree after the last markClean()/markNew()
// -- including non-undoable imports -- so undoing back to the saved state
// still reads as dirty (simple and safe).
class SongSession : private juce::ValueTree::Listener
{
public:
    explicit SongSession (SongDocument& document);
    ~SongSession() override;

    bool isDirty() const noexcept { return dirty; }
    const juce::File& getFile() const noexcept { return file; }
    bool isUntitled() const noexcept { return file == juce::File(); }
    juce::String getName() const;
    juce::String displayTitle() const;

    // After a save or a load: remember `file`, clear the dirty flag.
    void markClean (const juce::File& newFile);
    // After New / Close: untitled and clean.
    void markNew();

    std::function<void()> onChanged;

private:
    void setDirty();
    void notify() { if (onChanged) onChanged(); }

    void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override { setDirty(); }
    void valueTreeChildAdded (juce::ValueTree&, juce::ValueTree&) override { setDirty(); }
    void valueTreeChildRemoved (juce::ValueTree&, juce::ValueTree&, int) override { setDirty(); }
    void valueTreeChildOrderChanged (juce::ValueTree&, int, int) override { setDirty(); }

    // Persistent handle: ValueTree::addListener registers on THIS handle
    // object, not the shared tree, so a temporary from doc.getTree() would
    // silently drop the registration (same convention as TrackListComponent).
    juce::ValueTree rootNode;
    juce::File file;
    bool dirty = false;
};

// Appends `defaultExtension` unless the name already ends with one of
// `acceptedExtensions` (case-insensitive). Used BEFORE our own overwrite
// confirmation, because the native chooser warns on the typed name.
juce::File withExtensionIfMissing (const juce::File& file, const juce::StringArray& acceptedExtensions,
                                   const juce::String& defaultExtension);

// Default Save-As target for an export: the Song file with its extension
// replaced, or <fallbackDirectory>/Untitled<extension> for an unsaved Song.
juce::File defaultExportFile (const juce::File& songFile, const juce::String& extension,
                              const juce::File& fallbackDirectory);

} // namespace lotro
