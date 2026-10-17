#pragma once

#include "UI/SongDocument.h"

#include <vector>

// The MIDI editor's Edit > Cut / Copy / Paste / Paste at Origin clipboard.
// Pure document logic: no UI, no Source/Core. The clipboard is internal to
// Songsmith (not the system clipboard) and lives in the TrackEditorPanel.
namespace lotro
{

struct NoteClipboard
{
    std::vector<juce::ValueTree> notes;   // detached NOTE copies, earliest start first

    bool empty() const noexcept { return notes.empty(); }
    // The earliest copied start, where Paste at Origin puts the copy; 0 when empty.
    int originTick() const;
};

// Detached copies of `notes` (never handles to the live ones), earliest start
// first; ties keep their given order.
NoteClipboard copyNotes (const std::vector<juce::ValueTree>& notes);

// Adds a copy of every clipboard note to `track`, shifted so the earliest
// starts at `startTick` and the rest keep their spacing, each built as a merge
// builds new material (newNoteForTrack: the track's channel, no provenance),
// except that into a track whose notes already use more than one channel each
// note keeps its own channel (and isDrum).
// Nothing is merged or trimmed. One undo transaction. Returns the new nodes;
// an empty clipboard or an invalid track adds nothing and opens no transaction.
std::vector<juce::ValueTree> pasteNotes (SongDocument& doc, juce::ValueTree track,
                                         const NoteClipboard& clipboard, int startTick);

} // namespace lotro
