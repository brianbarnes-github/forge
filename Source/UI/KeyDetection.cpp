#include "UI/KeyDetection.h"

#include "UI/SongDocument.h"

namespace lotro
{

std::vector<KeyNote> collectKeyNotes (const juce::ValueTree& songRoot)
{
    std::vector<KeyNote> notes;
    for (auto track : songRoot.getChildWithName (SongIDs::SOURCE_MIDI))
    {
        if (! track.hasType (SongIDs::MIDI_TRACK))
            continue;
        for (auto note : SongDocument::getNotesNode (track))
            notes.push_back ({ (int) note.getProperty (SongIDs::pitch),
                               (int) note.getProperty (SongIDs::durationTicks),
                               (bool) note.getProperty (SongIDs::isDrum) });
    }
    return notes;
}

juce::String detectedKeyText (const SongDocument& doc, KeyProfileSet set)
{
    return juce::String::fromUTF8 (formatKeyText (detectKey (collectKeyNotes (doc.getTree()), set)).c_str());
}

} // namespace lotro
