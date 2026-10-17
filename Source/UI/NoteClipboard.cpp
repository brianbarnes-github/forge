#include "UI/NoteClipboard.h"

#include "UI/NoteMerge.h"

#include <algorithm>

namespace lotro
{

namespace
{
    int startOf (const juce::ValueTree& n) { return (int) n.getProperty (SongIDs::startTick); }

    bool notesUseSeveralChannels (const juce::ValueTree& notes)
    {
        for (int i = 1; i < notes.getNumChildren(); ++i)
            if (notes.getChild (i).getProperty (SongIDs::channel) != notes.getChild (0).getProperty (SongIDs::channel))
                return true;
        return false;
    }
}

int NoteClipboard::originTick() const
{
    return notes.empty() ? 0 : startOf (notes.front());
}

NoteClipboard copyNotes (const std::vector<juce::ValueTree>& notes)
{
    NoteClipboard clip;
    clip.notes.reserve (notes.size());
    for (const auto& n : notes)
        clip.notes.push_back (n.createCopy());
    std::stable_sort (clip.notes.begin(), clip.notes.end(),
                      [] (const juce::ValueTree& a, const juce::ValueTree& b) { return startOf (a) < startOf (b); });
    return clip;
}

std::vector<juce::ValueTree> pasteNotes (SongDocument& doc, juce::ValueTree track,
                                         const NoteClipboard& clipboard, int startTick)
{
    std::vector<juce::ValueTree> added;
    if (clipboard.empty() || ! track.isValid())
        return added;

    const int shift = startTick - clipboard.originTick();
    auto target = SongDocument::getNotesNode (track);
    const bool keepChannels = notesUseSeveralChannels (target);
    doc.getUndoManager().beginNewTransaction();
    for (const auto& src : clipboard.notes)
    {
        const int start = startOf (src) + shift;
        auto note = newNoteForTrack (src, start, start + (int) src.getProperty (SongIDs::durationTicks), track);
        if (keepChannels)
            for (const auto* id : { &SongIDs::channel, &SongIDs::isDrum })
                note.setProperty (*id, src.getProperty (*id), nullptr);
        doc.addChild (target, note, false);
        added.push_back (note);
    }
    return added;
}

} // namespace lotro
