#include "SongSession.h"

namespace lotro
{

SongSession::SongSession (SongDocument& document) : rootNode (document.getTree())
{
    rootNode.addListener (this);
}

SongSession::~SongSession() { rootNode.removeListener (this); }

juce::String SongSession::getName() const
{
    return isUntitled() ? juce::String ("Untitled") : file.getFileNameWithoutExtension();
}

juce::String SongSession::displayTitle() const
{
    return getName() + (dirty ? "*" : "") + juce::String::fromUTF8 (" \xe2\x80\x94 Songsmith");
}

void SongSession::setDirty()
{
    if (dirty)
        return;
    dirty = true;
    notify();
}

void SongSession::markClean (const juce::File& newFile)
{
    file = newFile;
    dirty = false;
    notify();
}

void SongSession::markNew()
{
    file = juce::File();
    dirty = false;
    notify();
}

juce::File withExtensionIfMissing (const juce::File& file, const juce::StringArray& acceptedExtensions,
                                   const juce::String& defaultExtension)
{
    for (const auto& ext : acceptedExtensions)
        if (file.getFileName().endsWithIgnoreCase (ext))
            return file;
    return file.getSiblingFile (file.getFileName() + defaultExtension);
}

juce::File defaultExportFile (const juce::File& songFile, const juce::String& extension,
                              const juce::File& fallbackDirectory)
{
    if (songFile == juce::File())
        return fallbackDirectory.getChildFile ("Untitled" + extension);
    return songFile.withFileExtension (extension);
}

} // namespace lotro
