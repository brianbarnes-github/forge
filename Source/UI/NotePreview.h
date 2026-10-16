#pragma once

#include <juce_data_structures/juce_data_structures.h>

#include <functional>
#include <vector>

namespace lotro
{

// What happened to the notes a MIDI-editor note change reports.
enum class NoteChange { inserted, edited, velocityChanged };

// The MIDI editor's Options > Note Preview checkboxes (saved in AppSettings).
struct NotePreviewSettings
{
    bool onInsert = false;
    bool onEdit = false;
    bool onVelocity = false;
};

inline bool operator== (const NotePreviewSettings& a, const NotePreviewSettings& b) noexcept
{
    return a.onInsert == b.onInsert && a.onEdit == b.onEdit && a.onVelocity == b.onVelocity;
}

inline bool operator!= (const NotePreviewSettings& a, const NotePreviewSettings& b) noexcept { return ! (a == b); }

// True when the checkbox matching the change is ticked.
constexpr bool previewWanted (const NotePreviewSettings& s, NoteChange c) noexcept
{
    switch (c)
    {
        case NoteChange::inserted:        return s.onInsert;
        case NoteChange::edited:          return s.onEdit;
        case NoteChange::velocityChanged: return s.onVelocity;
    }
    return false;
}

// Called by the note editor with the notes that were inserted or edited.
using NoteChangeListener = std::function<void (NoteChange change, const std::vector<juce::ValueTree>& notes)>;

} // namespace lotro
